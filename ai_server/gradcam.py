"""
gradcam.py — GradCAM 히트맵 생성

사용법:
  단건:   python gradcam.py --image test_images/1.png
  폴더:   python gradcam.py --folder test_images/
  val셋:  python gradcam.py --val
  크롭:   python gradcam.py --folder test_images/ --crop 0.15
  온도:   python gradcam.py --folder test_images/ --temp 0.50
"""
import os
import sys
import argparse
import numpy as np
import torch
import torch.nn.functional as F
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
from PIL import Image

from config import CLASSES, IMG_SIZE, RESULT_DIR, PASS_THRESHOLD, UNCERTAIN_THRESHOLD
from dataset import get_transforms

# 한글 폰트 → 영문 fallback
_kor = [f.name for f in fm.fontManager.ttflist
        if any(k in f.name for k in ('Nanum', 'Malgun', 'Gothic', 'Batang'))]
plt.rcParams['font.family'] = _kor[0] if _kor else 'DejaVu Sans'
plt.rcParams['axes.unicode_minus'] = False


# ============================================================
#  GradCAM 클래스
# ============================================================
class GradCAM:
    def __init__(self, model, target_layer):
        self.model        = model
        self.target_layer = target_layer
        self.gradients    = None
        self.activations  = None
        self._register_hooks()

    def _register_hooks(self):
        def forward_hook(module, input, output):
            self.activations = output.detach()

        def backward_hook(module, grad_in, grad_out):
            self.gradients = grad_out[0].detach()

        self.target_layer.register_forward_hook(forward_hook)
        self.target_layer.register_full_backward_hook(backward_hook)

    def generate(self, img_tensor, class_idx=None, temperature=1.0):
        self.model.eval()
        img_tensor = img_tensor.requires_grad_(True)

        output = self.model(img_tensor)
        scaled = output / temperature
        probs  = F.softmax(scaled, dim=1)

        if class_idx is None:
            class_idx = output.argmax(dim=1).item()

        self.model.zero_grad()
        output[0, class_idx].backward()

        weights = self.gradients.mean(dim=(2, 3), keepdim=True)
        cam     = (weights * self.activations).sum(dim=1, keepdim=True)
        cam     = F.relu(cam)
        cam     = F.interpolate(cam, size=(IMG_SIZE, IMG_SIZE),
                                mode='bilinear', align_corners=False)
        cam     = cam.squeeze().cpu().numpy()

        cam_min, cam_max = cam.min(), cam.max()
        if cam_max - cam_min > 1e-8:
            cam = (cam - cam_min) / (cam_max - cam_min)

        return cam, class_idx, probs.squeeze().detach().cpu().numpy()


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
#  단일 이미지 GradCAM 시각화
# ============================================================
def visualize_gradcam(model, image_path, device, save_name='gradcam',
                       crop_ratio=0.0, temperature=1.0):
    os.makedirs(RESULT_DIR, exist_ok=True)

    target_layer = model.features[-1]
    gradcam      = GradCAM(model, target_layer)
    transform    = get_transforms('val')

    img_pil    = Image.open(image_path)
    img_pil    = center_crop(img_pil, crop_ratio)
    img_tensor = transform(img_pil).unsqueeze(0).to(device)

    cam, pred_idx, probs = gradcam.generate(img_tensor, temperature=temperature)
    pred_class  = CLASSES[pred_idx]
    confidence  = probs[pred_idx]
    normal_prob = probs[CLASSES.index('normal')]
    max_prob    = probs.max()

    # 3단계 판정
    if normal_prob >= PASS_THRESHOLD:
        verdict = 'PASS'
    elif max_prob < UNCERTAIN_THRESHOLD:
        verdict = 'UNCERTAIN'
    else:
        verdict = 'FAIL'

    verdict_colors = {'PASS': '#639922', 'FAIL': '#E24B4A', 'UNCERTAIN': '#EF9F27'}
    v_color = verdict_colors[verdict]

    # 원본 이미지
    img_show = img_pil.convert('L').convert('RGB')
    img_show = img_show.resize((IMG_SIZE, IMG_SIZE))
    img_np   = np.array(img_show) / 255.0

    # 히트맵 오버레이
    heatmap = plt.cm.jet(cam)[:, :, :3]
    overlay = np.clip(0.5 * img_np + 0.5 * heatmap, 0, 1)

    # ---- 시각화 레이아웃: 원본 / 히트맵 / 오버레이 / 확률 막대 ----
    fig, axes = plt.subplots(1, 4, figsize=(16, 4))
    fig.suptitle(
        f"Verdict: {verdict}  |  Pred: {pred_class} ({confidence:.3f})  |  "
        f"normal_prob: {normal_prob:.3f}  |  T={temperature:.2f}",
        color=v_color, fontsize=11
    )

    # 1. 원본
    axes[0].imshow(img_np, cmap='gray')
    axes[0].set_title('Original')
    axes[0].axis('off')
    for spine in axes[0].spines.values():
        spine.set_edgecolor(v_color)
        spine.set_linewidth(3)

    # 2. GradCAM 히트맵
    axes[1].imshow(cam, cmap='jet')
    axes[1].set_title('GradCAM Heatmap')
    axes[1].axis('off')

    # 컬러바
    sm = plt.cm.ScalarMappable(cmap='jet', norm=plt.Normalize(0, 1))
    plt.colorbar(sm, ax=axes[1], fraction=0.046, pad=0.04)

    # 3. 오버레이
    axes[2].imshow(overlay)
    axes[2].set_title('Overlay')
    axes[2].axis('off')

    # 4. 확률 막대
    bar_colors = ['#E24B4A' if c != 'normal' else '#639922' for c in CLASSES]
    bars = axes[3].barh(CLASSES, probs, color=bar_colors)
    axes[3].set_xlim(0, 1.05)
    axes[3].set_xlabel('Probability')
    axes[3].set_title('Class Probability')
    axes[3].axvline(PASS_THRESHOLD, color='#639922', linestyle='--',
                    lw=0.8, label=f'PASS threshold ({PASS_THRESHOLD})')
    axes[3].axvline(UNCERTAIN_THRESHOLD, color='#EF9F27', linestyle='--',
                    lw=0.8, label=f'UNCERTAIN threshold ({UNCERTAIN_THRESHOLD})')
    axes[3].legend(fontsize=7)
    for bar, p in zip(bars, probs):
        axes[3].text(p + 0.01, bar.get_y() + bar.get_height() / 2,
                     f'{p:.3f}', va='center', fontsize=9)

    plt.tight_layout()
    save_path = os.path.join(RESULT_DIR, f'{save_name}.png')
    plt.savefig(save_path, dpi=150, bbox_inches='tight')
    plt.close('all')

    fname = os.path.basename(image_path)
    print(f"  {fname:<25} verdict={verdict:<10} pred={pred_class:<10} "
          f"normal={normal_prob:.3f}  max={max_prob:.3f}  → {save_path}")

    return cam, verdict, pred_class, probs


