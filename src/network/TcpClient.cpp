#include "network/TcpClient.h"
#include "network/Protocol.h"

#include <QEventLoop>
#include <QTimer>

namespace smartpark{
TcpClient::TcpClient(QObject *parent)
    : QObject(parent){
}

bool TcpClient::connectToHost(const QString &host, quint16 port, int timeoutMs){
    if (socket_ == nullptr){
        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
        connect(socket_, &QTcpSocket::disconnected, this, [this]{
            token_.clear();
            buffer_.clear();
            responses_.clear();
        });
    }
    lastError_.clear();
    socket_->connectToHost(host, port);
    if (!socket_->waitForConnected(timeoutMs)){
        lastError_ = socket_->errorString();
        return false;
    }
    return true;
}

void TcpClient::disconnectFromHost(){
    if (socket_ != nullptr){
        socket_->disconnectFromHost();
    }
}

bool TcpClient::connected() const{
    return socket_ != nullptr
        && socket_->state() == QAbstractSocket::ConnectedState;
}

void TcpClient::setToken(const QString &token){
    token_ = token;
}

const QString &TcpClient::lastError() const noexcept{
    return lastError_;
}

void TcpClient::onReadyRead(){
    buffer_.append(socket_->readAll());
    QJsonObject message;
    while (true){
        const auto status = protocol::tryDecodeFrame(buffer_, &message);
        if (status == protocol::FrameStatus::NeedMore){
            return;
        }
        if (status == protocol::FrameStatus::Invalid){
            lastError_ = QStringLiteral("收到非法帧长度，连接关闭");
            socket_->disconnectFromHost();
            return;
        }
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response")){
            responses_.insert(message.value(QStringLiteral("id")).toString(),
                              message);
        } else if (type == QStringLiteral("event")){
            events_.push_back(message);
            if (onEvent){
                onEvent(message);
            }
        }
    }
}

std::optional<QJsonObject> TcpClient::request(const QString &action,
                                              const QJsonObject &payload,
                                              int timeoutMs){
    if (!connected()){
        lastError_ = QStringLiteral("未连接");
        return std::nullopt;
    }
    const QString id = QStringLiteral("c%1").arg(++requestCounter_);
    socket_->write(protocol::encodeFrame(
        protocol::makeRequest(action, token_, payload, id)));
    socket_->flush();

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poller;
    poller.setInterval(20);
    connect(&poller, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    poller.start();
    while (responses_.find(id) == responses_.end() && timeout.isActive()){
        loop.exec();   // 驱动 socket 读事件
    }
    poller.stop();
    const auto it = responses_.find(id);
    if (it == responses_.end()){
        lastError_ = lastError_.isEmpty() ? QStringLiteral("请求超时") : lastError_;
        return std::nullopt;
    }
    const QJsonObject response = it.value();
    responses_.erase(it);
    return response;
}

std::vector<QJsonObject> TcpClient::takeEvents(){
    std::vector<QJsonObject> taken;
    taken.swap(events_);
    return taken;
}

} // namespace smartpark
