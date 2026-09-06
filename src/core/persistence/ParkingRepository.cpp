#include "core/persistence/ParkingRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <chrono>
#include <sstream>
#include <utility>

namespace smartpark {
namespace {

constexpr qint64 invalidTime = -1;

qint64 toMilliseconds(const ParkingRecord::TimePoint &time)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               time.time_since_epoch())
        .count();
}

ParkingRecord::TimePoint fromMilliseconds(qint64 milliseconds)
{
    return ParkingRecord::TimePoint{} + std::chrono::milliseconds(milliseconds);
}

bool validStatus(SpotStatus status) noexcept
{
    switch (status) {
    case SpotStatus::Available:
    case SpotStatus::Occupied:
    case SpotStatus::Reserved:
    case SpotStatus::Disabled:
        return true;
    }
    return false;
}

bool prepare(QSqlQuery &query, const QString &statement, std::string &error)
{
    if (!query.prepare(statement)) {
        error = query.lastError().text().toStdString();
        return false;
    }
    return true;
}

bool exec(QSqlQuery &query, std::string &error)
{
    if (!query.exec()) {
        error = query.lastError().text().toStdString();
        return false;
    }
    return true;
}

bool exec(QSqlQuery &query, const QString &statement, std::string &error)
{
    if (!query.exec(statement)) {
        error = query.lastError().text().toStdString();
        return false;
    }
    return true;
}

} // namespace

ParkingRepository::ParkingRepository(QSqlDatabase &database)
    : database_(database)
{
    createSchema();
}

void ParkingRepository::createSchema()
{
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS parking_spots ("
                       "identifier TEXT PRIMARY KEY,"
                       "zone TEXT NOT NULL,"
                       "type INTEGER NOT NULL,"
                       "row_index INTEGER NOT NULL,"
                       "column_index INTEGER NOT NULL,"
                       "x REAL NOT NULL,"
                       "y REAL NOT NULL,"
                       "width REAL NOT NULL,"
                       "height REAL NOT NULL,"
                       "access_x REAL NOT NULL,"
                       "access_y REAL NOT NULL,"
                       "status INTEGER NOT NULL,"
                       "plate_number TEXT,"
                       "vehicle_type INTEGER,"
                       "reserved_until_ms INTEGER)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS parking_records ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "plate_number TEXT NOT NULL,"
                       "spot_id TEXT NOT NULL,"
                       "entry_time_ms INTEGER NOT NULL,"
                       "exit_time_ms INTEGER,"
                       "fee REAL NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS layout_snapshot ("
                       "name TEXT PRIMARY KEY,"
                       "value TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parking_records_plate"
                       " ON parking_records(plate_number)")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(database_);
        if (!exec(query, statement, lastError_)) {
            return;
        }
    }
}

std::string ParkingRepository::layoutSignature(const ParkingLayout &layout) const
{
    std::ostringstream stream;
    stream.precision(15);
    stream << layout.siteWidth() << '|' << layout.siteHeight();
    for (const Point &point : layout.entrances()) {
        stream << "|E" << point.x << ',' << point.y;
    }
    for (const Point &point : layout.exits()) {
        stream << "|X" << point.x << ',' << point.y;
    }
    for (const Rectangle &region : layout.regions()) {
        stream << "|R" << region.origin.x << ',' << region.origin.y << ','
               << region.width << ',' << region.height;
    }
    for (const ParkingSpot &spot : layout.spots()) {
        stream << "|S" << spot.identifier() << ',' << spot.zone() << ','
               << static_cast<int>(spot.type()) << ',' << spot.row() << ','
               << spot.column() << ',' << spot.bounds().origin.x << ','
               << spot.bounds().origin.y << ',' << spot.bounds().width << ','
               << spot.bounds().height << ',' << spot.accessPoint().x << ','
               << spot.accessPoint().y;
    }
    return stream.str();
}

