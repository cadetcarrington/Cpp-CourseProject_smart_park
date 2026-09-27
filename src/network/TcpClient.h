#pragma once
#include "network/Protocol.h"

#include <QHash>
#include <QObject>
#include <QTcpSocket>

#include <functional>
#include <optional>
#include <vector>

namespace smartpark{

// 协议 v1 测试/演示客户端：连接服务端，发送请求并同步等待应答
// （内嵌事件循环），收集广播事件。供服务端 --selftest 与后续
// Gate / 用户端复用。
class TcpClient : public QObject{
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);

    bool connectToHost(const QString &host, quint16 port, int timeoutMs = 5000);
    void disconnectFromHost();
    bool connected() const;
    const QString &lastError() const noexcept;

    // 登录后由调用方保存 token，后续请求自动携带。
    void setToken(const QString &token);

    // 发送 action 并等待同 id 应答；超时/断开返回 nullopt。
    std::optional<QJsonObject> request(const QString &action,
                                       const QJsonObject &payload,
                                       int timeoutMs = 5000);

    // 已收到但未被取走的广播事件。
    std::vector<QJsonObject> takeEvents();

    // 事件回调（收到事件即时触发）。
    std::function<void(const QJsonObject &)> onEvent;

private:
    void onReadyRead();
    QTcpSocket *socket_{nullptr};
    QByteArray buffer_;
    QHash<QString, QJsonObject> responses_;
    std::vector<QJsonObject> events_;
    QString token_;
    QString lastError_;
    quint64 requestCounter_{0};
};

} // namespace smartpark
