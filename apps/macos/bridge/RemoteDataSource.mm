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

// 协议里的路线：REST 用 {points:[{x,y}],distanceM,turns}，TCP 的
// reservation.create 把它摊平成 entryPoints/entryDistance/entryTurns。
// 两种形状都认，缺字段按 0 处理（老服务端）。
Route routeFromPoints(const QJsonArray &points, double distance, int turns){
    Route route;
    route.distance = distance;
    route.turnCount = turns;
    for (const QJsonValue &item : points){
        const QJsonObject point = item.toObject();
        route.points.push_back(Point{point.value(QStringLiteral("x")).toDouble(),
                                     point.value(QStringLiteral("y")).toDouble()});
    }
    return route;
}

Route routeFromObject(const QJsonValue &value){
    const QJsonObject object = value.toObject();
    return routeFromPoints(object.value(QStringLiteral("points")).toArray(),
                           object.value(QStringLiteral("distanceM")).toDouble(),
                           object.value(QStringLiteral("turns")).toInt());
}

// 预约订单解析：admin.snapshot 的 reservations[] 与 reservation.create /
// reservation.checkin 的应答用同一套字段名，共用这一个解析器。
std::optional<Reservation> reservationFromJson(const QJsonObject &object){
    const std::string plate =
        object.value(QStringLiteral("plate")).toString().toStdString();
    if (plate.empty()){
        return std::nullopt;
    }
    const auto type = protocol::vehicleTypeFromString(
        object.value(QStringLiteral("vehicleType")).toString());
    return Reservation(
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
        ExpectedRoute{},   // 预期路线在管理端只按 create 应答展示，不入快照模型
        object.value(QStringLiteral("accessible")).toBool());
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
        if (auto parsed = reservationFromJson(item.toObject())){
            reservations.push_back(std::move(*parsed));
        }
    }
    reservations_ = std::move(reservations);

    const QJsonObject rule = snapshot.value(QStringLiteral("reservationRule")).toObject();
    reservationRule_ = ReservationRuleView{};
    reservationRule_.deposit = rule.value(QStringLiteral("deposit")).toDouble();
    reservationRule_.maxAdvanceDays = rule.value(QStringLiteral("maxAdvanceDays")).toInt();
    reservationRule_.gracePeriodMin = rule.value(QStringLiteral("gracePeriodMin")).toInt();
    // 提前量/最短时长/到场窗口在旧服务端快照里没有；缺失时用核心的默认值，
    // 让表单至少不至于把「至少提前 0 分钟」当成规则。
    reservationRule_.minLeadTimeMin =
        rule.value(QStringLiteral("minLeadTimeMin")).toInt(
            static_cast<int>(ReservationRule{}.minLeadTime.count()));
    reservationRule_.minDurationMin =
        rule.value(QStringLiteral("minDurationMin")).toInt(
            static_cast<int>(ReservationRule{}.minDuration.count()));
    reservationRule_.lockLeadTimeMin =
        rule.value(QStringLiteral("lockLeadTimeMin")).toInt(
            static_cast<int>(ReservationRule{}.lockLeadTime.count()));
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
    // 服务端把这次分配规划出的路线一并回传：车位图据此画进场/出场路线
    // （老服务端只回距离与转弯数，那就没有折线可画，界面不会假装有）。
    result.entryRoute = routeFromPoints(
        body.value(QStringLiteral("entryPoints")).toArray(),
        body.value(QStringLiteral("entryDistance")).toDouble(),
        body.value(QStringLiteral("entryTurns")).toInt());
    result.exitRoute = routeFromPoints(
        body.value(QStringLiteral("exitPoints")).toArray(),
        body.value(QStringLiteral("exitDistance")).toDouble(),
        body.value(QStringLiteral("exitTurns")).toInt());
    result.entranceIndex = static_cast<std::size_t>(
        body.value(QStringLiteral("entranceIndex")).toInt());
    result.exitIndex = static_cast<std::size_t>(
        body.value(QStringLiteral("exitIndex")).toInt());
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

