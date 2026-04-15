# config.py - 서버 전체 설정값 관리 (코드 수정 없이 여기서만 변경)

# ── 내 서버(운용 서버) 설정 ──────────────────────────
OPERATION_HOST = "0.0.0.0"   # 모든 IP에서 접속 허용
CAMERA_PORT    = 8000         # 카메라 PC가 접속할 포트
MFC_PORT       = 8001         # MFC GUI가 접속할 포트

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
PASS_THRESHOLD      = 0.80   # normal 확률 >= 0.80 이면 PASS
UNCERTAIN_THRESHOLD = 0.60   # max_prob < 0.60 이면 UNCERTAIN
# FAIL = 위 두 조건 모두 해당 없음

# ── 타임아웃 ─────────────────────────────────────────
PIPELINE_TIMEOUT_MS = 150    # 150ms 초과 시 자동 FAIL

# ── 아두이노 시리얼 ───────────────────────────────────(추후 수정)
ARDUINO_PORT     = "/dev/ttyUSB0"   # 리눅스 시리얼 포트
ARDUINO_BAUDRATE = 9600