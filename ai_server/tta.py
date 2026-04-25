"""
tta.py — Test Time Augmentation (TTA)

원리:
  단일 이미지를 여러 방향으로 증강해서 N번 추론한 뒤
  확률값을 평균냄 → 확신도 향상 + 도메인 갭 완화

  예) 원본 + 좌우반전 + 상하반전 + 90도 회전 + 밝기 조정
      → 5번 추론 후 평균 확률 사용

TTA 변환 목록 (실사 금속 이미지 기준):
  1. 원본
  2. 좌우 반전
  3. 상하 반전
  4. 90도 회전
  5. 270도 회전
  6. 밝기 +20%
  7. 밝기 -20%
  8. 대비 +20%
"""
import torch
import torch.nn.functional as F
import torchvision.transforms as T
from PIL import Image, ImageEnhance
import numpy as np

from config import IMG_SIZE, CLASSES
from dataset import GrayToRGB


# ============================================================
#  TTA 변환 목록 정의
# ============================================================
def _get_tta_transforms():
    """
    각 TTA 변환은 PIL Image → PIL Image 함수 리스트로 반환
    이후 공통 전처리(Resize, ToTensor, Normalize)를 적용
    """
    normalize = T.Normalize(
        mean=[0.485, 0.456, 0.406],
        std =[0.229, 0.224, 0.225]
    )
    base = T.Compose([
        GrayToRGB(),
        T.Resize((IMG_SIZE, IMG_SIZE)),
        T.ToTensor(),
        normalize,
    ])

    def apply(pil_fn, img):
        return base(pil_fn(img))

    augmentations = [
        lambda img: img,                                          # 1. 원본
        lambda img: img.transpose(Image.FLIP_LEFT_RIGHT),        # 2. 좌우 반전
        lambda img: img.transpose(Image.FLIP_TOP_BOTTOM),        # 3. 상하 반전
        lambda img: img.rotate(90),                              # 4. 90도 회전
        lambda img: img.rotate(270),                             # 5. 270도 회전
        lambda img: ImageEnhance.Brightness(img).enhance(1.2),  # 6. 밝기 +20%
        lambda img: ImageEnhance.Brightness(img).enhance(0.8),  # 7. 밝기 -20%
        lambda img: ImageEnhance.Contrast(img).enhance(1.2),    # 8. 대비 +20%
    ]

    return augmentations, base


# ============================================================
#  TTA 추론
# ============================================================
def predict_with_tta(model, img: Image.Image, device, temperature=1.0):
    """
    img        : PIL Image (이미 crop 적용된 상태)
    temperature: 온도 스케일링과 함께 적용
    반환값     : (N_classes,) 평균 확률 numpy array
    """
    augmentations, base = _get_tta_transforms()
    model.eval()

    all_probs = []
    with torch.no_grad():
        for aug_fn in augmentations:
            try:
                aug_img = aug_fn(img)
                # base transform 적용
                from dataset import GrayToRGB
                import torchvision.transforms as T2
                normalize = T2.Normalize(
                    mean=[0.485, 0.456, 0.406],
                    std =[0.229, 0.224, 0.225]
                )
                tensor = T2.Compose([
                    GrayToRGB(),
                    T2.Resize((IMG_SIZE, IMG_SIZE)),
                    T2.ToTensor(),
                    normalize,
                ])(aug_img).unsqueeze(0).to(device)

                logits = model(tensor)
                scaled = logits / temperature
                probs  = F.softmax(scaled, dim=1).squeeze().cpu().numpy()
                all_probs.append(probs)
            except Exception:
                continue

    # 평균 확률
    avg_probs = np.mean(all_probs, axis=0)
    return avg_probs


# ============================================================
#  TTA 적용 여부에 따른 추론 분기
# ============================================================
def predict_with_tta_or_single(model, img: Image.Image, device,
                                transform, temperature=1.0, use_tta=True):
    """
    use_tta=True  → TTA 8회 평균 추론
    use_tta=False → 단일 추론 (온도 스케일링만 적용)
    """
    if use_tta:
        probs = predict_with_tta(model, img, device, temperature)
    else:
        tensor = transform(img).unsqueeze(0).to(device)
        model.eval()
        with torch.no_grad():
            logits = model(tensor)
            scaled = logits / temperature
            probs  = F.softmax(scaled, dim=1).squeeze().cpu().numpy()

    return probs
