#!/bin/bash
#SBATCH --job-name=plate-det
#SBATCH --partition=gpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --gres=gpu:v100:2
#SBATCH --time=04:00:00
#SBATCH --output=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/runs/license_plate/slurm-%j.out
#SBATCH --error=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/runs/license_plate/slurm-%j.err

# 车牌检测器训练（YOLO11m）。--task 决定标签形态，需要对应权重：
#   det （默认）ccpd.yaml     + model/yolo11m.pt
#   obb        ccpd_obb.yaml  + model/yolo11m-obb.pt   ← 推理可直接透视矫正（推荐）
#   pose       ccpd_pose.yaml + model/yolo11m-pose.pt  ← 四关键点带语义顺序，旋转图也能摆正
# 例：sbatch scripts/train_lpr.slurm --task obb --name license_plate_obb
set -euo pipefail
ROOT="/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park"
PY="${SMARTPARK_LPR_PY:-/home/inspur/nfs/home/cadetcarrington/miniforge3/envs/smartpark-lpr/bin/python}"

mkdir -p "$ROOT/model/runs/license_plate"
cd "$ROOT"

if [[ ! -x "$PY" ]]; then
  echo "conda env smartpark-lpr not found: $PY" >&2
  exit 2
fi

echo "host=$(hostname) job=${SLURM_JOB_ID:-na} date=$(date)"
echo "CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-unset}"
nvidia-smi || true

# 4h partition cap：这个任务不自动续跑，超时后重投同一命令即可（YOLO 会写 last.pt）。
exec "$PY" -u "$ROOT/scripts/train_lpr.py" --workers 16 "$@"
