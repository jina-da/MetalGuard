import os
import numpy as np
import torch
import torch.nn.functional as F
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from sklearn.metrics import (
    classification_report, confusion_matrix,
    roc_auc_score, f1_score, precision_score, recall_score
)

from config import CLASSES, NUM_CLASSES, RESULT_DIR


# ============================================================
#  검증 루프 + 성능 지표 계산
# ============================================================
def evaluate(model, loader, device, verbose=True):
    model.eval()
    all_labels  = []
    all_preds   = []
    all_probs   = []
    total_loss  = 0.0
    correct     = 0
    total       = 0

    criterion = torch.nn.CrossEntropyLoss()

    with torch.no_grad():
        for imgs, labels in loader:
            imgs, labels = imgs.to(device), labels.to(device)
            outputs = model(imgs)
            loss    = criterion(outputs, labels)

            probs  = F.softmax(outputs, dim=1)
            preds  = probs.argmax(dim=1)

            total_loss += loss.item() * imgs.size(0)
            correct    += (preds == labels).sum().item()
            total      += imgs.size(0)

            all_labels.extend(labels.cpu().numpy())
            all_preds.extend(preds.cpu().numpy())
            all_probs.extend(probs.cpu().numpy())

    all_labels = np.array(all_labels)
    all_preds  = np.array(all_preds)
    all_probs  = np.array(all_probs)

    val_loss = total_loss / total
    val_acc  = correct / total

    # AUROC (OvR 멀티클래스)
    auroc = roc_auc_score(
        all_labels, all_probs,
        multi_class='ovr', average='macro'
    )

    # 불량 클래스 기준 Recall (normal 제외한 4클래스 평균)
    defect_idx = [i for i, c in enumerate(CLASSES) if c != 'normal']
    recall_per_class = recall_score(
        all_labels, all_preds,
        labels=defect_idx, average=None, zero_division=0
    )
    recall    = recall_per_class.mean()
    f1        = f1_score(all_labels, all_preds, average='macro', zero_division=0)
    precision = precision_score(all_labels, all_preds, average='macro', zero_division=0)

    metrics = {
        'val_loss' : val_loss,
        'val_acc'  : val_acc,
        'recall'   : recall,
        'auroc'    : auroc,
        'f1'       : f1,
        'precision': precision,
    }

    if verbose:
        print("\n" + "=" * 60)
        print(" 성능 지표 요약")
        print("=" * 60)
        print(f"  Val Loss   : {val_loss:.4f}")
        print(f"  Val Acc    : {val_acc:.4f}")
        print(f"  Recall     : {recall:.4f}  (목표 >= 0.90) {'✓' if recall >= 0.90 else '✗'}")
        print(f"  AUROC      : {auroc:.4f}  (목표 >= 0.85) {'✓' if auroc >= 0.85 else '✗'}")
        print(f"  F1         : {f1:.4f}  (목표 >= 0.82) {'✓' if f1 >= 0.82 else '✗'}")
        print(f"  Precision  : {precision:.4f}  (목표 >= 0.75) {'✓' if precision >= 0.75 else '✗'}")

        print("\n[클래스별 Classification Report]")
        print(classification_report(
            all_labels, all_preds,
            target_names=CLASSES, zero_division=0
        ))

        # Confusion Matrix 저장
        _save_confusion_matrix(all_labels, all_preds)

        # AUROC 곡선 저장
        _save_auroc_curve(all_labels, all_probs)

    return metrics


# ============================================================
#  Confusion Matrix 시각화
# ============================================================
def _save_confusion_matrix(labels, preds):
    os.makedirs(RESULT_DIR, exist_ok=True)
    cm = confusion_matrix(labels, preds)
    fig, ax = plt.subplots(figsize=(7, 6))
    im = ax.imshow(cm, cmap='Blues')
    plt.colorbar(im, ax=ax)
    ax.set_xticks(range(NUM_CLASSES))
    ax.set_yticks(range(NUM_CLASSES))
    ax.set_xticklabels(CLASSES, rotation=45, ha='right')
    ax.set_yticklabels(CLASSES)
    ax.set_xlabel('Predicted')
    ax.set_ylabel('True')
    ax.set_title('Confusion Matrix')

    for i in range(NUM_CLASSES):
        for j in range(NUM_CLASSES):
            ax.text(j, i, str(cm[i, j]),
                    ha='center', va='center',
                    color='white' if cm[i, j] > cm.max() / 2 else 'black')

    plt.tight_layout()
    path = os.path.join(RESULT_DIR, 'confusion_matrix.png')
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"Confusion Matrix 저장: {path}")


# ============================================================
#  AUROC 곡선 시각화
# ============================================================
def _save_auroc_curve(labels, probs):
    from sklearn.preprocessing import label_binarize
    from sklearn.metrics import roc_curve, auc

    os.makedirs(RESULT_DIR, exist_ok=True)
    labels_bin = label_binarize(labels, classes=list(range(NUM_CLASSES)))

    fig, ax = plt.subplots(figsize=(8, 6))
    colors = ['#E24B4A', '#378ADD', '#639922', '#EF9F27', '#7F77DD']

    for i, (cls, color) in enumerate(zip(CLASSES, colors)):
        fpr, tpr, _ = roc_curve(labels_bin[:, i], probs[:, i])
        roc_auc     = auc(fpr, tpr)
        ax.plot(fpr, tpr, color=color, lw=1.5,
                label=f'{cls} (AUC={roc_auc:.3f})')

    ax.plot([0, 1], [0, 1], 'k--', lw=0.8)
    ax.set_xlim([0.0, 1.0])
    ax.set_ylim([0.0, 1.05])
    ax.set_xlabel('False Positive Rate')
    ax.set_ylabel('True Positive Rate')
    ax.set_title('ROC Curve per Class')
    ax.legend(loc='lower right', fontsize=9)

    plt.tight_layout()
    path = os.path.join(RESULT_DIR, 'auroc_curve.png')
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"AUROC 곡선 저장: {path}")
