#include "network/Protocol.h"

#include <QJsonDocument>

namespace smartpark{
namespace protocol{
namespace{
constexpr char kTypeRequest[] = "request";
constexpr char kTypeResponse[] = "response";
constexpr char kTypeEvent[] = "event";

QJsonObject baseMessage(const char *type){
    QJsonObject message;
    message.insert(QStringLiteral("v"), kProtocolVersion);
    message.insert(QStringLiteral("type"), QLatin1String(type));
    return message;
}
} // namespace

QByteArray encodeFrame(const QJsonObject &message){
    const QByteArray payload =
        QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame;
    const quint32 length = static_cast<quint32>(payload.size());
    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(payload);
    return frame;
}

FrameStatus tryDecodeFrame(QByteArray &buffer, QJsonObject *message){
    if (buffer.size() < 4){
        return FrameStatus::NeedMore;
    }
    const uchar *raw = reinterpret_cast<const uchar *>(buffer.constData());
    const quint32 length = (quint32(raw[0]) << 24) | (quint32(raw[1]) << 16)
        | (quint32(raw[2]) << 8) | quint32(raw[3]);
    if (length > static_cast<quint32>(kMaxFrameBytes)){
        return FrameStatus::Invalid;
    }
    if (buffer.size() < 4 + static_cast<int>(length)){
        return FrameStatus::NeedMore;
    }
    if (message != nullptr){
        const QByteArray payload = buffer.mid(4, static_cast<int>(length));
        *message = QJsonDocument::fromJson(payload).object();
    }
    buffer.remove(0, 4 + static_cast<int>(length));
    return FrameStatus::Ok;
}

QJsonObject makeRequest(const QString &action, const QString &token,
                        const QJsonObject &payload, const QString &id){
    QJsonObject message = baseMessage(kTypeRequest);
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("action"), action);
    if (!token.isEmpty()){
        message.insert(QStringLiteral("token"), token);
    }
    message.insert(QStringLiteral("payload"), payload);
    return message;
}

QJsonObject makeResponse(const QString &id, bool ok,
                         const QJsonObject &payload, const QString &error){
    QJsonObject message = baseMessage(kTypeResponse);
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("ok"), ok);
    if (!payload.isEmpty()){
        message.insert(QStringLiteral("payload"), payload);
    }
    if (!error.isEmpty()){
        message.insert(QStringLiteral("error"), error);
    }
    return message;
}

QJsonObject makeEvent(const QString &event, const QJsonObject &payload){
    QJsonObject message = baseMessage(kTypeEvent);
    message.insert(QStringLiteral("event"), event);
    if (!payload.isEmpty()){
        message.insert(QStringLiteral("payload"), payload);
    }
    return message;
}

} // namespace protocol
} // namespace smartpark
