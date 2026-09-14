#!/bin/bash
#SBATCH --job-name=plate-rec
#SBATCH --partition=gpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --gres=gpu:v100:2
#SBATCH --time=04:00:00
#SBATCH --output=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/runs/plate_rec/slurm-%j.out
#SBATCH --error=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/runs/plate_rec/slurm-%j.err

set -euo pipefail
ROOT="/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park"
PY="${SMARTPARK_OCR_PY:-/home/inspur/nfs/home/cadetcarrington/miniforge3/envs/smartpark-ocr/bin/python}"

mkdir -p "$ROOT/model/runs/plate_rec"
cd "$ROOT"

if [[ ! -x "$PY" ]]; then
  echo "conda env smartpark-ocr not found: $PY" >&2
  exit 2
fi

echo "host=$(hostname) job=${SLURM_JOB_ID:-na} date=$(date)"
echo "CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-unset}"
nvidia-smi || true
"$PY" - <<'PY'
import paddle
print("paddle", paddle.__version__, "cuda", paddle.is_compiled_with_cuda(), "gpus", paddle.device.cuda.device_count())
PY

# 4h partition cap: this job resumes from model/runs/plate_rec/latest if present.
# Resubmit the same script after timeout until 20 epochs finish.
exec "$PY" -u "$ROOT/scripts/train_rec.py" --gpus 0,1 --resume --workers 4 "$@"
