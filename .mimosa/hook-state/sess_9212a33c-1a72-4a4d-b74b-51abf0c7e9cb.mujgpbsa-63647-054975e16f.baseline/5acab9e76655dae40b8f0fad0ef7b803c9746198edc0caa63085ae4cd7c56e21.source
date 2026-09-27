#pragma once
#include "core/persistence/DatabaseManager.h"
#include "core/persistence/ParkingRepository.h"
#include <QString>
#include <memory>
namespace smartpark{
// 组合 DatabaseManager 与 ParkingRepository 的 RAII 辅助类：
// 负责数据库打开与连接生命周期，供 CLI / GUI 客户端直接注入 ParkingService。
class Persistence{
public:
    static QString defaultDatabasePath();
    explicit Persistence(const QString &databasePath);
    ~Persistence() = default;
    Persistence(const Persistence &) = delete;
    Persistence &operator=(const Persistence &) = delete;
    Persistence(Persistence &&) = delete;
    Persistence &operator=(Persistence &&) = delete;
    DatabaseManager &databaseManager() noexcept;
    ParkingRepository &repository() noexcept;
    const QString &lastError() const noexcept;
private:
    DatabaseManager databaseManager_;
    std::unique_ptr<ParkingRepository> repository_;
};
} // namespace smartpark
