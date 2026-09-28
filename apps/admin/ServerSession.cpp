#include "ServerSession.h"

#include <QAbstractSocket>
#include <QTcpSocket>

namespace smartpark{

ServerSession::ServerSession(QObject *parent)
    : QObject(parent){
    heartbeatTimer_.setInterval(std::chrono::seconds(20));
    reconnectTimer_.setSingleShot(true);
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this]{
        if (state_ != State::Online){
            return;
        }
        // 死连接看门狗：连接假死（无 FIN 断网）时心跳永远无应答，
        // 连续 3 次未回即主动断开，交给退避重连恢复。
        if (missedHeartbeats_ >= 2){
            missedHeartbeats_ = 0;
            if (socket_ != nullptr){
                socket_->abort();
            }
            return;
        }
        ++missedHeartbeats_;
        request(QStringLiteral("heartbeat"), {}, [this](bool ok,
                                                        const QString &,
                                                        const QJsonObject &){
            if (ok){
                missedHeartbeats_ = 0;
            }
        });
    });
    connect(&reconnectTimer_, &QTimer::timeout, this, [this]{
        if (!intentionalStop_){
            establishConnection();
        }
    });
}

ServerSession::~ServerSession(){
    stop();
}

void ServerSession::start(const QString &host, quint16 port,
                          const QString &user, const QString &password){
    host_ = host;
    port_ = port;
    user_ = user;
    password_ = password;
    intentionalStop_ = false;
    reconnectDelayMs_ = 1000;
    establishConnection();
}

void ServerSession::stop(){
    intentionalStop_ = true;
    reconnectTimer_.stop();
    heartbeatTimer_.stop();
    password_.clear();
    token_.clear();
    failPending(QStringLiteral("会话已关闭"));
    if (socket_ != nullptr){
        socket_->abort();
    }
    setState(State::Disconnected);
}

void ServerSession::request(const QString &action, const QJsonObject &payload,
                            const ReplyHandler &handler){
    if (state_ != State::Online || socket_ == nullptr
        || socket_->state() != QAbstractSocket::ConnectedState){
        if (handler){
            handler(false, QStringLiteral("未连接到服务端"), {});
        }
        return;
    }
    const QString id = QStringLiteral("a%1").arg(++requestCounter_);
    pending_.insert(id, PendingRequest{handler});
    sendFrame(protocol::makeRequest(action, token_, payload, id));
}

void ServerSession::setState(State state){
    if (state_ == state){
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void ServerSession::establishConnection(){
    if (socket_ == nullptr){
        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::connected, this, [this]{
            authenticate();
        });
        connect(socket_, &QTcpSocket::readyRead, this,
                &ServerSession::onReadyRead);
        // 统一由状态变化驱动重连：对端断开与 connectToHost 被拒绝
        // （服务端未启动/端口未监听）都汇聚到 UnconnectedState，
        // 只靠 disconnected 信号会在"连接被拒"时漏掉重连。
        connect(socket_, &QTcpSocket::stateChanged, this,
                [this](QAbstractSocket::SocketState socketState){
            if (socketState == QAbstractSocket::UnconnectedState
                && !intentionalStop_ && state_ != State::Disconnected){
                onSocketDisconnected();
            }
        });
    }
    buffer_.clear();
    token_.clear();
    missedHeartbeats_ = 0;
    setState(State::Connecting);
    socket_->connectToHost(host_, port_);
}

void ServerSession::authenticate(){
    setState(State::LoggingIn);
    const QString id = QStringLiteral("a%1").arg(++requestCounter_);
    pending_.insert(id, PendingRequest{[this](bool ok, const QString &error,
                                              const QJsonObject &payload){
        // failPending（传输中断）也会走到这里：只有服务端真实应答的
        // ok=false 才算认证失败；断连交由重连逻辑处理。
        if (failingForTransport_){
            return;
        }
        if (!ok){
            token_.clear();
            heartbeatTimer_.stop();
            setState(State::Disconnected);
            emit authFailed(error);
            // 口令错误重试无意义；只保留断线重连语义。
            intentionalStop_ = true;
            return;
        }
        token_ = payload.value(QStringLiteral("token")).toString();
        reconnectDelayMs_ = 1000;
        missedHeartbeats_ = 0;
        setState(State::Online);
        heartbeatTimer_.start();
    }});
    sendFrame(protocol::makeRequest(
        QStringLiteral("login"), {},
        QJsonObject{{QStringLiteral("user"), user_},
                    {QStringLiteral("pass"), password_}},
        id));
}

void ServerSession::sendFrame(const QJsonObject &message){
    if (socket_ != nullptr){
        socket_->write(protocol::encodeFrame(message));
        socket_->flush();
    }
}

void ServerSession::failPending(const QString &reason){
    const auto pending = pending_;
    pending_.clear();
    failingForTransport_ = true;
    for (auto it = pending.begin(); it != pending.end(); ++it){
        if (it.value().handler){
            it.value().handler(false, reason, {});
        }
    }
    failingForTransport_ = false;
}

void ServerSession::scheduleReconnect(){
    reconnectTimer_.start(reconnectDelayMs_);
    reconnectDelayMs_ = std::min(reconnectDelayMs_ * 2, 15000);
}

void ServerSession::onReadyRead(){
    buffer_.append(socket_->readAll());
    QJsonObject message;
    while (true){
        const auto status = protocol::tryDecodeFrame(buffer_, &message);
        if (status == protocol::FrameStatus::NeedMore){
            return;
        }
        if (status == protocol::FrameStatus::Invalid){
            // 非法帧：丢弃缓冲并断开，交由重连逻辑恢复。
            buffer_.clear();
            socket_->abort();
            return;
        }
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response")){
            const QString id = message.value(QStringLiteral("id")).toString();
            const auto it = pending_.find(id);
            if (it != pending_.end()){
                const PendingRequest pending = it.value();
                pending_.erase(it);
                if (pending.handler){
                    pending.handler(
                        message.value(QStringLiteral("ok")).toBool(),
                        message.value(QStringLiteral("error")).toString(),
                        message.value(QStringLiteral("payload")).toObject());
                }
            }
        } else if (type == QStringLiteral("event")){
            emit eventReceived(
                message.value(QStringLiteral("event")).toString(),
                message.value(QStringLiteral("payload")).toObject());
        }
    }
}

void ServerSession::onSocketDisconnected(){
    token_.clear();
    heartbeatTimer_.stop();
    failPending(QStringLiteral("连接已断开"));
    setState(State::Disconnected);
    if (!intentionalStop_){
        scheduleReconnect();
    }
}

} // namespace smartpark
