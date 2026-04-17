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
  # 라벨을 정확히 아는 경우
  python temperature_scaling.py --folder test_images/ --labels 0,3,2,2,4,0

  # 라벨을 모르는 이미지는 ? 로 표시 (모델 예측값을 임시 라벨로 자동 사용)
  python temperature_scaling.py --folder test_images/ --labels 0,?,?,?,0,3,3,1,3,2,2,4,0

  # 라벨을 전혀 모르는 경우 (전부 모델 예측값으로 자동 설정)
  python temperature_scaling.py --folder test_images/ --labels auto

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

from config import CLASSES, RESULT_DIR, PASS_THRESHOLD, UNCERTAIN_THRESHOLD
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


def get_logits(model, image_path, device, transform):
    """logit만 추출 (온도 탐색용)"""
    img    = Image.open(image_path)
    tensor = transform(img).unsqueeze(0).to(device)
    model.eval()
    with torch.no_grad():
        logits = model(tensor).squeeze().cpu()
    return logits


# ============================================================
#  라벨 파싱 — ? 지원 + auto 모드
#  ? 인 경우 모델 예측값(T=1.0 기준 argmax)을 임시 라벨로 사용
# ============================================================
def parse_labels(label_str, image_paths, model, device, transform):
    """
    label_str : '0,?,2,3,auto' 형식 또는 'auto' (전체 자동)
    ? 또는 auto → 모델 예측값을 임시 라벨로 사용
    """
    if label_str.strip().lower() == 'auto':
        raw = ['?'] * len(image_paths)
    else:
        raw = [x.strip() for x in label_str.split(',')]

    if len(raw) != len(image_paths):
        print(f"라벨 수({len(raw)})와 이미지 수({len(image_paths)})가 다릅니다.")
        print("이미지 순서:", [os.path.basename(p) for p in image_paths])
        exit(1)

    labels   = []
    auto_cnt = 0
    print("\n이미지 순서 및 라벨 확인:")
    for img_path, r in zip(image_paths, raw):
        fname = os.path.basename(img_path)
        if r == '?':
            # 모델 예측값을 임시 라벨로 사용
            logits = get_logits(model, img_path, device, transform)
            pred   = int(logits.argmax().item())
            labels.append(pred)
            auto_cnt += 1
            print(f"  {fname:<20} → {CLASSES[pred]:<10} (label={pred})  [자동]")
        else:
            lbl = int(r)
            labels.append(lbl)
            print(f"  {fname:<20} → {CLASSES[lbl]:<10} (label={lbl})")

    if auto_cnt > 0:
        print(f"\n  ※ {auto_cnt}개 이미지는 모델 예측값을 임시 라벨로 사용했습니다.")
        print(f"  ※ 정확한 라벨을 알고 있다면 직접 입력하면 더 정확한 T를 찾을 수 있습니다.")

    return labels


# ============================================================
#  최적 온도 탐색 — NLL 최소화 기준
# ============================================================
def find_best_temperature(model, image_paths, labels, device, transform,
                           t_range=(0.1, 10.0), steps=100):
    print("\n[온도 스케일링] 최적 T 탐색 중...")
    print(f"  탐색 범위: T={t_range[0]} ~ {t_range[1]}  ({steps}개 구간)")

    # 모든 이미지의 logit 수집
    all_logits = []
    for img_path in image_paths:
        all_logits.append(get_logits(model, img_path, device, transform))

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
    print(f"  {'파일명':<20} {'예측클래스':<12} {'normal':<8} {'max_prob':<10} {'판정'}")
    print(f"  {'-'*65}")
    for img_path, lbl in zip(image_paths, labels):
        probs_t     = predict_with_temperature(model, img_path, device, transform, best_t)
        pred_cls    = CLASSES[probs_t.argmax()]
        normal_prob = probs_t[CLASSES.index('normal')]
        max_prob    = probs_t.max()
        true_cls    = CLASSES[lbl]
        verdict     = ('PASS'      if normal_prob >= PASS_THRESHOLD
                  else 'UNCERTAIN' if max_prob    <  UNCERTAIN_THRESHOLD
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
                        help=(
                            'crack=0,hole=1,normal=2,rust=3,scratch=4 순서로 쉼표 구분. '
                            '모르는 이미지는 ? 로 표시 (모델 예측값 자동 사용). '
                            '전부 모를 경우 auto 입력.'
                        ))
    parser.add_argument('--version', type=str, default='best')
    parser.add_argument('--tmin',    type=float, default=0.1,  help='탐색 최소 T (기본 0.1)')
    parser.add_argument('--tmax',    type=float, default=10.0, help='탐색 최대 T (기본 10.0)')
    parser.add_argument('--steps',   type=int,   default=100,  help='탐색 단계 수 (기본 100)')
    args = parser.parse_args()

    exts   = ('.jpg', '.jpeg', '.png', '.bmp')
    images = sorted([
        os.path.join(args.folder, f)
        for f in os.listdir(args.folder)
        if f.lower().endswith(exts)
    ])

    if not images:
        print(f"이미지 파일 없음: {args.folder}")
        exit(1)

    device    = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model     = load_model(args.version, device)
    transform = get_transforms('val')

    # 라벨 파싱 (? 및 auto 지원)
    labels = parse_labels(args.labels, images, model, device, transform)

    best_t = find_best_temperature(
        model, images, labels, device, transform,
        t_range=(args.tmin, args.tmax), steps=args.steps
    )

    print(f"\n{'='*50}")
    print(f" 권장 명령어:")
    print(f" python predict.py --folder {args.folder} --tta --temp {best_t:.2f}")
    print(f" run.sh의 TEMP={best_t:.2f} 로 수정하세요.")
    print(f"{'='*50}")
