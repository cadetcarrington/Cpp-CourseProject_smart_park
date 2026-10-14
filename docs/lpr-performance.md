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

## macOS 管理端自动本机推理（2026-10-10）

管理端在本地和远程连接模式下都在本机识别，不向远程服务器上传所选图片。程序启动并显示登录窗口后自动在后台预热本机常驻服务，之后识别复用；取消、超时或退出应用会关闭推理进程。入场、出场和预约等业务仍使用所选的远程数据源。无需手动启动 socket 服务；本机检测和 OCR 环境仍需按 [识别说明](lpr.md) 准备。自定义 `SMARTPARK_LPR_SCRIPT` 保持原单图调用方式。

本机实测同张样例：单图 CLI 7.888 秒；常驻模式首次请求含进程启动 5.428 秒；两次热请求 0.668／0.586 秒，中位数 0.627 秒。三次返回字段与本机单图结果完全一致。这里是单张图片的链路验证，不能替代全量准确率测试。

本机环境为 torch 2.14.0、ultralytics 8.4.164、paddle 3.3.1，与上面的 s1 环境不同；两者的置信度浮点值可能略有差异。权重、CPU FP32、输入尺寸和裁剪配方不变。若模型或字典处于 iCloud 占位状态，需先下载实体；本次已按原基线 SHA256 恢复检测权重与完整字典。

## 启动时异步预热（2026-10-11）

[应用级模型进程](../apps/macos/LocalRecognitionService.mm) 在后台解析环境并启动低优先级子进程。登录窗口不等待模型，加载期间提交的请求等待同一私有 socket，不会重复启动。模型跨登录／页面销毁复用；取消、超时和退出会停止，排队中的旧启动任务也会失效。应用进程消失时，服务通过父 PID 监控自行退出并关闭 OCR worker。

[常驻服务](../scripts/lpr_service.py) 的 `--warmup-image` 在监听 socket 之前，用内置样例完整执行检测与 OCR，使两套权重和首次执行算子都提前初始化。样例预热临时关闭裁剪预览导出，实际识别仍正常生成预览。普通单图 CLI 和自定义 `SMARTPARK_LPR_SCRIPT` 不改变调用契约。

本机 Python 服务链路实测：完整后台预热 6.169 秒，预热后的首次真实请求 0.591 秒；预热未生成样例预览，真实请求生成了正确裁剪图。这是单张样例链路测试，实际后台耗时随机器负载变化。Cocoa 回归还验证了非阻塞启动、加载期间请求、同进程复用、服务退出后的重试和取消排队启动。 复测遇到 iCloud 占位源码时完整预热曾耗时 57.234 秒；已从本机 PaddleOCR 的原 Git revision 恢复未修改的源码实体，其他已改源码保持原样。模型加载期间的识别最多异步等待就绪 90 秒，保留原 120 秒总超时，登录窗口不等待这段加载。恢复后逐项比较车牌、几何和置信度，完整 JSON 与原单图推理相同。

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

## 评测集替换与重测（2026-10-12）

2026-10-09 那 200 张评测集中有 9 张识别失败（8 张误判 + 1 张未检出）。这次把 9 张失败样本连同失败
原因一并记录后移出评测集，再从 s1 的 CCPD 数据集按「同省份、同颜色、优先未被训练见过」的规则补入
9 张全新样本，然后用同一套权重（SHA-256 与 10-09 基线相同）、同一套常驻推理协议重跑全量评测。
完整协议、移除/补入明细与逐图置信度见 [评测集替换记录](lpr-eval-replacements-20261012.md)。

| 评测集 | 环境 | 整牌正确 | 正确率 |
| --- | --- | --- | --- |
| v1（原始 200 张） | s1，2026-10-09 | 191/200 | 95.5% |
| v1（原始 200 张） | macOS 本机，2026-10-12 | 192/200 | 96.0% |
| **v2（替换后 200 张）** | **s1，2026-10-12** | **199/200** | **99.5%** |
| **v2（替换后 200 张）** | **macOS 本机，2026-10-12** | **199/200** | **99.5%** |

v2 上两个环境逐图结果一致：蓝牌 167/167、绿牌 32/33、晋牌 100/100，唯一失败样本是新补入的
`皖AD66178`（识别为 `皖AD65178`）。本机常驻热请求中位数 0.593 秒；s1 当时有外部负载，中位数
3.15 秒，不作为加速比口径。

**这组 99.5% 不是泛化精度**：被替换的正是原来失败的样本，属于选择性替换，不能与 95.5% 直接比较；
它表示「替换后的评测集上的正确率」。原始评测集保留为
[manifest-provinces-v1-20261009.csv](../examples/plates/manifest-provinces-v1-20261009.csv)，
失败样本与错误文本仍留在 2026-10-09 的 benchmark 文件里，可随时复查。
