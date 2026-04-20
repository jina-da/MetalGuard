"""
verdict_engine.py
Queue에서 ImageTask를 꺼내 AI 추론 → 판정 → DB 저장 → MFC/아두이노 전송

[철판 단위 종합 판정 흐름]
1. 장별로 AI 추론 + DB 저장 (inspection_result)
2. plate_id별로 결과 버퍼에 누적
3. N장(total_shots) 다 모이면 종합 판정 → MFC/아두이노 전송

[종합 판정 로직]
- N장 중 FAIL 1개라도 있으면 → FAIL
- 전부 PASS → PASS
- FAIL 없고 UNCERTAIN 있으면 → UNCERTAIN
"""

import logging
import queue
import threading
import time
from datetime import datetime
from typing import Callable

import config
from constants import CmdID
from server.handlers.ai_client import AIClient
from server.handlers.mfc_handler import send_result_to_mfc
from server.handlers.camera_handler import ImageTask
from server.engine.arduino_serial import ArduinoSerial
from server.db.db_manager import DBManager

logger = logging.getLogger(__name__)

# AI 응답 불량 확률 키 목록
DEFECT_KEYS = ["prob_crack", "prob_hole", "prob_rust", "prob_scratch"]


class PlateBuffer:
    """
    철판 1개 분량의 추론 결과를 누적하는 버퍼.
    plate_id별로 생성되어 _plate_buffer dict에 저장됨.
    """
    def __init__(self, plate_id: int, total_shots: int):
        self.plate_id = plate_id
        self.total_shots = total_shots                   # 수신 예정 총 장수
        self.results: list[dict] = []                   # 장별 추론 결과 누적
        self.verdicts: list[str] = []                   # 장별 판정 결과 누적
        self.defect_classes: list[str] = []             # 장별 불량 클래스 누적
        self.first_received_at: float = time.monotonic()  # 첫 장 수신 시점 (파이프라인 측정용)

    def add(self, verdict: str, defect_class: str, ai_response: dict) -> None:
        """장별 결과 추가."""
        self.results.append(ai_response)
        self.verdicts.append(verdict)
        self.defect_classes.append(defect_class)

    def is_complete(self) -> bool:
        """N장이 모두 수신됐는지 확인."""
        return len(self.results) >= self.total_shots

    def get_final_verdict(self) -> tuple[str, str]:
        """
        종합 판정.
        - FAIL 1개라도 있으면 → FAIL (가장 심한 불량 클래스 반환)
        - FAIL 없고 UNCERTAIN 있으면 → UNCERTAIN
        - 전부 PASS → PASS

        Returns:
            (final_verdict, final_defect_class)
        """
        if "FAIL" in self.verdicts:
            # FAIL 중 가장 먼저 나온 불량 클래스 반환
            fail_idx = self.verdicts.index("FAIL")
            return "FAIL", self.defect_classes[fail_idx]
        elif "UNCERTAIN" in self.verdicts:
            return "UNCERTAIN", "unknown"
        else:
            return "PASS", "normal"


