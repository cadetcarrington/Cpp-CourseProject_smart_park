# SmartPark

SmartPark 是一个基于 C++ 和 Qt 的智能停车场管理系统课程项目。项目目标是逐步构建一个包含停车业务、可视化管理、数据持久化、客户端与服务端通信，以及车牌识别能力的完整软件工程系统。

## 系统规划

最终系统由一个统一的 SmartPark Server 和多个终端组成：

```text
                   SmartPark Server
                         |
            +------------+------------+
            |            |            |
            v            v            v
      Admin Client   Gate Terminal  User Client
       管理员端        出入口端        用户端
```

服务器将负责停车业务、车位状态、收费、车辆记录、用户与预约管理、数据库访问及网络通信。管理员端用于管理与可视化，出入口端用于车辆入场和离场处理。

## 当前范围：SmartPark 0.6

当前已完成：

- C++17、CMake 与 Qt 6 Widgets 的基础工程配置。
- `Vehicle`、`ParkingSpot`、`ParkingLayout`、`ParkingRecord` 核心模型。
- 独立的 `SpotAllocator`：`ParkingService` 只负责状态机，选位、评分和路径规划从业务层拆出。
- 默认 60 车位、3 个矩形分区的自动分配服务。
- 用户自定义多矩形停车场布局，支持不同车位长宽、通道位置、车位类型和多个出入口。
- 基于 0.5 米栅格的 A* / 一次搜索到多目标 Dijkstra，自动避开车位障碍并生成入口/出口路线。
- 拥堵写入 A* 边权，同时保留附近占用数作为车位评分项。
- `WeightedCost` 默认策略和 `Nearest` 对照策略。
- 预留状态机：`Available -> Reserved(ttl) -> Occupied -> Available`。
- CLI 自动演示与 Qt GUI 实时车位图、路线绘制、布局编辑和策略切换。
- `ParkingService::enter()` / `leave()` / `reserve()` 入离场与预留流程。
- `DatabaseManager` 与 `ParkingRepository`：SQLite 建表、入场/离场/预约持久化和重启恢复。
- `Persistence` 辅助类：统一管理数据库连接，CLI 与 Qt Admin GUI 默认接入 SQLite，跨重启恢复车位状态与停车记录。
- `BillingRule` 与 `BillingService`：免费时长、计费单元、首单元费用、后续单元费用和单次封顶；`ParkingService` 在离场/释放车位时自动计算费用并写入停车记录。
- CLI 支持 `--db <路径>` 指定数据库、`--reset` 清空数据库后演示。
- Qt Admin GUI 支持 `--db <路径>`，应用布局时若与数据库签名不一致会提示并可选重置数据库。

CLI 启动时打印默认计费规则，离场时打印本次费用；Qt Admin GUI 显示计费规则，并在释放最近车位后显示本次费用。

下一步将实现 TCP 服务端与出入口终端，并把 `BillingRule` 扩展为 GUI 可配置、可持久化。

尚未接入 TCP 通信、真实 LPR、多线程、用户端或统计图表。调研来源与明确不做的方案见 `docs/research-sources.md`。

## 计费规则

当前内置默认规则：

| 项目 | 默认值 |
| --- | --- |
| 免费时长 | 30 分钟 |
| 计费单元 | 30 分钟 |
| 首单元费用 | 5 元 |
| 后续每单元费用 | 5 元 |
| 单次封顶 | 100 元 |

计费采用“免费时长后向上取整到计费单元”的方式。`ParkingService::leave()` 与 `ParkingService::release()` 会先调用 `BillingService::calculateFee()`，再通过 `ParkingRepository::saveExit()` 在同一事务中写入费用、关闭停车记录并释放车位。

## 当前进度

截至 2026-09-07，项目处于 **SmartPark 0.6**：

