"""
predict.py — 실사 이미지 단건 / 폴더 일괄 테스트
TTA(Test Time Augmentation) + 온도 스케일링 통합 적용

사용법:
  기본:      python predict.py --folder test_images/
  TTA 적용:  python predict.py --folder test_images/ --tta
  온도 적용: python predict.py --folder test_images/ --temp 2.0
  둘 다:     python predict.py --folder test_images/ --tta --temp 2.0
  크롭:      python predict.py --folder test_images/ --tta --temp 2.0 --crop 0.15
  단건:      python predict.py --image 1.png --tta --temp 2.0
"""
import os
import sys
import argparse
import numpy as np
import torch
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
from PIL import Image

from config import CLASSES, IMG_SIZE, RESULT_DIR, PASS_THRESHOLD, UNCERTAIN_THRESHOLD
from dataset import get_transforms
from model import load_model
from tta import predict_with_tta_or_single

# 한글 폰트 → 영문 fallback
_kor = [f.name for f in fm.fontManager.ttflist
        if any(k in f.name for k in ('Nanum', 'Malgun', 'Gothic', 'Batang'))]
plt.rcParams['font.family'] = _kor[0] if _kor else 'DejaVu Sans'
plt.rcParams['axes.unicode_minus'] = False


# ============================================================
#  ROI 크롭
# ============================================================
def center_crop(img, crop_ratio):
    if crop_ratio <= 0:
        return img
    w, h   = img.size
    left   = int(w * crop_ratio)
    top    = int(h * crop_ratio)
    right  = int(w * (1 - crop_ratio))
    bottom = int(h * (1 - crop_ratio))
    return img.crop((left, top, right, bottom))


# ============================================================
#  단일 이미지 추론 (TTA + 온도 스케일링 통합)
# ============================================================
def predict_single(model, image_path, device, transform,
                   crop_ratio=0.0, temperature=1.0, use_tta=False):
    img  = Image.open(image_path)
    img  = center_crop(img, crop_ratio)

    probs = predict_with_tta_or_single(
        model, img, device, transform,
        temperature=temperature, use_tta=use_tta
    )

    pred_idx    = probs.argmax()
    pred_class  = CLASSES[pred_idx]
    max_prob    = probs.max()
    normal_prob = probs[CLASSES.index('normal')]

    # 3단계 판정
    if normal_prob >= PASS_THRESHOLD:
        verdict = 'PASS'
    elif max_prob < UNCERTAIN_THRESHOLD:
        verdict = 'UNCERTAIN'
    else:
        verdict = 'FAIL'

    return {
        'image_path' : image_path,
        'verdict'    : verdict,
        'pred_class' : pred_class,
        'max_prob'   : float(max_prob),
        'normal_prob': float(normal_prob),
        'probs'      : probs,
        'crop_img'   : img,
    }


# ============================================================
#  결과 시각화
# ============================================================
def visualize_result(result, save_name, temperature=1.0, use_tta=False):
    os.makedirs(RESULT_DIR, exist_ok=True)

    img    = result['crop_img'].convert('L').convert('RGB')
    img    = img.resize((IMG_SIZE, IMG_SIZE))
    img_np = np.array(img) / 255.0

    verdict_colors = {'PASS': '#639922', 'FAIL': '#E24B4A', 'UNCERTAIN': '#EF9F27'}
    color = verdict_colors[result['verdict']]

    fig, axes = plt.subplots(1, 2, figsize=(10, 4))

    axes[0].imshow(img_np, cmap='gray')
    mode_str = f"{'TTA ' if use_tta else ''}T={temperature:.2f}"
    axes[0].set_title(
        f"Verdict: {result['verdict']}  |  Pred: {result['pred_class']}  [{mode_str}]\n"
        f"max_prob: {result['max_prob']:.4f}  |  normal_prob: {result['normal_prob']:.4f}",
        color=color, fontsize=10
    )
    axes[0].axis('off')
    for spine in axes[0].spines.values():
        spine.set_edgecolor(color)
        spine.set_linewidth(3)

    bar_colors = ['#E24B4A' if c != 'normal' else '#639922' for c in CLASSES]
    bars = axes[1].barh(CLASSES, result['probs'], color=bar_colors)
    axes[1].set_xlim(0, 1.05)
    axes[1].set_xlabel('Probability')
    axes[1].set_title('Class Probability')
    for bar, p in zip(bars, result['probs']):
        axes[1].text(p + 0.01, bar.get_y() + bar.get_height() / 2,
                     f'{p:.4f}', va='center', fontsize=9)

    plt.tight_layout()
    save_path = os.path.join(RESULT_DIR, f'{save_name}.png')
    plt.savefig(save_path, dpi=150)
    plt.close()
    return save_path


