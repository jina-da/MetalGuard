import os

# ============================================================
#  MetalGuard - 학습 설정
# ============================================================

BASE_DIR   = os.path.dirname(os.path.abspath(__file__))
DATA_DIR   = os.path.join(BASE_DIR, 'data', 'metal_defects')
WEIGHT_DIR = os.path.join(BASE_DIR, 'weights')
RESULT_DIR = os.path.join(BASE_DIR, 'results')

# 클래스 정의
CLASSES      = ['crack', 'hole', 'normal', 'rust', 'scratch']
NUM_CLASSES  = len(CLASSES)

# 입력 크기
IMG_SIZE = 260

# 학습 하이퍼파라미터
BATCH_SIZE = 64
NUM_EPOCHS = 30
LR         = 1e-4
WEIGHT_DECAY = 1e-2

# class_weight: normal에 3.0 부여하여 FN 최소화
# CrossEntropyLoss는 클래스 인덱스 순서대로 가중치 적용
# CLASSES 순서: crack=0, hole=1, normal=2, rust=3, scratch=4
CLASS_WEIGHTS = [1.0, 1.0, 3.0, 1.0, 1.0]

# 판정 임계값
PASS_THRESHOLD      = 0.80   # normal 확률 >= 0.80 → PASS (실사 환경 도메인 갭 반영)
UNCERTAIN_THRESHOLD = 0.60   # max_prob < 0.60 → UNCERTAIN

# 목표 성능
TARGET_RECALL    = 0.90
TARGET_AUROC     = 0.85
TARGET_F1        = 0.82
TARGET_PRECISION = 0.75

# 기타
NUM_WORKERS = 4
SEED        = 42
