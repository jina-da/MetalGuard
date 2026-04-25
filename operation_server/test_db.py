# test_db.py - DB 연결 및 INSERT 테스트용 임시 스크립트
# 확인 후 삭제해도 됨

import logging
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent))

from server.db.db_manager import DBManager

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s - %(message)s"
)

def main():
    print("=" * 40)
    print("DB 연결 테스트 시작")
    print("=" * 40)

    db = DBManager()

    # 1. 연결 테스트
    print("\n[1] DB 연결 시도...")
    if not db.connect():
        print("❌ 연결 실패")
        return
    print("✅ 연결 성공")

    # 2. inspection_result INSERT 테스트
    print("\n[2] inspection_result INSERT 테스트...")
    new_id = db.insert_inspection_result(
        verdict="PASS",
        defect_class="normal",
        prob_normal=0.85,
        prob_crack=0.05,
        prob_hole=0.03,
        prob_rust=0.04,
        prob_scratch=0.03,
        max_prob=0.85,
        inference_ms=9.5,
        pipeline_ms=120.3,
        image_path=None,
        heatmap_path=None,
        model_version_id=3,
    )

    if new_id is None:
        print("❌ INSERT 실패")
    else:
        print(f"✅ INSERT 성공: id={new_id}")

    # 3. pipeline_log INSERT 테스트
    print("\n[3] pipeline_log INSERT 테스트...")
    success = db.insert_pipeline_log(
        inspection_id=new_id,
        is_sampled=True,
        capture_ms=5.0,
        transfer_ms=25.0,
        preprocess_ms=10.0,
        inference_ms=9.5,
        arduino_ms=30.0,
        servo_ms=60.0,
        total_ms=120.3,
    )
    print("✅ INSERT 성공" if success else "❌ INSERT 실패")

    db.close()
    print("\n" + "=" * 40)
    print("테스트 완료")

if __name__ == "__main__":
    main()