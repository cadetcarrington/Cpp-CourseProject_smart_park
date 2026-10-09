# 车牌识别：运行、训练与评测

当前实现为 [recognize_plate.py](../scripts/recognize_plate.py)，串行处理一张图片。macOS 本地模式启动该脚本；远程模式通过 TCP `lpr.recognize` 交给服务端执行。没有配置服务端识别命令时，返回按图片哈希生成的模拟结果。

## 运行环境

检测与 OCR 分别使用 PyTorch 和 Paddle 环境，避免依赖冲突。macOS 的锁定环境由 [setup_uv_env.sh](../scripts/setup_uv_env.sh) 同步，缺省解释器位于 `~/.smartpark/lpr/bin/python` 和 `~/.smartpark/ocr/bin/python`。PaddleOCR 源码另外准备到 `third_party/PaddleOCR`，也可通过 `--paddleocr` 指定。该框架不随 Git 分发。

```bash
scripts/setup_uv_env.sh
YOLO_OFFLINE=true ~/.smartpark/lpr/bin/python -B scripts/recognize_plate.py \
    examples/plates/blue-01.jpg --ocr-python ~/.smartpark/ocr/bin/python
```

默认优先选择四关键点检测权重 `smartpark_plate_pose_best.pt` 和省份均衡识别权重 `smartpark_plate_ppocrv5_bal.pdparams`；文件缺失时分别退回轴对齐检测器和基础识别器。仓库中的基础权重经 Git LFS 分发；均衡版是否可用取决于部署目录中是否已经提供实体权重。首次克隆后确认下载的是模型实体，而非 LFS 指针。

配套配置见 [OCR 配置](../model/weights/smartpark_plate_ppocrv5_config.yml)。必须使用训练时的完整 [ppocrv5_dict.txt](../scripts/rec/ppocrv5_dict.txt)，不能换成车牌小字典。解释器路径只展开为绝对路径，不能解析掉虚拟环境的 Python 符号链接，否则可能绕过虚拟环境配置。

成功返回一行 JSON，包含 `plate`、检测／识别置信度、`bounding_box`、`valid`、`crop`、`quad_source` 和 `flip_checked`；失败打印原因到 stderr 并返回非零状态。脚本默认使用 CPU，每次请求重新加载模型，暂不提供多图批处理。`valid` 仅表示字符格式合法，不等于模型识别正确；管理端仍要求人工核对。

## s1 的已验证配置

2026-10-09 复用训练目录 `~/Cpp-CourseProject_smart_park` 的模型及脚本，检测解释器为 `~/miniforge3/envs/smartpark-lpr/bin/python`，OCR 解释器为 `~/miniforge3/envs/smartpark-ocr/bin/python`。对应版本为 torch 2.6.0、ultralytics 8.4.142、paddle 3.1.1。

```bash
export YOLO_OFFLINE=true
export OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4 MKL_NUM_THREADS=4
LPR_ROOT="$HOME/Cpp-CourseProject_smart_park"
LPR_PY="$HOME/miniforge3/envs/smartpark-lpr/bin/python"
OCR_PY="$HOME/miniforge3/envs/smartpark-ocr/bin/python"
LPR_COMMAND="\"$LPR_PY\" -B \"$LPR_ROOT/scripts/recognize_plate.py\" \"%1\" --ocr-python \"$OCR_PY\""
# 将 --lpr-command "$LPR_COMMAND" 加到现有服务器启动参数中。
```

`s1` 的 YOLO 导入会在外网 DNS 探测等待约 58 秒，导致单图流程约 69.55 秒，超过服务端 60 秒限制。启用离线模式后，命令行同图实测 14.84 秒，TCP 同图实测 13.66 秒，返回“皖AMJ570”、`backend=script`、`valid=true`。这是单张 [blue-01.jpg](../examples/plates/blue-01.jpg) 的延迟与链路验证，不代表全量准确率。

## 裁剪几何

