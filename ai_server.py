"""
ai_server.py — MetalGuard AI 추론 서버 (REQ-B19)

통신 프로토콜:
  INFER_REQ (101): 운용 서버 → AI 서버
    [4B: 이미지 크기 (uint32, big-endian)] + [이미지 바이트 (260×260×3)]

  INFER_RES (102): AI 서버 → 운용 서버
    JSON:
    {
      "prob_normal"      : 0.02,
      "prob_crack"       : 0.88,
      "prob_hole"        : 0.05,
      "prob_rust"        : 0.03,
      "prob_scratch"     : 0.02,
      "inference_ms"     : 8.5,
      "model_version_id" : 1
    }

실행:
  python ai_server.py
  python ai_server.py --host 0.0.0.0 --port 9000 --version best
"""
import os
import io
import json
import struct
import socket
import threading
import time
import argparse
import logging
import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

from config import CLASSES, IMG_SIZE, WEIGHT_DIR
from dataset import get_transforms
from model import load_model

# ============================================================
#  로그 설정
# ============================================================
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(levelname)s %(message)s',
    datefmt='%H:%M:%S'
)
log = logging.getLogger('AI-Server')


# ============================================================
#  추론 엔진
# ============================================================
class InferenceEngine:
    def __init__(self, version_tag, device):
        self.device      = device
        self.transform   = get_transforms('val')
        self.model       = load_model(version_tag, device)
        self.model.eval()

        # model_version_id: weights 파일명에서 추출 (없으면 1)
        self.model_version_id = self._get_version_id(version_tag)
        log.info(f"모델 로드 완료 | version_tag={version_tag} | "
                 f"model_version_id={self.model_version_id} | device={device}")

        # GPU 워밍업 — 첫 추론의 커널 컴파일 시간 제거 (5회 더미 추론)
        self._warmup()

    def _get_version_id(self, version_tag):
        path = os.path.join(WEIGHT_DIR, f'metalguard_{version_tag}.pth')
        try:
            ckpt = torch.load(path, map_location='cpu', weights_only=False)
            result = ckpt.get('result', {})
            return result.get('model_version_id', 1)
        except Exception:
            return 1

    def _warmup(self):
        log.info("GPU 워밍업 시작...")

        # 1) 더미 JPEG 바이트 생성 (실제 infer() 경로 전체 예열)
        import io as _io
        dummy_pil = Image.fromarray(
            np.zeros((IMG_SIZE, IMG_SIZE, 3), dtype=np.uint8), mode='RGB'
        )
        buf = _io.BytesIO()
        dummy_pil.save(buf, format='JPEG', quality=95)
        dummy_bytes = buf.getvalue()

        # 2) 실제 infer() 전체 경로로 20회 워밍업
        # PIL 디코딩 + 전처리 + GPU 추론 + synchronize 모두 포함
        for i in range(20):
            self.infer(dummy_bytes)
            if self.device.type == 'cuda':
                torch.cuda.synchronize()

        log.info("GPU 워밍업 완료 — 이제 추론 속도가 안정적입니다.")

    def infer(self, image_bytes):
        """
        image_bytes : JPEG/PNG 바이트 또는 raw RGB 바이트
        반환값      : dict (INFER_RES 포맷)
        """
        t0 = time.perf_counter()

        # 이미지 디코딩
        try:
            img = Image.open(io.BytesIO(image_bytes))
        except Exception:
            arr = np.frombuffer(image_bytes, dtype=np.uint8)
            arr = arr.reshape((IMG_SIZE, IMG_SIZE, 3))
            img = Image.fromarray(arr, mode='RGB')
        t1 = time.perf_counter()

        # 전처리
        tensor = self.transform(img).unsqueeze(0).to(self.device)
        t2 = time.perf_counter()

        # 추론
        with torch.no_grad():
            output = self.model(tensor)
            probs  = F.softmax(output, dim=1).squeeze().cpu().numpy()
        if self.device.type == 'cuda':
            torch.cuda.synchronize()
        t3 = time.perf_counter()

        decode_ms    = (t1 - t0) * 1000
        preproc_ms   = (t2 - t1) * 1000
        infer_ms     = (t3 - t2) * 1000
        inference_ms = (t3 - t0) * 1000

        if not hasattr(self, '_first_done'):
            self._first_done = True
            log.info(f"[구간별 시간] 디코딩={decode_ms:.1f}ms | 전처리={preproc_ms:.1f}ms | GPU추론={infer_ms:.1f}ms | 합계={inference_ms:.1f}ms")

        prob_normal  = float(probs[CLASSES.index('normal')])
        prob_crack   = float(probs[CLASSES.index('crack')])
        prob_hole    = float(probs[CLASSES.index('hole')])
        prob_rust    = float(probs[CLASSES.index('rust')])
        prob_scratch = float(probs[CLASSES.index('scratch')])
        max_prob     = float(probs.max())
        pred_idx     = int(probs.argmax())
        defect_class = CLASSES[pred_idx]

        # 3단계 판정
        from config import PASS_THRESHOLD, UNCERTAIN_THRESHOLD
        if prob_normal >= PASS_THRESHOLD:
            verdict = 'PASS'
        elif max_prob < UNCERTAIN_THRESHOLD:
            verdict = 'UNCERTAIN'
        else:
            verdict = 'FAIL'

        result = {
            # 확률값
            'prob_normal'      : round(prob_normal,  4),
            'prob_crack'       : round(prob_crack,   4),
            'prob_hole'        : round(prob_hole,    4),
            'prob_rust'        : round(prob_rust,    4),
            'prob_scratch'     : round(prob_scratch, 4),
            # 판정 결과
            'verdict'          : verdict,
            'defect_class'     : defect_class,
            'max_prob'         : round(max_prob, 4),
            # 메타
            'inference_ms'     : round(inference_ms, 2),
            'model_version_id' : self.model_version_id,
        }
        return result


