# 🔩 MetalGuard - 금속 표면 불량 자동 검출 시스템

> EfficientNet-B2 기반 컨베이어 벨트 금속판 표면 불량 자동 검출 시스템

---

## 📌 프로젝트 개요

공장 컨베이어 벨트 위를 지나가는 금속판을 카메라로 촬영하여
AI 모델이 표면 불량을 실시간으로 검출하고 **PASS / FAIL / UNCERTAIN** 3단계로 자동 판정하는 시스템입니다.

| 항목 | 내용 |
|------|------|
| 모델 | EfficientNet-B2 |
| 판정 | PASS / FAIL / UNCERTAIN |
| 불량 클래스 | normal / crack / hole / rust / scratch |
| 파이프라인 목표 | 철판 1개당 1000ms 이내 |

---

## 👥 팀 구성

| 이름 | 담당 | IP / 포트 |
|------|------|-----------|
| 이지나 | 운용 서버 (Python) | 10.10.10.109 / 8000 |
| 김범준 | AI 서버 (Python) + DB | 10.10.10.128 / 9000 |
| 김민기 | 카메라 + MFC (C++) | → 8000 연결 |
| 김희창 | 아두이노 PC (C++ 콘솔) | → 8000 연결 |

---

## 🏗️ 시스템 아키텍처

```
[카메라 + MFC (C++)]
        │
        │ TCP 8000 (IMG_SEND / IMG_RECLASSIFY)
        ▼
[운용 서버 - Python] ────────────────────────────▶ [MariaDB]
        │                                           10.10.10.101:3306
        │ TCP 9000 (INFER_REQ / INFER_RES)
        ▼
[AI 서버 - Python]

[운용 서버 - Python] ────────────────────────────▶ [아두이노 PC (C++ 콘솔)]
           TCP 8000 (VERDICT_PASS/FAIL/UNCERTAIN)   → 아두이노 시리얼 전달

[운용 서버 - Python] ────────────────────────────▶ [MFC]
           TCP 8000 (RESULT_SEND)
```

---

## 📁 프로젝트 구조

```
metal_guard_server/
├── main.py               # 서버 진입점, 전체 초기화 및 종료 처리
├── config.py             # 전체 설정값 관리
├── constants.py          # PacketHeader 상수, CmdID, 유틸 함수
└── server/
    ├── tcp_server.py     # 단일 포트 TCP 서버, cmdId 기반 분기
    ├── handlers/
    │   ├── camera_handler.py  # 이미지 수신 → ImageTask → Queue 적재
    │   ├── ai_client.py       # AI 서버 TCP 통신 (INFER_REQ/RES)
    │   └── mfc_handler.py     # MFC 판정 결과 전송
    ├── engine/
    │   └── verdict_engine.py  # Queue 소비 → 추론 → 판정 → 전송
    └── db/
        └── db_manager.py      # MariaDB INSERT (재연결 로직 포함)
```

---

## 📡 통신 프로토콜

### PacketHeader 구조 (8바이트 고정)

```
[2B: signature(0x4D47)] + [2B: cmdId] + [4B: bodySize] + [JSON 바디]
이미지 포함 시: 위 구조 + [4B: 이미지크기] + [이미지 바이트]
```

### CmdID 목록

