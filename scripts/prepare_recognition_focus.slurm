#!/bin/bash
#SBATCH --job-name=rec-jin-data
#SBATCH --partition=gpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --time=01:00:00
#SBATCH --output=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_focus_prepare.log
#SBATCH --error=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_focus_prepare.err

# 造"某省重点"的识别微调集（默认晋牌）：纯 CPU/IO，不申请 GPU。
# 会排除 examples/plates/manifest-provinces.csv 里出现过的图片，保证评测集不进训练。
set -euo pipefail
ROOT=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park
PY="${SMARTPARK_LPR_PY:-$HOME/miniforge3/envs/smartpark-lpr/bin/python}"
cd "$ROOT"
echo "host=$(hostname) job=${SLURM_JOB_ID:-na} date=$(date)"
exec "$PY" -u "$ROOT/scripts/prepare_recognition_focus.py" \
    --province 晋 \
    --exclude-manifest "$ROOT/examples/plates/manifest-provinces.csv" \
    --base "$ROOT/model/datasets/ccpd_rec" \
    --crop-mode quad \
    --target-share 0.10 \
    --workers 8 \
    --output "$ROOT/model/datasets/ccpd_rec_jin"