训练与推理共用 [plate_geometry.py](../scripts/plate_geometry.py) 和 [裁剪配方](../model/weights/smartpark_plate_crop_recipe.json)：四角透视矫正、外扩 6%、高度 64、宽度 160–320、JPEG 质量 95。

`--crop auto` 按配方裁剪，没有可靠四角时回退到检测框；`--crop quad` 强制四角，不具备四角时明确失败；`--crop bbox` 强制轴对齐框。`--crop-recipe` 可指定训练数据集对应配方。pose 关键点具有语义顺序；OBB 四角存在上下歧义，必要时用 `--flip-check` 增加一次 180° 翻转 OCR 比较。

## 历史评测与限制

2026-10-05 对 200 张省份样例评测的整牌精确匹配率：

| 方案 | 200 张合计 | tilt 40 张 | rotate 18 张 |
| --- | --- | --- | --- |
| 旧检测框裁剪 + 旧识别器 | 64.5% | 27.5% | 16.7% |
| pose 四关键点 + 基础识别器，quad 裁剪 | 93.5% | 100% | 100% |
| 旧检测器 + 运行时裁剪重训识别器，bbox 裁剪 | 88.5% | 85.0% | 77.8% |

省份均衡微调后，200 张合计为 **96.0%**，晋牌 100 张为 98.0%，非晋 100 张为 94.0%。单省重点版在晋牌同为 98.0%，但非晋降为 81.0%，因此默认优先使用均衡版。

**绿牌评测存在历史污染**：33 张绿牌中有 17 张裁剪图进入过旧训练集；旧版全体绿牌成绩不能视为独立测试成绩。干净的 16 张上，基础版为 15/16，均衡版和绿牌加权版均为 14/16。绿牌加权未带来明确收益，需补充独立新能源样例。以上为已有历史结果，本次目录整理没有重新跑模型全量评测。

```bash
# 复现整图评测；需要完整识别环境。
YOLO_OFFLINE=true ~/.smartpark/lpr/bin/python scripts/evaluate_plates.py \
    --manifest examples/plates/manifest-provinces.csv --workers 4 \
    --detector model/weights/smartpark_plate_pose_best.pt --tag route1-pose
```

样例来源、授权及 manifest 见 [样例说明](../examples/plates/README.md)。

## 训练入口

数据准备、训练和评测脚本仍保留在主分支。s1 使用 Slurm `gpu` 分区，现有 V100 双卡节点；作业默认一小时，最长四小时，脚本需显式设置运行时限。

```bash
# 路线 1：四关键点检测，推理走训练同款四角矫正。
"$SMARTPARK_LPR_PY" scripts/prepare_ccpd.py --task pose --no-download \
    --output "$ROOT/model/datasets/ccpd_yolo_pose"
sbatch scripts/train_lpr.slurm --task pose --name license_plate_pose \
    --imgsz 640 --batch 32 --device 0,1
sbatch scripts/train_lpr.slurm --task pose --name license_plate_pose --resume

# 路线 2：按运行时检测框重新准备 OCR 数据与训练。
sbatch scripts/prepare_recognition_runtime.slurm
sbatch scripts/train_rec.slurm \
    --config scripts/rec/PP-OCRv5_server_rec_plate_runtime.yml \
    --data "$ROOT/model/datasets/ccpd_rec_runtime" \
    --output "$ROOT/model/runs/plate_rec_runtime"

# 省份均衡微调。
sbatch scripts/prepare_recognition_balance.slurm
sbatch scripts/train_rec.slurm \
    --config scripts/rec/PP-OCRv5_server_rec_plate.yml \
    --data "$ROOT/model/datasets/ccpd_rec_balance" \
    --pretrained "$ROOT/model/weights/smartpark_plate_ppocrv5_best.pdparams" \
    --output "$ROOT/model/runs/plate_rec_balance" --epochs 3 --lr 0.0001
```

上述命令中的 `ROOT`、`SMARTPARK_LPR_PY` 应指向训练目录与检测环境。更换权重时同时检查裁剪配方，随后重跑独立评测再更新数字。完整旧版方案和实验记录可在 `archive/legacy-apps-20261009` 分支的 README 中查看。
