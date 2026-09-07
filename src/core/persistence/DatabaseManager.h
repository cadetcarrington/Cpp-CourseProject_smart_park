#pragma once
#include <QSqlDatabase>
#include <QString>
namespace smartpark{
class DatabaseManager{
public:
    explicit DatabaseManager(const QString &databasePath);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;
    DatabaseManager(DatabaseManager &&) = delete;
    DatabaseManager &operator=(DatabaseManager &&) = delete;
    QSqlDatabase &database() noexcept;
    const QString &lastError() const noexcept;
private:
    QString connectionName_;
    QSqlDatabase database_;
    QString lastError_;
};
} // namespace smartpark
