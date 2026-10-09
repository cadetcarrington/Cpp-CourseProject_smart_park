# 保持精度的常驻推理优化（2026-10-09）

优化前检查点是 `763e75c`，恢复分支为 `archive/inference-baseline-20261009`；固定权重、脚本和环境见 [基线说明](lpr-baseline.md) 与 [指纹记录](../scripts/lpr/baseline.json)。均衡 OCR 权重已用 Git LFS 保存。

## 实测结果

在 s1 的同一套 CPU 环境、同一模型和原裁剪配方下验证：

| 测量 | 结果 |
| --- | --- |
| 原单图 CLI，同张图片 3 次，中位数 | 14.284 秒 |
| 常驻模式，经真实 TCP 服务端，5 次热请求中位数 | 1.427 秒 |
| 热请求加速比 | 10.01 倍 |
| 200 张样例的常驻热请求中位数／P95 | 1.308／1.342 秒 |
| 常驻进程启动／首张请求，200 张评测 | 6.063／6.273 秒 |
| TCP 桥接的首张请求，不含进程启动 | 7.486 秒 |
| 优化前／后整牌正确数 | 191／200，均为 95.5% |
| 完整输出一致性 | 199 张成功返回的全部字段与数值完全相同；1 张同样未检出，完整错误文本相同 |

200 张结果包括原本识别错误的图片，没有排除失败样例。检测框、裁剪模式、车牌格式判定、检测／识别置信度均逐图核对；实际浮点差异为零。比较工具允许的置信度误差上限为 `1e-6`，遇到车牌／几何变化、新错误或不同错误原因会失败退出。

[逐图 CSV](lpr-benchmark-20261009.csv) 和 [测量 JSON](lpr-benchmark-20261009.json) 保存原始对照与延迟记录。200 张准确率是当前环境的新实测值，不能用旧文档的 96.0% 替代；这组历史样例仍存在已记录的训练污染，仅用来验证优化没有改变原输出，不是独立泛化测试。

冷启动仍要导入框架、加载模型和首次执行算子；热请求的加速来自摊销初始化成本。进程退出后首次请求会重新产生这些开销。

## 改了什么

[常驻服务](../scripts/lpr_service.py) 持有一个 YOLO 对象和一个独立 OCR Python 子进程，串行处理请求，避免并发修改预测器状态。检测仍为 CPU FP32、`imgsz=960`、原阈值；四角裁剪、JPEG 质量、字典和后处理不变。

[OCR worker](../scripts/lpr_ocr_worker.py) 继续执行第三方原 `tools/infer_rec.py::main()`，只缓存 `build_post_process`、`build_model`、`load_model`、`create_operators` 四个初始化步骤；每张图仍重新解码、预处理、前向、CTC 解码和输出结果，绝不缓存上一张车牌结果。临时文件的路径拼写保持不变，兼容 macOS 的 `/var` 符号链接。

[轻量客户端](../scripts/lpr_client.py) 只依赖 Python 标准库，适配现有 `--lpr-command` 协议。默认单图 CLI 仍可用；没有常驻服务时客户端明确报错，不返回模拟结果。模型与 OCR 配置在常驻进程中固定，变更权重、字典或配置后必须重新启动服务。

## 启用方式

先同步当前代码和模型实体到目标机器；常驻模式是显式选项，本次测试使用隔离目录和临时数据库，没有更改 s1 在线启动脚本。

```bash
LPR_ROOT="$HOME/Cpp-CourseProject_smart_park"
LPR_PY="$HOME/miniforge3/envs/smartpark-lpr/bin/python"
OCR_PY="$HOME/miniforge3/envs/smartpark-ocr/bin/python"
mkdir -p "$HOME/.smartpark"
SOCKET="$HOME/.smartpark/lpr.sock"
export YOLO_OFFLINE=true OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4 MKL_NUM_THREADS=4

# 在受管理的长期进程中启动；SIGTERM 会关闭 OCR 子进程并清理自己创建的 socket。
"$LPR_PY" -B "$LPR_ROOT/scripts/lpr_service.py" \
  --socket "$SOCKET" --ocr-python "$OCR_PY" \
  --detector "$LPR_ROOT/model/weights/smartpark_plate_pose_best.pt" \
  --recognizer "$LPR_ROOT/model/weights/smartpark_plate_ppocrv5_bal.pdparams"
```

看到 `{"ready": true, ...}` 表示服务已监听。首张实际图片会初始化预测器和 OCR 模型；若希望首个业务请求也快，可先识别一张样例进行预热，不需要写停车业务数据。在另一个终端确认客户端：

```bash
python3 "$LPR_ROOT/scripts/lpr_client.py" \
  "$LPR_ROOT/examples/plates/blue-01.jpg" --socket "$SOCKET"

# 将这个模板作为现有 smartpark_server 的 --lpr-command 值。
LPR_COMMAND="python3 \"$LPR_ROOT/scripts/lpr_client.py\" \"%1\" --socket \"$SOCKET\""
```

macOS 启动常驻进程时将两套解释器改为 `~/.smartpark/lpr/bin/python`、`~/.smartpark/ocr/bin/python`，其余协议相同。服务不会覆盖已有 socket／普通文件；异常终止留下的路径应先确认所属进程已退出，再清理。

## 复现与回退

[benchmark_lpr.py](../scripts/benchmark_lpr.py) 接受已标注清单、原模型参数和同清单的单图基线 JSONL：

```bash
"$LPR_PY" -B scripts/benchmark_lpr.py \
  --manifest examples/plates/manifest-provinces.csv \
  --baseline /path/to/baseline.jsonl --out /path/to/resident.jsonl \
  --ocr-python "$OCR_PY"
```

需先对同一批实体图片、固定 pose／均衡权重运行原单图评测；不要使用缺图清单或另一版权重的历史结果作为基线。单图基线可在检查点的独立工作目录运行：

```bash
git worktree add ../smartpark-lpr-baseline archive/inference-baseline-20261009
```

回退只需恢复原 `recognize_plate.py` 单图命令模板并停止常驻进程。数据库、布局和 TCP／REST 业务协议不需要迁移。新增回归测试见 [test_lpr_resident.py](../tests/test_lpr_resident.py)，已纳入 CTest。
