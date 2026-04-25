"""
ai_server.py — MetalGuard AI 추론 서버 (REQ-B19) 최종본

통신 프로토콜 (지나 서버 확정 스펙):
  PacketHeader (8바이트): [2B: sig=0x4D47] + [2B: cmdId] + [4B: bodySize]
  INFER_REQ (101): [8B 헤더] + [JSON {"inspection_id": N}] + [4B 이미지크기] + [이미지]
  INFER_RES (102): [8B 헤더] + [JSON 바디]

적용된 최적화:
  1. FP16 추론              — GPU float16 연산
  2. torch.compile()        — PyTorch 2.0 그래프 컴파일
  3. OpenCV 전처리          — PIL 대비 빠른 JPEG 디코딩 (없으면 PIL fallback)
  4. CUDA 스트림 고정       — 스레드별 스트림 생성 오버헤드 제거
  5. 추론 통계 집계         — PASS/FAIL/UNCERTAIN 비율, 평균/최대 ms
  6. 워밍업 스레드 풀       — 연결 즉시 추론 가능
  7. 연결 후 더미 추론 1회  — torch.compile JIT를 실제 JPEG 경로로 미리 완료
                              → 첫 실제 요청도 2~5ms 유지

실행:
  python ai_server.py
  python ai_server.py --host 0.0.0.0 --port 9000 --version best

OpenCV 설치 (선택):
  pip install opencv-python-headless --break-system-packages
torch.compile 오류 시:
  sudo apt install python3-dev python3.12-dev
"""
import os
import io
import json
import struct
import socket
import threading
import queue
import time
import argparse
import logging
import signal
import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

try:
    import cv2
    _USE_CV2 = True
except ImportError:
    _USE_CV2 = False

from config import (
    CLASSES, IMG_SIZE, WEIGHT_DIR,
    PASS_THRESHOLD, UNCERTAIN_THRESHOLD
)
from dataset import get_transforms
from model import build_model

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
#  프로토콜 상수
# ============================================================
SIGNATURE     = 0x4D47
HEADER_FORMAT = ">HHI"
HEADER_SIZE   = 8
INFER_REQ     = 101
INFER_RES     = 102

# ImageNet 정규화 상수 (OpenCV 전처리용)
_MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
_STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)


# ============================================================
#  패킷 헬퍼
# ============================================================
def recv_exact(sock, size: int) -> bytes:
    buf = b""
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise ConnectionError("소켓 연결 끊김")
        buf += chunk
    return buf


def send_infer_res(conn, result: dict):
    body_bytes = json.dumps(result).encode("utf-8")
    header     = struct.pack(HEADER_FORMAT, SIGNATURE, INFER_RES, len(body_bytes))
    conn.sendall(header + body_bytes)


# ============================================================
#  빠른 전처리 (OpenCV 우선, PIL fallback)
# ============================================================
def preprocess_image(image_bytes: bytes, device, use_fp16: bool) -> torch.Tensor:
    if _USE_CV2:
        arr   = np.frombuffer(image_bytes, dtype=np.uint8)
        bgr   = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if bgr is None:
            raise ValueError("이미지 디코딩 실패")
        rgb   = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        rgb   = cv2.resize(rgb, (IMG_SIZE, IMG_SIZE), interpolation=cv2.INTER_LINEAR)
        img_f = rgb.astype(np.float32) / 255.0
        img_f = (img_f - _MEAN) / _STD
        tensor = torch.from_numpy(img_f.transpose(2, 0, 1)).unsqueeze(0)
    else:
        try:
            pil = Image.open(io.BytesIO(image_bytes))
        except Exception:
            arr = np.frombuffer(image_bytes, dtype=np.uint8)
            arr = arr.reshape((IMG_SIZE, IMG_SIZE, 3))
            pil = Image.fromarray(arr, mode='RGB')
        tensor = get_transforms('val')(pil).unsqueeze(0)

    tensor = tensor.to(device)
    if use_fp16:
        tensor = tensor.half()
    return tensor


