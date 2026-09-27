#include "core/service/AuditLogService.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace smartpark{
namespace{
constexpr char kGenesisHash[] = "GENESIS";

std::string toHex(const QByteArray &digest){
    return QString::fromLatin1(digest.toHex()).toStdString();
}
std::string optionalText(const QSqlQuery &query, int column){
    return query.value(column).toString().toStdString();
}
} // namespace

AuditLogService::AuditLogService(QSqlDatabase database)
    : database_(std::move(database)){
    createSchema();
}

void AuditLogService::createSchema(){
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS audit_logs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "ts_ms INTEGER NOT NULL,"
            "actor TEXT NOT NULL,"
            "action TEXT NOT NULL,"
            "detail TEXT,"
            "prev_hash TEXT NOT NULL,"
            "hash TEXT NOT NULL)"))){
        lastError_ = query.lastError().text().toStdString();
    }
}

std::string AuditLogService::hashEntry(long long id, long long tsMs,
                                       const std::string &actor, const std::string &action,
                                       const std::string &detail, const std::string &prevHash){
    const QByteArray payload = QString(QStringLiteral("%1|%2|%3|%4|%5|%6"))
                                   .arg(id)
                                   .arg(tsMs)
                                   .arg(QString::fromStdString(actor))
                                   .arg(QString::fromStdString(action))
                                   .arg(QString::fromStdString(detail))
                                   .arg(QString::fromStdString(prevHash))
                                   .toUtf8();
    return toHex(QCryptographicHash::hash(payload, QCryptographicHash::Sha256));
}

std::optional<AuditLogService::Entry> AuditLogService::lastEntry() const{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, ts_ms, actor, action, detail, prev_hash, hash"
            " FROM audit_logs ORDER BY id DESC LIMIT 1"))){
        return std::nullopt;
    }
    if (!query.next()){
        return std::nullopt;
    }
    Entry entry;
    entry.id = query.value(0).toLongLong();
    entry.tsMs = query.value(1).toLongLong();
    entry.actor = optionalText(query, 2);
    entry.action = optionalText(query, 3);
    entry.detail = optionalText(query, 4);
    entry.prevHash = optionalText(query, 5);
    entry.hash = optionalText(query, 6);
    return entry;
}

bool AuditLogService::record(const std::string &actor, const std::string &action,
                             const std::string &detail){
    const auto previous = lastEntry();
    if (!previous.has_value() && !lastError_.empty()){
        return false;
    }
    const QString prevHash = previous.has_value()
        ? QString::fromStdString(previous->hash)
        : QString::fromLatin1(kGenesisHash);
    const qint64 tsMs = QDateTime::currentMSecsSinceEpoch();

    // id 由自增主键产生，先占位插入再回填哈希，保证链上 id 连续。
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO audit_logs(ts_ms, actor, action, detail, prev_hash, hash)"
        " VALUES(:ts, :actor, :action, :detail, :prevHash, '')"));
    insert.bindValue(QStringLiteral(":ts"), tsMs);
    insert.bindValue(QStringLiteral(":actor"), QString::fromStdString(actor));
    insert.bindValue(QStringLiteral(":action"), QString::fromStdString(action));
    insert.bindValue(QStringLiteral(":detail"), QString::fromStdString(detail));
    insert.bindValue(QStringLiteral(":prevHash"), prevHash);
    if (!insert.exec()){
        lastError_ = insert.lastError().text().toStdString();
        return false;
    }
    const long long id = insert.lastInsertId().toLongLong();
    const std::string hash = hashEntry(id, tsMs, actor, action, detail,
                                       prevHash.toStdString());
    QSqlQuery update(database_);
    update.prepare(QStringLiteral(
        "UPDATE audit_logs SET hash = :hash WHERE id = :id"));
    update.bindValue(QStringLiteral(":hash"),
                     QString::fromStdString(hash));
    update.bindValue(QStringLiteral(":id"), id);
    if (!update.exec()){
        lastError_ = update.lastError().text().toStdString();
        return false;
    }
    return true;
}

std::vector<AuditLogService::Entry> AuditLogService::recent(int limit) const{
    std::vector<Entry> entries;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, ts_ms, actor, action, detail, prev_hash, hash"
        " FROM audit_logs ORDER BY id DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec()){
        return entries;
    }
    while (query.next()){
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.tsMs = query.value(1).toLongLong();
        entry.actor = optionalText(query, 2);
        entry.action = optionalText(query, 3);
        entry.detail = optionalText(query, 4);
        entry.prevHash = optionalText(query, 5);
        entry.hash = optionalText(query, 6);
        entries.push_back(std::move(entry));
    }
    return entries;
}

AuditLogService::VerifyResult AuditLogService::verifyChain() const{
    VerifyResult result;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, ts_ms, actor, action, detail, prev_hash, hash"
            " FROM audit_logs ORDER BY id ASC"))){
        result.ok = false;
        return result;
    }
    std::string expectedPrev(kGenesisHash);
    while (query.next()){
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.tsMs = query.value(1).toLongLong();
        entry.actor = optionalText(query, 2);
        entry.action = optionalText(query, 3);
        entry.detail = optionalText(query, 4);
        entry.prevHash = optionalText(query, 5);
        entry.hash = optionalText(query, 6);
        ++result.checked;
        if (entry.prevHash != expectedPrev
            || entry.hash != hashEntry(entry.id, entry.tsMs, entry.actor,
                                       entry.action, entry.detail, entry.prevHash)){
            result.ok = false;
            result.brokenAtId = entry.id;
            return result;
        }
        expectedPrev = entry.hash;
    }
    return result;
}

const std::string &AuditLogService::lastError() const noexcept{
    return lastError_;
}

} // namespace smartpark