# ============================================================
#  폴더 일괄 테스트
# ============================================================
def predict_folder(model, folder_path, device, transform,
                   crop_ratio=0.0, temperature=1.0, use_tta=False):
    exts   = ('.jpg', '.jpeg', '.png', '.bmp')
    images = [
        os.path.join(folder_path, f)
        for f in sorted(os.listdir(folder_path))
        if f.lower().endswith(exts)
    ]
    if not images:
        print(f"이미지 파일 없음: {folder_path}")
        return

    mode_str = f"TTA={'ON' if use_tta else 'OFF'}  T={temperature:.2f}  crop={crop_ratio}"
    print(f"\n{'='*60}")
    print(f" 일괄 테스트: {len(images)}장  [{mode_str}]")
    print(f"{'='*60}")
    print(f"{'파일명':<30} {'판정':<12} {'예측클래스':<12} {'max_prob':<10} {'normal_prob'}")
    print('-' * 80)

    results = []
    for img_path in images:
        r = predict_single(model, img_path, device, transform,
                           crop_ratio, temperature, use_tta)
        results.append(r)
        fname = os.path.basename(img_path)
        print(
            f"{fname:<30} {r['verdict']:<12} {r['pred_class']:<12} "
            f"{r['max_prob']:.4f}      {r['normal_prob']:.4f}"
        )
        save_name = f"predict_{os.path.splitext(fname)[0]}"
        visualize_result(r, save_name, temperature, use_tta)

    verdicts = [r['verdict'] for r in results]
    print(f"\n{'='*60}")
    print(f" 요약  [{mode_str}]")
    print(f"{'='*60}")
    print(f"  PASS      : {verdicts.count('PASS')}건")
    print(f"  FAIL      : {verdicts.count('FAIL')}건")
    print(f"  UNCERTAIN : {verdicts.count('UNCERTAIN')}건")
    print(f"  결과 저장 : {RESULT_DIR}/predict_*.png")


# ============================================================
#  메인
# ============================================================
def main():
    parser = argparse.ArgumentParser(description='MetalGuard 실사 이미지 테스트')
    parser.add_argument('--image',   type=str,   help='단건 이미지 경로')
    parser.add_argument('--folder',  type=str,   help='폴더 경로 (일괄 테스트)')
    parser.add_argument('--version', type=str,   default='best', help='모델 버전')
    parser.add_argument('--crop',    type=float, default=0.0,
                        help='배경 제거 crop 비율 (예: 0.15)')
    parser.add_argument('--temp',    type=float, default=1.0,
                        help='온도 스케일링 T (기본 1.0, 실사 환경 권장 1.5~3.0)')
    parser.add_argument('--tta',     action='store_true',
                        help='TTA(Test Time Augmentation) 적용 (8회 평균 추론)')
    args = parser.parse_args()

    if not args.image and not args.folder:
        parser.print_help()
        sys.exit(1)

    device    = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model     = load_model(args.version, device)
    transform = get_transforms('val')

    if args.tta:
        print(f"[TTA] 활성화 — 이미지당 8회 추론 후 평균")
    if args.temp != 1.0:
        print(f"[온도 스케일링] T={args.temp:.2f} 적용")

    if args.image:
        r = predict_single(model, args.image, device, transform,
                           args.crop, args.temp, args.tta)
        print(f"\n{'='*60}")
        print(f" 판정 결과: {r['verdict']}")
        print(f"{'='*60}")
        print(f"  예측 클래스 : {r['pred_class']}")
        print(f"  max_prob    : {r['max_prob']:.4f}")
        print(f"  normal_prob : {r['normal_prob']:.4f}")
        print(f"\n  클래스별 확률:")
        for cls, p in zip(CLASSES, r['probs']):
            bar = '█' * int(p * 30)
            print(f"    {cls:<10} {bar:<30} {p:.4f}")
        fname     = os.path.splitext(os.path.basename(args.image))[0]
        save_path = visualize_result(r, f'predict_{fname}', args.temp, args.tta)
        print(f"\n  결과 저장: {save_path}")

    elif args.folder:
        predict_folder(model, args.folder, device, transform,
                       args.crop, args.temp, args.tta)


if __name__ == '__main__':
    main()
