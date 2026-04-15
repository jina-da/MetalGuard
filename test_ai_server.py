"""
test_ai_server.py — AI 추론 서버 연동 테스트 클라이언트
지나(운용 서버)와 연동 전 단독 테스트용

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
#  이미지 → JPEG 바이트 변환
# ============================================================
def image_to_bytes(image_path, img_size=260):
    img = Image.open(image_path).convert('L').convert('RGB')
    img = img.resize((img_size, img_size))
    buf = io.BytesIO()
    img.save(buf, format='JPEG', quality=95)
    return buf.getvalue()


# ============================================================
#  INFER_REQ 전송 → INFER_RES 수신 (연결 유지 방식)
# ============================================================
def send_infer_request(host, port, image_bytes, timeout=5.0, sock=None):
    """
    sock: 기존 연결 소켓 전달 시 재사용 (Keep-alive)
          None이면 새 연결 생성
    반환값: (result_dict, sock) — sock을 다음 호출에 재사용 가능
    """
    new_conn = sock is None
    if new_conn:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))

    t0 = time.perf_counter()

    # INFER_REQ: [4B 크기] + [이미지 바이트]
    sock.sendall(struct.pack('>I', len(image_bytes)) + image_bytes)

    # INFER_RES: [4B 크기] + [JSON 바이트]
    header = _recv_exact(sock, 4)
    res_size = struct.unpack('>I', header)[0]
    res_bytes = _recv_exact(sock, res_size)

    rtt_ms = (time.perf_counter() - t0) * 1000
    result = json.loads(res_bytes.decode('utf-8'))
    result['rtt_ms'] = round(rtt_ms, 2)
    result['new_conn'] = new_conn
    return result, sock


def _recv_exact(s, n):
    data = b''
    while len(data) < n:
        packet = s.recv(n - len(data))
        if not packet:
            raise ConnectionError("연결이 끊겼습니다.")
        data += packet
    return data


# ============================================================
#  결과 출력
# ============================================================
def print_result(fname, result, conn_tag=""):
    classes = ['normal', 'crack', 'hole', 'rust', 'scratch']
    probs   = {c: result.get(f'prob_{c}', 0) for c in classes}
    pred    = max(probs, key=probs.get)

    print(f"\n{'='*55}")
    print(f" {fname}  {conn_tag}")
    print(f"{'='*55}")
    print(f"  예측 클래스  : {pred} ({probs[pred]:.4f})")
    print(f"  추론 시간    : {result.get('inference_ms', 0):.1f} ms")
    print(f"  RTT          : {result.get('rtt_ms', 0):.1f} ms")
    print(f"  model_ver_id : {result.get('model_version_id', '-')}")
    print(f"\n  클래스별 확률:")
    for cls in classes:
        p   = probs[cls]
        bar = '█' * int(p * 30)
        mark = ' ←' if cls == pred else ''
        print(f"    {cls:<10} {bar:<30} {p:.4f}{mark}")

    # 간단 판정 (임계값 기준)
    normal_prob = probs['normal']
    max_prob    = max(probs.values())
    if normal_prob >= 0.85:
        verdict = 'PASS'
    elif max_prob < 0.60:
        verdict = 'UNCERTAIN'
    else:
        verdict = 'FAIL'
    print(f"\n  판정 결과    : {verdict}  (normal={normal_prob:.4f})")


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

    # 연결 유지 방식으로 전체 이미지 테스트
    sock = None
    for i, img_path in enumerate(images):
        fname       = os.path.basename(img_path)
        image_bytes = image_to_bytes(img_path)
        try:
            result, sock = send_infer_request(args.host, args.port, image_bytes, sock=sock)
            conn_tag = "(새 연결)" if result.get("new_conn") else "(연결 유지)"
            print_result(fname, result, conn_tag)
        except Exception as e:
            print(f"  [{fname}] 오류: {e}")
            sock = None  # 오류 시 다음 요청에서 재연결
    if sock:
        sock.close()


if __name__ == '__main__':
    main()
