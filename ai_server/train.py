import os
import random
import numpy as np
import torch
import torch.nn as nn
from torch.optim import AdamW
from torch.optim.lr_scheduler import CosineAnnealingLR

from config import (
    NUM_EPOCHS, LR, WEIGHT_DECAY, CLASS_WEIGHTS,
    RESULT_DIR, SEED, TARGET_RECALL, TARGET_AUROC
)
from dataset import get_dataloaders
from model import build_model, save_model
from evaluate import evaluate


# ============================================================
#  재현성 고정
# ============================================================
def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)
    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark     = False


# ============================================================
#  학습 1 에포크
# ============================================================
def train_one_epoch(model, loader, criterion, optimizer, device):
    model.train()
    total_loss, correct, total = 0.0, 0, 0

    for imgs, labels in loader:
        imgs, labels = imgs.to(device), labels.to(device)

        optimizer.zero_grad()
        outputs = model(imgs)
        loss    = criterion(outputs, labels)
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * imgs.size(0)
        preds       = outputs.argmax(dim=1)
        correct    += (preds == labels).sum().item()
        total      += imgs.size(0)

    return total_loss / total, correct / total


# ============================================================
#  메인 학습 루프
# ============================================================
def train():
    set_seed(SEED)
    os.makedirs(RESULT_DIR, exist_ok=True)

    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"사용 디바이스: {device}")
    if device.type == 'cuda':
        print(f"GPU: {torch.cuda.get_device_name(0)}")

    # 데이터 로더
    train_loader, val_loader = get_dataloaders()

    # 모델
    model = build_model(pretrained=True).to(device)
    print(f"파라미터 수: {sum(p.numel() for p in model.parameters()):,}")

    # 손실 함수 + class_weight (FN 최소화: normal 3.0)
    weights   = torch.tensor(CLASS_WEIGHTS, dtype=torch.float).to(device)
    criterion = nn.CrossEntropyLoss(weight=weights)

    # 옵티마이저 + 스케줄러
    optimizer = AdamW(model.parameters(), lr=LR, weight_decay=WEIGHT_DECAY)
    scheduler = CosineAnnealingLR(optimizer, T_max=NUM_EPOCHS, eta_min=1e-6)

    best_recall = 0.0
    best_epoch  = 0
    log_lines   = ['epoch,train_loss,train_acc,val_loss,val_acc,recall,auroc,f1,precision']

    print("\n" + "=" * 60)
    print(" MetalGuard EfficientNet-B2 학습 시작")
    print("=" * 60)

    for epoch in range(1, NUM_EPOCHS + 1):
        # 학습
        train_loss, train_acc = train_one_epoch(
            model, train_loader, criterion, optimizer, device
        )

        # 검증
        metrics = evaluate(model, val_loader, device, verbose=False)

        scheduler.step()

        recall    = metrics['recall']
        auroc     = metrics['auroc']
        f1        = metrics['f1']
        precision = metrics['precision']
        val_loss  = metrics['val_loss']
        val_acc   = metrics['val_acc']

        log_lines.append(
            f"{epoch},{train_loss:.4f},{train_acc:.4f},"
            f"{val_loss:.4f},{val_acc:.4f},"
            f"{recall:.4f},{auroc:.4f},{f1:.4f},{precision:.4f}"
        )

        print(
            f"[Epoch {epoch:02d}/{NUM_EPOCHS}] "
            f"loss: {train_loss:.4f} | acc: {train_acc:.4f} || "
            f"val_loss: {val_loss:.4f} | val_acc: {val_acc:.4f} | "
            f"Recall: {recall:.4f} | AUROC: {auroc:.4f} | "
            f"F1: {f1:.4f} | LR: {scheduler.get_last_lr()[0]:.2e}"
        )

        # 목표 달성 여부 표시
        goal_met = recall >= TARGET_RECALL and auroc >= TARGET_AUROC
        if goal_met:
            print(f"  ✓ 목표 달성! (Recall >= {TARGET_RECALL}, AUROC >= {TARGET_AUROC})")

        # Best Recall 기준으로 모델 저장
        if recall > best_recall:
            best_recall = recall
            best_epoch  = epoch
            save_model(model, version_tag='best', result_dict=metrics)
            print(f"  → Best 모델 저장 (Recall: {best_recall:.4f})")

    # 최종 모델 저장
    save_model(model, version_tag='final', result_dict=metrics)

    # 학습 로그 저장
    log_path = os.path.join(RESULT_DIR, 'train_log.csv')
    with open(log_path, 'w') as f:
        f.write('\n'.join(log_lines))
    print(f"\n학습 로그 저장: {log_path}")

    print("\n" + "=" * 60)
    print(f" 학습 완료 | Best Recall: {best_recall:.4f} (Epoch {best_epoch})")
    print("=" * 60)

    # 최종 평가 (best 모델 기준)
    print("\n[Best 모델 최종 평가]")
    from model import load_model
    best_model = load_model('best', device)
    evaluate(best_model, val_loader, device, verbose=True)


if __name__ == '__main__':
    train()
