"""
test_ai_server.py — AI 추론 서버 연동 테스트 클라이언트
지나(운용 서버)와 연동 전 단독 테스트용

프로토콜 (지나 서버 확정 스펙):
  INFER_REQ (101):
    [8B: PacketHeader] + [JSON {"inspection_id": N}] + [4B: 이미지크기] + [이미지]
  INFER_RES (102):
    [8B: PacketHeader] + [JSON 바디]

사용법:
  python test_ai_server.py --image test_images/1.png
  python test_ai_server.py --folder test_images/
  python test_ai_server.py --image test_images/1.png --host 192.168.0.10
"""
import io
import json
import struct
import socket
import time
import argparse
import os
from PIL import Image

# ============================================================
#  프로토콜 상수
# ============================================================
SIGNATURE     = 0x4D47
HEADER_FORMAT = ">HHI"   # big-endian: uint16 + uint16 + uint32
HEADER_SIZE   = 8
INFER_REQ     = 101
INFER_RES     = 102


# ============================================================
#  헬퍼
# ============================================================
def recv_exact(sock, size: int) -> bytes:
    buf = b""
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise ConnectionError("소켓 연결 끊김")
        buf += chunk
    return buf


def image_to_bytes(image_path, img_size=260):
    img = Image.open(image_path).convert('L').convert('RGB')
    img = img.resize((img_size, img_size))
    buf = io.BytesIO()
    img.save(buf, format='JPEG', quality=95)
    return buf.getvalue()


# ============================================================
#  INFER_REQ 전송 → INFER_RES 수신
# ============================================================
def send_infer_request(sock, image_bytes, inspection_id=1):
    """
    새 프로토콜로 추론 요청 전송 및 응답 수신.
    sock: 기존 연결 소켓 (연결 유지 방식)
    """
    t0 = time.perf_counter()

    # ── INFER_REQ 송신 ──────────────────────────────────────
    # 1) JSON 바디
    body       = json.dumps({"inspection_id": inspection_id}).encode("utf-8")
    # 2) PacketHeader: [signature=0x4D47] + [cmdId=101] + [bodySize]
    header     = struct.pack(HEADER_FORMAT, SIGNATURE, INFER_REQ, len(body))
    # 3) 이미지 크기 + 이미지
    img_header = struct.pack(">I", len(image_bytes))

    sock.sendall(header + body + img_header + image_bytes)

    # ── INFER_RES 수신 ──────────────────────────────────────
    # 1) PacketHeader 수신
    res_header                   = recv_exact(sock, HEADER_SIZE)
    signature, cmd_id, body_size = struct.unpack(HEADER_FORMAT, res_header)

    if signature != SIGNATURE:
        raise ValueError(f"잘못된 signature: 0x{signature:04X}")
    if cmd_id != INFER_RES:
        raise ValueError(f"예상치 못한 cmdId: {cmd_id}")

    # 2) JSON 바디 수신
    res_body = recv_exact(sock, body_size)
    result   = json.loads(res_body.decode("utf-8"))

    rtt_ms          = (time.perf_counter() - t0) * 1000
    result['rtt_ms'] = round(rtt_ms, 2)
    return result


# ============================================================
#  결과 출력
# ============================================================
def print_result(fname, result):
    classes = ['normal', 'crack', 'hole', 'rust', 'scratch']
    probs   = {c: result.get(f'prob_{c}', 0) for c in classes}
    pred    = max(probs, key=probs.get)
    verdict = result.get('verdict', '-')

    verdict_icons = {'PASS': '✅', 'FAIL': '❌', 'UNCERTAIN': '⚠️'}
    icon = verdict_icons.get(verdict, '?')

    print(f"\n{'='*55}")
    print(f" {fname}")
    print(f"{'='*55}")
    print(f"  판정 결과    : {icon} {verdict}")
    print(f"  예측 클래스  : {pred} ({probs[pred]:.4f})")
    print(f"  추론 시간    : {result.get('inference_ms', 0):.1f} ms")
    print(f"  RTT          : {result.get('rtt_ms', 0):.1f} ms")
    print(f"  inspection_id: {result.get('inspection_id', '-')}")
    print(f"  model_ver_id : {result.get('model_version_id', '-')}")
    print(f"\n  클래스별 확률:")
    for cls in classes:
        p    = probs[cls]
        bar  = '█' * int(p * 30)
        mark = ' ←' if cls == pred else ''
        print(f"    {cls:<10} {bar:<30} {p:.4f}{mark}")

    if 'error' in result:
        print(f"\n  ⚠️  오류: {result['error']}")


# ============================================================
#  메인
# ============================================================
def main():
    parser = argparse.ArgumentParser(description='AI 서버 연동 테스트')
    parser.add_argument('--image',  type=str, help='단건 이미지 경로')
    parser.add_argument('--folder', type=str, help='폴더 경로 (일괄)')
    parser.add_argument('--host',   type=str, default='127.0.0.1')
    parser.add_argument('--port',   type=int, default=9000)
    args = parser.parse_args()

    if not args.image and not args.folder:
        parser.print_help()
        return

    # 테스트 이미지 목록 수집
    images = []
    if args.image:
        images = [args.image]
    elif args.folder:
        exts   = ('.jpg', '.jpeg', '.png', '.bmp')
        images = sorted([
            os.path.join(args.folder, f)
            for f in os.listdir(args.folder)
            if f.lower().endswith(exts)
        ])

    print(f"AI 서버 연결: {args.host}:{args.port}")
    print(f"테스트 이미지: {len(images)}장")

    # 연결 1회 후 연결 유지 방식으로 전체 테스트
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(30.0)   # 워밍업 20초 + 여유
        sock.connect((args.host, args.port))
        print(f"✅ 연결 성공\n")
    except Exception as e:
        print(f"❌ 연결 실패: {e}")
        return

    for i, img_path in enumerate(images, start=1):
        fname       = os.path.basename(img_path)
        try:
            image_bytes = image_to_bytes(img_path)
            result      = send_infer_request(sock, image_bytes, inspection_id=1000+i)
            print_result(fname, result)
        except Exception as e:
            print(f"  [{fname}] 오류: {e}")
            break   # 소켓 오류 시 이후 요청도 불가능하므로 중단

    sock.close()
    print(f"\n{'='*55}")
    print("테스트 완료")


if __name__ == '__main__':
    main()
