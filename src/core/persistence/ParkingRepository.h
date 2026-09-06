#pragma once

#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/model/ParkingSpot.h"
#include "core/model/Vehicle.h"

#include <QSqlDatabase>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace smartpark {

struct PersistedSpotState
{
    std::string spotId;
    SpotStatus status{SpotStatus::Available};
    std::optional<Vehicle> vehicle;
    std::optional<ParkingSpot::TimePoint> reservationExpiresAt;
};

struct PersistedRecord
{
    std::string plateNumber;
    std::string spotId;
    ParkingRecord::TimePoint entryTime{};
    std::optional<ParkingRecord::TimePoint> exitTime;
    double fee{0.0};
};

class ParkingRepository
{
public:
    explicit ParkingRepository(QSqlDatabase &database);

    bool saveLayout(const ParkingLayout &layout);
    bool saveEntry(const ParkingRecord &record, const ParkingSpot &spot);
    bool saveReservation(const ParkingSpot &spot);
    bool saveExit(const ParkingRecord &record,
                  const ParkingRecord::TimePoint &exitTime,
                  double fee = 0.0);
    bool saveSpotState(const ParkingSpot &spot);
    std::vector<PersistedSpotState> loadSpotStates();
    std::vector<PersistedRecord> loadRecords();
    const std::string &lastError() const noexcept;

private:
    void createSchema();
    std::string layoutSignature(const ParkingLayout &layout) const;
    bool saveSpotStateInTransaction(const ParkingSpot &spot);
    bool closeRecordInTransaction(const ParkingRecord &record,
                                  qint64 exitTimeMs,
                                  double fee);
    bool markSpotAvailableInTransaction(const std::string &spotId);

    QSqlDatabase database_;
    std::string lastError_;
};

} // namespace smartpark
