#!/bin/bash
#SBATCH --job-name=rec-runtime-data
#SBATCH --partition=gpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --time=04:00:00
#SBATCH --output=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_runtime_prepare.log
#SBATCH --error=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_runtime_prepare.err

# 路线 2 的数据集：按"运行时裁剪"（检测框原始裁剪）重造识别训练集。
# 纯 CPU/IO 作业，不申请 GPU。产出里的 crop_recipe.json 就是要随识别权重一起发布的配方。
set -euo pipefail
ROOT=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park
PY="${SMARTPARK_LPR_PY:-$HOME/miniforge3/envs/smartpark-lpr/bin/python}"
cd "$ROOT"
echo "host=$(hostname) job=${SLURM_JOB_ID:-na} date=$(date)"
exec "$PY" -u "$ROOT/scripts/prepare_recognition.py" \
    --no-download --workers 16 --green-repeat 5 --crop-mode bbox \
    --output "$ROOT/model/datasets/ccpd_rec_runtime"
