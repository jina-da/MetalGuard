"""
verdict_engine.py
Queue에서 이미지를 꺼내 AI 추론 요청 → PASS/FAIL/UNCERTAIN 판정 → DB 저장
"""

import logging
import queue
import threading
import time
from datetime import datetime
from typing import TypedDict, Callable

from config import (
    PASS_THRESHOLD,       # 0.80
    UNCERTAIN_THRESHOLD,  # 0.60
)
from server.handlers.ai_client import send_image_to_ai, recv_ai_response
from server.handlers.mfc_handler import send_result_to_mfc  # [수정] 상단으로 이동 (순환 import 제거)
from server.engine.arduino_serial import ArduinoSerial       # [추가] 아두이노 연동
from server.db.db_manager import DBManager

logger = logging.getLogger(__name__)


class VerdictResult(TypedDict):
    cmd_no: int           # 301 (RESULT_SEND)
    image_id: str
    verdict: str          # "PASS" | "FAIL" | "UNCERTAIN"
    normal_prob: float
    max_prob: float
    defect_type: str | None
    timestamp: str


class VerdictEngine:
    """
    이미지 큐를 소비하여 AI 추론 → 판정 → DB 저장 → MFC 전송 루프.

    카메라 핸들러가 image_queue에 적재하면,
    이 엔진이 별도 쓰레드에서 지속적으로 소비함.
    """

    def __init__(
        self,
        image_queue: queue.Queue,
        ai_socket,
        db_manager: DBManager,
        mfc_conn_getter: Callable,              # () -> socket | None
        arduino: ArduinoSerial | None = None,   # [추가] 아두이노 인스턴스 주입
    ):
        self._queue = image_queue
        self._ai_socket = ai_socket
        self._db = db_manager
        self._get_mfc_conn = mfc_conn_getter
        self._arduino = arduino                 # [추가]
        self._running = False
        self._thread: threading.Thread | None = None

    # ──────────────────────────────────────────────
    # 엔진 시작 / 정지
    # ──────────────────────────────────────────────

    def start(self) -> None:
        """판정 루프 쓰레드 시작"""
        self._running = True
        self._thread = threading.Thread(target=self._run_loop, daemon=True, name="VerdictEngine")
        self._thread.start()
        logger.info("[VerdictEngine] 시작됨")

    def stop(self) -> None:
        """판정 루프 정지 (최대 3초 대기)"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=3.0)
        logger.info("[VerdictEngine] 정지됨")

    # ──────────────────────────────────────────────
    # 메인 루프
    # ──────────────────────────────────────────────

    def _run_loop(self) -> None:
        while self._running:
            try:
                # 0.5초 타임아웃: 정지 신호 확인 주기
                item = self._queue.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                self._process(item)
            except Exception as e:
                logger.error(f"[VerdictEngine] 처리 중 예외: {e}")
            finally:
                self._queue.task_done()

    def _process(self, item: dict) -> None:
        """
        단일 이미지 처리 파이프라인
        item: {"image_id": str, "image_bytes": bytes, "received_at": float}
        """
        image_id: str = item["image_id"]
        image_bytes: bytes = item["image_bytes"]
        received_at: float = item["received_at"]  # [수정] 카메라 수신 시점 기준으로 측정

        # 1. AI 추론 요청
        ai_ok = send_image_to_ai(self._ai_socket, image_bytes)
        if not ai_ok:
            logger.error(f"[VerdictEngine] AI 전송 실패: image_id={image_id}")
            self._db.insert_pipeline_log(image_id, "AI_SEND_FAIL", 0, False)
            return

        ai_response = recv_ai_response(self._ai_socket)
        if ai_response is None:
            logger.error(f"[VerdictEngine] AI 응답 수신 실패: image_id={image_id}")
            self._db.insert_pipeline_log(image_id, "AI_RECV_FAIL", 0, False)
            return

        # 2. 판정
        result = self._judge(image_id, ai_response)

        # 3. 파이프라인 소요시간: 카메라 수신 시점 기준 (received_at) [수정]
        elapsed_ms = int((time.monotonic() - received_at) * 1000)
        if elapsed_ms > 150:
            logger.warning(f"[VerdictEngine] 파이프라인 초과: {elapsed_ms}ms (image_id={image_id})")

        # 4. DB 저장
        self._save_to_db(result, elapsed_ms)

        # 5. MFC 전송
        self._send_to_mfc(result)

        # 6. 아두이노 전송 [추가]
        self._send_to_arduino(result["verdict"])

        logger.info(
            f"[VerdictEngine] 완료: image_id={image_id}, "
            f"verdict={result['verdict']}, {elapsed_ms}ms"
        )

    # ──────────────────────────────────────────────
    # 판정 로직
    # ──────────────────────────────────────────────

    def _judge(self, image_id: str, ai_response: dict) -> VerdictResult:
        """
        AI 응답에서 확률값 추출 후 판정

        ai_response 예시:
        {"normal": 0.85, "scratch": 0.08, "dent": 0.07}
        """
        probs: dict[str, float] = {k: v for k, v in ai_response.items() if k != "cmd_no"}
        normal_prob: float = probs.get("normal", 0.0)

        # normal 제외한 불량 확률 중 최대 불량 타입 추출 [수정: 미사용 max_defect_prob 제거]
        defect_probs = {k: v for k, v in probs.items() if k != "normal"}
        max_defect_type = max(defect_probs, key=defect_probs.get) if defect_probs else None

        # 전체 확률 중 최대값 (UNCERTAIN 판정용)
        max_prob = max(probs.values(), default=0.0)

        # 판정 규칙
        if normal_prob >= PASS_THRESHOLD:
            verdict = "PASS"
            defect_type = None
        elif max_prob < UNCERTAIN_THRESHOLD:
            verdict = "UNCERTAIN"
            defect_type = None
        else:
            verdict = "FAIL"
            defect_type = max_defect_type

        return VerdictResult(
            cmd_no=301,
            image_id=image_id,
            verdict=verdict,
            normal_prob=round(normal_prob, 4),
            max_prob=round(max_prob, 4),
            defect_type=defect_type,
            timestamp=datetime.now().isoformat(),
        )

    # ──────────────────────────────────────────────
    # DB 저장
    # ──────────────────────────────────────────────

    def _save_to_db(self, result: VerdictResult, elapsed_ms: int) -> None:
        try:
            self._db.insert_inspection_result(
                image_id=result["image_id"],
                verdict=result["verdict"],
                normal_prob=result["normal_prob"],
                max_prob=result["max_prob"],
                defect_type=result["defect_type"],
                timestamp=result["timestamp"],
            )
            self._db.insert_pipeline_log(
                image_id=result["image_id"],
                status="DONE",
                elapsed_ms=elapsed_ms,
                success=True,
            )
        except Exception as e:
            logger.error(f"[VerdictEngine] DB 저장 실패: {e}")

    # ──────────────────────────────────────────────
    # MFC 전송
    # ──────────────────────────────────────────────

    def _send_to_mfc(self, result: VerdictResult) -> None:
        # [수정] 상단 import로 이동, 매 호출마다 import 제거
        mfc_conn = self._get_mfc_conn()
        if mfc_conn is None:
            logger.warning("[VerdictEngine] MFC 연결 없음, 전송 스킵")
            return
        send_result_to_mfc(mfc_conn, dict(result))

    # ──────────────────────────────────────────────
    # 아두이노 전송 [추가]
    # ──────────────────────────────────────────────

    def _send_to_arduino(self, verdict: str) -> None:
        if self._arduino is None:
            return
        ok = self._arduino.send_verdict(verdict)
        if not ok:
            logger.warning(f"[VerdictEngine] 아두이노 전송 실패: verdict={verdict}")