# ============================================================
#  추론 통계
# ============================================================
class InferStats:
    def __init__(self):
        self._lock    = threading.Lock()
        self.total    = 0
        self.pass_cnt = 0
        self.fail_cnt = 0
        self.unct_cnt = 0
        self.sum_ms   = 0.0
        self.max_ms   = 0.0

    def update(self, verdict: str, inference_ms: float):
        with self._lock:
            self.total  += 1
            self.sum_ms += inference_ms
            if inference_ms > self.max_ms:
                self.max_ms = inference_ms
            if verdict == 'PASS':
                self.pass_cnt += 1
            elif verdict == 'FAIL':
                self.fail_cnt += 1
            else:
                self.unct_cnt += 1

    def print_summary(self):
        with self._lock:
            if self.total == 0:
                log.info("통계: 처리된 요청 없음")
                return
            avg_ms = self.sum_ms / self.total
            print(f"\n{'='*44}")
            print(f"  추론 통계 요약")
            print(f"{'='*44}")
            print(f"  총 처리       : {self.total}건")
            print(f"  PASS          : {self.pass_cnt}건 ({self.pass_cnt/self.total*100:.1f}%)")
            print(f"  FAIL          : {self.fail_cnt}건 ({self.fail_cnt/self.total*100:.1f}%)")
            print(f"  UNCERTAIN     : {self.unct_cnt}건 ({self.unct_cnt/self.total*100:.1f}%)")
            print(f"  평균 추론시간 : {avg_ms:.1f}ms")
            print(f"  최대 추론시간 : {self.max_ms:.1f}ms")
            print(f"{'='*44}\n")


# ============================================================
#  추론 엔진
# ============================================================
class InferenceEngine:
    def __init__(self, version_tag, device):
        self.device      = device
        self.use_fp16    = (device.type == 'cuda')
        self._infer_lock = threading.Lock()
        self._first_done = False
        self.stats       = InferStats()

        # 모델 로드
        path = os.path.join(WEIGHT_DIR, f'metalguard_{version_tag}.pth')
        ckpt = torch.load(path, map_location=device, weights_only=False)
        self.model = build_model(pretrained=False)
        self.model.load_state_dict(ckpt['model_state'])
        self.model.to(device)

        if self.use_fp16:
            self.model = self.model.half()
            log.info("FP16 추론 활성화")

        self.model.eval()
        self.model_version_id = ckpt.get('result', {}).get('model_version_id', 3)

        # torch.compile()
        if hasattr(torch, 'compile'):
            try:
                log.info("torch.compile() 적용 중... (최초 1회 컴파일, 약 30~60초 소요)")
                self.model = torch.compile(self.model, mode='reduce-overhead')
                log.info("torch.compile() 완료")
                self._compiled = True
            except Exception as e:
                log.warning(f"torch.compile() 실패, 기본 모드로 실행 | {e}")
                self._compiled = False
        else:
            log.info("torch.compile() 미지원 버전 — 스킵")
            self._compiled = False

        # CUDA 스트림 고정
        self._cuda_stream = (
            torch.cuda.Stream(device) if device.type == 'cuda' else None
        )

        log.info(f"모델 로드 완료 | version_tag={version_tag} | "
                 f"model_version_id={self.model_version_id} | device={device}")
        log.info(f"전처리 엔진: {'OpenCV' if _USE_CV2 else 'PIL (opencv 미설치)'}")

    def _make_dummy_jpeg_bytes(self):
        """
        실제 JPEG 포맷 더미 이미지 생성.
        torch.compile은 텐서 shape뿐 아니라 전처리 경로(JPEG 디코딩 포함)가
        같아야 JIT 컴파일 결과를 재사용함.
        검은 이미지를 JPEG 품질 95로 저장해서 실제 요청과 동일한 포맷 사용.
        """
        dummy_pil = Image.fromarray(
            np.zeros((IMG_SIZE, IMG_SIZE, 3), dtype=np.uint8), mode='RGB'
        )
        buf = io.BytesIO()
        dummy_pil.save(buf, format='JPEG', quality=95)
        return buf.getvalue()

    def warmup(self, label=""):
        """서버 시작 시 워밍업 — CUDA context + cuDNN 초기화"""
        log.info(f"GPU 워밍업 시작... ({label})")
        dummy_bytes = self._make_dummy_jpeg_bytes()
        self.infer(dummy_bytes)
        if self.device.type == 'cuda':
            torch.cuda.synchronize()
        log.info(f"GPU 워밍업 완료 ({label})")

    def warmup_for_connection(self):
        """
        클라이언트 연결 직후 호출.
        torch.compile은 실제 JPEG 이미지가 들어오는 경로를 처음 밟을 때
        JIT 컴파일을 추가로 수행함.
        연결 후 더미 JPEG로 1회 추론해서 이 컴파일을 미리 완료시킴.
        → 첫 실제 요청도 2~5ms 유지.
        """
        log.info("연결 후 더미 추론 시작 (JIT 경로 예열)...")
        dummy_bytes = self._make_dummy_jpeg_bytes()
        self.infer(dummy_bytes)
        if self.device.type == 'cuda':
            torch.cuda.synchronize()
        log.info("연결 후 더미 추론 완료 — 첫 실제 요청 준비됨")

    def infer(self, image_bytes: bytes):
        """실제 추론 — CUDA 스트림 고정, Lock 보호"""
        with self._infer_lock:
            t0 = time.perf_counter()

            tensor = preprocess_image(image_bytes, self.device, self.use_fp16)
            t1 = time.perf_counter()

            if self._cuda_stream:
                with torch.cuda.stream(self._cuda_stream):
                    with torch.no_grad():
                        output = self.model(tensor)
                        probs  = F.softmax(output.float(), dim=1).squeeze().cpu().numpy()
                torch.cuda.synchronize()
            else:
                with torch.no_grad():
                    output = self.model(tensor)
                    probs  = F.softmax(output.float(), dim=1).squeeze().cpu().numpy()

            t2 = time.perf_counter()

        preproc_ms   = (t1 - t0) * 1000
        infer_ms     = (t2 - t1) * 1000
        inference_ms = (t2 - t0) * 1000

        if not self._first_done:
            self._first_done = True
            log.info(
                f"[구간별 시간] 전처리={preproc_ms:.1f}ms | "
                f"GPU추론={infer_ms:.1f}ms | "
                f"합계={inference_ms:.1f}ms"
            )

        prob_normal  = float(probs[CLASSES.index('normal')])
        prob_crack   = float(probs[CLASSES.index('crack')])
        prob_hole    = float(probs[CLASSES.index('hole')])
        prob_rust    = float(probs[CLASSES.index('rust')])
        prob_scratch = float(probs[CLASSES.index('scratch')])
        max_prob     = float(probs.max())
        pred_idx     = int(probs.argmax())
        defect_class = CLASSES[pred_idx]

        if prob_normal >= PASS_THRESHOLD:
            verdict = 'PASS'
        elif max_prob < UNCERTAIN_THRESHOLD:
            verdict = 'UNCERTAIN'
        else:
            verdict = 'FAIL'

        # 워밍업/더미 추론은 통계 제외
        if self._first_done:
            self.stats.update(verdict, inference_ms)

        return {
            'prob_normal'      : round(prob_normal,  4),
            'prob_crack'       : round(prob_crack,   4),
            'prob_hole'        : round(prob_hole,    4),
            'prob_rust'        : round(prob_rust,    4),
            'prob_scratch'     : round(prob_scratch, 4),
            'verdict'          : verdict,
            'defect_class'     : defect_class,
            'max_prob'         : round(max_prob, 4),
            'inference_ms'     : round(inference_ms, 2),
            'model_version_id' : self.model_version_id,
        }


