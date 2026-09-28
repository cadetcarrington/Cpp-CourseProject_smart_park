#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/qt}"

# 车牌识别（选图审阅）默认环境：uv 建立在 ~/.smartpark/{lpr,ocr}。
# 已显式导出 SMARTPARK_LPR_PY / SMARTPARK_OCR_PY 时以用户为准。
if [[ -z "${SMARTPARK_LPR_PY:-}" && -x "$HOME/.smartpark/lpr/bin/python" ]]; then
    export SMARTPARK_LPR_PY="$HOME/.smartpark/lpr/bin/python"
fi
if [[ -z "${SMARTPARK_OCR_PY:-}" && -x "$HOME/.smartpark/ocr/bin/python" ]]; then
    export SMARTPARK_OCR_PY="$HOME/.smartpark/ocr/bin/python"
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
    APP="$BUILD_DIR/apps/admin/smartpark_admin.app/Contents/MacOS/smartpark_admin"
else
    APP="$BUILD_DIR/apps/admin/smartpark_admin"
fi

if [[ ! -x "$APP" ]]; then
    echo "Admin GUI not built: $APP" >&2
    echo "Run scripts/build-admin.sh first." >&2
    exit 1
fi

exec "$APP" "$@"
