#pragma once
#include "network/Protocol.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class QTcpSocket;

namespace smartpark{

// 管理端远程会话：异步 TCP 客户端（协议 v1，见 docs/tcp-protocol.md）。
// 与 Gate 复用的同步 TcpClient 不同，本类全程回调驱动、不嵌套事件循环，
// 可安全用于 GUI 线程：
// - 登录成功后每 20 秒发送心跳（服务端 60 秒无帧判定掉线）；
// - 断线后按 1s→2s→4s…（上限 15s）指数退避自动重连，重连成功自动重新登录；
//   账号口令仅保存在内存，stop() 或析构即清除；
// - 结果不明的写请求（断线时在途）直接以失败回调，绝不自动重试。
class ServerSession : public QObject{
    Q_OBJECT

public:
    enum class State{
        Disconnected,  // 未连接（含登录失败、主动断开）
        Connecting,    // TCP 握手 / 等待重连计时
        LoggingIn,     // 已连接，等待 login 应答
        Online         // 已登录，可收发请求与事件
    };

    using ReplyHandler = std::function<void(bool ok, const QString &error,
                                            const QJsonObject &payload)>;

    explicit ServerSession(QObject *parent = nullptr);
    ~ServerSession() override;

    // 建立会话并登录；失败（含口令错误）不重试登录，仅自动重连网络。
    void start(const QString &host, quint16 port,
               const QString &user, const QString &password);
    // 主动断开并停止自动重连，清除内存中的凭据。
    void stop();

    void request(const QString &action, const QJsonObject &payload,
                 const ReplyHandler &handler);

    State state() const noexcept{ return state_; }
    const QString &host() const noexcept{ return host_; }
    quint16 port() const noexcept{ return port_; }
    const QString &user() const noexcept{ return user_; }
    // 是否存在已发出但未应答的请求（界面可用于显示“同步中”）。
    bool hasPendingRequests() const noexcept{ return !pending_.isEmpty(); }

signals:
    void stateChanged(smartpark::ServerSession::State state);
    // 登录被服务端拒绝（口令错误/账号不存在）；会话停留在 Disconnected。
    void authFailed(const QString &error);
    // 服务端广播事件（parking.entered / parking.exited / reservation.* / gate.replayed）。
    void eventReceived(const QString &event, const QJsonObject &payload);

private:
    struct PendingRequest{
        ReplyHandler handler;
    };

    void establishConnection();
    void authenticate();
    void sendFrame(const QJsonObject &message);
    void setState(State state);
    void failPending(const QString &reason);
    void scheduleReconnect();
    void onReadyRead();
    void onSocketDisconnected();

    QTcpSocket *socket_{nullptr};
    QByteArray buffer_;
    QHash<QString, PendingRequest> pending_;
    QTimer heartbeatTimer_;
    QTimer reconnectTimer_;
    QString host_;
    quint16 port_{0};
    QString user_;
    QString password_;
    QString token_;
    State state_{State::Disconnected};
    int reconnectDelayMs_{1000};
    int missedHeartbeats_{0};
    bool intentionalStop_{false};
    // failPending 重入标记：区分"传输失败回调"与"服务端业务错误应答"。
    bool failingForTransport_{false};
    quint64 requestCounter_{0};
};

} // namespace smartpark
