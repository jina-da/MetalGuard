import os
import numpy as np
from PIL import Image

import torch
from torch.utils.data import Dataset, DataLoader
import torchvision.transforms as T

from config import DATA_DIR, CLASSES, IMG_SIZE, BATCH_SIZE, NUM_WORKERS, SEED


# ============================================================
#  그레이스케일 → 3채널 복제 변환
#  카메라는 그레이스케일 촬영 → 동일한 값으로 3채널 복제
#  ImageNet 사전학습 가중치 100% 활용 가능
# ============================================================
class GrayToRGB:
    def __call__(self, img):
        # PIL Image를 그레이스케일로 변환 후 RGB로 복제
        gray = img.convert('L')
        return gray.convert('RGB')


def get_transforms(mode='train'):
    """
    mode: 'train' | 'val'
    증강 전략 (v2 - 실사 환경 대응 강화):
      - 수평/수직 플립
      - 랜덤 회전 (카메라 각도 미세 오차 대응)
      - 밝기/대비/채도 조정 강화 (조명 변화 대응)
      - 랜덤 블러 (카메라 포커스 흔들림 대응)
      - 가우시안 노이즈 강화 std 0.02 → 0.05
      - 랜덤 원근 변환 (카메라 각도 변화 대응)
      - RandomErasing 확률/크기 상향 (부분 가림 강화)
    """
    normalize = T.Normalize(
        mean=[0.485, 0.456, 0.406],
        std =[0.229, 0.224, 0.225]
    )

    if mode == 'train':
        return T.Compose([
            GrayToRGB(),
            T.Resize((IMG_SIZE, IMG_SIZE)),
            T.RandomHorizontalFlip(p=0.5),
            T.RandomVerticalFlip(p=0.5),
            T.RandomRotation(degrees=15),
            T.ColorJitter(brightness=0.4, contrast=0.4, saturation=0.2),
            T.RandomApply([T.GaussianBlur(kernel_size=3, sigma=(0.1, 2.0))], p=0.3),
            T.RandomPerspective(distortion_scale=0.2, p=0.3),
            T.ToTensor(),
            AddGaussianNoise(std=0.05),
            T.RandomErasing(p=0.4, scale=(0.02, 0.20), ratio=(0.3, 3.3)),
            normalize,
        ])
    else:
        return T.Compose([
            GrayToRGB(),
            T.Resize((IMG_SIZE, IMG_SIZE)),
            T.ToTensor(),
            normalize,
        ])


class AddGaussianNoise:
    """가우시안 노이즈 추가 (ToTensor 이후 적용)"""
    def __init__(self, std=0.05):
        self.std = std

    def __call__(self, tensor):
        return tensor + torch.randn_like(tensor) * self.std


class MetalDefectDataset(Dataset):
    def __init__(self, root, mode='train', transform=None):
        """
        root  : data/metal_defects/
        mode  : 'train' | 'val'
        """
        self.transform = transform
        self.samples   = []
        self.labels    = []

        split_dir = os.path.join(root, mode)
        for class_idx, class_name in enumerate(CLASSES):
            class_dir = os.path.join(split_dir, class_name)
            if not os.path.isdir(class_dir):
                raise FileNotFoundError(f"클래스 폴더 없음: {class_dir}")
            for fname in sorted(os.listdir(class_dir)):
                if fname.lower().endswith(('.jpg', '.jpeg', '.png', '.bmp')):
                    self.samples.append(os.path.join(class_dir, fname))
                    self.labels.append(class_idx)

        print(f"[{mode}] 총 {len(self.samples)}장 로드 완료")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        img   = Image.open(self.samples[idx])
        label = self.labels[idx]
        if self.transform:
            img = self.transform(img)
        return img, label


def get_dataloaders():
    train_dataset = MetalDefectDataset(
        root=DATA_DIR, mode='train',
        transform=get_transforms('train')
    )
    val_dataset = MetalDefectDataset(
        root=DATA_DIR, mode='val',
        transform=get_transforms('val')
    )

    g = torch.Generator()
    g.manual_seed(SEED)

    train_loader = DataLoader(
        train_dataset,
        batch_size=BATCH_SIZE,
        shuffle=True,
        num_workers=NUM_WORKERS,
        pin_memory=True,
        worker_init_fn=lambda wid: np.random.seed(SEED + wid),
        generator=g,
    )
    val_loader = DataLoader(
        val_dataset,
        batch_size=BATCH_SIZE,
        shuffle=False,
        num_workers=NUM_WORKERS,
        pin_memory=True,
    )

    return train_loader, val_loader
