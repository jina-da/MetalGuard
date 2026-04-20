# 🔍 MetalGuard — 금속 표면 불량 자동 검출 시스템

> EfficientNet-B2 기반 5클래스 머신비전 통합 시스템 | AI 모델링 & 추론 서버 담당

![Python](https://img.shields.io/badge/Python-3.12-3776AB?style=flat-square&logo=python&logoColor=white)
![PyTorch](https://img.shields.io/badge/PyTorch-2.x-EE4C2C?style=flat-square&logo=pytorch&logoColor=white)
![CUDA](https://img.shields.io/badge/CUDA-FP16-76B900?style=flat-square&logo=nvidia&logoColor=white)
![OpenCV](https://img.shields.io/badge/OpenCV-4.x-5C3EE8?style=flat-square&logo=opencv&logoColor=white)

---

## 📌 프로젝트 개요

| 항목 | 내용 |
|------|------|
| **기간** | 2025. 04. 13 ~ 04. 25 (13일) |
| **팀 구성** | 4인 (카메라·클라이언트 / **AI 모델·서버** / 운용 서버·DB / GUI·아두이노) |
| **담당** | AI 모델링 & 추론 서버 개발 (AI 엔지니어) |
| **목적** | Basler 산업용 카메라로 촬영한 금속 부품을 실시간 분류해 불량품 자동 선별 |
| **데이터셋** | Kaggle — Synthetic Industrial Metal Surface Defects (15,000장, 5클래스) |

**전체 파이프라인**
```
카메라(Basler) → 클라이언트(C++, 전처리·전송)
  → 운용 서버(Python, 150ms 타임아웃·DB·판정)
  → AI 서버(Python, EfficientNet-B2 추론, 4~7ms)  ← 담당
  → 아두이노(서보모터 게이트·LED·부저)
```

---

## 🧠 담당 업무

### 1. EfficientNet-B2 모델 학습

- **5클래스 분류**: crack / hole / normal / rust / scratch (입력 260×260 RGB)
- **FN 최소화 설계**: `CrossEntropyLoss + class_weight` — normal 클래스에 가중치 3.0 부여, 불량 미검출(FN) 최소화
- **데이터 증강**: 수평·수직 플립, 밝기/대비 조정, 가우시안 노이즈(std=0.05), RandomErasing, 원근 변환
- **학습 전략**: ImageNet 사전학습 Fine-tuning, AdamW + CosineAnnealingLR (30 Epoch)
- **그레이스케일 처리**: 산업용 카메라는 그레이스케일 촬영 → 동일 채널 3회 복제(H×W×1 → H×W×3)로 ImageNet 가중치 100% 활용

### 2. 분석 도구 구현

- **GradCAM**: 모델이 집중하는 영역 시각화 — 불량 원인 해석 및 디버깅
- **TTA (Test Time Augmentation)**: 8방향 증강 후 평균 추론 — 확신도 향상 및 도메인 갭 완화
- **온도 스케일링**: NLL 최소화 기준 최적 T값 탐색 — 합성→실사 도메인 갭 보정

### 3. AI 추론 서버 개발 (`ai_server.py`)

- **TCP 소켓 서버**: 운용 서버(지나)와 협의한 확정 프로토콜 구현
  ```
  INFER_REQ (101): [8B 헤더(sig=0x4D47 + cmdId + bodySize)] + [JSON {"inspection_id": N}] + [4B 이미지크기] + [JPEG]
  INFER_RES (102): [8B 헤더] + [JSON 바디 (확률값 + 판정 결과 + inspection_id)]
  ```
- **inspection_id 매칭**: 요청·응답에 동일 ID 포함 — 운용 서버의 비동기 매칭 지원
- **멀티스레드 안전성**: `threading.Lock()`으로 GPU 경합 방지
- **안정적 종료**: `SIGINT`/`SIGTERM` 핸들러 등록, `server.settimeout(1.0)`으로 Ctrl+C 즉시 처리

### 4. 추론 속도 최적화 (20,000ms → 7.5ms)

| 최적화 기법 | 효과 |
|------------|------|
| **FP16 추론** (`model.half()`) | GPU float16 연산, 메모리 절약 |
| **torch.compile** (`mode='reduce-overhead'`) | JIT 그래프 컴파일, steady-state 속도 향상 |
| **OpenCV 전처리** | PIL 대비 JPEG 디코딩 30~50% 빠름 |
| **CUDA 스트림 고정** (`torch.cuda.Stream()`) | 매 추론마다 스트림 생성 오버헤드 제거 |
| **워밍업 스레드 풀** (`queue.Queue`) | 연결 전 CUDA context 미리 초기화 |
| **연결 후 더미 추론** | torch.compile JIT 경로 미리 완료 |

---

## 🔧 기술적 도전과 해결

### ① CUDA context 스레드 귀속 문제

```
문제: 워밍업은 메인 스레드에서 했는데, 실제 추론은 새 핸들러 스레드에서 실행
     → 새 스레드에서 CUDA context 재초기화 → 첫 추론 20초 소요

원인: CUDA context는 프로세스 전체가 아닌 각 스레드에 개별 바인딩됨

해결: queue.Queue 기반 워밍업 스레드 풀 설계
     - 서버 시작과 동시에 백그라운드 스레드에서 워밍업 실행
     - 클라이언트 연결 시 워밍업 완료된 스레드에 즉시 연결 전달
     - 첫 요청도 정상 속도로 처리
```

### ② torch.compile JIT 재컴파일 문제

```
문제: torch.compile은 실제 JPEG 이미지 처리 경로를 처음 밟을 때
     JIT 컴파일을 추가 수행 → 워밍업 후에도 첫 요청 87ms

원인: 더미 이미지(검은 PNG)와 실제 JPEG는 내부 처리 경로가 달라
     별도 JIT 컴파일 발생

해결: 클라이언트 연결 직후 실제 JPEG 포맷 더미 이미지로 1회 추론
     (warmup_for_connection) → JIT 미리 완료 → 첫 실제 요청 7.5ms
```

### ③ FP16 softmax 수치 불안정

```
문제: FP16 추론 시 logit이 float16으로 나와 softmax 오버플로우 가능성

해결: output.float()으로 float32 변환 후 softmax 적용
     추론 속도(FP16) + 확률 계산 안정성(FP32) 동시 확보
```

---

## 📊 성능 결과

### 모델 성능 (val 셋, 3,000장)

| 지표 | 목표 | 실제 | |
|------|------|------|--|
| Recall (불량 검출률) | ≥ 0.90 | **0.9946** | ✅ |
| AUROC | ≥ 0.85 | **0.9999** | ✅ |
| F1-Score | ≥ 0.82 | **0.9953** | ✅ |
| Precision | ≥ 0.75 | **0.9954** | ✅ |
| Val Accuracy | - | **99.53%** | ✅ |

> 3,000장 중 오분류 14장. hole 클래스 600장 전수 정확 분류.

### 추론 서버 속도

| 구간 | 최적화 전 | 최적화 후 |
|------|----------|----------|
| 첫 번째 요청 | 20,000ms | **7.5ms** |
| 연속 요청 (steady state) | 8~16ms | **4~5ms** |
| 전체 파이프라인 목표 | 150ms 이내 | ✅ 충분히 달성 |

---

## 🛠 기술 스택

```
Language     Python 3.12
DL Framework PyTorch 2.x, torchvision
Model        EfficientNet-B2 (9.2M params, ImageNet Fine-tuning)
Optimization torch.compile, FP16 half precision, CUDA Stream, OpenCV
CV Tools     GradCAM, TTA, Temperature Scaling
Server       TCP Socket, threading, queue.Queue, signal
Hardware     NVIDIA RTX 5090, Basler acA1920-150um
```

---

## 📁 파일 구성

```
├── train.py              # 모델 학습 (EfficientNet-B2 Fine-tuning)
├── model.py              # 모델 정의 및 저장/로드
├── dataset.py            # 데이터셋 및 증강 파이프라인
├── evaluate.py           # 성능 평가 (Recall, AUROC, F1, Confusion Matrix)
├── ai_server.py          # TCP 추론 서버 (PacketHeader 프로토콜)
├── gradcam.py            # GradCAM 시각화
├── tta.py                # Test Time Augmentation
├── temperature_scaling.py # 온도 스케일링 최적화
├── predict.py            # 단건/폴더 추론 테스트
└── config.py             # 하이퍼파라미터 및 경로 설정
```
