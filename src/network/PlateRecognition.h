#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace smartpark::network{

// 车牌识别的服务端实现，REST 网关与 TCP 服务端共用。
//
// 两种后端：
//   lprCommand 非空 -> 脚本桥接：写临时图片，执行命令模板（%1 = 图片路径），
//                      解析 stdout 的 JSON；
//   lprCommand 为空 -> mock：由图片内容哈希确定性生成车牌，供没有识别环境时演示。
//
// 识别要起 Python 进程（s1 上实测约 13 秒），**服务端一律走异步入口**：
// 早先的同步实现在 HTTP/TCP 处理器里 waitForFinished，把整个事件循环（HTTP、
// WebSocket、TCP 心跳与快照）一起堵住十几秒——网页转圈，管理端还会因为错过
// 心跳而掉线重连（实测：识别 13.3 秒期间 GET /parking/status 等了 12.3 秒）。
enum class LprStatus{
    Ok,        // 识别到车牌
    NoPlate,   // 脚本正常跑完，但画面里没有车牌（换张照片即可，不是服务端故障）
    Failed     // 脚本启动失败/超时/输出无法解析（服务端要排障）
};

struct LprOutcome{
    LprStatus status{LprStatus::Failed};
    QJsonObject result;   // Ok：识别结果（plate/confidence/bounding_box/…）
    QString note;         // Ok：后端说明（"script"/"mock"）；失败：人类可读原因
    QString detail;       // 脚本原话摘要（排障用，可能为空）
};

// 纯函数：把脚本的退出码与输出归类。同步/异步入口共用同一套判断，
// 也便于直接单测（不需要真的起进程）。
LprOutcome classifyLprOutput(int exitCode, const QByteArray &stdoutBytes,
                             const QByteArray &stderrBytes);

// 同步识别：跑脚本并等它结束。仅用于不介意阻塞的场合（测试、命令行）；
// 服务端处理器请用 recognizePlateAsync。note 回填后端说明或失败原因。
QJsonObject recognizePlate(const QByteArray &imageBytes,
                           const QString &lprCommand, QString *note);

// 异步识别：进程在事件循环里跑，done 在 context 所属线程回调（保证只回调一次）。
// context 用于生命周期绑定：context 销毁时排队任务丢弃、运行中进程树被杀，回调
// 不再触发。mock 模式（lprCommand 为空）同样异步回调，调用方不必区分两条路径。
//
// 内部是进程级单例调度器：同时最多跑 N 个脚本进程，多出来的进 FIFO 队列；
// 队列也满时立刻异步失败（Failed，note = "识别任务排队已满，请稍后重试"）。
// 每个脚本进程都 setsid() 自成会话/进程组，超时/取消/进程退出时向 -pid 发
// SIGTERM、宽限后补 SIGKILL，再 waitForFinished 回收，脚本再 Popen 的 Paddle
// OCR 子进程不会变成孤儿。本函数必须在调度器所属的事件循环线程调用（服务端的
// REST/TCP 处理器都在同一个主线程；首次调用时创建调度器）。
//
// 参数在进程内第一次用到识别时读取一次，之后不再变化（测试靠它缩短超时）：
//   SMARTPARK_LPR_MAX_CONCURRENCY  同时运行的识别进程数，默认 2；
//   SMARTPARK_LPR_QUEUE_LIMIT      排队上限，默认 16，超出的请求立刻失败；
//   SMARTPARK_LPR_TIMEOUT_MS       识别超时（毫秒），默认 60000；
//                                  异步与同步入口都生效。
void recognizePlateAsync(const QByteArray &imageBytes, const QString &lprCommand,
                         QObject *context, std::function<void(LprOutcome)> done);

// 无脚本时的 mock 车牌：同一张图片总是得到同一个车牌。
QString mockPlateFromImage(const QByteArray &imageBytes);

} // namespace smartpark::network
