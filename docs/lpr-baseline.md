# 2026-10-09 推理基线

此检查点保存性能优化前 s1 使用的单图推理方式。当前服务端仍调用 [recognize_plate.py](../scripts/recognize_plate.py)，每张图启动检测 Python，再启动独立 Paddle Python。此处不包含其他正在进行的布局或地图修改。

实际部署指纹见 [baseline.json](../scripts/lpr/baseline.json)：模型、裁剪配方、OCR 配置、完整字典与上游 PaddleOCR 推理脚本的 SHA-256，以及两个环境的实际版本与线程变量。均衡 OCR 权重此前被忽略，现以 Git LFS 保存；原 pose 权重也由 LFS 保存。第三方 PaddleOCR 仍单独安装，恢复时需核对记录中的推理脚本指纹。

固定推理方式：CPU FP32；YOLO pose 输入尺寸 960、置信度阈值 0.25；使用 `smartpark_plate_ppocrv5_bal.pdparams`；原四角裁剪配方、JPEG 质量和完整字符字典；`crop=auto`、不启用翻转复核。固定入口如下：

```bash
bash scripts/lpr/run-baseline.sh examples/plates/blue-01.jpg
```

可用 `SMARTPARK_LPR_PY`、`SMARTPARK_OCR_PY` 指定机器对应的解释器。s1 默认使用两套 miniforge 环境，macOS 默认使用 `~/.smartpark` 环境；跨系统输出数值可能受框架版本影响，精度／速度对照必须在同一机器、同一环境进行。

后续优化首先尝试复用模型与解释器，保留权重、FP32、输入尺寸、裁剪、字典和后处理。逐图对照完整车牌、有效性、裁剪方式、检测框和两阶段置信度；已有数据集存在历史训练污染，结果只证明该样本集上的一致性，不能作为独立泛化评估。