- 已完成核心模型、60 车位自动分配、自定义多矩形布局、栅格 A* / Dijkstra 路线、拥堵边权、预留 TTL、CLI 与 Qt GUI。
- SQLite 持久化已接入 CLI 与 Admin GUI，支持跨重启恢复车位状态、预约和停车记录。
- 收费服务已接入 `ParkingService`、CLI 与 Admin GUI；离场费用随停车记录持久化。
- 在 `s1` 上验证：CLI 与 Qt 构建通过，`cli-tests` / `qt-tests` 均为 2/2 通过，GUI offscreen 启动正常。
- CLI 默认持久化可重复运行，连续运行至 60/60 满场后仍稳定输出 `RESULT: PASS`。

尚未完成：TCP Server、Gate Terminal、真实 LPR、用户端与统计图表。

## 后续发展路线

按调研结论推进，优先级从高到低：

1. **P0 — TCP 协议与服务端**：先写 `docs/tcp-protocol.md`，再用 `QTcpServer` 实现登录、心跳和入场/离场事件。
2. **P1 — Gate Terminal + Fake LPR + 离线队列**：先以假识别打通出入口，再补齐断线缓存和补报。
3. **P2 — 双路线真实 LPR**：先接入 HyperLPR3 作为可运行基线，再训练 `YOLO11m + PP-OCRv5` 中国车牌专用模型；使用 CCPD 与 SmartPark 场景数据做统一评测。
4. **P3 — BillingRule 配置化、支付、图表、用户端**：在前述链路稳定后再扩展外围能力。

明确不做：EasyPR、纯云端计费、微信小程序、Go 微服务，以及把 LPR 放进 `ParkingService`。

## 技术栈

- C++17
- Qt 6 Widgets
- CMake
- SQLite 与 Qt SQL
- QTcpServer 与 QTcpSocket（后续）
- OpenCV 4
- HyperLPR3（LPR 基线）
- YOLO11m + PP-OCRv5（中国车牌专用训练路线）
- ONNX / ONNX Runtime（自训练模型的 C++ 部署目标）
- QThread、std::thread 与 STL
- Qt Charts（后期）

## 目录结构

```text
.
├── apps/
│   ├── admin/       # 管理员端 Qt GUI：布局编辑、车位图、路线显示
│   ├── cli/         # 终端演示程序：自动分配与自定义布局验证
│   ├── server/      # SmartPark 服务端入口（预留）
│   └── gate/        # 出入口终端入口（预留）
├── docs/            # 调研来源与路线说明
├── src/
│   ├── core/
│   │   ├── model/   # Geometry、Vehicle、ParkingSpot、ParkingLayout、ParkingRecord
│   │   ├── persistence/ # DatabaseManager、ParkingRepository、Persistence
│   │   └── service/ # GridPlanner、SpotAllocator、ParkingService
│   ├── database/    # 数据库连接与仓储层
│   ├── network/     # TCP 协议与通信实现
│   └── lpr/         # 车牌识别统一接口及两种后端实现
├── models/          # 后续：ONNX / HyperLPR 模型资源；大权重不直接提交 Git
├── training/        # 后续：检测、识别、数据转换与评测脚本
├── resources/
│   ├── icons/       # 图标资源
│   ├── styles/      # Qt 样式表
│   └── images/      # 图片资源
├── sql/             # 数据库建表与初始化脚本
└── tests/           # 单元测试与集成测试
```

## 开发里程碑

1. ✅ 工程跑起来：完成 CMake、Qt 6、C++17 和主窗口。
2. ✅ 纯 C++ 停车核心：实现停车位、车辆、停车记录和入场/离场流程。
3. ✅ 停车场 GUI：实时展示车位状态、自动分配结果和行驶路线。
4. ✅ 分配器重构：独立选位算法、拥堵边权、预留 TTL、车位类型和多出入口。
5. ✅ SQLite 持久化：核心层已完成，CLI/GUI 已接入并支持重启恢复。
6. ✅ 收费系统：根据停车时长、免费时长、计费单元和单次封顶计算并持久化费用，CLI/GUI 已展示。
7. ⬜ 服务端：以 TCP 建立管理员端与服务端架构。
8. ⬜ 出入口终端：手动输入车牌并通过服务端处理业务。
9. ⬜ 车牌识别：实现 HyperLPR3 基线，并完成 YOLO11m + PP-OCRv5 中国车牌专用模型训练、评测与 C++ 部署。

