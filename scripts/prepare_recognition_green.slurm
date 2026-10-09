#!/bin/bash
#SBATCH --job-name=rec-green-data
#SBATCH --partition=gpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --time=00:30:00
#SBATCH --output=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_green_prepare.log
#SBATCH --error=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_green_prepare.err
# 绿牌(新能源)加权微调集：只重建标签列表，不重新裁图，几秒钟完成。
set -euo pipefail
ROOT=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park
PY="${SMARTPARK_LPR_PY:-$HOME/miniforge3/envs/smartpark-lpr/bin/python}"
cd "$ROOT"
exec "$PY" -u "$ROOT/scripts/prepare_recognition_green.py" \
    --base "$ROOT/model/datasets/ccpd_rec_balance" \
    --ccpd2020 "$ROOT/model/datasets/CCPD2020/ccpd_green/train" \
    --exclude-manifest "$ROOT/examples/plates/manifest-provinces.csv" \
    --target-share 0.30 \
    --output "$ROOT/model/datasets/ccpd_rec_green"
