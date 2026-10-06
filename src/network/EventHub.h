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

    void publish(const QString &name, const QJsonObject &payload){
        emit eventOccurred(name, payload);
    }

signals:
    void eventOccurred(const QString &name, const QJsonObject &payload);
};

} // namespace smartpark
