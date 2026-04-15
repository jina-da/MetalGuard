"""
temperature_scaling.py — 온도 스케일링 (Temperature Scaling)

원리:
  합성 데이터로 학습한 모델은 실사 이미지에서 softmax 확률이
  전반적으로 낮게 나오는 경향이 있음 (under-confident).
  온도 T로 logit을 나눠주면 확률 분포가 보정됨.

  T > 1 → 확률 분포를 더 균일하게 (under-confident 보정)
  T < 1 → 확률 분포를 더 날카롭게 (over-confident 보정)
  T = 1 → 원본 그대로

사용법:
  python temperature_scaling.py --folder test_images/ --labels 0,3,2,2,4,0
  (labels: crack=0, hole=1, normal=2, rust=3, scratch=4)

  --tmin, --tmax, --steps 으로 탐색 범위 조정 가능
  예) --tmin 0.1 --tmax 10.0 --steps 100
"""
import os
import argparse
import numpy as np
import torch
import torch.nn.functional as F
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from PIL import Image

from config import CLASSES, RESULT_DIR
from dataset import get_transforms
from model import load_model


# ============================================================
#  온도 스케일링 적용 추론
# ============================================================
def predict_with_temperature(model, image_path, device, transform, temperature=1.0):
    img    = Image.open(image_path)
    tensor = transform(img).unsqueeze(0).to(device)
    model.eval()
    with torch.no_grad():
        logits = model(tensor)
        scaled = logits / temperature
        probs  = F.softmax(scaled, dim=1).squeeze().cpu().numpy()
    return probs


# ============================================================
#  최적 온도 탐색 — NLL 최소화 기준
# ============================================================
def find_best_temperature(model, image_paths, labels, device, transform,
                           t_range=(0.1, 10.0), steps=100):
    print("\n[온도 스케일링] 최적 T 탐색 중...")
    print(f"  탐색 범위: T={t_range[0]} ~ {t_range[1]}  ({steps}개 구간)")

    # 모든 이미지의 logit 수집
    all_logits = []
    model.eval()
    for img_path in image_paths:
        img    = Image.open(img_path)
        tensor = transform(img).unsqueeze(0).to(device)
        with torch.no_grad():
            logits = model(tensor).squeeze().cpu()
        all_logits.append(logits)

    all_logits = torch.stack(all_logits)
    all_labels = torch.tensor(labels, dtype=torch.long)

    best_t   = 1.0
    best_nll = float('inf')
    results  = []

    t_values = np.linspace(t_range[0], t_range[1], steps)
    for t in t_values:
        scaled = all_logits / t
        probs  = F.softmax(scaled, dim=1)
        nll    = F.nll_loss(torch.log(probs + 1e-8), all_labels).item()
        results.append((t, nll))
        if nll < best_nll:
            best_nll = nll
            best_t   = t

    # T=1.0 기준 NLL
    t1_nll = None
    for t, nll in results:
        if abs(t - 1.0) < (t_range[1] - t_range[0]) / steps * 1.5:
            t1_nll = nll
            break

    print(f"\n  [결과]")
    print(f"  최적 Temperature : {best_t:.3f}")
    print(f"  최적 NLL         : {best_nll:.4f}")
    if t1_nll:
        print(f"  T=1.0 NLL        : {t1_nll:.4f}  (개선: {t1_nll - best_nll:.4f})")

    # 각 이미지에 최적 T 적용 후 확률 출력
    print(f"\n  [T={best_t:.2f} 적용 시 각 이미지 확률]")
    print(f"  {'파일명':<20} {'예측클래스':<12} {'normal':<8} {'max_prob':<10} {'판정예상'}")
    print(f"  {'-'*65}")
    from config import PASS_THRESHOLD, UNCERTAIN_THRESHOLD
    for img_path, lbl in zip(image_paths, labels):
        probs_t = predict_with_temperature(model, img_path, device, transform, best_t)
        pred_cls    = CLASSES[probs_t.argmax()]
        normal_prob = probs_t[CLASSES.index('normal')]
        max_prob    = probs_t.max()
        true_cls    = CLASSES[lbl]
        verdict = ('PASS' if normal_prob >= PASS_THRESHOLD
                   else 'UNCERTAIN' if max_prob < UNCERTAIN_THRESHOLD
                   else 'FAIL')
        correct = '✓' if pred_cls == true_cls else '✗'
        fname   = os.path.basename(img_path)
        print(f"  {fname:<20} {pred_cls:<12} {normal_prob:.4f}   {max_prob:.4f}     {verdict} {correct}")

    _save_temperature_search(results, best_t)
    return best_t


def _save_temperature_search(results, best_t):
    os.makedirs(RESULT_DIR, exist_ok=True)
    ts   = [r[0] for r in results]
    nlls = [r[1] for r in results]

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.plot(ts, nlls, color='#378ADD', lw=1.5, label='NLL Loss')
    ax.axvline(best_t, color='#E24B4A', linestyle='--', lw=1.2,
               label=f'Best T={best_t:.3f}')
    ax.axvline(1.0, color='#888780', linestyle=':', lw=1.0, label='T=1.0 (원본)')
    ax.set_xlabel('Temperature')
    ax.set_ylabel('NLL Loss')
    ax.set_title('Temperature Scaling Search')
    ax.legend()
    plt.tight_layout()
    path = os.path.join(RESULT_DIR, 'temperature_search.png')
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"\n  탐색 그래프 저장: {path}")


# ============================================================
#  CLI
# ============================================================
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='온도 스케일링 최적 T 탐색')
    parser.add_argument('--folder',  type=str, required=True)
    parser.add_argument('--labels',  type=str, required=True,
                        help='crack=0,hole=1,normal=2,rust=3,scratch=4 순서로 쉼표 구분')
    parser.add_argument('--version', type=str, default='best')
    parser.add_argument('--tmin',    type=float, default=0.1, help='탐색 최소 T (기본 0.1)')
    parser.add_argument('--tmax',    type=float, default=10.0, help='탐색 최대 T (기본 10.0)')
    parser.add_argument('--steps',   type=int,   default=100, help='탐색 단계 수 (기본 100)')
    args = parser.parse_args()

    labels = [int(x) for x in args.labels.split(',')]
    exts   = ('.jpg', '.jpeg', '.png', '.bmp')
    images = sorted([
        os.path.join(args.folder, f)
        for f in os.listdir(args.folder)
        if f.lower().endswith(exts)
    ])

    if len(images) != len(labels):
        print(f"이미지 수({len(images)})와 라벨 수({len(labels)})가 다릅니다.")
        print("이미지 순서:", [os.path.basename(p) for p in images])
        exit(1)

    print("이미지 순서 확인:")
    for i, (p, l) in enumerate(zip(images, labels)):
        print(f"  {os.path.basename(p)} → {CLASSES[l]} (label={l})")

    device    = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model     = load_model(args.version, device)
    transform = get_transforms('val')

    best_t = find_best_temperature(
        model, images, labels, device, transform,
        t_range=(args.tmin, args.tmax), steps=args.steps
    )
    print(f"\n{'='*50}")
    print(f" 권장 명령어:")
    print(f" python predict.py --folder {args.folder} --tta --temp {best_t:.2f}")
    print(f"{'='*50}")
