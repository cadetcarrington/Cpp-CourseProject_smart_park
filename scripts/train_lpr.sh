#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec /home/inspur/nfs/home/cadetcarrington/miniforge3/envs/smartpark-lpr/bin/python "$ROOT/scripts/train_lpr.py" "$@"