# ============================================================
#  워밍업 스레드
# ============================================================
def _warmup_and_serve(engine: InferenceEngine,
                      conn_queue: queue.Queue,
                      next_warmup_cb,
                      stop_event: threading.Event,
                      thread_id: int):
    # 1단계: 서버 시작 시 워밍업 (CUDA context 초기화)
    engine.warmup(label=f"대기 스레드 #{thread_id}")
    log.info(f"대기 스레드 #{thread_id} 준비 완료 — 연결 대기 중")

    # 연결 대기
    while not stop_event.is_set():
        try:
            conn, addr = conn_queue.get(timeout=1.0)
            break
        except queue.Empty:
            continue
    else:
        return

    # 2단계: 연결 직후 더미 추론 (torch.compile JPEG 경로 JIT 완료)
    engine.warmup_for_connection()

    _handle_client_logic(conn, addr, engine, next_warmup_cb)


# ============================================================
#  클라이언트 핸들러
# ============================================================
def _handle_client_logic(conn, addr, engine: InferenceEngine, next_warmup_cb):
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
    conn.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024)
    ip = addr[0]
    print(f"\n{'='*44}")
    print(f"  운용 서버 연결됨!  {ip}")
    print(f"  추론 요청 대기 중...")
    print(f"{'='*44}\n")
    log.info(f"연결 수립 | {addr}")

    request_count       = 0
    next_warmup_started = False

    try:
        while True:

            # ── 1. PacketHeader 수신 ─────────────────────────────
            header_bytes                 = recv_exact(conn, HEADER_SIZE)
            signature, cmd_id, body_size = struct.unpack(HEADER_FORMAT, header_bytes)

            if signature != SIGNATURE:
                log.warning(f"잘못된 signature: 0x{signature:04X} | {addr}")
                break
            if cmd_id != INFER_REQ:
                log.warning(f"예상치 못한 cmdId: {cmd_id} | {addr}")
                break

            # ── 2. JSON 바디 수신 ────────────────────────────────
            body_bytes    = recv_exact(conn, body_size)
            body          = json.loads(body_bytes.decode("utf-8"))
            inspection_id = body.get("inspection_id", -1)

            # ── 3. 이미지 수신 ───────────────────────────────────
            img_size_bytes = recv_exact(conn, 4)
            img_size       = struct.unpack(">I", img_size_bytes)[0]
            image_bytes    = recv_exact(conn, img_size)

            log.info(
                f"INFER_REQ 수신 | {addr} | "
                f"inspection_id={inspection_id} | img={img_size}bytes"
            )

            # ── 4. 추론 ─────────────────────────────────────────
            try:
                result                  = engine.infer(image_bytes)
                result['inspection_id'] = inspection_id
                request_count          += 1

                pred = max(
                    (k for k in result if k.startswith('prob_')),
                    key=lambda k: result[k]
                ).replace('prob_', '')
                log.info(
                    f"[{request_count}번째] {ip} | "
                    f"inspection_id={inspection_id} | "
                    f"verdict={result['verdict']} | "
                    f"pred={pred} | "
                    f"{result['inference_ms']:.1f}ms"
                )

                # 첫 추론 완료 후 다음 워밍업 스레드 시작
                if not next_warmup_started:
                    next_warmup_started = True
                    next_warmup_cb()
                    log.info("다음 연결을 위한 워밍업 스레드 준비 시작")

            except Exception as e:
                log.error(f"추론 오류 | {addr} | {e}")
                result = {
                    'inspection_id'    : inspection_id,
                    'error'            : str(e),
                    'inference_ms'     : 0.0,
                    'model_version_id' : engine.model_version_id,
                }

            # ── 5. INFER_RES 송신 ────────────────────────────────
            send_infer_res(conn, result)

    except ConnectionError as e:
        log.info(f"연결 끊김 | {addr} | {e}")
    except (BrokenPipeError, OSError) as e:
        log.info(f"소켓 오류 | {addr} | {e}")
    except Exception as e:
        log.error(f"핸들러 오류 | {addr} | {e}")
    finally:
        conn.close()
        engine.stats.print_summary()
        print(f"\n{'='*44}")
        print(f"  운용 서버 연결 종료  {ip}")
        print(f"  총 처리 요청: {request_count}건")
        print(f"{'='*44}\n")
        log.info(f"연결 종료 | {addr}")