# ============================================================
#  클라이언트 핸들러 (스레드)
# ============================================================
def handle_client(conn, addr, engine):
    # Nagle 알고리즘 비활성화 → 첫 패킷 지연 제거
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)  # 수신 버퍼 1MB
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024)  # 송신 버퍼 1MB
    ip = addr[0]
    print(f"\n{'='*44}")
    print(f"  운용 서버 연결됨!  {ip}")
    print(f"  추론 요청 대기 중...")
    print(f"{'='*44}\n")
    log.info(f"연결 수립 | {addr}")
    request_count = 0
    try:
        while True:
            # --- INFER_REQ 수신 ---
            # 1) 이미지 크기 헤더 4바이트 수신
            header = _recv_exact(conn, 4)
            if not header:
                break
            img_size = struct.unpack('>I', header)[0]

            # 2) 이미지 바이트 수신
            image_bytes = _recv_exact(conn, img_size)
            if not image_bytes:
                break

            log.info(f"이미지 수신 | {addr} | {img_size} bytes")

            # --- 추론 ---
            try:
                result     = engine.infer(image_bytes)
                response   = json.dumps(result).encode('utf-8')
                request_count += 1
                pred = max(
                    (k for k in result if k.startswith('prob')),
                    key=lambda k: result[k]
                ).replace('prob_', '')
                log.info(
                    f"[{request_count}번째 요청] {ip} | "
                    f"pred={pred} | "
                    f"{result['inference_ms']:.1f}ms"
                )
            except Exception as e:
                log.error(f"추론 오류 | {addr} | {e}")
                response = json.dumps({'error': str(e)}).encode('utf-8')

            # --- INFER_RES 전송 ---
            # 응답 크기 4바이트 + JSON 바이트
            conn.sendall(struct.pack('>I', len(response)) + response)

    except ConnectionResetError:
        log.info(f"연결 끊김 | {addr}")
    except Exception as e:
        log.error(f"핸들러 오류 | {addr} | {e}")
    finally:
        conn.close()
        print(f"\n{'='*44}")
        print(f"  운용 서버 연결 종료  {ip}")
        print(f"  총 처리 요청: {request_count}건")
        print(f"{'='*44}\n")
        log.info(f"연결 종료 | {addr}")


def _recv_exact(conn, n):
    """정확히 n 바이트 수신"""
    data = b''
    while len(data) < n:
        packet = conn.recv(n - len(data))
        if not packet:
            return None
        data += packet
    return data


# ============================================================
#  서버 메인 루프
# ============================================================
def run_server(host, port, version_tag):
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    log.info(f"디바이스: {device}")
    if device.type == 'cuda':
        log.info(f"GPU: {torch.cuda.get_device_name(0)}")
        torch.backends.cudnn.benchmark = True   # cuDNN 자동 튜닝 고정
        torch.backends.cudnn.deterministic = False

    engine = InferenceEngine(version_tag, device)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(5)

    log.info(f"AI 추론 서버 시작 | {host}:{port}")
    log.info(f"프로토콜 INFER_REQ(101) 수신 대기 중...")

    try:
        while True:
            conn, addr = server.accept()
            t = threading.Thread(
                target=handle_client,
                args=(conn, addr, engine),
                daemon=True
            )
            t.start()
    except KeyboardInterrupt:
        log.info("서버 종료")
    finally:
        server.close()


# ============================================================
#  메인
# ============================================================
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='MetalGuard AI 추론 서버')
    parser.add_argument('--host',    type=str, default='0.0.0.0',
                        help='바인딩 주소 (기본: 0.0.0.0)')
    parser.add_argument('--port',    type=int, default=9000,
                        help='포트 번호 (기본: 9000)')
    parser.add_argument('--version', type=str, default='best',
                        help='모델 버전 (기본: best)')
    args = parser.parse_args()

    run_server(args.host, args.port, args.version)