| CmdID | 값 | 방향 | 설명 |
|-------|----|------|------|
| IMG_SEND | 1 | 카메라 → 운용서버 | 촬영 이미지 전송 (첫 분류) |
| IMG_RECLASSIFY | 2 | 카메라 → 운용서버 | 재분류 이미지 전송 |
| INFER_REQ | 101 | 운용서버 → AI서버 | 추론 요청 |
| INFER_RES | 102 | AI서버 → 운용서버 | 추론 결과 |
| VERDICT_PASS | 201 | 운용서버 → 아두이노PC | 정상 판정 |
| VERDICT_FAIL | 202 | 운용서버 → 아두이노PC | 불량 판정 |
| VERDICT_UNCERTAIN | 203 | 운용서버 → 아두이노PC | 미분류 판정 |
| DONE_PASS | 205 | 아두이노PC → 운용서버 | PASS 동작 완료 |
| DONE_FAIL | 206 | 아두이노PC → 운용서버 | FAIL 서보모터 동작 완료 |
| DONE_UNCERTAIN | 207 | 아두이노PC → 운용서버 | UNCERTAIN 서보모터 동작 완료 |
| RESULT_SEND | 301 | 운용서버 → MFC | 판정 결과 전송 |
| PING | 501 | 아두이노PC → 운용서버 | 연결 등록 |
| PONG | 502 | 운용서버 → 아두이노PC | 연결 등록 응답 |
| ERROR_RES | 503 | 운용서버 → 클라이언트 | 에러 응답 |

---

## 🔄 판정 흐름

```
1. 카메라가 프레임 차분으로 철판 진입 감지
2. 즉시 1장씩 총 4장 촬영 → 운용서버로 순차 전송
3. 운용서버: 장별 AI 추론 → plate_id별 PlateBuffer에 결과 누적
4. 4장 완료 시 종합 판정
   - FAIL 1개라도 있으면 → FAIL
   - 전부 PASS → PASS
   - FAIL 없고 UNCERTAIN 있으면 → UNCERTAIN
5. 아두이노PC에 TCP로 VERDICT 패킷 전송 + MFC에 RESULT_SEND 전송 + DB 저장
```

### 첫 분류 vs 재분류

| 판정 | 첫 분류 (IMG_SEND) | 재분류 (IMG_RECLASSIFY) |
|------|-------------------|------------------------|
| PASS | 통과 | 통과 |
| FAIL | 게이트 A (폐기) | 게이트 A (폐기) |
| UNCERTAIN | 게이트 B (재분류 라인) | FAIL과 동일 (게이트 A 폐기) |

> 재분류에서 UNCERTAIN이 나오면 운용 서버가 FAIL로 처리합니다.

---

## ⚙️ 판정 임계값

| 판정 | 조건 |
|------|------|
| PASS | `prob_normal ≥ 0.80` |
| UNCERTAIN | `max_prob < 0.60` |
| FAIL | 위 두 조건 모두 해당 없음 |

---

## 🛠️ 설치 및 실행

### 요구 사항

```bash
Python 3.11 이상
pip install pymysql
```

### 실행

```bash
cd metal_guard_server
python main.py
```

### 종료

```
Ctrl+C
```

---

## ⚙️ 설정 (config.py)

```python
# 운용 서버
OPERATION_HOST = "0.0.0.0"
SERVER_PORT = 8000

# AI 서버
AI_HOST = "10.10.10.128"
AI_PORT = 9000

# DB
DB_HOST = "10.10.10.101"
DB_PORT = 3306

# 판정 임계값
PASS_THRESHOLD      = 0.80
UNCERTAIN_THRESHOLD = 0.60

# 파이프라인 목표 (철판 단위)
PIPELINE_TIMEOUT_MS = 1000

# 철판 촬영 파라미터
SHOT_COUNT        = 4     # 철판 1개당 촬영 장수
SHOT_INTERVAL_SEC = 0.25  # 촬영 간격 (초)
MOTION_THRESHOLD  = 500   # 프레임 차분 임계값 (픽셀), 실측 후 조정
```

---

## 🗄️ DB 스키마

| 테이블 | 설명 |
|--------|------|
| `model_version` | AI 모델 버전 관리 |
| `inspection_result` | 판정 결과 (장별 1행) |
| `pipeline_log` | 파이프라인 구간별 지연시간 로그 |

> 스키마 상세: `metalguard_schema_v4.sql` 참고

---

## 📋 개발 환경

| 항목 | 내용 |
|------|------|
| OS | Ubuntu 22.04 (WSL2) |
| Language | Python 3.11 |
| DB | MariaDB |
| 통신 | TCP Socket |
| 프로토콜 | 자체 설계 PacketHeader |
| 개발 도구 | VS Code |
