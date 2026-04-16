# main.py - 운용 서버 진입점
#
# 전체 실행 순서:
#   1. DB 연결          → 판정 결과 저장소
#   2. AI 서버 연결     → 이미지 추론 요청 (연결 유지, 재연결 시 ~50ms 오버헤드)
#   3. 아두이노 연결    → 판정 결과 시리얼 전송 (실패해도 서버 계속 실행)
#   4. TCP 서버 준비    → 카메라(8000), MFC(8001) 포트 Listen 준비
#   5. 판정 엔진 시작   → 이미지 Queue 소비 루프 쓰레드 시작
#   6. TCP 서버 시작    → 카메라/MFC Accept 루프 쓰레드 시작
#   7. 시그널 등록      → Ctrl+C 시 graceful 종료
#   8. 대기             → 메인 쓰레드는 signal.pause()로 대기

import logging
import queue
import signal
import sys

import config
from server.tcp_server import TCPServer
from server.handlers.ai_client import AIClient
from server.engine.verdict_engine import VerdictEngine
from server.engine.arduino_serial import ArduinoSerial
from server.db.db_manager import DBManager

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s - %(message)s"
)
logger = logging.getLogger(__name__)


def main():
    logger.info("=" * 50)
    logger.info("MetalGuard 운용 서버 시작")
    logger.info("=" * 50)

    # 카메라 → 판정 엔진으로 이미지를 넘기는 중간 버퍼
    # camera_handler가 넣고, verdict_engine이 꺼냄
    image_queue: queue.Queue = queue.Queue()

    # ── 1. DB 연결 ────────────────────────────────
    # inspection_result, pipeline_log, reclassify_result 테이블 사용
    db_manager = DBManager()
    if not db_manager.connect():
        logger.error("DB 연결 실패. 서버를 종료합니다.")
        sys.exit(1)

    # ── 2. AI 서버 연결 ───────────────────────────
    # 연결을 한 번 맺고 계속 유지 (재연결 시 ~50ms 오버헤드 발생)
    # 10.10.10.128:9000 (김범준 AI 서버)
    ai_client = AIClient()
    if not ai_client.connect():
        logger.error("AI 서버 연결 실패. 서버를 종료합니다.")
        db_manager.close()
        sys.exit(1)

    # ── 3. 아두이노 연결 ──────────────────────────
    # 판정 결과를 시리얼로 전송: P\n / F\n / U\n
    # 연결 실패해도 서버는 계속 실행 (arduino=None이면 전송 스킵)
    arduino = ArduinoSerial(
        port=config.ARDUINO_PORT,    # "/dev/ttyACM0"
        baudrate=config.ARDUINO_BAUD # 9600
    )
    if not arduino.connect():
        logger.warning("아두이노 연결 실패. 아두이노 없이 계속 실행합니다.")
        arduino = None

    # ── 4. TCP 서버 준비 ──────────────────────────
    # start()는 5번 이후에 호출 (판정 엔진이 먼저 준비돼야 이미지 유실 없음)
    tcp_server = TCPServer(image_queue, db_manager)

    # verdict_engine이 MFC로 결과를 전송할 때 현재 연결된 소켓을 가져오는 클로저
    # tcp_server.mfc_client_sock은 MFC 연결/해제 시 동적으로 바뀌므로 클로저로 참조
    def get_mfc_conn():
        return tcp_server.get_mfc_conn()

    # ── 5. 판정 엔진 시작 ─────────────────────────
    # image_queue에서 이미지 꺼냄 → AI 추론 → 판정 → DB 저장 → MFC/아두이노 전송
    verdict_engine = VerdictEngine(
        image_queue=image_queue,
        ai_client=ai_client,
        db_manager=db_manager,
        mfc_conn_getter=get_mfc_conn,
        arduino=arduino,
        model_version_id=config.MODEL_VERSION_ID,  # DB model_version 테이블 id
    )
    verdict_engine.start()

    # ── 6. TCP 서버 시작 ──────────────────────────
    # 카메라(8000), MFC(8001) Accept 루프 쓰레드 시작
    tcp_server.start()

    # ── 7. 종료 시그널 처리 ───────────────────────
    # Ctrl+C (SIGINT) 또는 kill (SIGTERM) 시 모든 연결 정리 후 종료
    def shutdown(sig, frame):
        logger.info("종료 시그널 수신. 서버를 종료합니다.")
        verdict_engine.stop()   # 판정 루프 쓰레드 종료 대기
        tcp_server.stop()       # Accept 루프 종료, 소켓 닫기
        ai_client.close()       # AI 서버 소켓 닫기
        db_manager.close()      # DB 커넥션 닫기
        if arduino:
            arduino.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # ── 8. 메인 루프 대기 ─────────────────────────
    # 실제 작업은 모두 쓰레드에서 처리되므로 메인 쓰레드는 시그널 대기
    logger.info("서버 실행 중... (종료: Ctrl+C)")
    signal.pause()


if __name__ == "__main__":
    main()