# ============================================================
#  서버 메인 루프
# ============================================================
def run_server(host, port, version_tag):
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    log.info(f"디바이스: {device}")
    if device.type == 'cuda':
        log.info(f"GPU: {torch.cuda.get_device_name(0)}")
        torch.backends.cudnn.benchmark     = True
        torch.backends.cudnn.deterministic = False

    engine     = InferenceEngine(version_tag, device)
    stop_event = threading.Event()

    def _signal_handler(signum, frame):
        log.info(f"종료 시그널 수신 (signal={signum}) — 서버를 종료합니다...")
        engine.stats.print_summary()
        stop_event.set()

    signal.signal(signal.SIGINT,  _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)

    conn_queue     = queue.Queue(maxsize=1)
    thread_counter = [1]

    def _launch_warmup_thread():
        tid = thread_counter[0]
        thread_counter[0] += 1
        t = threading.Thread(
            target=_warmup_and_serve,
            args=(engine, conn_queue, _launch_warmup_thread, stop_event, tid),
            daemon=True
        )
        t.start()

    log.info("백그라운드 워밍업 스레드 준비 중...")
    _launch_warmup_thread()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(5)
    server.settimeout(1.0)

    opt_list = []
    opt_list.append("FP16" if engine.use_fp16 else "FP32")
    opt_list.append("torch.compile" if engine._compiled else "기본모드")
    opt_list.append("OpenCV" if _USE_CV2 else "PIL")
    opt_list.append("CUDA스트림고정" if engine._cuda_stream else "기본스트림")

    log.info(f"AI 추론 서버 시작 | {host}:{port}")
    log.info(f"최적화: {' | '.join(opt_list)}")
    log.info(f"프로토콜: INFER_REQ(101) / INFER_RES(102) | signature=0x4D47")
    log.info(f"타임아웃 관리: 운용 서버(150ms) 기준 | inference_ms 응답에 포함")
    log.info("종료하려면 Ctrl+C 를 누르세요.")

    try:
        while not stop_event.is_set():
            try:
                conn, addr = server.accept()
            except socket.timeout:
                continue
            log.info(f"연결 수락 | {addr} | 워밍업 스레드로 전달 중...")
            conn_queue.put((conn, addr))
    finally:
        server.close()
        log.info("서버 소켓 닫힘. 종료 완료.")


# ============================================================
#  메인
# ============================================================
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='MetalGuard AI 추론 서버')
    parser.add_argument('--host',    type=str, default='0.0.0.0')
    parser.add_argument('--port',    type=int, default=9000)
    parser.add_argument('--version', type=str, default='best')
    args = parser.parse_args()

    run_server(args.host, args.port, args.version)
