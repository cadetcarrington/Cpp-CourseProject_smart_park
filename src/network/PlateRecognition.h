#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace smartpark::network{

// 车牌识别的服务端实现，REST 网关与 TCP 服务端共用。
//
// 两种后端：
//   lprCommand 非空 -> 脚本桥接：写临时图片，执行命令模板（%1 = 图片路径），
//                      解析 stdout 的 JSON；
//   lprCommand 为空 -> mock：由图片内容哈希确定性生成车牌，供没有识别环境时演示。
//
// note 回填后端说明（"script" / "mock" / 失败原因），成功时返回结果的 JSON。
QJsonObject recognizePlate(const QByteArray &imageBytes,
                           const QString &lprCommand, QString *note);

// 无脚本时的 mock 车牌：同一张图片总是得到同一个车牌。
QString mockPlateFromImage(const QByteArray &imageBytes);

} // namespace smartpark::network
