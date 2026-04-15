# verdict_engine.py - 판정 엔진
# 역할: Queue에서 이미지 꺼내기 → AI 추론 요청 → PASS/FAIL/UNCERTAIN 판정
# 구조: 별도 쓰레드로 상시 동작, 이미지 1건당 타임아웃 쓰레드 생성

import queue
import threading
import logging
from datetime import datetime

import config
from server.handlers.ai_client import AIClient
from server.handlers.camera_handler import ImageTask

logger = logging.getLogger(__name__)

# 판정 결과 상수
VERDICT_PASS      = "PASS"
VERDICT_FAIL      = "FAIL"
VERDICT_UNCERTAIN = "UNCERTAIN"


class VerdictEngine:
    def __init__(self, image_queue: queue.Queue, ai_client: AIClient):
        """
        Args:
            image_queue: 카메라 핸들러가 적재한 이미지 Queue
            ai_client  : 이미 연결된 AI 서버 클라이언트
        """
        self.image_queue = image_queue
        self.ai_client   = ai_client
        self.running     = False

        # 판정 결과를 MFC/아두이노로 전달할 콜백 (나중에 등록)
        # send_result(task, verdict, ai_result) 형태로 호출됨
        self.on_result = None

    def start(self):
        """판정 엔진 쓰레드 시작."""
        self.running = True
        threading.Thread(
            target=self._run,
            name="VerdictEngineThread",
            daemon=True
        ).start()
        logger.info("판정 엔진 시작")

    def stop(self):
        self.running = False
        logger.info("판정 엔진 종료")

    def _run(self):
        """
        메인 루프: Queue에서 이미지 꺼내서 처리.
        Queue가 비어있으면 이미지 올 때까지 대기 (블로킹).
        """
        while self.running:
            try:
                # 1초 대기 후 없으면 다시 루프 (서버 종료 감지 위해 timeout 설정)
                task: ImageTask = self.image_queue.get(timeout=1.0)
                self._process(task)
            except queue.Empty:
                continue  # 이미지 없으면 다시 대기
            except Exception as e:
                logger.error(f"판정 엔진 오류: {e}")

    def _process(self, task: ImageTask):
        """
        이미지 1건 처리.
        타임아웃 쓰레드와 판정 쓰레드가 동시에 뜨고,
        먼저 완료된 쪽이 결과를 전송함.
        """
        # 결과가 이미 전송됐는지 추적 (타임아웃 vs 정상 판정 중복 방지)
        result_sent = threading.Event()

        # 타임아웃 쓰레드 시작
        timeout_thread = threading.Timer(
            interval=config.PIPELINE_TIMEOUT_MS / 1000,  # ms → 초 변환
            function=self._on_timeout,
            args=(task, result_sent)
        )
        timeout_thread.start()

        try:
            # AI 서버에 추론 요청
            logger.info(f"AI 추론 요청: mode={task.mode} client={task.client_id}")
            ai_result = self.ai_client.infer(task.image_bytes)

            if ai_result is None:
                logger.error("AI 추론 결과 없음 → FAIL 처리")
                verdict = VERDICT_FAIL
            else:
                verdict = self._judge(ai_result)

            # 타임아웃보다 먼저 왔으면 결과 전송
            if not result_sent.is_set():
                result_sent.set()         # 타임아웃 쓰레드가 중복 전송 못하게 막기
                timeout_thread.cancel()   # 타임아웃 쓰레드 취소
                self._send_result(task, verdict, ai_result)

        except Exception as e:
            logger.error(f"판정 처리 오류: {e}")
            if not result_sent.is_set():
                result_sent.set()
                timeout_thread.cancel()
                self._send_result(task, VERDICT_FAIL, None)

    def _judge(self, ai_result: dict) -> str:
        """
        AI 추론 결과로 PASS/FAIL/UNCERTAIN 판정.
        
        판정 기준 (config.py에서 관리):
          PASS      : normal 확률 >= PASS_THRESHOLD (0.80)
          UNCERTAIN : max_prob < UNCERTAIN_THRESHOLD (0.60)
          FAIL      : 위 두 조건 모두 해당 없음
        """
        prob_normal = ai_result.get("prob_normal", 0.0)
        
        # 5개 클래스 중 최대 확률값
        max_prob = max(
            ai_result.get("prob_normal",  0.0),
            ai_result.get("prob_crack",   0.0),
            ai_result.get("prob_hole",    0.0),
            ai_result.get("prob_rust",    0.0),
            ai_result.get("prob_scratch", 0.0),
        )

        if prob_normal >= config.PASS_THRESHOLD:
            return VERDICT_PASS
        elif max_prob < config.UNCERTAIN_THRESHOLD:
            return VERDICT_UNCERTAIN
        else:
            return VERDICT_FAIL

    def _on_timeout(self, task: ImageTask, result_sent: threading.Event):
        """
        150ms 초과 시 호출.
        정상 판정이 먼저 완료됐으면 아무것도 안 함.
        """
        if not result_sent.is_set():
            result_sent.set()
            logger.warning(
                f"타임아웃 발생: mode={task.mode} "
                f"client={task.client_id} → FAIL 처리"
            )
            self._send_result(task, VERDICT_FAIL, None, timeout=True)

    def _send_result(
        self,
        task: ImageTask,
        verdict: str,
        ai_result: dict | None,
        timeout: bool = False
    ):
        """
        판정 결과 전달.
        콜백(on_result)이 등록돼 있으면 호출 (MFC 전송, DB 저장 등).
        """
        pipeline_ms = (datetime.now() - task.received_at).total_seconds() * 1000

        logger.info(
            f"판정 완료: verdict={verdict} "
            f"pipeline={pipeline_ms:.1f}ms "
            f"timeout={timeout}"
        )

        if self.on_result:
            self.on_result(task, verdict, ai_result, timeout, pipeline_ms)