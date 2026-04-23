# config.py - 서버 전체 설정값 관리 (코드 수정 없이 여기서만 변경)

# ── 내 서버(운용 서버) 설정 ──────────────────────────
OPERATION_HOST = "0.0.0.0"   # 모든 IP에서 접속 허용
SERVER_PORT = 8000            # 단일 포트로 통합

# ── AI 서버 설정 (김범준) ──────────────────────────────
AI_HOST = "10.10.10.128"
AI_PORT = 9000

# ── DB 설정 ──────────────────────────────────────────
DB_HOST     = "10.10.10.101"
DB_PORT     = 3306
DB_USER     = "metal_team"
DB_PASSWORD = "1234"
DB_NAME     = "metalguard_db"

# ── 판정 임계값 (김범준 튜닝 기준, 변경 시 여기만 수정) ──
PASS_THRESHOLD = 0.70        # normal 확률 >= 0.70 이면 PASS
UNCERTAIN_THRESHOLD = 0.40   # max_prob < 0.40 이면 UNCERTAIN
# FAIL = 위 두 조건 모두 해당 없음

# ── 타임아웃 ─────────────────────────────────────────
# 기존 150ms(장 단위) → 1000ms(철판 단위)로 변경
# N=8장, 간격=0.25초 기준 실제 소요 ≈ 2000ms
PIPELINE_TIMEOUT_MS = 2000


# ── 모델 버전 ─────────────────────────────────────────
MODEL_VERSION_ID = 3

# ── MFC 전송 ─────────────────────────────────────────
SEND_RESULT_TO_MFC = True    # True로 바꾸면 MFC 전송 활성화

# ── 철판 촬영 파라미터 ────────────────────────────────
SHOT_COUNT = 8               # 철판 1개당 촬영 장수
SHOT_INTERVAL_SEC = 0.25     # 촬영 간격 (초)
MOTION_THRESHOLD = 500       # 프레임 차분 임계값 (픽셀 수), 실측 후 조정 가능
PLATE_BUFFER_EXPIRE_SEC = 6.0  # 8장 × 0.25초 + 여유