# ============================================================
#  폴더 일괄 GradCAM
# ============================================================
def visualize_folder_gradcam(model, folder_path, device,
                              crop_ratio=0.0, temperature=1.0):
    exts   = ('.jpg', '.jpeg', '.png', '.bmp')
    images = [
        os.path.join(folder_path, f)
        for f in sorted(os.listdir(folder_path))
        if f.lower().endswith(exts)
    ]
    if not images:
        print(f"이미지 파일 없음: {folder_path}")
        return

    print(f"\n{'='*60}")
    print(f" GradCAM 일괄 생성: {len(images)}장  (crop={crop_ratio}, T={temperature:.2f})")
    print(f"{'='*60}")

    for img_path in images:
        fname     = os.path.splitext(os.path.basename(img_path))[0]
        save_name = f'gradcam_{fname}'
        visualize_gradcam(model, img_path, device, save_name, crop_ratio, temperature)

    print(f"\n결과 저장: {RESULT_DIR}/gradcam_*.png")


# ============================================================
#  val 셋 클래스별 샘플 GradCAM
# ============================================================
def visualize_val_gradcam(model, device, n_per_class=1):
    from config import DATA_DIR
    import random

    print("\n[GradCAM — val 셋 클래스별 샘플]")
    for class_name in CLASSES:
        class_dir = os.path.join(DATA_DIR, 'val', class_name)
        images    = [f for f in os.listdir(class_dir)
                     if f.lower().endswith(('.jpg', '.jpeg', '.png'))]
        samples   = random.sample(images, min(n_per_class, len(images)))
        for i, fname in enumerate(samples):
            img_path  = os.path.join(class_dir, fname)
            save_name = f'gradcam_val_{class_name}_{i}'
            visualize_gradcam(model, img_path, device, save_name)


# ============================================================
#  메인
# ============================================================
def main():
    parser = argparse.ArgumentParser(description='MetalGuard GradCAM 시각화')
    parser.add_argument('--image',   type=str,   help='단건 이미지 경로')
    parser.add_argument('--folder',  type=str,   help='폴더 경로 (일괄)')
    parser.add_argument('--val',     action='store_true', help='val 셋 클래스별 시각화')
    parser.add_argument('--version', type=str,   default='best')
    parser.add_argument('--crop',    type=float, default=0.0,  help='배경 제거 비율')
    parser.add_argument('--temp',    type=float, default=1.0,  help='온도 스케일링 T')
    args = parser.parse_args()

    if not args.image and not args.folder and not args.val:
        parser.print_help()
        sys.exit(1)

    from model import load_model
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model  = load_model(args.version, device)

    if args.image:
        fname     = os.path.splitext(os.path.basename(args.image))[0]
        save_name = f'gradcam_{fname}'
        visualize_gradcam(model, args.image, device, save_name,
                          args.crop, args.temp)

    elif args.folder:
        visualize_folder_gradcam(model, args.folder, device,
                                 args.crop, args.temp)

    elif args.val:
        visualize_val_gradcam(model, device)


if __name__ == '__main__':
    main()