// 旧服务端的 reservation.checkin 只回 spotId：到场后的订单状态要从快照缓存里补。
// 同一车牌可能有多笔订单（历史已完成/已取消 + 本次到场的），必须挑「刚刚到场的
// 那一笔」——车位号一致、状态已是 CheckedIn 或仍待到场、开始时间最晚的那一笔。
// 只按车牌取第一笔会在用过的库上拿到几轮前的旧订单：实测同一车牌第二次预约
// 到场时，回给界面的是上一轮的 Completed，状态断言直接失败。
const Reservation *RemoteDataSource::findCheckInCandidate(
    const std::string &plate, const std::string &spotId) const{
    const Reservation *best = nullptr;
    int bestRank = -1;
    for (const Reservation &reservation : reservations_){
        if (reservation.plateNumber() != plate){
            continue;
        }
        if (!spotId.empty() && reservation.spotId() != spotId){
            continue;
        }
        // CheckedIn 说明快照已经刷新过（比缓存新），其次才是等待到场的订单；
        // 已结束（Completed/Cancelled/NoShow/Expired）的一律不是本次到场的那笔。
        int rank = -1;
        switch (reservation.status()){
        case ReservationStatus::CheckedIn:      rank = 2; break;
        case ReservationStatus::Confirmed:      rank = 1; break;
        case ReservationStatus::PendingPayment: rank = 0; break;
        default:                                rank = -1; break;
        }
        if (rank < 0){
            continue;
        }
        if (rank > bestRank
            || (rank == bestRank && best != nullptr
                && reservation.startTime() > best->startTime())){
            best = &reservation;
            bestRank = rank;
        }
    }
    return best;
}

std::optional<ParkingDataSource::ReservationCreation>
RemoteDataSource::createReservation(const std::string &plate, VehicleType type,
                                    ParkingRecord::TimePoint start,
                                    std::chrono::minutes duration,
                                    bool accessible){
    if (session_->state() != ServerSession::State::Online){
        setError(QStringLiteral("未连接服务端，无法创建预约"));
        return std::nullopt;
    }
    const auto startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        start.time_since_epoch()).count();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        duration).count();
    QJsonObject body;
    QString error;
    // 选位、冲突检查、定金收取与路线规划都在服务端完成，这里只提交表单。
    const bool ok = awaitRequest(
        QStringLiteral("reservation.create"),
        QJsonObject{{QStringLiteral("plate"), QString::fromStdString(plate)},
                    {QStringLiteral("vehicleType"),
                     protocol::vehicleTypeToString(type)},
                    {QStringLiteral("startMs"), startMs},
                    {QStringLiteral("durationMin"),
                     static_cast<int>(duration.count())},
                    {QStringLiteral("accessible"), accessible}},
        &body, &error);
    if (!ok){
        setError(error.isEmpty() ? QStringLiteral("服务端拒绝创建预约") : error);
        return std::nullopt;
    }
    const std::string spotId =
        body.value(QStringLiteral("spotId")).toString().toStdString();
    if (spotId.empty()){
        // 应答形状不对就说清楚，别让界面显示一条没有车位的「成功」。
        setError(QStringLiteral("服务端应答缺车位号，预约结果无法确认"));
        return std::nullopt;
    }
    // endMs / graceDeadlineMs 由服务端下发；连的是旧服务端时按请求参数与
    // 快照规则补算（此时只是本地估算，随后的快照刷新会覆盖成服务端事实）。
    const qint64 endMs = body.contains(QStringLiteral("endMs"))
        ? body.value(QStringLiteral("endMs")).toInteger()
        : startMs + durationMs;
    const qint64 graceMs = body.contains(QStringLiteral("graceDeadlineMs"))
        ? body.value(QStringLiteral("graceDeadlineMs")).toInteger()
        : startMs + static_cast<qint64>(reservationRule_.gracePeriodMin) * 60 * 1000;
    const auto parsed = reservationFromJson(body);
    const Reservation reservation = parsed.has_value()
        ? *parsed
        : Reservation(body.value(QStringLiteral("reservationId")).toString().toStdString(),
                      plate, type, spotId, ParkingRecord::Clock::now(),
                      timeFromMs(startMs), timeFromMs(endMs), timeFromMs(graceMs),
                      body.value(QStringLiteral("deposit")).toDouble(), std::string(),
                      ReservationStatus::Confirmed, DepositState::Pending,
                      ExpectedRoute{}, accessible);

    ReservationCreation creation(reservation);
    creation.entryRoute = routeFromPoints(body.value(QStringLiteral("entryPoints")).toArray(),
                                          body.value(QStringLiteral("entryDistance")).toDouble(),
                                          body.value(QStringLiteral("entryTurns")).toInt());
    creation.exitRoute = routeFromPoints(body.value(QStringLiteral("exitPoints")).toArray(),
                                         body.value(QStringLiteral("exitDistance")).toDouble(),
                                         body.value(QStringLiteral("exitTurns")).toInt());
    // REST 形状（{points,distanceM,turns}）也认：两条入口共用同一段解析。
    if (body.contains(QStringLiteral("entryRoute"))){
        creation.entryRoute = routeFromObject(body.value(QStringLiteral("entryRoute")));
    }
    if (body.contains(QStringLiteral("exitRoute"))){
        creation.exitRoute = routeFromObject(body.value(QStringLiteral("exitRoute")));
    }
    creation.entranceIndex = static_cast<std::size_t>(
        body.value(QStringLiteral("entranceIndex")).toInt());
    creation.exitIndex = static_cast<std::size_t>(
        body.value(QStringLiteral("exitIndex")).toInt());
    setError(QString());
    // 写操作成功后立刻刷新：界面拿到的是服务端事实，不是本地猜测。
    requestSnapshot();
    return creation;
}

