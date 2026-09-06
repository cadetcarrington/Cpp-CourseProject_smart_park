#include "core/persistence/DatabaseManager.h"

#include <QSqlError>

namespace smartpark {

DatabaseManager::DatabaseManager(const QString &databasePath)
{
    connectionName_ = QStringLiteral("smartpark-%1").arg(
        QString::number(reinterpret_cast<quintptr>(this), 16));
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath);
    if (!database_.open()) {
        lastError_ = database_.lastError().text();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

DatabaseManager::~DatabaseManager()
{
    if (database_.isValid()) {
        database_.close();
        database_ = QSqlDatabase();
    }
    if (!connectionName_.isEmpty()) {
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

QSqlDatabase &DatabaseManager::database() noexcept
{
    return database_;
}

const QString &DatabaseManager::lastError() const noexcept
{
    return lastError_;
}

} // namespace smartpark
