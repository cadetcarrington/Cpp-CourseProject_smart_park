#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="/home/inspur/nfs/home/cadetcarrington/miniforge3/envs/smartpark-ocr/bin/python"
if [[ ! -x "$PY" ]]; then
  echo "conda env smartpark-ocr not found: $PY" >&2
  exit 2
fi
exec "$PY" -u "$ROOT/scripts/train_rec.py" "$@"
