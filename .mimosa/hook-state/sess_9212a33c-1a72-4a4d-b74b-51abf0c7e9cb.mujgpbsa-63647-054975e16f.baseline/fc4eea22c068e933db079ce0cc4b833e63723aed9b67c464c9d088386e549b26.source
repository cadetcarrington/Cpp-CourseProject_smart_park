#pragma once
#include <QSqlDatabase>
#include <optional>
#include <string>
#include <vector>

namespace smartpark{

// 防篡改操作审计日志：append-only 表 + 哈希链。
// 每条记录的 hash = SHA-256(prev_hash + 本条内容)，任何删改都会断链，
// verifyChain() 可检测。用于登录、出入场、预约、应急等敏感操作留痕。
class AuditLogService{
public:
    struct Entry{
        long long id{0};
        long long tsMs{0};
        std::string actor;
        std::string action;
        std::string detail;
        std::string prevHash;
        std::string hash;
    };
    struct VerifyResult{
        bool ok{true};
        long long brokenAtId{0};   // 首个断链记录 id，链完整时为 0
        int checked{0};
    };

    explicit AuditLogService(QSqlDatabase database);

    bool record(const std::string &actor, const std::string &action,
                const std::string &detail = {});
    std::vector<Entry> recent(int limit = 50) const;
    VerifyResult verifyChain() const;
    const std::string &lastError() const noexcept;

    static std::string hashEntry(long long id, long long tsMs,
                                 const std::string &actor, const std::string &action,
                                 const std::string &detail, const std::string &prevHash);

private:
    void createSchema();
    std::optional<Entry> lastEntry() const;

    QSqlDatabase database_;
    std::string lastError_;
};

} // namespace smartpark