## 最近工作记录

最近一轮完成“BillingService 接入 CLI/GUI”：

- 新增 `BillingRule` / `BillingService`，离场费用在 `ParkingService` 中统一计算并写入 `ParkingRecord`。
- `ParkingRepository::saveExit()` 保留费用参数，费用与停车记录在同一 SQLite 事务中持久化。
- CLI 打印默认计费规则与离场费用；Admin GUI 增加计费规则说明，并在释放车位时显示本次费用。
- 对应提交：`9416bf9 feat: integrate parking billing and layout improvements`。

## 文献调研

调研来源、评分和取舍记录在 `docs/research-sources.md`。当前重点参考：

- [DB4403/T 313 智慧停车业务数据与接口规范](https://amr.sz.gov.cn/attachment/1/1566/1566543/9772236.pdf)：TCP 登录、心跳和事件清单。
- [PARCS 五层架构与费率引擎](https://parkingpaymentguide.com/payment-systems/how-parking-payment-systems-work/)：费率引擎、离线队列和宽限期。
- [MDPI SPMS 参考架构](https://www.mdpi.com/2079-8954/13/2/70)：Server / Gate / Admin 分层。
- [佛山禅城 2026 停车收费新规](https://www.163.com/dy/article/L5OVP5IU05129QAF.html)：15 分钟计费、30 分钟免费和日封顶。
- [HyperLPR3](https://github.com/szad670401/HyperLPR)：中文车牌识别基线，只放在 Gate，不进入 core。
- [CCPD](https://github.com/detectRecog/CCPD)：中国城市停车场车牌数据集，作为检测、识别与统一评测的核心数据来源。
- [北京 DB11/T 3001 ETC 停车场接口](https://jtw.beijing.gov.cn/xxgk/flfg/jthy/201912/P020191231388015746585.pdf)：Gate 作为 TCP 客户端、请求应答与重传。
- [深圳公共智慧停车平台数据接入规范](https://jtys.sz.gov.cn/attachment/1/1596/1596572/12076334.pdf)：心跳、NTP 和断线补报。

以上来源用于提炼接口、计费规则和架构边界，不直接复制外部项目代码。

## 车牌识别（LPR）方案

SmartPark 的 LPR 只部署在 `Gate Terminal`，不进入 `ParkingService`。Gate 从摄像头或测试图片获取画面，完成车牌识别后，仅把标准化后的车牌号、置信度、时间戳等结构化结果发送给 SmartPark Server。

为了兼顾课程项目的可实现性和模型训练研究价值，项目保留两条路线：

- **路线 A：HyperLPR3** —— 快速建立稳定、可运行的中文车牌识别基线。
- **路线 B：YOLO11m + PP-OCRv5** —— 项目的主要训练路线，针对中国停车场车牌进行检测和字符识别微调。

### 路线 A：HyperLPR3 基线

```text
Camera / Image
      |
      v
    OpenCV
      |
      v
  HyperLPR3
      |
      v
 Plate Result
      |
      v
Gate Terminal -> SmartPark Server
```

该路线主要承担以下作用：

- 尽快打通真实摄像头到 Gate、TCP Server、停车业务的完整链路。
- 提供中国车牌识别的 baseline，避免自训练模型尚未完成时阻塞系统开发。
- 与自训练路线在完全相同的测试集上比较整牌准确率、困难场景准确率和推理延迟。
- 保持 LPR 与停车核心解耦，后续可以无侵入切换识别引擎。

HyperLPR3 对常见中国蓝牌、黄牌、新能源车牌等已有专门支持，并提供 C/C++ 推理接口，因此适合用作 Gate 端基线。

### 路线 B：YOLO11m + PP-OCRv5 中国车牌专用模型

最终主路线采用“检测 + 序列识别”的两阶段结构：

```text
Camera / Image
      |
      v
    OpenCV
      |
      v
YOLO11m Plate Detector
  CCPD Fine-tuning
      |
      v
 Bounding Box
      |
      v
Crop / Perspective Rectification
      |
      v
PP-OCRv5 Server Recognizer
Chinese Plate Fine-tuning
      |
      v
 PlateValidator
      |
      v
   晋A12345
      |
      v
Gate Terminal -> SmartPark Server
```

#### 车牌检测模型

初始权重：

```text
yolo11m.pt
```

选择 `YOLO11m` 的原因：

- 属于中量级检测模型，复杂度明显高于 `n/s`，又没有 `l/x` 的部署成本。
- 车牌通常只占 1080p 入口画面中的较小区域，中量模型在困难、小目标场景中有更大的精度空间。
- 训练生态成熟，便于从 PyTorch 导出 ONNX，并在 C++ Gate 中部署。
- 检测阶段只设置一个类别：`license_plate`，不让 YOLO 承担中文字符分类。

建议首轮训练配置以 `imgsz=960` 为起点，并额外对 `640 / 960 / 1280` 做分辨率对比实验。

最终检测权重命名建议：

```text
smartpark_plate_yolo11m_best.pt
smartpark_plate_yolo11m.onnx
```

#### 字符识别模型

初始模型采用：

```text
PP-OCRv5_server_rec
```

检测得到车牌区域后，先完成裁剪和必要的透视矫正，再由 OCR 直接输出完整字符序列。项目不采用“第二个 YOLO 逐字符检测”的方案，避免字符漏检、字符排序、粘连字符和新能源 8 位车牌带来的额外复杂度。

OCR 将使用中国车牌专用字典缩小识别空间，核心字符包括：

```text
省级简称：京 沪 津 渝 冀 豫 云 辽 黑 湘 皖 鲁 新 苏 浙 赣 鄂 桂 甘 晋 蒙 陕 吉 闽 贵 粤 青 藏 川 宁 琼
数字：0-9
字母：A-Z
按需求扩展：警 学 港 澳 使 领 挂 等特殊字符
```

最终识别权重命名建议：

```text
smartpark_plate_rec.onnx
plate_dict.txt
```

### 两条路线对比

| 项目 | 路线 A：HyperLPR3 | 路线 B：YOLO11m + PP-OCRv5 |
| --- | --- | --- |
| 项目定位 | 可运行 baseline / 保底方案 | 最终主路线 / 训练研究方案 |
| 中国车牌针对性 | 已针对中国车牌设计 | 使用中国车牌数据进一步专门微调 |
| 是否需要自行训练 | 否 | 是 |
| 检测 | 框架内部完成 | YOLO11m 独立检测 |
| 字符识别 | 框架内部完成 | PP-OCRv5 序列识别 |
| 模型可控性 | 中 | 高，可控制训练集、增强、字典与阈值 |
| 训练难度 | 低 | 中高 |
| C++ 集成难度 | 低到中 | 中，计划统一导出 ONNX |
| 可解释/可做实验内容 | 中 | 高，可进行检测、OCR、分辨率、数据集消融实验 |
| 对 SmartPark 的价值 | 快速打通 Gate 实机链路 | 形成项目自身的中国车牌识别能力 |

最终不把两条路线设计成互斥方案，而是通过统一接口并存：

```text
ILicensePlateRecognizer
          |
     +----+----+
     |         |
     v         v
HyperLPR3   SmartParkLPR
 Baseline    YOLO11m
                +
             PP-OCRv5
```

这样可以在相同 Gate 输入与相同测试集上直接切换识别引擎并完成公平对比。

### 数据集方案

训练和评测以中国停车场场景为核心，不使用只包含欧美车牌的数据作为主训练集。

#### 1. CCPD2019

核心数据集使用 [CCPD — Chinese City Parking Dataset](https://github.com/detectRecog/CCPD)。该数据集来自中国城市停车场场景，并包含大量带车牌位置和车牌字符标注的图片，非常适合本项目。

计划使用的主要子集包括：

| 子集 | 用途 |
| --- | --- |
| `CCPD-Base` | 主训练集和基础评测 |
| `CCPD-DB` | 不同亮度、曝光场景 |
| `CCPD-Blur` | 模糊、运动模糊场景 |
| `CCPD-Rotate` | 车牌旋转场景 |
| `CCPD-Tilt` | 倾斜和透视形变场景 |
| `CCPD-FN` | 困难检测场景 |
| `CCPD-Challenge` | 综合困难场景评测 |

CCPD 文件名中的标注信息可以转换为：

- YOLO 所需的车牌检测框标签；
- OCR 所需的车牌裁剪图与完整字符序列标签。

因此同一份数据能够分别支持 Detector 和 Recognizer 的训练。

#### 2. CCPD2020 Green

新能源车牌使用 `CCPD2020 / CCPD-Green` 补充训练和独立评测，重点解决中国新能源 8 位绿色车牌。

目标至少覆盖：

```text
普通蓝牌：晋A12345
新能源牌：晋AD12345
```

新能源数据不能只混入总测试集，还应保留单独的 Green 指标，以避免整体准确率掩盖新能源车牌效果。

#### 3. 黄牌与特殊车牌扩展数据

如果后期基础模型已经稳定，可进一步加入包含黄牌以及更多车牌类型的公开数据，或选择合适的 `CCPD-Plus` 类扩展数据作为补充。

这部分作为增强项，不阻塞第一版 LPR：

```text
第一阶段：CCPD2019 + CCPD Green
第二阶段：黄牌 / 特殊牌照扩展
```

#### 4. SmartPark 自建停车场场景集

公共数据负责获得通用中国车牌能力，但最终微调应加入少量与实际 Gate 摄像头分布一致的数据。

建议后期采集或合规制作约 `2,000 ~ 5,000` 张 SmartPark 场景图片，覆盖：

- 入口与出口不同摄像机视角；
- 白天、夜间、逆光和车灯干扰；
- 雨天、反光和轻度污损；
- 不同车辆距离和不同车牌占画面比例；
- 水平、倾斜和透视角度；
- 蓝牌与新能源牌。

自建数据不作为公开车牌隐私数据直接提交 GitHub；仓库只保存数据格式说明、脱敏示例和训练脚本。

### 数据处理与目录规划

训练代码和 C++ 应用代码分离。Python 可以用于模型训练和数据转换，但最终 Gate 不依赖 Python 运行环境。

计划目录：

```text
training/
├── detector/
│   ├── prepare_ccpd.py
│   ├── ccpd.yaml
│   └── train.py
├── recognizer/
│   ├── prepare_recognition.py
│   ├── plate_dict.txt
│   └── configs/
└── evaluation/
    ├── evaluate_detector.py
    └── evaluate_lpr.py

models/
├── detector/
│   ├── smartpark_plate_yolo11m.onnx
│   └── metadata.json
└── recognizer/
    ├── smartpark_plate_rec.onnx
    └── plate_dict.txt
```

其中 Detector 数据转换为典型 YOLO 结构：

```text
datasets/ccpd_yolo/
├── images/
│   ├── train/
│   ├── val/
│   └── test/
├── labels/
│   ├── train/
│   ├── val/
│   └── test/
└── ccpd.yaml
```

Recognizer 将车牌区域裁剪出来并生成“图片路径 + 完整车牌字符串”的标签：

```text
train/plate_000001.jpg    晋A12345
train/plate_000002.jpg    京B88888
train/plate_000003.jpg    粤AD12345
```

### C++ 部署方案

自训练模型计划统一导出 ONNX：

```text
Training
PyTorch / Paddle
      |
      v
     ONNX
      |
      v
OpenCV + ONNX Runtime
      |
      v
C++ Qt Gate Terminal
```

Gate 只对上层暴露稳定的识别结果结构，不让业务层感知 YOLO、OCR 或 HyperLPR 的实现细节。

建议接口：

```cpp
struct PlateRecognitionResult {
    std::string plateNumber;
    float detectionConfidence;
    float recognitionConfidence;
    cv::Rect boundingBox;
};

class ILicensePlateRecognizer {
public:
    virtual ~ILicensePlateRecognizer() = default;

    virtual std::optional<PlateRecognitionResult>
    recognize(const cv::Mat& frame) = 0;
};
```

后续分别实现：

```text
HyperLPRRecognizer
SmartParkLprRecognizer
```

### 车牌规则后处理

OCR 结果后增加独立的 `PlateValidator`，用于格式校验、标准化和低置信度拒绝，但不允许用规则“伪造”模型未识别出的字符。

主要校验包括：

- 第一位是否为合法省级简称；
- 第二位是否符合车牌字母规则；
- 普通车牌与新能源车牌长度是否合法；
- 是否包含不允许出现的字符；
- OCR 置信度是否低于 Gate 的人工确认阈值。

低置信度时 Gate 应进入人工确认流程，而不是自动放行。

### LPR 评测设计

两条路线必须在相同测试划分上比较，至少记录：

| 指标 | 说明 |
| --- | --- |
| Detection Precision / Recall | 车牌检测精确率和召回率 |
| `mAP@0.5` / `mAP@0.5:0.95` | Detector 检测质量 |
| Character Accuracy | 单字符识别准确率 |
| Full Plate Accuracy | 整块车牌完全正确的比例，作为核心业务指标 |
| Green Plate Accuracy | 新能源车牌整牌准确率 |
| Blur / Tilt / Challenge Accuracy | 困难子集整牌准确率 |
| End-to-End Latency | 从输入帧到最终车牌字符串的耗时 |
| Model Size / Memory | Gate 部署资源占用 |

最终课程报告计划至少比较三个实验组：

```text
A. HyperLPR3

B. YOLO11m + 原始 PP-OCRv5

C. SmartPark LPR
   YOLO11m CCPD fine-tune
   + PP-OCRv5 中国车牌 fine-tune
   + 专用字符字典
   + SmartPark 场景数据
   + PlateValidator
```

其中 C 为最终模型。通过 B → C 的变化可以量化“中国车牌专用微调”带来的收益，而 A 则提供成熟专用框架的工程基线。

> 注意：训练数据、预训练模型和第三方框架均应遵循各自许可证与数据使用要求。特别是自建真实车牌数据应考虑隐私与脱敏；模型权重如体积较大，优先使用 Release、Git LFS 或独立下载说明，不直接放入普通 Git 历史。

## 自动分配算法

停车场由多个互不重叠的矩形分区组成。每个分区可独立设置行数、列数、车位宽、车位长、通道宽、通道位置和车位类型，因此可组合出长宽不同的车位和异形整体布局。场地可以有多个入口和出口。

构建布局时，系统验证：

- 分区必须完全位于场地边界内。
- 分区之间不能重叠。
- 通道宽度至少 2.5 米。
- 每个车位必须能从至少一个入口到达，并通往至少一个出口。

路径规划使用 0.5 米栅格。单目标查询使用 A*，分配器对每个入口/出口做一次到全部候选车位的搜索，避免每辆车对 60 个车位分别跑两次 A*。车位矩形为障碍，通道和空地为可通行区域。搜索支持 8 方向移动，禁止斜穿障碍，并增加转向代价。拥堵区域会提高边权：

```text
edgeCost = baseCost * (1 + k * localOccupancy)
```

默认 `k=0.35`，即局部每多一辆占用车，经过该格子的边权提高 35%。拥堵在两个层面生效：路径搜索把局部占用写入边权（`route.cost`），让规划出的路线自动绕开拥堵通道；车位评分再用 `nearbyOccupiedSpots`（`w3`）对局部占用密集的车位单独加罚。

`SpotAllocator` 默认使用 `WeightedCost` 策略：

```text
score =
    w1 * entryPathDistance
  + w2 * exitPathDistance
  + w3 * nearbyOccupiedSpots
  + w4 * turnCount
  + typePenalty
```

默认权重为 `w1=1.0`、`w2=0.35`、`w3=2.5`、`w4=0.4`。`Nearest` 只比较入口路径长度，作为对照策略。充电车位对电动车有加分，对普通车有惩罚；无障碍和 VIP 车位不是预留车位，而是对普通车附加类型惩罚（无障碍 `+25`、VIP `+40`），有替代空位时车辆会优先避开，只有绕行或距离代价高于类型惩罚时才会选择它们。

车位状态机：

```text
Available -> Reserved(ttl) -> Occupied -> Available
Available -> Occupied -> Available
```

预留未在 TTL 内确认占用时自动释放，避免后续 TCP 双端把同一车位分给两辆车。

## 自定义布局格式

CLI 和 GUI 共用以下文本布局格式：

```text
site 100 60
entrance 0 30
exit 100 30
region A 5 8 10 2 1.2 5.5 6 left normal
region B 38 24 10 2 1.4 6.0 6 right charging
region C 71 40 10 2 1.2 5.5 6 left accessible
```

字段含义：

- `site 宽 高`：场地边界，单位米。
- `entrance x y` 和 `exit x y`：入口与出口坐标，可重复多条。
- `region 名称 x y 行数 列数 车位宽 车位长 通道宽 left|right [类型]`：一个矩形分区；每一列都是“一条通道 + 一排车位”的独立车 bay，每个分区可以使用不同车位尺寸。
- 类型可选 `normal`、`charging`、`accessible`、`vip`，也支持 `type=charging` 写法；省略时默认为 `normal`。
- `#` 开头的行是注释。

## 终端验证（无需 Qt、Conda 或图形桌面）

项目使用 CMake Presets 统一管理构建目录。所有构建产物都放在根目录的 `build/` 下，其中 `build/cli` 是终端版本，`build/qt` 是 Qt 版本。在项目根目录执行，需 CMake 3.21+、Ninja 和支持 C++17 的编译器：

```bash
/usr/bin/cmake --preset cli-debug
/usr/bin/cmake --build --preset cli-debug --parallel
/usr/bin/ctest --preset cli-tests
./build/cli/apps/cli/smartpark_cli
./build/cli/apps/cli/smartpark_cli my-layout.txt
./build/cli/apps/cli/smartpark_cli --db /tmp/smartpark.db --reset
```

如果需要全新构建，可先删除 `build/cli` 或整个 `build/` 目录。CLI 与 Qt 的 CMake 缓存分别保存在 `build/cli` 和 `build/qt`，互不影响。

不带参数时使用内置 60 车位布局；带文本文件参数时加载自定义布局。程序自动执行验证，无需输入。它打印默认计费规则，分配 3 辆车、释放 1 辆车并再次自动分配，同时打印车位编号、类型、入口距离、出口距离、附近占用数、综合评分和离场费用；成功输出 `RESULT: PASS` 并返回 0。

CLI 默认把车位与停车记录持久化到 SQLite：未指定 `--db` 时使用用户数据目录 `smartpark/smartpark.db`，重启后可恢复占用/预留状态与停车记录；`--reset` 在启动前删除数据库文件，适合反复演示。测试中的演示用例固定使用构建目录下的临时数据库并带 `--reset`，保证结果确定。

当前版本不包含网络或车牌识别。

### 文件职责与运行流程

| 文件 | 用途 |
| --- | --- |
| `CMakeLists.txt` | 设置 C++17、CTest 和各构建目标；关闭 `SMARTPARK_BUILD_ADMIN` 后不查找 Qt。 |
| `CMakePresets.json` | 定义 CLI 与 Qt 的标准构建目录、构建参数和测试命令。 |
| `docs/research-sources.md` | 记录仓库/论文评分，以及对本阶段路线的取舍。 |
| `apps/cli/CMakeLists.txt` | 构建 `smartpark_cli`，链接核心库并注册终端演示测试。 |
| `apps/cli/main.cpp` | 终端入口，加载默认或自定义布局、SQLite 数据库参数并演示自动分配。 |
| `src/core/CMakeLists.txt` | 将模型、服务与持久化实现编译为 `smartpark_core` 静态库。 |
| `src/core/model/Geometry.h` | 定义坐标、矩形和几何工具。 |
| `src/core/model/ParkingLayout.h/.cpp` | 解析自定义布局并生成车位矩形、类型和出入口。 |
| `src/core/model/ParkingRecord.h/.cpp` | 保存一次停车的车牌、车位、时间、时长和费用。 |
| `src/core/model/Vehicle.h` | 声明车辆类型、车辆数据与只读访问接口。 |
| `src/core/model/Vehicle.cpp` | 实现车辆构造、非空车牌校验和数据访问。 |
| `src/core/persistence/DatabaseManager.h/.cpp` | SQLite 连接与生命周期管理。 |
| `src/core/persistence/ParkingRepository.h/.cpp` | 建表、入场/离场/预约持久化与状态恢复。 |
| `src/core/persistence/Persistence.h/.cpp` | CLI/GUI 共用的数据库封装，提供默认路径与 RAII 生命周期。 |
| `src/core/model/ParkingSpot.h` | 声明车位状态、类型、当前车辆、预留和占用接口。 |
| `src/core/model/ParkingSpot.cpp` | 实现单个车位的状态转换，拒绝重复占用或释放。 |
| `src/core/service/GridPlanner.h/.cpp` | 实现障碍感知栅格 A*、多目标搜索和拥堵边权。 |
| `src/core/service/SpotAllocator.h/.cpp` | 独立选位、策略、评分和路径缓存。 |
| `src/core/service/Billing.h/.cpp` | 定义计费规则并计算离场费用。 |
| `src/core/service/ParkingService.h/.cpp` | 实现入场、离场、计费、预留 TTL、剩余车位和历史记录查询。 |
| `tests/CMakeLists.txt` | 构建并注册模型单元测试。 |
| `tests/core_model_tests.cpp` | 验证模型、预留、计费边界、类型匹配、拥堵绕行和多入口选择。 |
| `apps/admin/CMakeLists.txt` | 构建可选 Qt 管理员端，Qt 自动处理只作用于该目标。 |
| `apps/admin/main.cpp` | 独立 GUI 入口，解析 `--db` 参数并启动主窗口。 |
| `apps/admin/MainWindow.h/.cpp` | 实现布局编辑、车位图、自动分配、路线显示与数据库恢复/重置交互。 |

运行流程：系统启动 `smartpark_cli` → 解析布局 → 构建障碍栅格 → `SpotAllocator` 按策略为候选车位计算路线和评分 → 选择最优车位并占用或预留 → 输出路线和状态 → 检查结果并返回退出码。

## Linux 构建与运行

当前 Linux 开发环境使用独立的 Conda 环境 `smartpark-qt68`，其中包含 Qt 6.8.4、CMake、Ninja 和 C++ 编译器。

```bash
source ~/miniforge3/etc/profile.d/conda.sh
conda activate smartpark-qt68

cmake --preset qt-debug
cmake --build --preset qt-debug --parallel
ctest --preset qt-tests
./build/qt/apps/admin/smartpark_admin
```

如果当前终端没有图形显示，可以使用 Qt 的 offscreen 平台插件做启动检查：

```bash
QT_QPA_PLATFORM=offscreen ./build/qt/apps/admin/smartpark_admin
```

该 Conda 环境用于构建 Linux x86_64 版本。Windows 和 Android 版本后续需要分别使用对应平台的 Qt Kit。
