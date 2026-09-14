#!/usr/bin/env bash
# Install PP-OCRv5 training deps into conda env smartpark-ocr.
# Do not install into smartpark-lpr (that env is PyTorch/YOLO).
set -euo pipefail
PY="${SMARTPARK_OCR_PY:-/home/inspur/nfs/home/cadetcarrington/miniforge3/envs/smartpark-ocr/bin/python}"
if [[ ! -x "$PY" ]]; then
  echo "conda env smartpark-ocr not found: $PY" >&2
  exit 2
fi

echo "python: $PY"
"$PY" -m pip install -U pip
if ! "$PY" -c "import paddle,sys; sys.exit(0 if hasattr(paddle,'__version__') and paddle.__version__ not in ('0.0.0','') else 1)"; then
  echo "installing paddlepaddle-gpu==3.1.1 (cu126)"
  "$PY" -m pip install "paddlepaddle-gpu==3.1.1" -i https://www.paddlepaddle.org.cn/packages/stable/cu126/
fi

# Headless OpenCV: the cluster login/compute images often lack libGL.
"$PY" -m pip install \
  shapely scikit-image pyclipper lmdb tqdm numpy rapidfuzz cython \
  Pillow pyyaml requests albumentations albucore packaging \
  opencv-python-headless
"$PY" -m pip uninstall -y opencv-python opencv-contrib-python || true

"$PY" - <<'PY'
import importlib
import paddle
mods = [
    "paddle", "cv2", "shapely", "skimage", "pyclipper", "lmdb",
    "tqdm", "numpy", "rapidfuzz", "PIL", "yaml", "albumentations",
]
print("paddle", paddle.__version__, "cuda", paddle.is_compiled_with_cuda())
for name in mods:
    importlib.import_module(name)
    print("ok", name)
PY
