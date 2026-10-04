#pragma once
#include "core/model/Vehicle.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace smartpark{
namespace protocol{

constexpr int kProtocolVersion = 1;
// 单帧 JSON 载荷上限；超限视为恶意/异常连接并断开。
constexpr int kMaxFrameBytes = 1024 * 1024;

// 封包：4 字节大端长度前缀 + UTF-8 JSON 载荷。
QByteArray encodeFrame(const QJsonObject &message);

enum class FrameStatus{
    NeedMore,   // 缓冲区不足一帧，继续等待
    Ok,         // 解出一帧，message 已填充，缓冲区移除已消费字节
    Invalid     // 长度非法（超上限），连接应断开
};
FrameStatus tryDecodeFrame(QByteArray &buffer, QJsonObject *message);

// 消息构造：请求 / 应答 / 事件（字段见 docs/tcp-protocol.md）。
QJsonObject makeRequest(const QString &action, const QString &token,
                        const QJsonObject &payload, const QString &id);
QJsonObject makeResponse(const QString &id, bool ok,
                         const QJsonObject &payload = {},
                         const QString &error = {});
QJsonObject makeEvent(const QString &event, const QJsonObject &payload = {});

// 车型的线上字符串编码（parking.enter / gate.replay / admin.snapshot 共用）。
// 定义在协议层而不是各自实现：服务端与客户端一旦对不上，
// 摩托车/货车/新能源车会被静默当成轿车处理。
QString vehicleTypeToString(smartpark::VehicleType type);
std::optional<smartpark::VehicleType> vehicleTypeFromString(const QString &text);

} // namespace protocol
} // namespace smartpark