bool ParkingRepository::saveLayout(const ParkingLayout &layout)
{
    QSqlQuery signatureQuery(database_);
    if (!prepare(signatureQuery,
                 QStringLiteral("SELECT value FROM layout_snapshot WHERE name='signature'"),
                 lastError_)) {
        return false;
    }
    if (!exec(signatureQuery, lastError_)) {
        return false;
    }
    const std::string signature = layoutSignature(layout);
    if (signatureQuery.next()
        && signatureQuery.value(0).toString().toStdString() != signature) {
        lastError_ = "persisted layout differs from the current layout";
        return false;
    }

    if (!database_.transaction()) {
        lastError_ = database_.lastError().text().toStdString();
        return false;
    }

    QSqlQuery snapshotQuery(database_);
    if (!prepare(snapshotQuery,
                 QStringLiteral("INSERT INTO layout_snapshot(name,value) VALUES('signature',:value)"
                                " ON CONFLICT(name) DO UPDATE SET value=excluded.value"),
                 lastError_)) {
        database_.rollback();
        return false;
    }
    snapshotQuery.bindValue(QStringLiteral(":value"),
                            QString::fromStdString(signature));
    if (!exec(snapshotQuery, lastError_)) {
        || !exec(snapshotQuery, lastError_)) {
        database_.rollback();
        return false;
    }

    QSqlQuery spotQuery(database_);
    if (!prepare(spotQuery,
                 QStringLiteral("INSERT INTO parking_spots("
                                "identifier,zone,type,row_index,column_index,x,y,width,height,"
                                "access_x,access_y,status,plate_number,vehicle_type,reserved_until_ms)"
                                " VALUES(:identifier,:zone,:type,:row,:column,:x,:y,:width,:height,"
                                ":accessX,:accessY,:status,NULL,NULL,NULL)"
                                " ON CONFLICT(identifier) DO UPDATE SET"
                                " zone=:zone,type=:type,row_index=:row,column_index=:column,"
                                "x=:x,y=:y,width=:width,height=:height,"
                                "access_x=:accessX,access_y=:accessY"),
                 lastError_)) {
        database_.rollback();
        return false;
    }

    for (const ParkingSpot &spot : layout.spots()) {
        spotQuery.bindValue(QStringLiteral(":identifier"),
                            QString::fromStdString(spot.identifier()));
        spotQuery.bindValue(QStringLiteral(":zone"), QString::fromStdString(spot.zone()));
        spotQuery.bindValue(QStringLiteral(":type"), static_cast<int>(spot.type()));
        spotQuery.bindValue(QStringLiteral(":row"), spot.row());
        spotQuery.bindValue(QStringLiteral(":column"), spot.column());
        spotQuery.bindValue(QStringLiteral(":x"), spot.bounds().origin.x);
        spotQuery.bindValue(QStringLiteral(":y"), spot.bounds().origin.y);
        spotQuery.bindValue(QStringLiteral(":width"), spot.bounds().width);
        spotQuery.bindValue(QStringLiteral(":height"), spot.bounds().height);
        spotQuery.bindValue(QStringLiteral(":accessX"), spot.accessPoint().x);
        spotQuery.bindValue(QStringLiteral(":accessY"), spot.accessPoint().y);
        spotQuery.bindValue(QStringLiteral(":status"), static_cast<int>(SpotStatus::Available));
        if (!exec(spotQuery, lastError_)) {
            database_.rollback();
            return false;
        }
    }

    if (!database_.commit()) {
        lastError_ = database_.lastError().text().toStdString();
        database_.rollback();
        return false;
    }
    return true;
}

bool ParkingRepository::saveEntry(const ParkingRecord &record, const ParkingSpot &spot)
{
    const bool hasVehicle = spot.parkedVehicle().has_value();
    if (record.isClosed() || spot.status() != SpotStatus::Occupied || !hasVehicle
        || record.spotId() != spot.identifier()
        || record.plateNumber() != spot.parkedVehicle()->plateNumber()) {
        lastError_ = "invalid entry state";
        return false;
    }

    if (!database_.transaction()) {
        lastError_ = database_.lastError().text().toStdString();
        return false;
    }
    if (!saveSpotStateInTransaction(spot)) {
        database_.rollback();
        return false;
    }

    QSqlQuery query(database_);
    if (!prepare(query,
                 QStringLiteral("INSERT INTO parking_records("
                                "plate_number,spot_id,entry_time_ms,exit_time_ms,fee)"
                                " VALUES(:plate,:spot,:entry,NULL,0)"),
                 lastError_)) {
        database_.rollback();
        return false;
    }
    query.bindValue(QStringLiteral(":plate"),
                    QString::fromStdString(record.plateNumber()));
    query.bindValue(QStringLiteral(":spot"), QString::fromStdString(record.spotId()));
    query.bindValue(QStringLiteral(":entry"), toMilliseconds(record.entryTime()));
    if (!exec(query, lastError_)) {
        database_.rollback();
        return false;
    }

    if (!database_.commit()) {
        lastError_ = database_.lastError().text().toStdString();
        database_.rollback();
        return false;
    }
    return true;
}

