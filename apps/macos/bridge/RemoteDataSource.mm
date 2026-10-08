#include "RemoteDataSource.h"

#include "core/model/Vehicle.h"
#include "network/Protocol.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>

#include <cmath>

namespace smartpark{
namespace{

// 快照没有 accessPoint：本地布局里它是车位进出车道上的接驳点，远程只拿得到
// 车位矩形。用矩形中心近似——服务端同样只按矩形渲染远程车位图。
Point accessPointFor(const Rectangle &bounds){
    return Point{bounds.origin.x + bounds.width / 2.0,
                 bounds.origin.y + bounds.height / 2.0};
}

Rectangle rectangleFrom(const QJsonObject &object){
    return Rectangle{Point{object.value(QStringLiteral("x")).toDouble(),
                           object.value(QStringLiteral("y")).toDouble()},
                     object.value(QStringLiteral("w")).toDouble(),
                     object.value(QStringLiteral("h")).toDouble()};
}

SpotType spotTypeFrom(const QString &text){
    const auto parsed = spotTypeFromString(text.toStdString());
    return parsed.value_or(SpotType::Normal);
}

ParkingRecord::TimePoint timeFromMs(qint64 milliseconds){
    return ParkingRecord::TimePoint(std::chrono::milliseconds(milliseconds));
}

} // namespace

RemoteDataSource::RemoteDataSource(QObject *parent)
    : QObject(parent)
    , session_(std::make_unique<ServerSession>())
    // ParkingLayout 没有公开的默认构造：先放内置布局占位，
    // ready() 为 false 时界面不渲染，拿到快照后会整体替换。
    , layout_(ParkingLayout::defaultLayout()){
    connect(session_.get(), &ServerSession::stateChanged, this,
            [this](ServerSession::State state){
        switch (state){
        case ServerSession::State::Online:
            setError(QString());
            // 登录成功：取一次全量快照与分析报告。
            requestSnapshot();
            requestAnalytics();
            if (onConnectionChanged){
                onConnectionChanged(true, QStringLiteral("已连接服务端 %1:%2")
                                              .arg(host_).arg(port_));
            }
            break;
        case ServerSession::State::Disconnected:
            snapshotReady_ = false;
            if (onConnectionChanged){
                onConnectionChanged(false, QStringLiteral("与 %1:%2 的连接已断开，正在重连")
                                              .arg(host_).arg(port_));
            }
            break;
        case ServerSession::State::Connecting:
            if (onConnectionChanged){
                onConnectionChanged(false, QStringLiteral("正在连接 %1:%2…")
                                              .arg(host_).arg(port_));
            }
            break;
        case ServerSession::State::LoggingIn:
            if (onConnectionChanged){
                onConnectionChanged(false, QStringLiteral("正在登录…"));
            }
            break;
        }
    });
    connect(session_.get(), &ServerSession::authFailed, this,
            [this](const QString &error){
        setError(error.isEmpty() ? QStringLiteral("登录被服务端拒绝") : error);
        if (onConnectionChanged){
            onConnectionChanged(false, lastError_.empty()
                                           ? QStringLiteral("登录失败")
                                           : QString::fromStdString(lastError_));
        }
    });
    connect(session_.get(), &ServerSession::eventReceived, this,
            [this](const QString &, const QJsonObject &){
        // 事件是增量语义，客户端不复刻状态机：直接重新拉一次快照。
        requestSnapshot();
    });
}

RemoteDataSource::~RemoteDataSource() = default;

void RemoteDataSource::start(const QString &host, quint16 port,
                             const QString &user, const QString &password){
    host_ = host;
    port_ = port;
    session_->start(host, port, user, password);
}

void RemoteDataSource::stop(){
    session_->stop();
    snapshotReady_ = false;
}

bool RemoteDataSource::online() const{
    return session_->state() == ServerSession::State::Online;
}

void RemoteDataSource::setError(const QString &text){
    lastError_ = text.toStdString();
}

bool RemoteDataSource::ready() const noexcept{
    return snapshotReady_;
}

const std::string &RemoteDataSource::lastError() const noexcept{
    return lastError_;
}

double RemoteDataSource::dailyRevenueTotal() const noexcept{
    double sum = 0.0;
    for (const QJsonValue &item : dailyRevenue_){
        sum += item.toObject().value(QStringLiteral("fee")).toDouble();
    }
    return sum;
}

ParkingDataSource::Capabilities RemoteDataSource::capabilities() const{
    Capabilities caps;
    // 记录、预约、定金随快照下发，洞察在本地用 ParkingInsightEngine 算，
    // 因此这四项与本地模式一致。
    // 仍然不可用的只有真正没有协议 action 的：应急入场、车型更正、
    // 分配策略（服务端统一配置）、布局编辑（服务端 --layout 决定）。
    caps.emergencyEnter = false;
    caps.vehicleTypeEdit = false;
    caps.strategyEdit = false;
    caps.layoutEditing = false;
    caps.layoutText = false;
    return caps;
}

bool RemoteDataSource::awaitRequest(const QString &action, const QJsonObject &payload,
                                    QJsonObject *result, QString *error, int timeoutMs){
    bool finished = false;
    bool succeeded = false;
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    session_->request(action, payload,
                      [&](bool ok, const QString &text, const QJsonObject &body){
        succeeded = ok;
        if (result != nullptr){
            *result = body;
        }
        if (error != nullptr){
            *error = text;
        }
        finished = true;
        loop.quit();
    });
    if (!finished){
        loop.exec();   // 等应答或超时
    }
    return finished && succeeded;
}

void RemoteDataSource::requestSnapshot(){
    if (session_->state() != ServerSession::State::Online){
        return;
    }
    session_->request(QStringLiteral("admin.snapshot"), {},
                      [this](bool ok, const QString &error, const QJsonObject &payload){
        if (!ok){
            setError(error.isEmpty() ? QStringLiteral("获取服务端快照失败") : error);
            return;
        }
        std::string parseError;
        if (!applySnapshot(payload, &parseError)){
            setError(QString::fromStdString(parseError));
            return;
        }
        setError(QString());
        if (onSnapshotRefreshed){
            onSnapshotRefreshed();
        }
    });
}

void RemoteDataSource::requestAnalytics(){
    if (session_->state() != ServerSession::State::Online){
        return;
    }
    session_->request(QStringLiteral("analytics.report"), {},
                      [this](bool ok, const QString &, const QJsonObject &payload){
        if (ok){
            analytics_ = payload;
        }
    });
}

bool RemoteDataSource::applySnapshot(const QJsonObject &snapshot, std::string *error){
    const QJsonObject layoutJson = snapshot.value(QStringLiteral("layout")).toObject();
    const double siteWidth = layoutJson.value(QStringLiteral("siteWidth")).toDouble();
    const double siteHeight = layoutJson.value(QStringLiteral("siteHeight")).toDouble();
    if (siteWidth <= 0.0 || siteHeight <= 0.0){
        if (error != nullptr){
            *error = "服务端快照缺少有效的场地尺寸";
        }
        return false;
    }

    std::vector<Point> entrances;
    for (const QJsonValue &item : layoutJson.value(QStringLiteral("entrances")).toArray()){
        const QJsonObject point = item.toObject();
        entrances.push_back(Point{point.value(QStringLiteral("x")).toDouble(),
                                  point.value(QStringLiteral("y")).toDouble()});
    }
    std::vector<Point> exits;
    for (const QJsonValue &item : layoutJson.value(QStringLiteral("exits")).toArray()){
        const QJsonObject point = item.toObject();
        exits.push_back(Point{point.value(QStringLiteral("x")).toDouble(),
                              point.value(QStringLiteral("y")).toDouble()});
    }
    std::vector<LayoutObstacle> obstacles;
    for (const QJsonValue &item : layoutJson.value(QStringLiteral("obstacles")).toArray()){
        const QJsonObject object = item.toObject();
        obstacles.push_back(LayoutObstacle{
            object.value(QStringLiteral("name")).toString().toStdString(),
            rectangleFrom(object)});
    }
    std::vector<Rectangle> regions;
    for (const QJsonValue &item : layoutJson.value(QStringLiteral("regions")).toArray()){
        regions.push_back(rectangleFrom(item.toObject()));
    }

    std::vector<ParkingSpot> spots;
    for (const QJsonValue &item : snapshot.value(QStringLiteral("spots")).toArray()){
        const QJsonObject object = item.toObject();
        const std::string identifier =
            object.value(QStringLiteral("spotId")).toString().toStdString();
        if (identifier.empty()){
            continue;
        }
        ParkingSpot::Geometry geometry;
        geometry.zone = object.value(QStringLiteral("zone")).toString().toStdString();
        geometry.bounds = rectangleFrom(object);
        geometry.accessPoint = accessPointFor(geometry.bounds);
        geometry.type = spotTypeFrom(object.value(QStringLiteral("type")).toString());

        ParkingSpot spot(identifier, geometry);
        const int status = object.value(QStringLiteral("status")).toInt();
        const std::string plateText =
            object.value(QStringLiteral("plate")).toString().toStdString();
        // 空闲/停用车位没有车牌，Vehicle 不接受空车牌，因此只在有车时才构造。
        if (!plateText.empty()){
            const auto type = protocol::vehicleTypeFromString(
                object.value(QStringLiteral("vehicleType")).toString());
            const Vehicle vehicle(plateText, type.value_or(VehicleType::Car));
            if (status == 1){
                spot.occupy(vehicle);
            } else if (status == 2){
                // 快照不含预留到期时刻（reservationExpiresAt 未序列化），
                // 这里只表达「已被预留」，不编造具体到期时间。
                spot.reserve(vehicle, timeFromMs(0));
            }
        }
        if (status == 3){
            spot.disable();
        }
        spots.push_back(std::move(spot));
    }

    // 停车记录：与服务端 admin.snapshot 的字段一一对应。
    std::vector<ParkingRecord> records;
    for (const QJsonValue &item : snapshot.value(QStringLiteral("records")).toArray()){
        const QJsonObject object = item.toObject();
        const std::string plate =
            object.value(QStringLiteral("plate")).toString().toStdString();
        const std::string spotId =
            object.value(QStringLiteral("spotId")).toString().toStdString();
        if (plate.empty()){
            continue;   // ParkingRecord 不接受空车牌
        }
        const auto type = protocol::vehicleTypeFromString(
            object.value(QStringLiteral("vehicleType")).toString());
        ParkingRecord record(plate, spotId,
            timeFromMs(object.value(QStringLiteral("entryTimeMs")).toInteger()),
            type.value_or(VehicleType::Car));
        if (object.contains(QStringLiteral("exitTimeMs"))){
            record.close(timeFromMs(object.value(QStringLiteral("exitTimeMs")).toInteger()),
                         object.value(QStringLiteral("fee")).toDouble());
        }
        records.push_back(std::move(record));
    }
    records_ = std::move(records);

    std::vector<Booking> bookings;
    for (const QJsonValue &item : snapshot.value(QStringLiteral("bookings")).toArray()){
        const QJsonObject object = item.toObject();
        const std::string plate =
            object.value(QStringLiteral("plate")).toString().toStdString();
        if (plate.empty()){
            continue;
        }
        bookings.emplace_back(
            object.value(QStringLiteral("id")).toString().toStdString(),
            plate,
            object.value(QStringLiteral("spotId")).toString().toStdString(),
            timeFromMs(object.value(QStringLiteral("createdAtMs")).toInteger()),
            timeFromMs(object.value(QStringLiteral("arrivalMs")).toInteger()),
            timeFromMs(object.value(QStringLiteral("deadlineMs")).toInteger()),
            object.value(QStringLiteral("deposit")).toDouble(),
            bookingStatusFromInt(object.value(QStringLiteral("status")).toInt())
                .value_or(BookingStatus::Booked));
    }
    bookings_ = std::move(bookings);
    pendingDeposits_ = snapshot.value(QStringLiteral("pendingDeposits")).toDouble();

    // 时段预约：与服务端 admin.snapshot 的字段一一对应。
    std::vector<smartpark::Reservation> reservations;
    for (const QJsonValue &item : snapshot.value(QStringLiteral("reservations")).toArray()){
        const QJsonObject object = item.toObject();
        const std::string plate =
            object.value(QStringLiteral("plate")).toString().toStdString();
        if (plate.empty()){
            continue;
        }
        const auto type = protocol::vehicleTypeFromString(
            object.value(QStringLiteral("vehicleType")).toString());
        reservations.emplace_back(
            object.value(QStringLiteral("id")).toString().toStdString(),
            plate,
            type.value_or(VehicleType::Car),
            object.value(QStringLiteral("spotId")).toString().toStdString(),
            timeFromMs(object.value(QStringLiteral("createdAtMs")).toInteger()),
            timeFromMs(object.value(QStringLiteral("startMs")).toInteger()),
            timeFromMs(object.value(QStringLiteral("endMs")).toInteger()),
            timeFromMs(object.value(QStringLiteral("graceDeadlineMs")).toInteger()),
            object.value(QStringLiteral("deposit")).toDouble(),
            std::string(),   // chargeTransactionId：管理端不展示
            reservationStatusFromInt(object.value(QStringLiteral("status")).toInt())
                .value_or(ReservationStatus::Confirmed),
            depositStateFromInt(object.value(QStringLiteral("depositState")).toInt())
                .value_or(DepositState::Pending),
            ExpectedRoute{},   // 预期路线：管理端不展示
            object.value(QStringLiteral("accessible")).toBool());
    }
    reservations_ = std::move(reservations);

    const QJsonObject rule = snapshot.value(QStringLiteral("reservationRule")).toObject();
    reservationRule_ = ReservationRuleView{};
    reservationRule_.deposit = rule.value(QStringLiteral("deposit")).toDouble();
    reservationRule_.maxAdvanceDays = rule.value(QStringLiteral("maxAdvanceDays")).toInt();
    reservationRule_.gracePeriodMin = rule.value(QStringLiteral("gracePeriodMin")).toInt();
    forfeitedDeposits_ = snapshot.value(QStringLiteral("forfeitedDeposits")).toDouble();

    layout_ = ParkingLayout::fromParts(siteWidth, siteHeight, std::move(spots),
                                       std::move(regions), std::move(obstacles),
                                       std::move(entrances), std::move(exits));
    spots_ = layout_.spots();
    generatedAtMs_ = snapshot.value(QStringLiteral("generatedAtMs")).toInteger();
    // dailyRevenue 随快照下发：单独存一份，不要塞进 analytics_。
    dailyRevenue_ = snapshot.value(QStringLiteral("dailyRevenue")).toArray();
    snapshotReady_ = true;
    return true;
}

void RemoteDataSource::recognizePlateRemotely(
    const QByteArray &imageBytes, std::function<void(PlateRecognition)> done){
    PlateRecognition outcome;
    if (imageBytes.isEmpty()){
        outcome.error = "图片为空";
        done(outcome);
        return;
    }
    if (imageBytes.size() > 6 * 1024 * 1024){
        outcome.error = "图片超过 6MB，服务端不接受";
        done(outcome);
        return;
    }
    // 识别跑在服务端：客户端不拉模型、不装 Python 环境，只把照片发过去。
    session_->request(
        QStringLiteral("lpr.recognize"),
        QJsonObject{{QStringLiteral("image"),
                     QString::fromLatin1(imageBytes.toBase64())}},
        [done](bool ok, const QString &error, const QJsonObject &body){
        PlateRecognition result;
        if (!ok){
            result.error = error.toStdString();
        } else {
            result.ok = true;
            result.plate =
                body.value(QStringLiteral("plate")).toString().toStdString();
            result.confidence =
                body.value(QStringLiteral("confidence")).toDouble();
            result.backend =
                body.value(QStringLiteral("backend")).toString().toStdString();
        }
        done(result);
    });
}

ParkingInsights RemoteDataSource::insights() const noexcept{
    // 记录与预约都在手里了，洞察可以直接用与本地模式同一个引擎算，
    // 不必再单独走 analytics.report（那份是给人看的文本结论）。
    return ParkingInsightEngine::analyze(spots_, records_, bookings_);
}

std::optional<AllocationResult> RemoteDataSource::enterVehicle(
    const std::string &plate, VehicleType type){
    if (session_->state() != ServerSession::State::Online){
        setError(QStringLiteral("未连接服务端，无法入库"));
        return std::nullopt;
    }
    QJsonObject body;
    QString error;
    const bool ok = awaitRequest(
        QStringLiteral("parking.enter"),
        QJsonObject{{QStringLiteral("plate"), QString::fromStdString(plate)},
                    {QStringLiteral("vehicleType"), protocol::vehicleTypeToString(type)}},
        &body, &error);
    if (!ok){
        setError(error.isEmpty() ? QStringLiteral("服务端拒绝入场") : error);
        return std::nullopt;
    }
    AllocationResult result;
    result.plateNumber = plate;
    result.spotId = body.value(QStringLiteral("spotId")).toString().toStdString();
    // 写操作成功后立刻刷新：界面拿到的是服务端事实，不是本地猜测。
    requestSnapshot();
    return result;
}

std::optional<ParkingRecord> RemoteDataSource::leaveVehicle(const std::string &plate){
    if (session_->state() != ServerSession::State::Online){
        setError(QStringLiteral("未连接服务端，无法出库"));
        return std::nullopt;
    }
    QJsonObject body;
    QString error;
    const bool ok = awaitRequest(QStringLiteral("parking.leave"),
                                 QJsonObject{{QStringLiteral("plate"),
                                              QString::fromStdString(plate)}},
                                 &body, &error);
    if (!ok){
        setError(error.isEmpty() ? QStringLiteral("服务端拒绝出场") : error);
        return std::nullopt;
    }
    requestSnapshot();
    const QString spotId = body.value(QStringLiteral("spotId")).toString();
    const auto now = ParkingRecord::Clock::now();
    try{
        ParkingRecord record(plate, spotId.toStdString(), now);
        record.close(now, body.value(QStringLiteral("fee")).toDouble());
        return record;
    } catch (const std::exception &){
        // 服务端已结算，只是本地记录无法重建：仍算出场成功，由快照刷新体现。
        return ParkingRecord(plate, spotId.toStdString(), now);
    }
}

} // namespace smartpark
