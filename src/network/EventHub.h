#pragma once
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace smartpark{

// 业务事件枢纽：TCP 动作与 REST 网关的入离场/预约/支付动作在此发布事件，
// TCP 广播与 WebSocket 推送各自订阅，保证两条入口对外事件一致。
// 事件名与负载结构以 docs/tcp-protocol.md 第 5 节为准（REST 扩展事件见
// docs/rest-api.md 第 7 节）。
class EventHub : public QObject{
    Q_OBJECT

public:
    explicit EventHub(QObject *parent = nullptr) : QObject(parent){}

    // 事件来源。TCP 服务端在处理 TCP 动作时已经**直接**把事件广播给 TCP 客户端了，
    // 随后才 publish 到 hub 让 WebSocket 也能收到；订阅端据此跳过自己刚发过的那些，
    // 否则同一条事件会被广播两次。
    enum class Origin{
        Tcp,
        Rest,
    };
    Q_ENUM(Origin)

    void publish(const QString &name, const QJsonObject &payload,
                 Origin origin = Origin::Rest){
        emit eventOccurred(name, payload, origin);
    }

signals:
    void eventOccurred(const QString &name, const QJsonObject &payload,
                       smartpark::EventHub::Origin origin);
};

} // namespace smartpark
