import torch
import torch.nn as nn
from torchvision import models

from config import NUM_CLASSES, WEIGHT_DIR
import os


# ============================================================
#  EfficientNet-B2 기반 5클래스 분류 모델
#  - ImageNet 사전학습 가중치 Fine-tuning
#  - 파라미터 9.2M / 입력 260×260 3채널
# ============================================================

def build_model(pretrained=True):
    weights = models.EfficientNet_B2_Weights.IMAGENET1K_V1 if pretrained else None
    model   = models.efficientnet_b2(weights=weights)

    # 마지막 분류기 교체 (1000 → NUM_CLASSES)
    in_features = model.classifier[1].in_features
    model.classifier = nn.Sequential(
        nn.Dropout(p=0.3, inplace=True),
        nn.Linear(in_features, NUM_CLASSES),
    )

    return model


def save_model(model, version_tag, result_dict=None):
    """
    모델 가중치 저장
    version_tag : 예) 'v1.0.0'
    result_dict : {'recall': 0.92, 'auroc': 0.87, ...} 성능 지표 함께 저장
    """
    os.makedirs(WEIGHT_DIR, exist_ok=True)
    path = os.path.join(WEIGHT_DIR, f'metalguard_{version_tag}.pth')
    torch.save({
        'version_tag'  : version_tag,
        'model_state'  : model.state_dict(),
        'result'       : result_dict or {},
    }, path)
    print(f"모델 저장 완료: {path}")
    return path


def load_model(version_tag, device):
    path = os.path.join(WEIGHT_DIR, f'metalguard_{version_tag}.pth')
    ckpt = torch.load(path, map_location=device, weights_only=False)
    model = build_model(pretrained=False)
    model.load_state_dict(ckpt['model_state'])
    model.to(device)
    model.eval()
    print(f"모델 로드 완료: {path}")
    return model
