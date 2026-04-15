# test_ai.py - AI 서버 통신 테스트용 임시 스크립트
# 여러 이미지를 순서대로 전송해서 추론 결과 확인

import json
import logging
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent))

from server.handlers.ai_client import AIClient

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s - %(message)s"
)

# 테스트할 이미지 파일 목록
IMAGE_FILES = ["1.png", "5.png", "6.png", "7.png", "8.png"]

def main():
    client = AIClient()

    print("=" * 40)
    print("AI 서버 통신 테스트 시작")
    print("=" * 40)

    # 연결 1회만
    print("\n[연결] AI 서버 연결 시도...")
    if not client.connect():
        print("❌ 연결 실패")
        return
    print("✅ 연결 성공\n")

    # 이미지 순서대로 전송
    for idx, filename in enumerate(IMAGE_FILES, start=1):
        print(f"[{idx}/{len(IMAGE_FILES)}] {filename} 전송 중...")

        # 이미지 파일 읽기
        try:
            with open(filename, "rb") as f:
                image_bytes = f.read()
        except FileNotFoundError:
            print(f"  ❌ 파일 없음: {filename}")
            continue

        # 추론 요청
        result = client.infer(image_bytes)

        if result is None:
            print(f"  ❌ 추론 실패\n")
        else:
            print(f"  ✅ 결과:")
            print(f"     추론시간  : {result.get('inference_ms')}ms")
            print(f"     normal    : {result.get('prob_normal'):.4f}")
            print(f"     crack     : {result.get('prob_crack'):.4f}")
            print(f"     hole      : {result.get('prob_hole'):.4f}")
            print(f"     rust      : {result.get('prob_rust'):.4f}")
            print(f"     scratch   : {result.get('prob_scratch'):.4f}\n")

    client.close()
    print("=" * 40)
    print("테스트 완료")

if __name__ == "__main__":
    main()