std::optional<Reservation> RemoteDataSource::checkInReservation(
    const std::string &plate){
    if (session_->state() != ServerSession::State::Online){
        setError(QStringLiteral("未连接服务端，无法确认到场"));
        return std::nullopt;
    }
    QJsonObject body;
    QString error;
    const bool ok = awaitRequest(QStringLiteral("reservation.checkin"),
                                 QJsonObject{{QStringLiteral("plate"),
                                              QString::fromStdString(plate)}},
                                 &body, &error);
    if (!ok){
        setError(error.isEmpty() ? QStringLiteral("服务端拒绝到场确认") : error);
        return std::nullopt;
    }
    setError(QString());
    // 到场后车位被占用并生成停车记录：快照刷新让车位图与记录页同步。
    requestSnapshot();
    // 应答里带的订单字段优先（新服务端）；否则用快照缓存里的订单改状态。
    const std::string arrivedSpot =
        body.value(QStringLiteral("spotId")).toString().toStdString();
    if (auto parsed = reservationFromJson(body)){
        return parsed;
    }
    if (const Reservation *cached = findCheckInCandidate(plate, arrivedSpot)){
        Reservation arrived = *cached;
        arrived.checkIn();   // 已经是 CheckedIn 时该调用无副作用
        return arrived;
    }
    // 连旧服务端且缓存里也没有（订单是别的客户端建的）：只有车牌与车位是
    // 服务端给的，时间留空由下一次快照刷新补全。
    const auto now = ParkingRecord::Clock::now();
    return Reservation(std::string(), plate, VehicleType::Car, arrivedSpot,
                       now, now, now, now, 0.0, std::string(),
                       ReservationStatus::CheckedIn, DepositState::Pending,
                       ExpectedRoute{}, false);
}

bool RemoteDataSource::cancelReservation(const std::string &plate){
    if (session_->state() != ServerSession::State::Online){
        setError(QStringLiteral("未连接服务端，无法取消预约"));
        return false;
    }
    QJsonObject body;
    QString error;
    const bool ok = awaitRequest(QStringLiteral("reservation.cancel"),
                                 QJsonObject{{QStringLiteral("plate"),
                                              QString::fromStdString(plate)}},
                                 &body, &error);
    if (!ok){
        setError(error.isEmpty() ? QStringLiteral("服务端拒绝取消预约") : error);
        return false;
    }
    setError(QString());
    requestSnapshot();
    return true;
}

} // namespace smartpark