bool ParkingRepository::saveReservation(const ParkingSpot &spot)
{
    if (spot.status() != SpotStatus::Reserved || !spot.parkedVehicle()
        || !spot.reservationExpiresAt()) {
        lastError_ = "invalid reservation state";
        return false;
    }
    return saveSpotState(spot);
}

bool ParkingRepository::saveExit(const ParkingRecord &record,
                                 const ParkingRecord::TimePoint &exitTime,
                                 double fee)
{
    if (record.isClosed() || exitTime < record.entryTime() || !std::isfinite(fee)
        || fee < 0.0) {
        lastError_ = "invalid parking exit state";
        return false;
    }
    if (!database_.transaction()) {
        lastError_ = database_.lastError().text().toStdString();
        return false;
    }
    if (!closeRecordInTransaction(record, exitTime, fee)
        || !markSpotAvailableInTransaction(record.spotId())) {
        database_.rollback();
        return false;
    }
    if (!database_.commit()) {
        lastError_ = database_.lastError().text().toStdString();
        database_.rollback();
        return false;
    }
    return true;
}

bool ParkingRepository::saveSpotStateInTransaction(const ParkingSpot &spot)
{
    if (spot.identifier().empty() || !validStatus(spot.status())) {
        lastError_ = "invalid parking spot state";
        return false;
    }
    const bool hasVehicle = spot.parkedVehicle().has_value();
    if ((spot.status() == SpotStatus::Available || spot.status() == SpotStatus::Disabled)
        && hasVehicle) {
        lastError_ = "inactive parking spot cannot store a vehicle";
        return false;
    }
    if ((spot.status() == SpotStatus::Occupied || spot.status() == SpotStatus::Reserved)
        && !hasVehicle) {
        lastError_ = "active parking spot requires a vehicle";
        return false;
    }
    if (spot.status() == SpotStatus::Reserved && !spot.reservationExpiresAt()) {
        lastError_ = "reserved parking spot requires an expiry time";
        return false;
    }
    if (spot.status() != SpotStatus::Reserved && spot.reservationExpiresAt()) {
        lastError_ = "only reserved parking spots can store an expiry time";
        return false;
    }

    QSqlQuery query(database_);
    if (!prepare(query,
                 QStringLiteral("UPDATE parking_spots SET status=:status,"
                                "plate_number=:plate,vehicle_type=:vehicleType,"
                                "reserved_until_ms=:reservedUntil WHERE identifier=:identifier"),
                 lastError_)) {
        return false;
    }
    query.bindValue(QStringLiteral(":status"), static_cast<int>(spot.status()));
    query.bindValue(QStringLiteral(":plate"),
                    hasVehicle ? QVariant(QString::fromStdString(
                                     spot.parkedVehicle()->plateNumber()))
                               : QVariant());
    query.bindValue(QStringLiteral(":vehicleType"),
                    hasVehicle ? QVariant(static_cast<int>(spot.parkedVehicle()->type()))
                               : QVariant());
    query.bindValue(QStringLiteral(":reservedUntil"),
                    spot.reservationExpiresAt()
                        ? QVariant(toMilliseconds(*spot.reservationExpiresAt()))
                        : QVariant(invalidTime));
    query.bindValue(QStringLiteral(":identifier"),
                    QString::fromStdString(spot.identifier()));
    if (!exec(query, lastError_)) {
        return false;
    }
    if (query.numRowsAffected() != 1) {
        lastError_ = "parking spot is not persisted: " + spot.identifier();
        return false;
    }
    return true;
}

bool ParkingRepository::saveSpotState(const ParkingSpot &spot)
{
    if (!database_.transaction()) {
        lastError_ = database_.lastError().text().toStdString();
        return false;
    }
    if (!saveSpotStateInTransaction(spot)) {
        database_.rollback();
        return false;
    }
    if (!database_.commit()) {
        lastError_ = database_.lastError().text().toStdString();
        database_.rollback();
        return false;
    }
    return true;
}

bool ParkingRepository::markSpotAvailableInTransaction(const std::string &spotId)
{
    QSqlQuery query(database_);
    if (!prepare(query,
                 QStringLiteral("UPDATE parking_spots SET status=:status,"
                                "plate_number=NULL,vehicle_type=NULL,reserved_until_ms=NULL"
                                " WHERE identifier=:identifier"),
                 lastError_)) {
        return false;
    }
    query.bindValue(QStringLiteral(":status"), static_cast<int>(SpotStatus::Available));
    query.bindValue(QStringLiteral(":identifier"), QString::fromStdString(spotId));
    if (!exec(query, lastError_)) {
        return false;
    }
    if (query.numRowsAffected() != 1) {
        lastError_ = "parking spot is not persisted: " + spotId;
        return false;
    }
    return true;
}

