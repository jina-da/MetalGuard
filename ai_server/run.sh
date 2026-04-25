#!/bin/bash
# ============================================================
#  MetalGuard 실행 스크립트
#  사용법:
#    ./run.sh            → 판정 (predict)
#    ./run.sh gradcam    → 판정 + GradCAM
#    ./run.sh server     → AI 추론 서버 실행
# ============================================================

cd "$(dirname "$0")"
source .venv/bin/activate

# 기본 설정
FOLDER="test_images/"
TEMP=0.50
CROP=0.15
VERSION="best"
SERVER_HOST="0.0.0.0"
SERVER_PORT=9000

if [ "$1" == "server" ]; then
  echo "============================================"
  echo " MetalGuard AI 추론 서버 시작"
  echo " Host   : $SERVER_HOST"
  echo " Port   : $SERVER_PORT"
  echo " Model  : $VERSION"
  echo "============================================"
  python ai_server.py \
    --host    "$SERVER_HOST" \
    --port    "$SERVER_PORT" \
    --version "$VERSION"

else
  echo "============================================"
  echo " MetalGuard 판정 시작"
  echo " 폴더  : $FOLDER"
  echo " TTA   : ON  |  Temp: $TEMP  |  Crop: $CROP"
  echo "============================================"

  python predict.py \
    --folder  "$FOLDER" \
    --version "$VERSION" \
    --tta \
    --temp    "$TEMP" \
    --crop    "$CROP"

  if [ "$1" == "gradcam" ]; then
    echo ""
    echo "============================================"
    echo " GradCAM 히트맵 생성"
    echo "============================================"
    python gradcam.py \
      --folder  "$FOLDER" \
      --version "$VERSION" \
      --temp    "$TEMP" \
      --crop    "$CROP"
  fi

  echo ""
  echo "완료. 결과 이미지: results/"
fi
