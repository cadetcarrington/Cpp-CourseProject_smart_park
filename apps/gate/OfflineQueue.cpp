#include "OfflineQueue.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace smartpark{
namespace gate{

OfflineQueue::OfflineQueue(QString filePath)
    : filePath_(std::move(filePath)){
    QFile file(filePath_);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
        for (const QByteArray &line : file.readAll().split('\n')){
            if (!line.trimmed().isEmpty()) ++cachedCount_;
        }
    }
}

bool OfflineQueue::append(const QJsonObject &event){
    QFile file(filePath_);
    if (!file.open(QIODevice::Append | QIODevice::Text)) return false;
    QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact);
    line.append('\n');
    if (file.write(line) != line.size() || !file.flush()) return false;
    file.close();
    if (file.error() != QFileDevice::NoError) return false;
    ++cachedCount_;
    return true;
}

QJsonArray OfflineQueue::pending(int limit) const{
    QJsonArray events;
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return events;
    for (const QByteArray &line : file.readAll().split('\n')){
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
        events.append(document.object());
        if (events.size() == limit) break;
    }
    return events;
}

bool OfflineQueue::acknowledge(int count){
    if (count < 1 || count > cachedCount_) return false;
    QFile source(filePath_);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QList<QByteArray> lines = source.readAll().split('\n');
    source.close();
    QSaveFile target(filePath_);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    int consumed = 0;
    for (const QByteArray &line : lines){
        if (line.trimmed().isEmpty()) continue;
        if (consumed < count){
            ++consumed;
            continue;
        }
        QByteArray output = line;
        output.append('\n');
        if (target.write(output) != output.size()) return false;
    }
    if (consumed != count || !target.commit()) return false;
    cachedCount_ -= count;
    return true;
}

int OfflineQueue::size() const{
    return cachedCount_;
}

int replayHandledPrefix(const QJsonObject &result, int submitted){
    if (submitted <= 0){
        return 0;
    }
    // 首选按逐条结论判定：results 与服务端处理顺序一致，只认「有结论」的前缀。
    int handled = 0;
    for (const QJsonValue &item : result.value(QStringLiteral("results")).toArray()){
        if (handled >= submitted){
            break;
        }
        if (!item.toObject().contains(QStringLiteral("ok"))){
            break;   // 从这里开始没有结论，它及其后全部保留待重报
        }
        ++handled;
    }
    if (handled > 0){
        return handled;
    }
    // 兼容没有 results 的旧响应：三个计数之和等于提交条数即视为整批已处理。
    // skipped 与 applied/duplicate 同等对待——被拒事件同样已有结论，不能堵住队列。
    const int applied = result.value(QStringLiteral("applied")).toInt();
    const int duplicate = result.value(QStringLiteral("duplicate")).toInt();
    const int skipped = result.value(QStringLiteral("skipped")).toInt();
    return applied + duplicate + skipped == submitted ? submitted : 0;
}

bool replayBatchHandled(const QJsonObject &result, int submitted){
    return submitted > 0 && replayHandledPrefix(result, submitted) == submitted;
}

QStringList replaySkippedDetails(const QJsonObject &result, int limit){
    QStringList details;
    int seen = 0;
    for (const QJsonValue &item : result.value(QStringLiteral("results")).toArray()){
        if (limit >= 0 && seen >= limit){
            break;
        }
        ++seen;
        const QJsonObject entry = item.toObject();
        if (entry.value(QStringLiteral("ok")).toBool()){
            continue;
        }
        details << QStringLiteral("%1 %2：%3")
                       .arg(entry.value(QStringLiteral("plate")).toString(),
                            entry.value(QStringLiteral("kind")).toString(),
                            entry.value(QStringLiteral("error")).toString());
    }
    return details;
}

} // namespace gate
} // namespace smartpark