bool ParkingRepository::closeRecordInTransaction(const ParkingRecord &record,
                                                 const ParkingRecord::TimePoint &exitTime,
                                                 double fee)
{
    QSqlQuery query(database_);
    if (!prepare(query,
                 QStringLiteral("UPDATE parking_records SET exit_time_ms=:exit,fee=:fee"
                                " WHERE plate_number=:plate AND spot_id=:spot"
                                " AND exit_time_ms IS NULL"),
                 lastError_)) {
        return false;
    }
    query.bindValue(QStringLiteral(":exit"), toMilliseconds(exitTime));
    query.bindValue(QStringLiteral(":fee"), fee);
    query.bindValue(QStringLiteral(":plate"),
                    QString::fromStdString(record.plateNumber()));
    query.bindValue(QStringLiteral(":spot"), QString::fromStdString(record.spotId()));
    if (!exec(query, lastError_)) {
        return false;
    }
    if (query.numRowsAffected() != 1) {
        lastError_ = "active parking record is not persisted";
        return false;
    }
    return true;
}

std::vector<PersistedSpotState> ParkingRepository::loadSpotStates()
{
    std::vector<PersistedSpotState> states;
    QSqlQuery query(database_);
    if (!prepare(query,
                 QStringLiteral("SELECT identifier,status,plate_number,vehicle_type,"
                                "reserved_until_ms FROM parking_spots ORDER BY identifier"),
                 lastError_)) {
        return states;
    }
    if (!exec(query, lastError_)) {
        return states;
    }

    while (query.next()) {
        PersistedSpotState state;
        state.spotId = query.value(0).toString().toStdString();
        const int status = query.value(1).toInt();
        if (status < static_cast<int>(SpotStatus::Available)
            || status > static_cast<int>(SpotStatus::Disabled)) {
            lastError_ = "invalid persisted spot status";
            return {};
        }
        state.status = static_cast<SpotStatus>(status);
        const QString plate = query.value(2).toString();
        if (!plate.isEmpty()) {
            const int vehicleType = query.value(3).toInt();
            if (vehicleType < static_cast<int>(VehicleType::Car)
                || vehicleType > static_cast<int>(VehicleType::Electric)) {
                lastError_ = "invalid persisted vehicle type";
                return {};
            }
            state.vehicle = Vehicle(plate.toStdString(), static_cast<VehicleType>(vehicleType));
        }
        const qint64 expiry = query.value(4).toLongLong();
        if (expiry != invalidTime) {
            state.reservationExpiresAt = fromMilliseconds(expiry);
        }
        if ((state.status == SpotStatus::Occupied
             || state.status == SpotStatus::Reserved)
            != state.vehicle.has_value()) {
            lastError_ = "inconsistent persisted parking spot: " + state.spotId;
            return {};
        }
        states.push_back(std::move(state));
    }
    return states;
}

std::vector<PersistedRecord> ParkingRepository::loadRecords()
{
    std::vector<PersistedRecord> records;
    QSqlQuery query(database_);
    if (!prepare(query,
                 QStringLiteral("SELECT plate_number,spot_id,entry_time_ms,exit_time_ms,fee"
                                " FROM parking_records ORDER BY id"),
                 lastError_)) {
        return records;
    }
    if (!exec(query, lastError_)) {
        return records;
    }

    while (query.next()) {
        PersistedRecord record;
        record.plateNumber = query.value(0).toString().toStdString();
        record.spotId = query.value(1).toString().toStdString();
        record.entryTime = fromMilliseconds(query.value(2).toLongLong());
        const qint64 exitTime = query.value(3).toLongLong();
        if (exitTime != invalidTime) {
            record.exitTime = fromMilliseconds(exitTime);
        }
        record.fee = query.value(4).toDouble();
        if (record.plateNumber.empty() || record.spotId.empty()
            || (record.exitTime && *record.exitTime < record.entryTime)) {
            lastError_ = "invalid persisted parking record";
            return {};
        }
        records.push_back(std::move(record));
    }
    return records;
}

const std::string &ParkingRepository::lastError() const noexcept
{
    return lastError_;
}

} // namespace smartpark
