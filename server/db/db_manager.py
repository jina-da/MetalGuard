# db_manager.py - MariaDB 연동 담당
# 역할: 판정 결과 INSERT, 재분류 결과 INSERT, 파이프라인 로그 INSERT
# 연결 끊김 대응: INSERT 실패 시 재연결 후 1회 재시도 (wait_timeout 대비)

import logging
import pymysql
import pymysql.cursors

import config

logger = logging.getLogger(__name__)


class DBManager:
    def __init__(self):
        self.conn = None  # DB 커넥션 객체 (connect() 호출 전까지 None)

    def connect(self) -> bool:
        """DB 연결. 서버 시작 시 1회 호출."""
        try:
            self.conn = pymysql.connect(
                host=config.DB_HOST,
                port=config.DB_PORT,
                user=config.DB_USER,
                password=config.DB_PASSWORD,
                database=config.DB_NAME,
                charset="utf8mb4",
                cursorclass=pymysql.cursors.DictCursor,  # 결과를 dict로 반환
                autocommit=True                           # INSERT마다 자동 커밋
            )
            logger.info(f"DB 연결 성공: {config.DB_HOST}:{config.DB_PORT}")
            return True
        except Exception as e:
            logger.error(f"DB 연결 실패: {e}")
            return False

    def reconnect(self) -> bool:
        """
        DB 재연결.
        MariaDB wait_timeout(기본 8시간) 초과로 연결 끊길 경우 INSERT 실패 시 호출됨.
        기존 연결 닫고 새로 연결 시도.
        """
        logger.warning("DB 재연결 시도...")
        self.close()
        return self.connect()

    def insert_inspection_result(
        self,
        verdict: str,
        defect_class: str,
        prob_normal: float,
        prob_crack: float,
        prob_hole: float,
        prob_rust: float,
        prob_scratch: float,
        max_prob: float,
        inference_ms: float,
        pipeline_ms: float,
        timeout_flag: bool,
        image_path: str | None,
        heatmap_path: str | None,
        model_version_id: int,
    ) -> int | None:
        """
        inspection_result 테이블에 판정 결과 INSERT.

        Returns:
            int: 생성된 레코드 id
            None: 실패 시
        """
        sql = """
            INSERT INTO inspection_result (
                verdict, defect_class,
                prob_normal, prob_crack, prob_hole, prob_rust, prob_scratch,
                max_prob, inference_ms, pipeline_ms,
                timeout_flag, image_path, heatmap_path, model_version_id
            ) VALUES (
                %s, %s,
                %s, %s, %s, %s, %s,
                %s, %s, %s,
                %s, %s, %s, %s
            )
        """
        # params 분리: 재시도 시 동일 파라미터 재사용
        params = (
            verdict, defect_class,
            prob_normal, prob_crack, prob_hole, prob_rust, prob_scratch,
            max_prob, inference_ms, pipeline_ms,
            1 if timeout_flag else 0,
            image_path, heatmap_path, model_version_id
        )
        try:
            with self.conn.cursor() as cursor:
                cursor.execute(sql, params)
                new_id = cursor.lastrowid  # INSERT된 레코드 id
                logger.info(f"inspection_result INSERT 완료: id={new_id} verdict={verdict}")
                return new_id
        except Exception as e:
            # 연결 끊김 등 INSERT 실패 시 재연결 후 1회 재시도
            logger.warning(f"inspection_result INSERT 실패, 재연결 후 재시도: {e}")
            if self.reconnect():
                try:
                    with self.conn.cursor() as cursor:
                        cursor.execute(sql, params)
                        new_id = cursor.lastrowid
                        logger.info(f"inspection_result INSERT 재시도 성공: id={new_id}")
                        return new_id
                except Exception as e2:
                    logger.error(f"inspection_result INSERT 재시도 실패: {e2}")
            return None

    def insert_pipeline_log(
        self,
        inspection_id: int,
        is_sampled: bool,
        capture_ms: float | None,
        transfer_ms: float | None,
        preprocess_ms: float | None,
        inference_ms: float | None,
        arduino_ms: float | None,
        servo_ms: float | None,
        total_ms: float,
    ) -> bool:
        """
        pipeline_log 테이블에 로그 INSERT.
        저장 조건:
          - timeout_flag=1 또는 total_ms > 150 → 전수 저장
          - 정상 건 → 100건당 1건 샘플링 (is_sampled=True)
        """
        sql = """
            INSERT INTO pipeline_log (
                inspection_id, is_sampled,
                capture_ms, transfer_ms, preprocess_ms,
                inference_ms, arduino_ms, servo_ms, total_ms
            ) VALUES (
                %s, %s,
                %s, %s, %s,
                %s, %s, %s, %s
            )
        """
        # params 분리: 재시도 시 동일 파라미터 재사용
        params = (
            inspection_id, 1 if is_sampled else 0,
            capture_ms, transfer_ms, preprocess_ms,
            inference_ms, arduino_ms, servo_ms, total_ms
        )
        try:
            with self.conn.cursor() as cursor:
                cursor.execute(sql, params)
                logger.info(f"pipeline_log INSERT 완료: inspection_id={inspection_id}")
                return True
        except Exception as e:
            # 연결 끊김 등 INSERT 실패 시 재연결 후 1회 재시도
            logger.warning(f"pipeline_log INSERT 실패, 재연결 후 재시도: {e}")
            if self.reconnect():
                try:
                    with self.conn.cursor() as cursor:
                        cursor.execute(sql, params)
                        logger.info(f"pipeline_log INSERT 재시도 성공: inspection_id={inspection_id}")
                        return True
                except Exception as e2:
                    logger.error(f"pipeline_log INSERT 재시도 실패: {e2}")
            return False

    def insert_reclassify_result(
        self,
        inspection_id: int,
        final_verdict: str,
        final_defect_class: str,
        confidence: float,
        reason: str,
    ) -> bool:
        """재분류 결과 INSERT. MFC에서 수동 판정 확정 시 호출."""
        sql = """
            INSERT INTO reclassify_result (
                inspection_id, final_verdict, final_defect_class, confidence, reason
            ) VALUES (%s, %s, %s, %s, %s)
        """
        # params 분리: 재시도 시 동일 파라미터 재사용
        params = (
            inspection_id, final_verdict,
            final_defect_class, confidence, reason
        )
        try:
            with self.conn.cursor() as cursor:
                cursor.execute(sql, params)
                logger.info(
                    f"reclassify_result INSERT 완료: "
                    f"inspection_id={inspection_id} final={final_verdict}"
                )
                return True
        except Exception as e:
            # 연결 끊김 등 INSERT 실패 시 재연결 후 1회 재시도
            logger.warning(f"reclassify_result INSERT 실패, 재연결 후 재시도: {e}")
            if self.reconnect():
                try:
                    with self.conn.cursor() as cursor:
                        cursor.execute(sql, params)
                        logger.info(f"reclassify_result INSERT 재시도 성공: inspection_id={inspection_id}")
                        return True
                except Exception as e2:
                    logger.error(f"reclassify_result INSERT 재시도 실패: {e2}")
            return False

    def close(self) -> None:
        """DB 연결 종료."""
        if self.conn:
            self.conn.close()
            self.conn = None
            logger.info("DB 연결 종료")