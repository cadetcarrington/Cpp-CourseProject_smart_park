#!/bin/bash
#SBATCH --job-name=ccpd-rec
#SBATCH --partition=gpu
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --gres=gpu:v100:1
#SBATCH --time=04:00:00
#SBATCH --output=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_prepare-%j.out
#SBATCH --error=/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park/model/datasets/ccpd_rec_prepare-%j.err

set -euo pipefail
ROOT="/home/inspur/nfs/home/cadetcarrington/Cpp-CourseProject_smart_park"
PY="${SMARTPARK_LPR_PY:-/home/inspur/nfs/home/cadetcarrington/miniforge3/envs/smartpark-lpr/bin/python}"
cd "$ROOT"
exec "$PY" -u "$ROOT/scripts/prepare_recognition.py" --no-download --workers 16 --green-repeat 5 --output "$ROOT/model/datasets/ccpd_rec"
