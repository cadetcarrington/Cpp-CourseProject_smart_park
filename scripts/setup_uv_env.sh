#!/usr/bin/env bash
# 用 uv 把 scripts/{lpr,ocr} 的锁定依赖同步到 ~/.smartpark/{lpr,ocr}——
# scripts/run-macos.sh 与识别对话框的默认解释器路径。可重复执行，
# 环境已与 uv.lock 一致时是无操作。uv 的安装方式见 docs.astral.sh/uv/。
set -euo pipefail
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UV="${UV:-uv}"

if ! command -v "$UV" > /dev/null 2>&1; then
    echo "uv not found: $UV (see docs.astral.sh/uv/ for installation)" >&2
    exit 2
fi

for name in lpr ocr; do
    export UV_PROJECT_ENVIRONMENT="$HOME/.smartpark/$name"
    echo "== uv sync: $name -> $UV_PROJECT_ENVIRONMENT"
    "$UV" sync --frozen --project "$SCRIPTS/$name"
done

"$HOME/.smartpark/lpr/bin/python" -c "import ultralytics, cv2, torch; print('lpr env ok: torch', torch.__version__)"
"$HOME/.smartpark/ocr/bin/python" -c "import paddle, cv2, yaml, PIL; print('ocr env ok: paddle', paddle.__version__)"
echo "done. scripts/run-macos.sh 会自动导出 SMARTPARK_LPR_PY / SMARTPARK_OCR_PY。"
