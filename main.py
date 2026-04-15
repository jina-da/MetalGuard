# main.py - 운용 서버 진입점
# 실행 순서: AI 서버 연결 → 판정 엔진 시작 → TCP 서버 시작 → 대기

import logging
import queue
import signal
import sys

import config
from server.tcp_server import TCPServer
from server.handlers.ai_client import AIClient
from server.engine.verdict_engine import VerdictEngine

# 로그 설정
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s - %(message)s"
)
logger = logging.getLogger(__name__)


def main():
    logger.info("=" * 50)
    logger.info("MetalGuard 운용 서버 시작")
    logger.info("=" * 50)

    # 카메라 → 판정 엔진으로 이미지 전달하는 Queue
    image_queue = queue.Queue()

    # ── 1. AI 서버 연결 ───────────────────────────────
    ai_client = AIClient()
    if not ai_client.connect():
        logger.error("AI 서버 연결 실패. 서버를 종료합니다.")
        sys.exit(1)

    # ── 2. 판정 엔진 시작 ─────────────────────────────
    verdict_engine = VerdictEngine(image_queue, ai_client)

    # 판정 결과 콜백 등록 (지금은 로그만 출력, 나중에 MFC/DB 연동)
    def on_result(task, verdict, ai_result, timeout, pipeline_ms):
        logger.info(
            f"[결과] verdict={verdict} "
            f"mode={task.mode} "
            f"pipeline={pipeline_ms:.1f}ms "
            f"timeout={timeout}"
        )
        if ai_result:
            logger.info(
                f"  normal={ai_result.get('prob_normal'):.4f} "
                f"crack={ai_result.get('prob_crack'):.4f} "
                f"hole={ai_result.get('prob_hole'):.4f} "
                f"rust={ai_result.get('prob_rust'):.4f} "
                f"scratch={ai_result.get('prob_scratch'):.4f}"
            )

    verdict_engine.on_result = on_result
    verdict_engine.start()

    # ── 3. TCP 서버 시작 ──────────────────────────────
    tcp_server = TCPServer(image_queue)
    tcp_server.start()

    # ── 4. 종료 시그널 처리 (Ctrl+C) ─────────────────
    def shutdown(sig, frame):
        """Ctrl+C 입력 시 graceful 종료."""
        logger.info("종료 시그널 수신. 서버를 종료합니다.")
        verdict_engine.stop()
        tcp_server.stop()
        ai_client.close()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # ── 5. 메인 루프 대기 ─────────────────────────────
    logger.info("서버 실행 중... (종료: Ctrl+C)")
    signal.pause()


if __name__ == "__main__":
    main()