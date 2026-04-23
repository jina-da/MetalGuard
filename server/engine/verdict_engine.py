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
import os
import queue
import threading
import time
from datetime import datetime
from typing import Callable

import config
from constants import CmdID, build_packet
from server.handlers.ai_client import AIClient
from server.handlers.mfc_handler import send_result_to_mfc
from server.handlers.camera_handler import ImageTask
from server.db.db_manager import DBManager

logger = logging.getLogger(__name__)

# 이미지 저장 루트 경로 (WSL → Windows 경로)
IMAGE_SAVE_ROOT = "/mnt/c/Users/lms/Desktop/metalguard_image"

# AI 응답 불량 확률 키 목록
DEFECT_KEYS = ["prob_crack", "prob_hole", "prob_rust", "prob_scratch"]


class PlateBuffer:
    """
    철판 1개 분량의 추론 결과를 누적하는 버퍼.
    plate_id별로 생성되어 _plate_buffer dict에 저장됨.
    """
    def __init__(self, plate_id: int, total_shots: int, is_reclassify: bool = False):
        self.plate_id = plate_id
        self.total_shots = total_shots                    # 수신 예정 총 장수
        self.results: list[dict] = []                    # 장별 추론 결과 누적
        self.verdicts: list[str] = []                    # 장별 판정 결과 누적
        self.defect_classes: list[str] = []              # 장별 불량 클래스 누적
        self.first_received_at: float = time.monotonic() # 첫 장 수신 시점 (파이프라인 측정용)
        self.is_reclassify: bool = is_reclassify

    def add(self, verdict: str, defect_class: str, ai_response: dict) -> None:
        """장별 결과 추가."""
        self.results.append(ai_response)
        self.verdicts.append(verdict)
        self.defect_classes.append(defect_class)

    def is_complete(self) -> bool:
        """N장이 모두 수신됐는지 확인."""
        return len(self.results) >= self.total_shots

    def get_final_verdict(self, is_reclassify: bool = False) -> tuple[str, str]:
        fail_count = self.verdicts.count("FAIL")
        uncertain_count = self.verdicts.count("UNCERTAIN")

        if fail_count >= 2:
            # FAIL 2장 이상 → FAIL (가장 많이 나온 defect_class 사용)
            fail_defects = [
                self.defect_classes[i]
                for i, v in enumerate(self.verdicts) if v == "FAIL"
            ]
            majority_defect = max(set(fail_defects), key=fail_defects.count)
            return "FAIL", majority_defect
        elif fail_count == 0 and uncertain_count >= 2:
            # FAIL 없고 UNCERTAIN 2장 이상 → UNCERTAIN (재분류면 FAIL)
            if is_reclassify:
                return "FAIL", "unknown"
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
        mfc_conn_getter: Callable,       # () -> socket | None
        arduino_conn_getter: Callable,   # () -> socket | None
        model_version_id: int = 1,
    ):
        self._queue = image_queue
        self._ai = ai_client
        self._db = db_manager
        self._get_mfc_conn = mfc_conn_getter         # MFC 소켓 getter
        self._get_arduino_conn = arduino_conn_getter  # 아두이노 소켓 getter
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
                self._cleanup_expired_buffers()  # Queue 비었을 때 만료 버퍼 정리
                continue

            try:
                self._process(task)
            except Exception as e:
                logger.error(f"[VerdictEngine] 처리 중 예외: {e}")
            finally:
                self._queue.task_done()

    def _cleanup_expired_buffers(self) -> None:
        """만료된 PlateBuffer 정리. Queue 비었을 때 주기적으로 호출."""
        now = time.monotonic()
        with self._plate_lock:
            expired = [
                pid for pid, buf in self._plate_buffer.items()
                if now - buf.first_received_at > config.PLATE_BUFFER_EXPIRE_SEC
            ]
            for plate_id in expired:
                buf = self._plate_buffer[plate_id]
                verdict_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                if len(buf.results) == 0:
                    # 버퍼만 생성되고 아무것도 안 쌓인 경우
                    logger.warning(f"[VerdictEngine] 버퍼 만료 (0장): plate={plate_id}, 전송 스킵")
                    del self._plate_buffer[plate_id]
                    continue

                # 모인 장수로 종합 판정
                final_verdict, final_defect_class = buf.get_final_verdict(is_reclassify=buf.is_reclassify)

                logger.warning(
                    f"[VerdictEngine] 버퍼 만료 강제 판정: plate={plate_id} | "
                    f"{final_verdict} | shots={len(buf.results)}/{buf.total_shots}"
                )

                self._send_to_mfc(final_verdict, final_defect_class, verdict_time)
                self._send_to_arduino(final_verdict)
                del self._plate_buffer[plate_id]

    def _process(self, task: ImageTask) -> None:
        """
        단일 이미지 처리.
        장별 AI 추론 + DB 저장 후 plate 버퍼에 누적.
        N장 완료 시 종합 판정 → MFC/아두이노 전송.
        """
        verdict_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # 1. AI 추론 요청 (INFER_REQ → INFER_RES)
        ai_response = self._ai.infer(task.image_bytes, task.inspection_id)

        if ai_response is None:
            logger.error(f"[VerdictEngine] AI 응답 없음, 해당 장 스킵: client={task.client_id}")
            return

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

        mode = "재분류" if task.cmd_id == CmdID.IMG_RECLASSIFY else "분류"

        # 5. 장별 DB 저장 (inspection_result) - 로그에 db_id 포함하기 위해 먼저 실행
        inspection_id = self._save_inspection(
            verdict, defect_class, ai_response,
            inference_ms, pipeline_ms,
            plate_id=task.plate_id,
            is_reclassify=task.cmd_id == CmdID.IMG_RECLASSIFY,
        )

        # 6. 장별 터미널 로그 (cmd_id + db_id 포함, 1줄 요약)
        logger.info(
            f"[장별판정][{mode}] plate={task.plate_id} "
            f"shot={task.shot_index}/{task.total_shots} | "
            f"{verdict} | defect={defect_class} | "
            f"normal={ai_response.get('prob_normal', 0):.4f} | "
            f"infer={inference_ms:.1f}ms | pipe={pipeline_ms:.1f}ms | "
            f"cmd={task.cmd_id} | db={inspection_id}"
        )

        # 7. 이미지 저장 (분류/재분류 폴더 분리, verdict별 하위 폴더)
        self._save_image(task, verdict, defect_class)

        # 8. pipeline_log DB 저장 (샘플링 적용)
        if inspection_id:
            self._save_pipeline_log(inspection_id, inference_ms, pipeline_ms)

        # 8. plate 버퍼에 누적 → N장 완료 시 종합 판정
        # plate_id가 없으면 (구버전 호환) 즉시 전송
        if task.plate_id is None:
            self._send_to_mfc(verdict, defect_class, verdict_time)
            return

        is_reclassify = task.cmd_id == CmdID.IMG_RECLASSIFY 
        self._accumulate_and_finalize(task, verdict, defect_class, ai_response, is_reclassify)

    def _accumulate_and_finalize(
        self,
        task: ImageTask,
        verdict: str,
        defect_class: str,
        ai_response: dict,
        is_reclassify: bool = False,
    ) -> None:
        """
        plate 버퍼에 장별 결과 누적.
        N장 완료 or 타임아웃 시 종합 판정 후 MFC/아두이노 전송.
        """
        with self._plate_lock:
            plate_id = task.plate_id

            # 버퍼 없으면 새로 생성
            if plate_id not in self._plate_buffer:
                self._plate_buffer[plate_id] = PlateBuffer(plate_id, task.total_shots, is_reclassify=is_reclassify)

            buf = self._plate_buffer[plate_id]
            buf.add(verdict, defect_class, ai_response)

            # N장 완료 or 타임아웃 → 종합 판정
            if buf.is_complete():
                final_verdict, final_defect_class = buf.get_final_verdict(is_reclassify=is_reclassify)
                verdict_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                mode = "재분류" if is_reclassify else "분류"
                logger.info(
                    f"[종합판정][{mode}] plate={plate_id} | {final_verdict} | "
                    f"defect={final_defect_class} | "
                    f"shots={len(buf.results)}/{buf.total_shots}"
                )

                # MFC 전송
                self._send_to_mfc(final_verdict, final_defect_class, verdict_time)

                # 아두이노 전송
                self._send_to_arduino(final_verdict)

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

    def _save_image(self, task: ImageTask, verdict: str, defect_class: str) -> None:
        """
        이미지를 로컬에 저장.
        저장 경로: {IMAGE_SAVE_ROOT}/{classify|reclassify}/{pass|fail|uncertain}/
        파일명: plate{plate_id}_shot{shot_index}_{defect_class}_{timestamp}.jpg
        """
        try:
            mode_dir = "reclassify" if task.cmd_id == CmdID.IMG_RECLASSIFY else "classify"
            verdict_dir = verdict.lower()  # pass / fail / uncertain

            save_dir = os.path.join(IMAGE_SAVE_ROOT, mode_dir, verdict_dir)
            os.makedirs(save_dir, exist_ok=True)  # 폴더 없으면 자동 생성

            # 파일명: plate1_shot2_scratch_20260423_09-39-53.jpg
            ts = datetime.now().strftime("%Y%m%d_%H-%M-%S")
            filename = (
                f"plate{task.plate_id}_shot{task.shot_index}"
                f"_{defect_class}_{ts}.jpg"
            )
            filepath = os.path.join(save_dir, filename)

            with open(filepath, "wb") as f:
                f.write(task.image_bytes)

            logger.debug(f"[이미지 저장] {filepath}")

        except Exception as e:
            logger.error(f"[이미지 저장 실패] plate={task.plate_id} shot={task.shot_index}: {e}")

    def _save_inspection(
        self,
        verdict: str,
        defect_class: str,
        ai_response: dict,
        inference_ms: float,
        pipeline_ms: float,
        plate_id: int | None = None,
        is_reclassify: bool = False,
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
            image_path=None,
            heatmap_path=None,
            model_version_id=ai_response.get("model_version_id", self._model_version_id),
            plate_id=plate_id,
            is_reclassify=is_reclassify,
        )

    def _save_pipeline_log(
        self,
        inspection_id: int,
        inference_ms: float,
        pipeline_ms: float,
    ) -> None:
        """
        pipeline_log INSERT.
        - 정상 건 → 100건당 1건 샘플링
        """
        self._normal_count += 1
        if self._normal_count % 100 != 0:
            return

        self._db.insert_pipeline_log(
            inspection_id=inspection_id,
            is_sampled=True,
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
    # 아두이노 전송 (VERDICT_PASS/FAIL/UNCERTAIN)
    # ──────────────────────────────────────────────

    def _send_to_arduino(self, verdict: str) -> None:
        """
        아두이노 클라이언트(희창님 PC)로 판정 결과 패킷 전송.
        PASS→VERDICT_PASS(201) / FAIL→VERDICT_FAIL(202)
        UNCERTAIN→VERDICT_UNCERTAIN(203)
        body 없이 cmdId만 전송, 희창님 PC가 받아서 아두이노로 시리얼 전달.
        """
        arduino_conn = self._get_arduino_conn()
        if arduino_conn is None:
            logger.warning("[VerdictEngine] 아두이노 클라이언트 미연결, 전송 스킵")
            return

        # verdict → cmdId 매핑
        verdict_cmd_map: dict[str, int] = {
            "PASS":      CmdID.VERDICT_PASS,
            "FAIL":      CmdID.VERDICT_FAIL,
            "UNCERTAIN": CmdID.VERDICT_UNCERTAIN,
        }
        cmd_id = verdict_cmd_map.get(verdict)
        if cmd_id is None:
            logger.error(f"[VerdictEngine] 알 수 없는 판정값: {verdict}")
            return

        try:
            # body 없이 cmdId만 전송
            packet = build_packet(cmd_id, b"")
            arduino_conn.sendall(packet)
            logger.debug(f"[VerdictEngine] 아두이노 전송: {verdict} (cmdId={cmd_id})")
        except Exception as e:
            logger.error(f"[VerdictEngine] 아두이노 전송 실패: {e}")