#!/usr/bin/env bash
# 2026-10-09 s1 单图 CPU FP32 推理基线；固定权重与几何，不跟随默认权重选择。
set -euo pipefail
if [[ "$#" -ne 1 ]]; then
  echo "Usage: $0 IMAGE" >&2
  exit 2
fi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ "$(uname -s)" == "Darwin" ]]; then
  LPR_PY="${SMARTPARK_LPR_PY:-$HOME/.smartpark/lpr/bin/python}"
  OCR_PY="${SMARTPARK_OCR_PY:-$HOME/.smartpark/ocr/bin/python}"
else
  LPR_PY="${SMARTPARK_LPR_PY:-$HOME/miniforge3/envs/smartpark-lpr/bin/python}"
  OCR_PY="${SMARTPARK_OCR_PY:-$HOME/miniforge3/envs/smartpark-ocr/bin/python}"
fi
export YOLO_OFFLINE=true PYTHONDONTWRITEBYTECODE=1
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4 MKL_NUM_THREADS=4
exec "$LPR_PY" -B "$ROOT/scripts/recognize_plate.py" "$1" \
  --ocr-python "$OCR_PY" \
  --detector "$ROOT/model/weights/smartpark_plate_pose_best.pt" \
  --recognizer "$ROOT/model/weights/smartpark_plate_ppocrv5_bal.pdparams" \
  --config "$ROOT/model/weights/smartpark_plate_ppocrv5_config.yml" \
  --dictionary "$ROOT/scripts/rec/ppocrv5_dict.txt" \
  --crop-recipe "$ROOT/model/weights/smartpark_plate_crop_recipe.json" \
  --confidence 0.25 --crop auto
