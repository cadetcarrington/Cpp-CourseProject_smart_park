#include "core/persistence/Persistence.h"
#include <QDir>
#include <QStandardPaths>
#include <stdexcept>
namespace smartpark{
QString Persistence::defaultDatabasePath(){
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()){
        base = QDir::homePath() + QStringLiteral("/.smartpark");
    }
    const QString directory = base + QStringLiteral("/smartpark");
    QDir dir(directory);
    if (!dir.exists()){
        dir.mkpath(QStringLiteral("."));
    }
    if (dir.exists()){
        return directory + QStringLiteral("/smartpark.db");
    }
    return QDir::current().filePath(QStringLiteral("smartpark.db"));
}
Persistence::Persistence(const QString &databasePath)
    : databaseManager_(databasePath){
    if (!databaseManager_.database().isOpen()){
        throw std::runtime_error("cannot open database: "
                                 + databaseManager_.lastError().toStdString());
    }
    repository_ = std::make_unique<ParkingRepository>(databaseManager_.database());
    if (!repository_->lastError().empty()){
        throw std::runtime_error("cannot initialize database schema: "
                                 + repository_->lastError());
    }
}
DatabaseManager &Persistence::databaseManager() noexcept{
    return databaseManager_;
}
ParkingRepository &Persistence::repository() noexcept{
    return *repository_;
}
const QString &Persistence::lastError() const noexcept{
    return databaseManager_.lastError();
}
} // namespace smartpark
