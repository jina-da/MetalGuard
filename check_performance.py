"""
check_performance.py — 학습된 모델 성능 평가

실행:
  python check_performance.py
  python check_performance.py --version best
"""
import torch
import argparse
from model import load_model
from dataset import get_dataloaders
from evaluate import evaluate

parser = argparse.ArgumentParser()
parser.add_argument('--version', type=str, default='best')
args = parser.parse_args()

device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
print(f"디바이스: {device}")

model = load_model(args.version, device)
_, val_loader = get_dataloaders()
metrics = evaluate(model, val_loader, device, verbose=True)