class VerdictEngine:
    """
    이미지 Queue를 소비하여 AI 추론 → 판정 → DB 저장 → MFC/아두이노 전송 루프.
    camera_handler가 image_queue에 ImageTask를 적재하면,
    이 엔진이 별도 쓰레드에서 지속적으로 소비함.
    """

    def __init__(
        self,
        image_queue: queue.Queue,
        ai_client: AIClient,
        db_manager: DBManager,
        mfc_conn_getter: Callable,            # () -> socket | None
        arduino: ArduinoSerial | None = None,
        model_version_id: int = 1,
    ):
        self._queue = image_queue
        self._ai = ai_client
        self._db = db_manager
        self._get_mfc_conn = mfc_conn_getter
        self._arduino = arduino
        self._model_version_id = model_version_id
        self._running = False
        self._thread: threading.Thread | None = None
        self._normal_count = 0  # 정상 건 샘플링 카운터 (100건당 1건 pipeline_log 저장)

        # plate_id별 결과 버퍼 (plate_id → PlateBuffer)
        self._plate_buffer: dict[int, PlateBuffer] = {}
        self._plate_lock = threading.Lock()  # 멀티스레드 안전

    # ──────────────────────────────────────────────
    # 엔진 시작 / 정지
    # ──────────────────────────────────────────────

    def start(self) -> None:
        """판정 루프 쓰레드 시작"""
        self._running = True
        self._thread = threading.Thread(
            target=self._run_loop, daemon=True, name="VerdictEngine"
        )
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
                task: ImageTask = self._queue.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                self._process(task)
            except Exception as e:
                logger.error(f"[VerdictEngine] 처리 중 예외: {e}")
            finally:
                self._queue.task_done()

    def _process(self, task: ImageTask) -> None:
        """
        단일 이미지 처리.
        장별 AI 추론 + DB 저장 후 plate 버퍼에 누적.
        N장 완료 시 종합 판정 → MFC/아두이노 전송.
        """
        verdict_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # 1. AI 추론 요청 (INFER_REQ → INFER_RES)
        ai_response = self._ai.infer(task.image_bytes, task.inspection_id)

        timeout_flag = ai_response is None
        if timeout_flag:
            logger.error(f"[VerdictEngine] AI 응답 없음 (타임아웃): client={task.client_id}")
            ai_response = {}

        # 2. AI 서버가 준 inference_ms 사용
        inference_ms: float = ai_response.get("inference_ms", 0.0)

        # 3. 장별 판정
        verdict, defect_class = self._judge(ai_response)

        # 4. 장별 파이프라인 소요시간 (카메라 수신 시점 기준)
        pipeline_ms = (time.monotonic() - task.received_at) * 1000
        if pipeline_ms > config.PIPELINE_TIMEOUT_MS:
            logger.warning(
                f"[VerdictEngine] 장별 파이프라인 초과: {pipeline_ms:.1f}ms "
                f"shot={task.shot_index}/{task.total_shots} client={task.client_id}"
            )

        # 5. 장별 터미널 로그
        logger.info(
            f"[장별판정] {verdict_time} | plate={task.plate_id} "
            f"shot={task.shot_index}/{task.total_shots} | "
            f"{verdict} | defect={defect_class} | "
            f"normal={ai_response.get('prob_normal', 0):.4f} | "
            f"inference={inference_ms:.1f}ms | pipeline={pipeline_ms:.1f}ms"
        )

        # 6. 장별 DB 저장 (inspection_result)
        inspection_id = self._save_inspection(
            verdict, defect_class, ai_response,
            inference_ms, pipeline_ms, timeout_flag
        )

        # 7. pipeline_log DB 저장 (샘플링 적용)
        if inspection_id:
            self._save_pipeline_log(inspection_id, inference_ms, pipeline_ms, timeout_flag)

        # 8. plate 버퍼에 누적 → N장 완료 시 종합 판정
        # plate_id가 없으면 (구버전 호환) 즉시 전송
        if task.plate_id is None:
            self._send_to_mfc(verdict, defect_class, verdict_time)
            arduino_verdict = "TIMEOUT" if timeout_flag else verdict
            self._send_to_arduino(arduino_verdict)
            return

        self._accumulate_and_finalize(task, verdict, defect_class, ai_response, timeout_flag)

    def _accumulate_and_finalize(
        self,
        task: ImageTask,
        verdict: str,
        defect_class: str,
        ai_response: dict,
        timeout_flag: bool,
    ) -> None:
        """
        plate 버퍼에 장별 결과 누적.
        N장 완료 or 타임아웃 시 종합 판정 후 MFC/아두이노 전송.
        """
        with self._plate_lock:
            plate_id = task.plate_id

            # 버퍼 없으면 새로 생성
            if plate_id not in self._plate_buffer:
                self._plate_buffer[plate_id] = PlateBuffer(plate_id, task.total_shots)

            buf = self._plate_buffer[plate_id]
            buf.add(verdict, defect_class, ai_response)

            # N장 완료 or 타임아웃 → 종합 판정
            if buf.is_complete() or timeout_flag:
                final_verdict, final_defect_class = buf.get_final_verdict()
                verdict_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                logger.info(
                    f"[종합판정] plate={plate_id} | {final_verdict} | "
                    f"defect={final_defect_class} | "
                    f"shots={len(buf.results)}/{buf.total_shots}"
                )

                # MFC 전송
                self._send_to_mfc(final_verdict, final_defect_class, verdict_time)

                # 아두이노 전송
                arduino_verdict = "TIMEOUT" if timeout_flag else final_verdict
                self._send_to_arduino(arduino_verdict)

                # 버퍼 제거 (메모리 정리)
                del self._plate_buffer[plate_id]

    # ──────────────────────────────────────────────
    # 판정 로직
    # ──────────────────────────────────────────────

    def _judge(self, ai_response: dict) -> tuple[str, str]:
        """
        AI 응답 확률값으로 장별 판정.

        Returns:
            (verdict, defect_class)
            - verdict: "PASS" | "FAIL" | "UNCERTAIN"
            - defect_class: "normal" | "crack" | "hole" | "rust" | "scratch" | "unknown"
        """
        prob_normal: float = ai_response.get("prob_normal", 0.0)
        defect_probs = {k: ai_response.get(k, 0.0) for k in DEFECT_KEYS}
        max_defect_key = max(defect_probs, key=defect_probs.get)
        max_prob = max([prob_normal] + list(defect_probs.values()))

        if prob_normal >= config.PASS_THRESHOLD:
            return "PASS", "normal"
        elif max_prob < config.UNCERTAIN_THRESHOLD:
            return "UNCERTAIN", "unknown"
        else:
            return "FAIL", max_defect_key.replace("prob_", "")

    # ──────────────────────────────────────────────
    # DB 저장
    # ──────────────────────────────────────────────

    def _save_inspection(
        self,
        verdict: str,
        defect_class: str,
        ai_response: dict,
        inference_ms: float,
        pipeline_ms: float,
        timeout_flag: bool,
    ) -> int | None:
        """inspection_result INSERT, 생성된 id 반환"""
        return self._db.insert_inspection_result(
            verdict=verdict,
            defect_class=defect_class,
            prob_normal=ai_response.get("prob_normal", 0.0),
            prob_crack=ai_response.get("prob_crack", 0.0),
            prob_hole=ai_response.get("prob_hole", 0.0),
            prob_rust=ai_response.get("prob_rust", 0.0),
            prob_scratch=ai_response.get("prob_scratch", 0.0),
            max_prob=max(ai_response.get(k, 0.0) for k in ["prob_normal"] + DEFECT_KEYS),
            inference_ms=round(inference_ms, 2),
            pipeline_ms=round(pipeline_ms, 2),
            timeout_flag=timeout_flag,
            image_path=None,
            heatmap_path=None,
            model_version_id=ai_response.get("model_version_id", self._model_version_id),
        )

    def _save_pipeline_log(
        self,
        inspection_id: int,
        inference_ms: float,
        pipeline_ms: float,
        timeout_flag: bool,
    ) -> None:
        """
        pipeline_log INSERT.
        - 타임아웃 또는 150ms 초과 → 전수 저장
        - 정상 건 → 100건당 1건 샘플링
        """
        force_save = timeout_flag or pipeline_ms > config.PIPELINE_TIMEOUT_MS

        if not force_save:
            self._normal_count += 1
            if self._normal_count % 100 != 0:
                return

        self._db.insert_pipeline_log(
            inspection_id=inspection_id,
            is_sampled=not force_save,
            capture_ms=None,
            transfer_ms=None,
            preprocess_ms=None,
            inference_ms=round(inference_ms, 2),
            arduino_ms=None,
            servo_ms=None,
            total_ms=round(pipeline_ms, 2),
        )

    # ──────────────────────────────────────────────
    # MFC 전송 (CmdID.RESULT_SEND: 301)
    # ──────────────────────────────────────────────

    def _send_to_mfc(self, verdict: str, defect_class: str, verdict_time: str) -> None:
        """
        MFC로 판정 결과 전송.
        config.SEND_RESULT_TO_MFC = False 이면 스킵.
        MFC 미연결 시 조용히 스킵.
        """
        mfc_conn = self._get_mfc_conn()
        if mfc_conn is None:
            return

        payload = {
            "timestamp": verdict_time,
            "verdict": verdict,
            "defect_class": defect_class,
        }
        send_result_to_mfc(mfc_conn, payload)

    # ──────────────────────────────────────────────
    # 아두이노 전송
    # ──────────────────────────────────────────────

    def _send_to_arduino(self, verdict: str) -> None:
        """
        아두이노로 판정 결과 시리얼 전송.
        PASS→P\\n / FAIL→F\\n / UNCERTAIN→U\\n / TIMEOUT→T\\n
        """
        if self._arduino is None:
            return
        ok = self._arduino.send_verdict(verdict)
        if not ok:
            logger.warning(f"[VerdictEngine] 아두이노 전송 실패: verdict={verdict}")