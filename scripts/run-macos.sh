#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/macos}"
APP="$BUILD_DIR/apps/macos/smartpark_admin_macos.app/Contents/MacOS/smartpark_admin_macos"

if [[ ! -x "$APP" ]]; then
    echo "macOS administrator application not built: $APP" >&2
    echo "Run scripts/build-macos.sh first." >&2
    exit 1
fi

# 本地识别的两套环境；远程模式由服务端负责推理。
if [[ -z "${SMARTPARK_LPR_PY:-}" && -x "$HOME/.smartpark/lpr/bin/python" ]]; then
    export SMARTPARK_LPR_PY="$HOME/.smartpark/lpr/bin/python"
fi
if [[ -z "${SMARTPARK_OCR_PY:-}" && -x "$HOME/.smartpark/ocr/bin/python" ]]; then
    export SMARTPARK_OCR_PY="$HOME/.smartpark/ocr/bin/python"
fi
# 使用本地权重时无需联网探测，避免离线网络上的 DNS 长时间等待。
export YOLO_OFFLINE="${YOLO_OFFLINE:-true}"
exec "$APP" "$@"
