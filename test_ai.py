# test_ai.py - AI 서버 통신 테스트용 임시 스크립트
# 확정 프로토콜(PacketHeader) 기반, 연결 유지하면서 반복 전송 가능

import logging
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent))

from server.handlers.ai_client import AIClient

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s - %(message)s"
)

# 테스트할 이미지 파일 목록 (파일명, 임시 inspection_id)
IMAGE_FILES: list[tuple[str, int]] = [
    ("1.png",  1001),
    ("5.png",  1005),
    ("6.png",  1006),
    ("7.png",  1007),
    ("8.png",  1008),
]


def send_all(client: AIClient) -> None:
    """이미지 목록 전체 순서대로 전송."""
    print()
    for idx, (filename, inspection_id) in enumerate(IMAGE_FILES, start=1):
        print(f"  [{idx}/{len(IMAGE_FILES)}] {filename} (inspection_id={inspection_id}) 전송 중...")

        try:
            with open(filename, "rb") as f:
                image_bytes = f.read()
        except FileNotFoundError:
            print(f"    ❌ 파일 없음: {filename}\n")
            continue

        result = client.infer(image_bytes, inspection_id)
        _print_result(result, inspection_id)


def send_one(client: AIClient) -> None:
    """파일명 직접 입력해서 단건 전송."""
    print()
    print("  사용 가능한 파일:")
    for i, (f, iid) in enumerate(IMAGE_FILES, start=1):
        print(f"    {i}. {f} (inspection_id={iid})")

    raw = input("  번호 또는 파일명 입력: ").strip()

    # 번호 입력 처리
    if raw.isdigit():
        idx = int(raw) - 1
        if not (0 <= idx < len(IMAGE_FILES)):
            print("  ❌ 잘못된 번호")
            return
        filename, inspection_id = IMAGE_FILES[idx]
    else:
        filename = raw
        # IMAGE_FILES에 있으면 inspection_id 재사용, 없으면 임시 ID
        inspection_id = next(
            (iid for f, iid in IMAGE_FILES if f == filename), 9999
        )

    try:
        with open(filename, "rb") as f:
            image_bytes = f.read()
    except FileNotFoundError:
        print(f"  ❌ 파일 없음: {filename}")
        return

    print(f"  전송 중: {filename} (inspection_id={inspection_id})")
    result = client.infer(image_bytes, inspection_id)
    _print_result(result, inspection_id)


def _print_result(result: dict | None, inspection_id: int) -> None:
    """추론 결과 출력."""
    if result is None:
        print("    ❌ 추론 실패\n")
        return

    res_id = result.get("inspection_id")
    id_ok = "✅" if res_id == inspection_id else f"⚠️  불일치(수신={res_id})"

    print(f"    ✅ 결과:")
    print(f"       inspection_id : {res_id} {id_ok}")
    print(f"       추론시간      : {result.get('inference_ms')}ms")
    print(f"       normal        : {result.get('prob_normal'):.4f}")
    print(f"       crack         : {result.get('prob_crack'):.4f}")
    print(f"       hole          : {result.get('prob_hole'):.4f}")
    print(f"       rust          : {result.get('prob_rust'):.4f}")
    print(f"       scratch       : {result.get('prob_scratch'):.4f}")
    print(f"       model_version : {result.get('model_version_id')}\n")


def main() -> None:
    client = AIClient()

    print("=" * 40)
    print("AI 서버 통신 테스트")
    print("=" * 40)

    print("\n[연결] AI 서버 연결 시도...")
    if not client.connect():
        print("❌ 연결 실패")
        return
    print("✅ 연결 성공")

    # 메뉴 루프 (연결 유지)
    while True:
        print("\n  1. 전체 이미지 순서대로 전송")
        print("  2. 단건 전송")
        print("  q. 종료")
        cmd = input("  선택: ").strip().lower()

        if cmd == "1":
            send_all(client)
        elif cmd == "2":
            send_one(client)
        elif cmd == "q":
            break
        else:
            print("  ❌ 잘못된 입력")

    client.close()
    print("\n" + "=" * 40)
    print("테스트 종료")


if __name__ == "__main__":
    main()