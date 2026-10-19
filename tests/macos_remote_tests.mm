// macOS 管理端「远程模式」端到端测试。
//
// 默认自带一个进程内服务端（真实 SmartParkTcpServer + 真实 SQLite），把管理端按远程
// 方式接上去，然后验证两件事：
//   1. 数据层——快照里的车位/记录/预约/定金/洞察都到位；
//   2. 界面层——远程模式下该显示的页面确实显示了，且渲染出服务端的数据。
//
// 也可以指向一台真实服务端（部署验证用）：
//   smartpark_macos_remote_tests --remote <host> <port> <user> [password]
//
// 临时夹具教训：这套断言原先散落在 /tmp 的一次性脚本里，被系统清理后无法重建，
// 所以收进仓库。
#include "bridge/ParkingBridge.h"

#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/ParkingService.h"
#include "core/service/ReservationService.h"
#include "core/service/UserStore.h"
#include "network/SmartParkTcpServer.h"
#include "network/TcpClient.h"

#import "LoginViewController.h"
#import "OperationsViewController.h"
#import "LocalRecognitionService.h"
#import "PlatePhotoArchive.h"
#import "TableView.h"
#import "MainWindowController.h"
#import "TextUtil.h"   // smartpark_ui::toNSString：中文安全的 QString -> NSString

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

// sidebar 是 id，编译期不知道 selectIndex:，补个声明让发送合法。
@interface NSObject (SmartParkSidebarShim)
- (void)selectIndex:(NSInteger)index;
@end

// 预约管理页的按钮只在 .mm 里实现（不在头文件里），补声明以便直接「点按钮」。
@interface NSObject (SmartParkBookingShim)
- (void)createReservation:(id)sender;
- (void)checkIn:(id)sender;
- (void)cancel:(id)sender;
@end

@interface OperationsViewController (SmartParkRecognitionTest)
- (void)runRecognitionForImage:(NSString *)imagePath;
- (void)cancelRecognition:(id)sender;
- (void)stopLocalRecognitionService:(NSNotification *)notification;
- (NSString *)applyNewEnergyVehicleTypeForResult:(NSDictionary *)result;
- (void)performRecognitionIntentWithPlate:(NSString *)plate result:(NSDictionary *)result;
- (NSString *)localRecognitionSocketForScript:(NSString *)script
                              detectorPython:(NSString *)detectorPython
                                   ocrPython:(NSString *)ocrPython
                                 environment:(NSDictionary<NSString *, NSString *> *)environment;
@end

@interface LocalRecognitionProbe : OperationsViewController
@property (nonatomic, strong) NSDictionary *candidate;
@property (nonatomic, copy) NSString *failure;
@property (nonatomic, copy) NSString *residentFixture;
@end
@implementation LocalRecognitionProbe
- (NSString *)localRecognitionSocketForScript:(NSString *)script
                              detectorPython:(NSString *)detectorPython
                                   ocrPython:(NSString *)ocrPython
                                 environment:(NSDictionary<NSString *, NSString *> *)environment{
    return [super localRecognitionSocketForScript:self.residentFixture ?: script
        detectorPython:detectorPython ocrPython:ocrPython environment:environment];
}
- (void)handleRecognitionOutput:(NSString *)output error:(NSString *)error exitCode:(int)exitCode{
    if (exitCode != 0){
        self.failure = error;
    } else {
        self.candidate = [NSJSONSerialization JSONObjectWithData:
            [output dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
    }
}
@end

static int failures = 0;

static void expect(bool ok, const std::string &label){
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label.c_str());
    if (!ok){
        ++failures;
    }
}

// 驱动 Qt 事件循环：远程连接与快照都是异步的，不泵事件就永远等不到。
static bool spin(const std::function<bool()> &done, int timeoutMs){
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < timeoutMs){
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

static void collectTextFields(NSView *view, NSMutableArray<NSString *> *out){
    if ([view isKindOfClass:[NSTextField class]]){
        NSString *text = [(NSTextField *)view stringValue];
        if (text.length > 0){
            [out addObject:text];
        }
    }
    for (NSView *child in view.subviews){
        collectTextFields(child, out);
    }
}

// 直接查服务端那本 SQLite：验证「入库」不是只在内存里。
static int sqlInt(QSqlDatabase &database, const QString &sql){
    QSqlQuery query(database);
    if (!query.exec(sql)){
        std::printf("       SQL 失败：%s（%s）\n", sql.toUtf8().constData(),
                    query.lastError().text().toUtf8().constData());
        return -1;
    }
    return query.next() ? query.value(0).toInt() : -2;
}

static bool viewContainsText(NSView *root, NSString *needle){
    NSMutableArray<NSString *> *texts = [NSMutableArray array];
    collectTextFields(root, texts);
    for (NSString *text in texts){
        if ([text containsString:needle]){
            return true;
        }
    }
    return false;
}

// ParkingBridge 是 C++ 类，@property(assign) 存的是裸指针，KVC 不保证能装箱，
// 直接按 ivar 偏移读更可靠。
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-bridge-casts-disallowed-in-nonarc"

static void *readPointerIvar(id object, const char *name){
    Ivar ivar = class_getInstanceVariable([object class], name);
    if (ivar == nullptr){
        return nullptr;
    }
    return *(void **)((char *)(__bridge void *)object + ivar_getOffset(ivar));
}

// 私有枚举/整数 ivar（如 _recognitionIntent）用同一个偏移技巧写入和读出。
static void writeIntegerIvar(id object, const char *name, long long value){
    Ivar ivar = class_getInstanceVariable([object class], name);
    if (ivar == nullptr){
        return;
    }
    void *slot = (char *)(__bridge void *)object + ivar_getOffset(ivar);
    switch (ivar_getTypeEncoding(ivar)[0]){
    case 'c': *(signed char *)slot = (signed char)value; break;
    case 's': *(short *)slot = (short)value; break;
    case 'i': *(int *)slot = (int)value; break;
    case 'l': *(long *)slot = (long)value; break;
    case 'q': *(long long *)slot = value; break;
    default: break;
    }
}

static long long readIntegerIvar(id object, const char *name){
    Ivar ivar = class_getInstanceVariable([object class], name);
    if (ivar == nullptr){
        return 0;
    }
    void *slot = (char *)(__bridge void *)object + ivar_getOffset(ivar);
    switch (ivar_getTypeEncoding(ivar)[0]){
    case 'c': return *(signed char *)slot;
    case 's': return *(short *)slot;
    case 'i': return *(int *)slot;
    case 'l': return *(long *)slot;
    case 'q': return *(long long *)slot;
    default: return 0;
    }
}

#pragma clang diagnostic pop

int main(int argc, char **argv){
    setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    // ---- 可选的 --remote：连一台真实服务端，而不是自带服务端 ----
    QString remoteHost;
    quint16 remotePort = 0;
    QString remoteUser = QStringLiteral("admin");
    QString remotePassword = QStringLiteral("smartpark");
    for (int i = 1; i < argc; ++i){
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--remote") && i + 2 < argc){
            remoteHost = QString::fromLocal8Bit(argv[i + 1]);
            remotePort = static_cast<quint16>(QString::fromLocal8Bit(argv[i + 2]).toUShort());
            if (i + 3 < argc){
                remoteUser = QString::fromLocal8Bit(argv[i + 3]);
            }
            if (i + 4 < argc){
                remotePassword = QString::fromLocal8Bit(argv[i + 4]);
            }
            break;
        }
    }
    const bool external = !remoteHost.isEmpty();

    std::unique_ptr<QTemporaryDir> tempDir;
    std::unique_ptr<smartpark::Persistence> persistence;
    std::unique_ptr<smartpark::AuditLogService> audit;
    std::unique_ptr<smartpark::UserStore> users;
    std::unique_ptr<smartpark::ParkingService> service;
    std::unique_ptr<smartpark::SmartParkTcpServer> server;
    long expectedSpots = 0;

    if (external){
        std::printf("连接外部服务端 %s:%u（用户 %s）\n\n",
                    remoteHost.toUtf8().constData(), remotePort,
                    remoteUser.toUtf8().constData());
    } else {
        tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()){
            std::printf("[FAIL] 无法创建临时目录\n");
            return 1;
        }
        persistence = std::make_unique<smartpark::Persistence>(
            tempDir->filePath(QStringLiteral("remote.db")));
        audit = std::make_unique<smartpark::AuditLogService>(
            persistence->databaseManager().database());
        users = std::make_unique<smartpark::UserStore>(
            tempDir->filePath(QStringLiteral("users.db")));
        service = std::make_unique<smartpark::ParkingService>(
            smartpark::ParkingLayout::garageLayout(),
            smartpark::AllocationStrategy::WeightedCost, &persistence->repository());

        // 造一份有区分度的数据：两辆车在场、一笔预约，远程端应当原样看到。
        const auto now = smartpark::ParkingRecord::Clock::now();
        service->enter(smartpark::Vehicle("京A10001", smartpark::VehicleType::Car));
        service->enter(smartpark::Vehicle("京A10002", smartpark::VehicleType::Truck),
                       now - std::chrono::hours(3));
        service->createBooking(
            smartpark::Vehicle("京A10003", smartpark::VehicleType::Electric),
            now + std::chrono::hours(2), now);
        // 时段预约（Reservation）：网页 H5 创建的是这一种，和上面的 Booking 并存。
        // 管理端原先只显示 Booking，网页上建的预约完全看不到。
        service->reservations().create(
            {std::string("京A20001"), smartpark::VehicleType::Car},
            now + std::chrono::minutes(45),
            now + std::chrono::minutes(105), now, false);

        smartpark::SmartParkTcpServer::Options options;
        options.port = 0;   // 让内核挑一个空闲端口
        options.heartbeatTimeoutMs = 600000;
        server = std::make_unique<smartpark::SmartParkTcpServer>(
            *service, audit.get(), users.get(), options);
        if (!server->listen()){
            std::printf("[FAIL] 进程内服务端监听失败\n");
            return 1;
        }
        remoteHost = QStringLiteral("127.0.0.1");
        remotePort = server->port();
        expectedSpots = static_cast<long>(service->spots().size());
        std::printf("自带服务端 127.0.0.1:%u（车位 %ld，记录 %zu，预约 %zu）\n\n",
                    remotePort, expectedSpots, service->records().size(),
                    service->bookings().size());
    }

    @autoreleasepool{
        [NSApplication sharedApplication];

        MainWindowController *main = [[MainWindowController alloc]
            initWithUserName:smartpark_ui::toNSString(remoteUser.toStdString())];
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 1280, 860)
                      styleMask:NSWindowStyleMaskTitled
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.contentViewController = main.contentViewController;

        QElapsedTimer clock;
        clock.start();
        [main connectToRemoteHost:smartpark_ui::toNSString(remoteHost.toStdString())
                             port:remotePort
                             user:smartpark_ui::toNSString(remoteUser.toStdString())
                         password:smartpark_ui::toNSString(remotePassword.toStdString())];
        const bool ready = spin([&]{ return [main isDatabaseReady]; }, 15000);
        std::printf("       连接耗时 %lld ms，状态：%s\n", (long long)clock.elapsed(),
                    [[main remoteStatusText] UTF8String] ?: "(空)");

        expect(ready, "远程连接建立并拿到快照");
        expect([main isRemote], "当前处于远程模式");
        if (!ready){
            std::printf("\nFAILURES (%d)\n", failures);
            return 1;
        }

        // ---- 数据层 ----
        NSArray *pages = [main valueForKey:@"pages"];
        expect(pages.count == 7, "七个页面均已创建");

        ParkingBridge *bridge = nullptr;
        for (id page in pages){
            bridge = (ParkingBridge *)readPointerIvar(page, "_bridge");
            if (bridge == nullptr){
                bridge = (ParkingBridge *)readPointerIvar(page, "bridge");
            }
            if (bridge != nullptr){
                break;
            }
        }
        expect(bridge != nullptr, "取到页面持有的 bridge");
        if (bridge == nullptr){
            std::printf("\nFAILURES (%d)\n", failures);
            return 1;
        }

        if (!external){
            expect(static_cast<long>(bridge->totalSpots()) == expectedSpots,
                   "车位总数与服务端一致");
            expect(bridge->occupiedSpots() == 2, "占用数来自服务端（2 辆在场）");
            expect(bridge->records().size() == 2, "停车记录随快照下发（2 条）");
            expect(bridge->bookings().size() == 1, "预约随快照下发（1 笔）");
            expect(bridge->reservations().size() == 1,
                   "时段预约随快照下发（1 笔，网页端创建的那种）");
            expect(bridge->pendingDeposits() > 0.0, "待结算定金非零");
        } else {
            expect(bridge->totalSpots() > 0, "车位总数来自服务端");
        }

        // 0.7 创建即锁位（WIP 新语义）：预约一创建车位就锁住，不再有
        // 「已预约未锁位」的窗口，pendingReservations 因此为空；预约车位在
        // 快照里直接是 Reserved，车位图/当前车位页按车位状态显示「已预约」。
        if (!external){
            const auto pending = bridge->pendingReservations();
            std::printf("       未锁位的预约：%zu 笔\n", pending.size());
            expect(pending.empty(),
                   "创建即锁位后没有未锁位的预约（pendingReservations 为空）");
            bool reservationSpotLocked = false;
            if (!bridge->reservations().empty()){
                const std::string reservedSpotId = bridge->reservations().front().spotId();
                for (const smartpark::ParkingSpot &spot : bridge->spots()){
                    if (spot.identifier() == reservedSpotId
                        && spot.status() == smartpark::SpotStatus::Reserved){
                        reservationSpotLocked = true;
                        break;
                    }
                }
            }
            expect(reservationSpotLocked,
                   "网页预约创建后车位在快照里已锁位（Reserved）");
        }

        // 这几项原先在远程模式下全是 false，页面与卡片因此被隐藏。
        const auto caps = bridge->capabilities();
        expect(caps.records && caps.bookings && caps.insights && caps.deposits,
               "记录/预约/洞察/定金能力与本地一致");
        expect(!caps.layoutEditing,
               "布局编辑仍禁用（布局由服务端 --layout 决定）");

        const auto insights = bridge->insights();
        std::printf("       洞察：有效容量 %d，当前占用 %d，预测 %zu 条，分区 %zu 条\n",
                    insights.effectiveCapacity, insights.currentOccupied,
                    insights.forecasts.size(), insights.zones.size());
        expect(insights.effectiveCapacity > 0, "洞察有效容量非零");
        expect(!insights.zones.empty(), "分区压力已算出");

        // ---- 写路径：创建预约 → 到场确认 → 记录与车位图 ----
        // 这一段对应用户实测的「远程服务器模式」：以前 RemoteDataSource 没有
        // 实现预约写操作，三个按钮静默返回 nullopt —— 预约没有路线、到场确认
        // 入不了库、记录与车位图当然也不会变。现在每一步都要求落到服务端库里。
        //
        // 指向外部服务端时默认只读（对方可能是生产库）；要用真实部署验证写路径
        // 时显式打开：SMARTPARK_REMOTE_WRITE_TEST=1 smartpark_macos_remote_tests --remote ...
        const bool writeChecks = !external
            || qEnvironmentVariableIsSet("SMARTPARK_REMOTE_WRITE_TEST");
        const QString writePlate = external ? QStringLiteral("京A90001")
                                                : QStringLiteral("京A30001");
        // 表单按钮那一条链路单独用一个车牌：写路径失败时不会连带把表单那条也带红，
        // 定位失败阶段更容易。清理阶段（外部服务端）也要用到，所以在这里声明。
        NSString *uiPlate = smartpark_ui::toNSString(
            (external ? QStringLiteral("京A90004")
                      : QStringLiteral("京A30004")).toStdString());
        if (writeChecks){
            const auto rule = bridge->reservationRule();
            std::printf("       预约规则：定金 %.2f，至少提前 %d 分钟，最短 %d 分钟，"
                        "最多提前 %d 天，到场窗口 开始前 %d 分钟起\n",
                        rule.deposit, rule.minLeadTimeMin, rule.minDurationMin,
                        rule.maxAdvanceDays, rule.lockLeadTimeMin);
            expect(rule.minLeadTimeMin > 0 && rule.minDurationMin > 0
                       && rule.lockLeadTimeMin > 0 && rule.deposit > 0.0,
                   "预约规则随快照下发（表单不再用本地默认策略）");

            // 开始时间取「最短提前量 + 3 秒」：满足提前量即可，到场窗口现在从创建时打开，无需等待。
            const auto nowWrite = smartpark::ParkingRecord::Clock::now();
            const auto startWrite = nowWrite
                + std::chrono::minutes(rule.minLeadTimeMin) + std::chrono::seconds(3);
            const auto spotIdWrite = [&]() -> std::string{
                auto created = bridge->createReservation(
                    writePlate.toStdString(), smartpark::VehicleType::Car, startWrite,
                    std::chrono::minutes(120), false);
                expect(created.has_value(),
                       "远程模式「创建预约」成功（不再静默返回空）");
                if (!created){
                    std::printf("       createReservation 失败：%s\n",
                                bridge->lastError().c_str());
                    return {};
                }
                expect(!created->reservation.spotId().empty(),
                       "预约带上服务端分配的车位");
                expect(!created->entryRoute.points.empty()
                           && created->entryRoute.distance > 0.0,
                       "预约带上规划好的预期入场路线（路线规划真的跑了）");
                std::printf("       预约 %s → 车位 %s：入场 %.1f 米 / %d 转弯，"
                            "出场 %.1f 米 / %d 转弯\n",
                            writePlate.toUtf8().constData(),
                            created->reservation.spotId().c_str(),
                            created->entryRoute.distance, created->entryRoute.turnCount,
                            created->exitRoute.distance, created->exitRoute.turnCount);
                return created->reservation.spotId();
            }();

            if (!spotIdWrite.empty()){
                // 快照刷新后管理端能看到这笔预约（「预约管理」页的数据源）。
                const bool inSnapshot = spin([&]{
                    for (const smartpark::Reservation &item : bridge->reservations()){
                        if (item.plateNumber() == writePlate.toStdString()){
                            return true;
                        }
                    }
                    return false;
                }, 5000);
                expect(inSnapshot, "新预约随快照出现在管理端（预约管理页）");

                // 服务端库里的确写了这笔订单，不是只在客户端内存里。
                if (!external){
                    const int rows = sqlInt(
                        persistence->databaseManager().database(),
                        QStringLiteral("SELECT COUNT(*) FROM reservations "
                                       "WHERE plate_number='%1'").arg(writePlate));
                    expect(rows == 1, "预约已入库（SQLite reservations 表）");
                }

                // 等到场窗口打开（服务端按真实时间判定），再点「到场确认」。
                spin([]{ return false; }, 4000);
                auto arrived = bridge->checkInReservation(writePlate.toStdString());
                expect(arrived.has_value(), "远程模式「到场确认」成功（车辆入库）");
                if (!arrived){
                    std::printf("       checkInReservation 失败：%s\n",
                                bridge->lastError().c_str());
                } else {
                    expect(arrived->status() == smartpark::ReservationStatus::CheckedIn,
                           "到场后订单状态为已到场");
                    expect(arrived->spotId() == spotIdWrite,
                           "到场占用的是预约时分配的车位");

                    // 车位图的数据源：该车位转为占用。
                    const bool occupied = spin([&]{
                        for (const smartpark::ParkingSpot &spot : bridge->spots()){
                            if (spot.identifier() == spotIdWrite){
                                return spot.status() == smartpark::SpotStatus::Occupied;
                            }
                        }
                        return false;
                    }, 5000);
                    expect(occupied, "到场后车位图里该车位已转为占用");

                    // 记录页的数据源：多了一条未离场的停车记录。
                    const bool recorded = spin([&]{
                        for (const smartpark::ParkingRecord &record : bridge->records()){
                            if (record.plateNumber() == writePlate.toStdString()
                                && !record.isClosed()){
                                return true;
                            }
                        }
                        return false;
                    }, 5000);
                    expect(recorded, "到场后停车记录里出现该车（未离场）");
                    if (!external){
                        const int rows = sqlInt(
                            persistence->databaseManager().database(),
                            QStringLiteral("SELECT COUNT(*) FROM parking_records "
                                           "WHERE plate_number='%1'").arg(writePlate));
                        expect(rows == 1, "停车记录已入库（SQLite parking_records 表）");
                    }
                }
            }

            // 取消路径：另建一笔再取消，定金应退回（订单不再处于未结束状态）。
            const QString cancelPlate = external ? QStringLiteral("京A90002")
                                                 : QStringLiteral("京A30002");
            auto cancelCreated = bridge->createReservation(
                cancelPlate.toStdString(), smartpark::VehicleType::Car,
                smartpark::ParkingRecord::Clock::now()
                    + std::chrono::minutes(rule.minLeadTimeMin)
                    + std::chrono::minutes(10),
                std::chrono::minutes(60), false);
            expect(cancelCreated.has_value(), "第二笔预约创建成功（用于取消路径）");
            if (cancelCreated){
                expect(bridge->cancelReservation(cancelPlate.toStdString()),
                       "远程模式「取消预约」成功");
                const bool cancelled = spin([&]{
                    for (const smartpark::Reservation &item : bridge->reservations()){
                        if (item.plateNumber() == cancelPlate.toStdString()){
                            return item.status() == smartpark::ReservationStatus::Cancelled;
                        }
                    }
                    return false;
                }, 5000);
                expect(cancelled, "取消后订单状态变为已取消（定金退回）");
                if (!external){
                    const int status = sqlInt(
                        persistence->databaseManager().database(),
                        QStringLiteral("SELECT status FROM reservations "
                                       "WHERE plate_number='%1'").arg(cancelPlate));
                    expect(status == smartpark::reservationStatusToInt(
                                        smartpark::ReservationStatus::Cancelled),
                           "取消结果已入库（SQLite reservations 表）");
                }
            }

            // 失败也要说实话：提前量不足时必须返回空并给出原因，不能假装成功。
            const QString rejectPlate = external ? QStringLiteral("京A90003")
                                                 : QStringLiteral("京A30003");
            auto rejected = bridge->createReservation(
                rejectPlate.toStdString(), smartpark::VehicleType::Car,
                smartpark::ParkingRecord::Clock::now() + std::chrono::minutes(1),
                std::chrono::minutes(60), false);
            expect(!rejected.has_value(), "提前量不足的预约被拒绝（不假装成功）");
            expect(!bridge->lastError().empty(), "拒绝时给出可读原因");
            std::printf("       提前量不足的拒绝原因：%s\n", bridge->lastError().c_str());
        } else {
            std::printf("       （外部服务端，跳过写路径检查；"
                        "SMARTPARK_REMOTE_WRITE_TEST=1 可开启）\n");
        }

        // ---- 跨端同步：别的端（网页 H5 / 用户端 / 闸机）建的预约要自动出现 ----
        // 管理端不轮询：靠服务端广播事件触发全量快照重拉。这条链路一断，表现就是
        // 「我在别处预约了，管理端记录没有同步」。这里用一个独立的 TCP 客户端
        // （模拟网页/用户端）建预约，再要求管理端自己把它捞回来。
        if (writeChecks){
            const QString crossPlate = external ? QStringLiteral("京A90005")
                                                : QStringLiteral("京A30005");
            smartpark::TcpClient other;
            const bool otherConnected = other.connectToHost(remoteHost, remotePort);
            expect(otherConnected, "第二个客户端（模拟网页/用户端）连上服务端");
            if (otherConnected){
                const auto otherLogin = other.request(
                    QStringLiteral("login"),
                    QJsonObject{{QStringLiteral("user"), remoteUser},
                                {QStringLiteral("pass"), remotePassword}});
                const bool otherLoggedIn = otherLogin.has_value()
                    && otherLogin->value(QStringLiteral("ok")).toBool();
                expect(otherLoggedIn, "第二个客户端登录成功");
                if (otherLoggedIn){
                    other.setToken(otherLogin->value(QStringLiteral("payload"))
                                       .toObject()
                                       .value(QStringLiteral("token")).toString());
                    const qint64 startMs = QDateTime::currentMSecsSinceEpoch()
                        + (bridge->reservationRule().minLeadTimeMin + 20) * 60 * 1000;
                    const auto created = other.request(
                        QStringLiteral("reservation.create"),
                        QJsonObject{{QStringLiteral("plate"), crossPlate},
                                    {QStringLiteral("vehicleType"), QStringLiteral("car")},
                                    {QStringLiteral("startMs"), startMs},
                                    {QStringLiteral("durationMin"), 60}});
                    expect(created.has_value()
                               && created->value(QStringLiteral("ok")).toBool(),
                           "第二个客户端创建预约成功");
                    // 管理端应当收到 reservation.created → 重拉快照 → 表格出现该行。
                    const bool synced = spin([&]{
                        for (const smartpark::Reservation &item : bridge->reservations()){
                            if (item.plateNumber() == crossPlate.toStdString()){
                                return true;
                            }
                        }
                        return false;
                    }, 8000);
                    expect(synced, "别处创建的预约自动同步到管理端（事件 → 快照 → 页面）");
                    other.disconnectFromHost();
                }
            }
        }

        // ---- 界面层 ----
        id sidebar = [main valueForKey:@"sidebar"];
        expect(sidebar != nil, "取到侧边栏");
        NSArray *visible = [sidebar valueForKey:@"visiblePageIndices"];
        expect([visible containsObject:@4], "「预约管理」页在远程模式下可见");
        expect([visible containsObject:@5], "「停车记录」页在远程模式下可见");
        expect(![visible containsObject:@6], "「设施配置」页仍隐藏");

        struct PageCheck{ NSInteger index; const char *name; NSString *needle; };
        // 自带服务端时数据是自己造的，可以断言到具体车牌；指向外部服务端时
        // 对方可能是个空库，只能断言页面框架渲染出来了。
        NSString *recordsNeedle = external ? @"停车记录" : @"京A1000";
        // 断言到具体预约车牌，光查标题的话空列表也会「通过」。
        NSString *bookingsNeedle = external ? @"预约" : @"京A10003";
        // 网页预约写的是 Reservation，这一条断言的是那个模型真的显示出来了。
        NSString *reservationNeedle = external ? @"预约" : @"京A20001";
        const PageCheck checks[] = {
            {0, "仪表盘",   @"总车位"},
            {1, "车位地图", @"车位"},
            {2, "车辆作业", @"车牌"},
            {3, "当前车位", @"车位"},
            {4, "预约管理", bookingsNeedle},
            {5, "停车记录", recordsNeedle},
        };
        for (const PageCheck &check : checks){
            [sidebar selectIndex:check.index];
            spin([]{ return false; }, 450);   // 让切页与布局跑完
            [main.contentViewController.view layoutSubtreeIfNeeded];
            char label[128];
            std::snprintf(label, sizeof(label), "「%s」页渲染出「%s」", check.name,
                          [check.needle UTF8String]);
            expect(viewContainsText(main.contentViewController.view, check.needle), label);
        }

        // 「当前车位」页：创建即锁位的预约要显示为「已预约」并带预约车牌
        // （用户报的就是「网页预约之后当前车位看不到」）。状态列显示「已预约」，
        // 车牌/车型/时段取预约订单上的值。
        // NSTableView 只实例化可见行，所以这里断言的是表格的行数据 + 页面提示，
        // 而不是去页面里找那行文字（车位 A0xx 可能正好在可视区之外）。
        [sidebar selectIndex:3];
        spin([]{ return false; }, 450);
        [main.contentViewController.view layoutSubtreeIfNeeded];
        expect(viewContainsText(main.contentViewController.view, @"已预约"),
               "「当前车位」页在提示里说明「已预约（已锁位）」");
        if (!external){
            id occupancyPage = pages[3];
            TableView *left = (__bridge TableView *)readPointerIvar(occupancyPage, "_table");
            TableView *right = (__bridge TableView *)readPointerIvar(occupancyPage, "_secondTable");
            expect(left != nil && right != nil, "取到当前车位页的两张表");
            NSMutableArray<NSArray<NSString *> *> *rows = [NSMutableArray array];
            for (TableView *table in @[left ?: (id)[NSNull null], right ?: (id)[NSNull null]]){
                if ((id)table == [NSNull null]){
                    continue;
                }
                NSArray<NSArray<NSString *> *> *part =
                    (__bridge NSArray<NSArray<NSString *> *> *)readPointerIvar(table, "_rows");
                if (part != nil){
                    [rows addObjectsFromArray:part];
                }
            }
            bool reservedRow = false;
            for (NSArray<NSString *> *row in rows){
                if (row.count >= 5 && [row[1] isEqualToString:@"已预约"]
                    && [row[2] isEqualToString:@"京A20001"]
                    && ![row[4] isEqualToString:@"-"]){
                    reservedRow = true;
                }
            }
            expect(reservedRow,
                   "「当前车位」页有一行：已预约 · 京A20001 · 预约时段（已锁位也看得见）");
        }
        // 「车位地图」页：橙色框图例（说明「已预约」的车位已经锁位）。
        [sidebar selectIndex:1];
        spin([]{ return false; }, 450);
        [main.contentViewController.view layoutSubtreeIfNeeded];
        expect(viewContainsText(main.contentViewController.view, @"橙色框"),
               "「车位地图」页渲染出「橙色框 = 已被预约」图例");

        // 「预约管理」页还要显示时段预约（网页端创建的那种）。
        [sidebar selectIndex:4];
        spin([]{ return false; }, 450);
        [main.contentViewController.view layoutSubtreeIfNeeded];
        expect(viewContainsText(main.contentViewController.view, reservationNeedle),
               external ? "「预约管理」页渲染出时段预约区块"
                        : "「预约管理」页显示时段预约 京A20001（网页端创建的那种）");

        // 直接点「预约管理」页的按钮：表单 → 数据源 → 服务端 → 快照 → 页面。
        // 用户报的就是「按了没反应」，所以这条链路必须端到端跑通。
        if (writeChecks){
            id bookingPage = pages[4];
            [sidebar selectIndex:4];
            spin([]{ return false; }, 450);
            NSTextField *plateField = (__bridge NSTextField *)readPointerIvar(bookingPage, "_plateField");
            NSDatePicker *startPicker = (__bridge NSDatePicker *)readPointerIvar(bookingPage, "_startPicker");
            NSTextField *statusLabel = (__bridge NSTextField *)readPointerIvar(bookingPage, "_statusLabel");
            expect(plateField != nil && startPicker != nil && statusLabel != nil,
                   "取到预约表单控件");
            if (plateField != nil && startPicker != nil && statusLabel != nil){
                const auto formRule = bridge->reservationRule();
                plateField.stringValue = uiPlate;
                // 开始时间落在「最短提前量 + 3 秒」：满足规则，且到场窗口随后就开。
                startPicker.dateValue = [NSDate dateWithTimeIntervalSinceNow:
                    formRule.minLeadTimeMin * 60.0 + 3.0];
                [bookingPage createReservation:nil];
                spin([]{ return false; }, 300);
                expect([statusLabel.stringValue containsString:@"预约成功"],
                       "点「创建预约」后页面报成功");
                expect(bridge->lastError().empty(), "创建后没有残留错误");
                expect(bridge->plannedRoute().valid,
                       "预约后车位图拿到规划好的路线（地图上会画出这条线）");
                const bool uiVisible = spin([&]{
                    for (const smartpark::Reservation &item : bridge->reservations()){
                        if (item.plateNumber() == uiPlate.UTF8String){
                            return true;
                        }
                    }
                    return false;
                }, 5000);
                expect(uiVisible, "按钮创建的预约随快照回到管理端");
                if (!external){
                    const int rows = sqlInt(
                        persistence->databaseManager().database(),
                        QStringLiteral("SELECT COUNT(*) FROM reservations "
                                       "WHERE plate_number='%1'")
                            .arg(QString::fromNSString(uiPlate)));
                    expect(rows == 1, "按钮创建预约已入库（SQLite reservations 表）");
                }
                std::printf("       按钮链路：%s\n",
                            [statusLabel.stringValue UTF8String]);
                bridge->clearPlannedRoute();
            }
        }

        // 表单创建 + 到场确认的结果要真的画到页面上，不只是数据层有：
        // 「预约管理」显示新订单，「停车记录」显示到场后的车辆。
        if (writeChecks){
            NSString *writeNeedle = smartpark_ui::toNSString(writePlate.toStdString());
            [sidebar selectIndex:4];
            spin([]{ return false; }, 450);
            [main.contentViewController.view layoutSubtreeIfNeeded];
            expect(viewContainsText(main.contentViewController.view, writeNeedle),
                   "「预约管理」页显示表单刚创建的预约");
            // 表单本身也不能是空的：按钮、时长与规则提示（规则来自快照，
            // 远程模式下不再是「至少提前 0 分钟」）。
            expect(viewContainsText(main.contentViewController.view, @"创建预约")
                       && viewContainsText(main.contentViewController.view, @"确认所选预约到场")
                       && viewContainsText(main.contentViewController.view, @"时长")
                       && viewContainsText(main.contentViewController.view, @"无障碍车位"),
                   "「预约管理」页表单渲染出按钮与字段");
            expect(viewContainsText(main.contentViewController.view, @"需至少提前"),
                   "「预约管理」页显示服务端下发的预约规则");
            [sidebar selectIndex:5];
            spin([]{ return false; }, 450);
            [main.contentViewController.view layoutSubtreeIfNeeded];
            expect(viewContainsText(main.contentViewController.view, writeNeedle),
                   "「停车记录」页显示到场确认后的车辆");

            // 指向真实部署时把测试痕迹清掉：写路径那笔已到场（车还在场内），
            // 表单那笔仍是未结束的预约。不清掉的话第二次对着同一台服务器跑，
            // 同一批车牌会撞上「车辆已在场内」/「该车牌已有未结束的时段预约」，
            // 测试自己把自己跑红（不是产品缺陷）。自带服务端是临时库，不必清理。
            // 这里只报告、不再新增断言：创建/到场/取消是否成功上面已经断言过了。
            if (external){
                const bool left = bridge->leaveVehicle(writePlate.toStdString()).has_value();
                const bool cancelled = bridge->cancelReservation(uiPlate.UTF8String);
                std::printf("       清理测试痕迹：离场 %s，取消表单预约 %s\n",
                            left ? "成功" : "失败", cancelled ? "成功" : "失败");
                spin([]{ return false; }, 600);
            }
        }

        // ---- 预约管理的「选中行」操作与窄窗口可读性 ----
        // 用户报告：在列表里点选一条预约后按「取消预约/到场确认」，实际操作
        // 的仍是输入框里敲的车牌，选中的记录被静默忽略；窗口缩到 1080×600
        // 时日期列被压成「2026-10-…」，车位类型也读不出来。
        if (!external){
            // 1080×600 接近窗口最小尺寸（960×600），正是用户缩放到的尺寸。
            [window setContentSize:NSMakeSize(1080, 600)];
            spin([]{ return false; }, 300);
            [main.contentViewController.view layoutSubtreeIfNeeded];
            [sidebar selectIndex:4];
            spin([]{ return false; }, 450);
            [main.contentViewController.view layoutSubtreeIfNeeded];

            id bookingPage = pages[4];
            TableView *bookingTable =
                (__bridge TableView *)readPointerIvar(bookingPage, "_table");
            NSTextField *plateField =
                (__bridge NSTextField *)readPointerIvar(bookingPage, "_plateField");
            NSTextField *statusLabel =
                (__bridge NSTextField *)readPointerIvar(bookingPage, "_statusLabel");
            NSButton *cancelButton =
                (__bridge NSButton *)readPointerIvar(bookingPage, "_cancelButton");
            NSButton *checkInButton =
                (__bridge NSButton *)readPointerIvar(bookingPage, "_checkInButton");
            expect(bookingTable != nil && plateField != nil && statusLabel != nil
                       && cancelButton != nil && checkInButton != nil,
                   "读预约管理页的表格、表单与「所选」操作按钮");
            if (bookingTable != nil && cancelButton != nil){
                // 1) 窄窗口：每列都保持在最小宽度以上，整表可横向滚动。
                const NSInteger minimums[] = {100, 88, 72, 130, 130, 130, 70, 80, 80};
                NSArray<NSTableColumn *> *columns = bookingTable.tableView.tableColumns;
                bool widthsOk = (columns.count == 9);
                for (NSUInteger index = 0; index < columns.count && index < 9; ++index){
                    if (columns[index].width < (CGFloat)minimums[index] - 0.5){
                        widthsOk = false;
                    }
                }
                expect(widthsOk, "预约管理页窄窗口下每列不小于最小宽度（日期不被压成省略号）");
                NSScrollView *bookingScroll = bookingTable.tableView.enclosingScrollView;
                const CGFloat documentWidth = bookingScroll.documentView.frame.size.width;
                const CGFloat viewportWidth = bookingScroll.contentView.bounds.size.width;
                std::printf("       窄窗口表格：内容 %.0f pt，可视 %.0f pt\n",
                            documentWidth, viewportWidth);
                expect(bookingScroll.hasHorizontalScroller
                           && documentWidth > viewportWidth + 0.5,
                       "预约管理页窄窗口下表格可横向滚动查看完整内容");

                // 2) 单元格 toolTip 带完整取值：被省略的日期悬停可读。
                NSArray<NSString *> *firstRow = [bookingTable rowAtIndex:0];
                NSString *fullValue = firstRow.count > 4 ? firstRow[4] : nil;
                bool tooltipOk = false;
                if (fullValue.length > 0){
                    id delegate = bookingTable.tableView.delegate;
                    NSView *cell = [delegate tableView:bookingTable.tableView
                                    viewForTableColumn:columns[4]
                                                   row:0];
                    tooltipOk = [cell.toolTip isEqualToString:fullValue];
                    if (!tooltipOk){
                        for (NSView *child in cell.subviews){
                            if ([child isKindOfClass:[NSTextField class]]
                                && [((NSTextField *)child).toolTip isEqualToString:fullValue]){
                                tooltipOk = true;
                            }
                        }
                    }
                }
                std::printf("       预约时间单元格 toolTip：%s\n",
                            fullValue.length > 0 ? [fullValue UTF8String] : "(无)");
                expect(tooltipOk, "预约时间单元格的 toolTip 是完整日期（被截断时可悬停查看）");

                // 3) 选中一条已确认的时段预约：表单同步、按钮指向所选。
                NSInteger reservationRow = -1;
                NSString *selectedId = nil;
                NSString *selectedPlate = nil;
                for (NSInteger row = 0; row < bookingTable.tableView.numberOfRows; ++row){
                    NSArray<NSString *> *values = [bookingTable rowAtIndex:row];
                    if (values.count >= 9 && [values[8] isEqualToString:@"时段预约"]
                        && [values[7] isEqualToString:@"已确认"]){
                        reservationRow = row;
                        selectedId = values[0];
                        selectedPlate = values[1];
                        break;
                    }
                }
                expect(reservationRow >= 0, "预约管理页有可操作的「时段预约」行");
                if (reservationRow >= 0){
                    [bookingTable.tableView selectRowIndexes:
                        [NSIndexSet indexSetWithIndex:(NSUInteger)reservationRow]
                                           byExtendingSelection:NO];
                    spin([]{ return false; }, 200);
                    expect([plateField.stringValue isEqualToString:selectedPlate],
                           "选中预约后车牌输入框同步为该记录的车牌");
                    expect([cancelButton.title containsString:@"所选"]
                               && [checkInButton.title containsString:@"所选"],
                           "操作按钮标题标明作用于「所选」预约");
                    expect([statusLabel.stringValue containsString:@"已选择"]
                               && [statusLabel.stringValue containsString:selectedPlate],
                           "状态栏显示选中目标（车牌/车位/状态/时段）");

                    // 选中后用户又改输入框：操作仍必须落在选中的记录上。
                    plateField.stringValue = @"京A99999";
                    [cancelButton performClick:nil];
                    spin([]{ return false; }, 300);
                    const std::string targetId = selectedId.UTF8String;
                    const bool cancelled = spin([&]{
                        for (const smartpark::Reservation &item : bridge->reservations()){
                            if (item.id() == targetId){
                                return item.status()
                                    == smartpark::ReservationStatus::Cancelled;
                            }
                        }
                        return false;
                    }, 5000);
                    expect(cancelled,
                           "改了输入框后点「取消所选预约」仍取消选中的预约");
                    expect(bridge->lastError().empty(),
                           "取消所选预约没有残留错误（没拿输入框里的京A99999 去操作）");
                    expect([statusLabel.stringValue containsString:@"取消成功"],
                           "取消所选预约后页面报成功");
                    expect([bookingTable.tableView selectedRow] == -1,
                           "数据刷新后选中状态被重置（旧行号不会指向别的记录）");
                }

                // 4) 历史预约（显示用）不可操作：选中后按钮禁用，直接调用也被拒。
                NSInteger historyRow = -1;
                for (NSInteger row = 0; row < bookingTable.tableView.numberOfRows; ++row){
                    NSArray<NSString *> *values = [bookingTable rowAtIndex:row];
                    if (values.count >= 9 && [values[8] isEqualToString:@"预约(历史)"]){
                        historyRow = row;
                        break;
                    }
                }
                expect(historyRow >= 0, "预约管理页显示历史预约行（只读）");
                if (historyRow >= 0){
                    [bookingTable.tableView selectRowIndexes:
                        [NSIndexSet indexSetWithIndex:(NSUInteger)historyRow]
                                           byExtendingSelection:NO];
                    spin([]{ return false; }, 200);
                    expect(!cancelButton.enabled && !checkInButton.enabled,
                           "选中历史预约后「取消/确认所选」按钮被禁用");
                    expect([statusLabel.stringValue containsString:@"只读"],
                           "选中历史预约后状态栏说明只读");
                    // 按钮已禁用，但程序化调用也不能拿历史记录去改真实数据。
                    [bookingPage cancel:nil];
                    spin([]{ return false; }, 200);
                    expect([statusLabel.stringValue containsString:@"只读"],
                           "对历史预约调用取消被拒绝并提示只读");
                }
            }

            // 5) 「当前车位」页响应式：窄窗口一张表显示全部车位，宽窗口恢复两表。
            TableView *occupancyLeft =
                (__bridge TableView *)readPointerIvar(pages[3], "_table");
            TableView *occupancyRight =
                (__bridge TableView *)readPointerIvar(pages[3], "_secondTable");
            expect(occupancyLeft != nil && occupancyRight != nil,
                   "读当前车位页的两张表（响应式布局用）");
            const NSUInteger spotCount = bridge->spots().size();
            if (occupancyLeft != nil && occupancyRight != nil && spotCount > 0){
                [sidebar selectIndex:3];
                spin([]{ return false; }, 450);
                [main.contentViewController.view layoutSubtreeIfNeeded];
                expect(occupancyRight.hidden, "窄窗口「当前车位」页隐藏右表");
                expect(occupancyLeft.tableView.numberOfRows == (NSInteger)spotCount,
                       "窄窗口左表显示全部车位（单表，选中偏移为 0）");

                [window setContentSize:NSMakeSize(1500, 860)];
                spin([]{ return false; }, 300);
                [main.contentViewController.view layoutSubtreeIfNeeded];
                const NSInteger split = (NSInteger)((spotCount + 1) / 2);
                expect(!occupancyRight.hidden, "宽窗口「当前车位」页恢复右表");
                expect(occupancyLeft.tableView.numberOfRows == split
                           && occupancyRight.tableView.numberOfRows
                               == (NSInteger)spotCount - split,
                       "宽窗口两张表各显示一半车位");

                // 恢复默认尺寸，后面的用例继续在 1280×860 下跑。
                [window setContentSize:NSMakeSize(1280, 860)];
                spin([]{ return false; }, 300);
                [main.contentViewController.view layoutSubtreeIfNeeded];
                [sidebar selectIndex:4];
                spin([]{ return false; }, 300);
            }
        }

    // ---- 管理端远程连接时，图片仍在本机识别 ----
    @autoreleasepool{
        if (bridge != nullptr){
            QTemporaryDir fixture;
            // 照片留档也指到临时目录，避免测试往仓库的 examples/platephoto 里写东西。
            setenv("SMARTPARK_PLATE_PHOTO_DIR", fixture.filePath("plate-photos").toUtf8().constData(), 1);
            NSString *script = smartpark_ui::toNSString(fixture.filePath("local_lpr.py").toStdString());
            NSString *image = smartpark_ui::toNSString(fixture.filePath("photo.jpg").toStdString());
            [@"local-photo-fixture" writeToFile:image atomically:YES encoding:NSUTF8StringEncoding error:nil];
            NSString *source = @"import json,sys\nfrom pathlib import Path\n"
                @"assert Path(sys.argv[1]).read_text() == 'local-photo-fixture'\n"
                @"print(json.dumps({'plate':'京A12345','detection_confidence':0.9,'recognition_confidence':0.99}))\n";
            [source writeToFile:script atomically:YES encoding:NSUTF8StringEncoding error:nil];
            NSDictionary *savedEnvironment = [[NSProcessInfo processInfo] environment];
            setenv("SMARTPARK_LPR_SCRIPT", script.UTF8String, 1);
            setenv("SMARTPARK_LPR_PY", "/usr/bin/python3", 1);
            setenv("SMARTPARK_OCR_PY", "/usr/bin/python3", 1);
            LocalRecognitionProbe *operations = [[LocalRecognitionProbe alloc] init];
            operations.bridge = bridge;
            (void)operations.view;
            [operations runRecognitionForImage:image];
            // NSTask completes on dispatch_get_main_queue, while the older
            // remote tests pump only Qt events. Drive the Cocoa run loop too.
            spin([&]{
                [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                return operations.candidate != nil || operations.failure != nil;
            }, 10000);
            expect([operations.candidate[@"plate"] isEqualToString:@"京A12345"],
                   "远程管理端执行本机脚本并接收本机候选车牌");
            expect(bridge->remoteMode(), "图片识别后仍保留远程停车数据源");

            // 绿牌＝新能源：识别结果里带 new_energy/color 时车辆类型自动切到电动车。
            NSPopUpButton *typePopup =
                (__bridge NSPopUpButton *)readPointerIvar(operations, "_typePopup");
            NSTextField *statusLabel =
                (__bridge NSTextField *)readPointerIvar(operations, "_statusLabel");
            expect(typePopup != nil && statusLabel != nil, "取到车辆类型下拉框与状态栏");
            [typePopup selectItemAtIndex:0];
            expect([operations applyNewEnergyVehicleTypeForResult:
                        @{@"color": @"green", @"new_energy": @YES}] != nil
                   && typePopup.indexOfSelectedItem == 3,
                   "绿色号牌把车辆类型自动切成电动车");
            [typePopup selectItemAtIndex:0];
            expect([operations applyNewEnergyVehicleTypeForResult:
                        @{@"color": @"blue", @"new_energy": @NO}] == nil
                   && typePopup.indexOfSelectedItem == 0,
                   "蓝色号牌不动车辆类型");

            // 走完整流程：识别即入场那条路要用上刚切好的类型。
            [typePopup selectItemAtIndex:0];
            writeIntegerIvar(operations, "_recognitionIntent", 1);  // SmartParkRecognitionIntentEnter
            [operations performRecognitionIntentWithPlate:@"皖AD12368"
                result:@{@"color": @"green", @"new_energy": @YES,
                         @"crop_image": image,
                         @"detection_confidence": @0.93, @"recognition_confidence": @0.97}];
            expect(typePopup.indexOfSelectedItem == 3
                   && [statusLabel.stringValue containsString:@"电动车"],
                   "识别入场时按绿牌记为电动车并如实提示");
            bool parkedAsElectric = false;
            bool parkedAsCar = false;
            spin([&]{
                for (const auto &spot : bridge->spots()){
                    if (spot.parkedVehicle().has_value()
                        && spot.parkedVehicle()->plateNumber() == "皖AD12368"){
                        parkedAsElectric =
                            spot.parkedVehicle()->type() == smartpark::VehicleType::Electric;
                        parkedAsCar =
                            spot.parkedVehicle()->type() == smartpark::VehicleType::Car;
                        return true;
                    }
                }
                return false;
            }, 5000);
            expect(parkedAsElectric && !parkedAsCar, "车位上的皖AD12368 记成了电动车");
            expect([PlatePhotoArchive photosForPlate:@"皖AD12368"].count == 1,
                   "入场后车牌照片留在 platephoto 根目录");

            // 出库走界面同一条路：入场、出库两张照片都挪进 history/，
            // 文件名换成「车牌_入库时间_出库时间」，在场目录随即清空。顺带清理测试车。
            writeIntegerIvar(operations, "_recognitionIntent", 2);  // SmartParkRecognitionIntentExit
            [operations performRecognitionIntentWithPlate:@"皖AD12368"
                result:@{@"color": @"green", @"new_energy": @YES,
                         @"crop_image": image,
                         @"detection_confidence": @0.93, @"recognition_confidence": @0.97}];
            expect([statusLabel.stringValue containsString:@"history"],
                   "出库状态栏提示照片已移入 history");
            expect([PlatePhotoArchive photosForPlate:@"皖AD12368"].count == 0,
                   "出库后在场照片目录清空");
            NSString *photoHistory = [PlatePhotoArchive historyDirectoryWithError:nil];
            NSArray<NSString *> *archived = [[[NSFileManager defaultManager]
                contentsOfDirectoryAtPath:photoHistory error:nil]
                sortedArrayUsingSelector:@selector(compare:)];
            expect(archived.count == 2, "出库后入场、出库两张照片都进了 history");
            // 文件名＝车牌 + 入库时间 + 出库时间（都是 yyyyMMdd-HHmmss）；同一趟停车
            // 第 2 张起带 _2 序号。入库和出库落在同一秒时两个时间相同，序号就顶到第 4 段。
            auto timestampLike = [](NSString *text){
                if (text.length != 15 || [text characterAtIndex:8] != '-'){
                    return false;
                }
                NSString *digits = [text stringByReplacingCharactersInRange:NSMakeRange(8, 1)
                                                                 withString:@""];
                return [digits rangeOfCharacterFromSet:
                    [[NSCharacterSet decimalDigitCharacterSet] invertedSet]].location == NSNotFound;
            };
            bool namedByStay = archived.count == 2;
            for (NSString *name in archived){
                NSArray<NSString *> *parts =
                    [[name stringByDeletingPathExtension] componentsSeparatedByString:@"_"];
                if (parts.count == 4){
                    parts = [parts subarrayWithRange:NSMakeRange(0, 3)];
                }
                namedByStay = namedByStay && parts.count == 3
                    && [parts[0] isEqualToString:@"皖AD12368"]
                    && timestampLike(parts[1]) && timestampLike(parts[2]);
            }
            expect(namedByStay, "history 文件名由车牌和入库、出库两个时间组成");
            expect([[archived.lastObject stringByDeletingPathExtension] hasSuffix:@"_2"],
                   "同一趟停车的第二张照片带 _2 后缀");

            NSString *sleeper = @"import time\ntime.sleep(10)\n";
            [sleeper writeToFile:script atomically:YES encoding:NSUTF8StringEncoding error:nil];
            operations.candidate = nil;
            operations.failure = nil;
            [operations runRecognitionForImage:image];
            [operations cancelRecognition:nil];
            spin([&]{
                [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                return readPointerIvar(operations, "_recognitionTask") == nullptr;
            }, 3000);
            expect(readPointerIvar(operations, "_recognitionTask") == nullptr,
                   "取消本机识别会结束子进程并解除忙碌状态");
            expect(operations.candidate == nil && operations.failure == nil,
                   "取消后不弹出迟到的识别候选结果");
            // Exercise the default resident branch with the real client,
            // private socket and service ownership. Only inference is stubbed.
            NSString *servicePath = smartpark_ui::toNSString(
                fixture.filePath("lpr_service.py").toStdString());
            NSString *clientMarker = smartpark_ui::toNSString(
                fixture.filePath("lpr_client.py").toStdString());
            [@"# fixture marker" writeToFile:clientMarker atomically:YES encoding:NSUTF8StringEncoding error:nil];
            NSString *serviceSource = @"import argparse,json,signal,socketserver,os,time\n"
                @"p=argparse.ArgumentParser();p.add_argument('--socket');p.add_argument('--ocr-python');a=p.parse_args()\n"
                @"class Handler(socketserver.StreamRequestHandler):\n"
                @" def handle(self):\n"
                @"  r=json.loads(self.rfile.readline())\n"
                @"  if 'slow' in r['image']: time.sleep(10)\n"
                @"  self.wfile.write((json.dumps({'result':{'plate':'京A12345'},'seconds':0.01})+'\\n').encode())\n"
                @"def stop(*args): raise KeyboardInterrupt\n"
                @"signal.signal(signal.SIGTERM,stop)\n"
                @"try:\n"
                @" with socketserver.UnixStreamServer(a.socket,Handler) as server: server.serve_forever(poll_interval=0.01)\n"
                @"except KeyboardInterrupt: pass\n"
                @"finally:\n"
                @" if os.path.exists(a.socket): os.unlink(a.socket)\n";
            [serviceSource writeToFile:servicePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
            operations.residentFixture = script;
            unsetenv("SMARTPARK_LPR_SCRIPT");
            auto waitForCandidate = [&]{
                return spin([&]{
                    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                        beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                    return operations.candidate != nil || operations.failure != nil;
                }, 10000);
            };
            [operations runRecognitionForImage:image];
            waitForCandidate();
            expect([operations.candidate[@"plate"] isEqualToString:@"京A12345"],
                   "默认常驻路径通过真实轻量客户端返回本机结果");
            NSTask *resident = (__bridge NSTask *)readPointerIvar(operations, "_localRecognitionService");
            expect(resident.running, "本机常驻推理进程保持运行");
            operations.candidate = nil;
            operations.failure = nil;
            [operations runRecognitionForImage:image];
            waitForCandidate();
            expect(operations.candidate != nil
                       && (__bridge void *)resident == readPointerIvar(operations, "_localRecognitionService"),
                   "后续图片复用同一个本机推理进程");
            operations.candidate = nil;
            operations.failure = nil;
            [operations runRecognitionForImage:[image stringByAppendingString:@"-slow"]];
            NSTask *clientTask = (__bridge NSTask *)readPointerIvar(operations, "_recognitionTask");
            [operations stopLocalRecognitionService:
                [NSNotification notificationWithName:NSApplicationWillTerminateNotification object:nil]];
            spin([&]{
                [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
                return !resident.running && !clientTask.running;
            }, 3000);
            expect(!resident.running && !clientTask.running,
                   "应用退出同时停止本机服务和客户端子进程");
            expect(readPointerIvar(operations, "_recognitionTask") == nullptr
                       && operations.candidate == nil && operations.failure == nil,
                   "退出后丢弃尚未送达的识别回调");

            for (NSString *key in @[@"SMARTPARK_LPR_SCRIPT", @"SMARTPARK_LPR_PY",
                                    @"SMARTPARK_OCR_PY", @"SMARTPARK_PLATE_PHOTO_DIR"]){
                NSString *value = savedEnvironment[key];
                if (value != nil){ setenv(key.UTF8String, value.UTF8String, 1); }
                else { unsetenv(key.UTF8String); }
            }
        }
    }

    // ---- 启动预热不等待模型；期间的请求等待同一进程就绪 ----
    @autoreleasepool{
        QTemporaryDir fixture;
        NSString *script = smartpark_ui::toNSString(fixture.filePath("recognize_plate.py").toStdString());
        NSString *servicePath = smartpark_ui::toNSString(fixture.filePath("lpr_service.py").toStdString());
        NSString *source = @"import argparse,json,os,signal,socketserver,time\n"
            @"p=argparse.ArgumentParser();p.add_argument('--socket');p.add_argument('--ocr-python');p.add_argument('--warmup-image');p.add_argument('--parent-pid');a=p.parse_args()\n"
            @"time.sleep(1) # simulate slow model loading before accepting requests\n"
            @"class H(socketserver.StreamRequestHandler):\n"
            @" def handle(self):\n"
            @"  json.loads(self.rfile.readline());self.wfile.write(b'{\"result\":{\"plate\":\"TEST123\"}}\\n')\n"
            @"def stop(*args): raise KeyboardInterrupt\n"
            @"signal.signal(signal.SIGTERM,stop)\n"
            @"try:\n"
            @" with socketserver.UnixStreamServer(a.socket,H) as server: server.serve_forever(poll_interval=0.01)\n"
            @"except KeyboardInterrupt: pass\n"
            @"finally:\n"
            @" if os.path.exists(a.socket): os.unlink(a.socket)\n";
        [source writeToFile:servicePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
        LocalRecognitionService *models = [[LocalRecognitionService alloc] init];
        QElapsedTimer clock;
        clock.start();
        NSString *socket = [models startWithScript:script detectorPython:@"/usr/bin/python3"
            ocrPython:@"/usr/bin/python3" environment:[[NSProcessInfo processInfo] environment]];
        expect(clock.elapsed() < 300, "启动预热立即返回，不等待模型加载");
        NSString *again = [models startWithScript:script detectorPython:@"/usr/bin/python3"
            ocrPython:@"/usr/bin/python3" environment:[[NSProcessInfo processInfo] environment]];
        expect([socket isEqualToString:again], "加载期间的重复启动复用同一 socket");
        NSTask *request = [[NSTask alloc] init];
        request.executableURL = [NSURL fileURLWithPath:@"/usr/bin/python3"];
        NSString *client = [[@SMARTPARK_LPR_SCRIPT_PATH stringByDeletingLastPathComponent]
            stringByAppendingPathComponent:@"lpr_client.py"];
        request.arguments = @[@"-B", client, @"fixture.jpg", @"--socket", socket, @"--wait-ready", @"5"];
        NSPipe *output = [NSPipe pipe];
        request.standardOutput = output;
        request.standardError = [NSFileHandle fileHandleWithNullDevice];
        NSError *error = nil;
        expect([request launchAndReturnError:&error], "预热期间可以提交识别请求");
        spin([&]{
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            return !request.running;
        }, 10000);
        NSData *reply = [output.fileHandleForReading readDataToEndOfFile];
        NSDictionary *result = [NSJSONSerialization JSONObjectWithData:reply options:0 error:nil];
        expect(request.terminationStatus == 0 && [result[@"plate"] isEqualToString:@"TEST123"],
               "早期请求等待后台加载完成并收到同一服务的结果");
        NSTask *resident = models.task;
        expect(resident.running, "首次请求复用已预热进程");
        // A service can die without stop() clearing its retained NSTask.
        [resident terminate];
        spin([&]{
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            return !resident.running;
        }, 3000);
        NSString *replacement = [models startWithScript:script detectorPython:@"/usr/bin/python3"
            ocrPython:@"/usr/bin/python3" environment:[[NSProcessInfo processInfo] environment]];
        expect(models.task != resident && ![replacement isEqualToString:socket],
               "进程意外退出后重试不暴露旧 PID");
        NSTask *retry = [[NSTask alloc] init];
        retry.executableURL = request.executableURL;
        retry.arguments = @[@"-B", client, @"fixture.jpg", @"--socket", replacement, @"--wait-ready", @"5"];
        retry.standardOutput = [NSFileHandle fileHandleWithNullDevice];
        retry.standardError = [NSFileHandle fileHandleWithNullDevice];
        expect([retry launchAndReturnError:&error], "恢复进程后可以再次请求识别");
        spin([&]{
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            return !retry.running;
        }, 10000);
        expect(retry.terminationStatus == 0, "预加载失败后可成功重试同一识别链路");
        resident = models.task;
        [models stop];
        spin([&]{
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
            return !resident.running;
        }, 3000);
        expect(!resident.running, "共享预热进程可正常停止");
        [models startWithScript:script detectorPython:@"/usr/bin/python3"
            ocrPython:@"/usr/bin/python3" environment:[[NSProcessInfo processInfo] environment]];
        [models stop];
        spin([]{ return false; }, 200);
        expect(models.task == nil, "取消尚未完成的启动不会发布迟到进程");
    }

    // 远程出库必须返回本次停车的真实入场时间与时长，而不是用当前时间重建记录。
    if (!external && bridge != nullptr){
        const auto entry = service->activeRecord("京A10002");
        expect(entry.has_value(), "结算回归保留三小时前入场的在场记录");
        const auto departed = bridge->leaveVehicle("京A10002");
        expect(departed.has_value(), "远程出库返回完整结算记录");
        if (entry && departed){
            const auto ms = [](auto time){
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                    time.time_since_epoch());
            };
            expect(ms(departed->entryTime()) == ms(entry->entryTime()),
                   "结算使用真实入场时间而不是出场时刻");
            expect(departed->vehicleType() == smartpark::VehicleType::Truck,
                   "结算保留真实车型");
            expect(departed->duration() >= std::chrono::hours(3),
                   "跨时段远程停车不会被重建为零分钟");
            bool sameSettlement = false;
            for (const auto &record : service->records()){
                if (record.plateNumber() == "京A10002" && record.isClosed()
                    && ms(record.entryTime()) == ms(entry->entryTime())){
                    sameSettlement = departed->fee() == record.fee()
                        && ms(*departed->exitTime()) == ms(*record.exitTime());
                }
            }
            expect(sameSettlement, "结算费用与离场时间来自服务端同一笔记录");
        }
    }

    // ---- 服务端识别 API 仍供其他客户端使用，与管理端图片路由独立 ----
    @autoreleasepool{
        if (bridge != nullptr){
            // mock 后端按图片内容哈希出车牌，这里只要一段非空字节即可；
            // 重点是确认这一趟真的经过了服务端（返回了 backend 说明）。
            QByteArray imageBytes = QByteArrayLiteral("fake-jpeg-bytes-for-test");
            bool finished = false;
            smartpark::ParkingDataSource::PlateRecognition outcome;
            bridge->recognizePlateRemotely(
                imageBytes,
                [&finished, &outcome](smartpark::ParkingDataSource::PlateRecognition result){
                    outcome = result;
                    finished = true;
                });
            spin([&]{ return finished; }, 15000);
            expect(finished, "服务端识别有应答（异步回调返回）");
            expect(outcome.ok, "服务端识别成功");
            expect(!outcome.plate.empty(), "服务端返回了车牌");
            expect(!outcome.backend.empty(), "返回 backend 说明识别跑在服务端");
            std::printf("       识别结果：%s（backend=%s，置信度 %.2f）\n",
                        outcome.plate.c_str(), outcome.backend.c_str(),
                        outcome.confidence);
            expect(bridge->supportsRemoteRecognition(),
                   "远程模式声明支持服务端识别");
        }
    }

    }

    // ---- 启动参数预填（--remote host:port / --user）----
    // 部署后一条命令直达：登录界面应勾好远程模式、填好地址端口账号。
    @autoreleasepool{
        QTemporaryDir loginDir;
        std::unique_ptr<smartpark::UserStore> loginUsers;
        if (loginDir.isValid()){
            loginUsers = std::make_unique<smartpark::UserStore>(
                loginDir.filePath(QStringLiteral("login.db")));
            LoginViewController *login = [[LoginViewController alloc]
                initWithUserStore:loginUsers.get()];
            (void)login.view;   // 触发界面构建
            [login prefillRemoteHost:@"10.108.17.55" port:9527 user:@"admin"];

            NSButton *check = (__bridge NSButton *)readPointerIvar(login, "_remoteCheck");
            NSTextField *host = (__bridge NSTextField *)readPointerIvar(login, "_hostField");
            NSTextField *port = (__bridge NSTextField *)readPointerIvar(login, "_portField");
            NSTextField *user = (__bridge NSTextField *)readPointerIvar(login, "_userNameField");
            NSStackView *row = (__bridge NSStackView *)readPointerIvar(login, "_remoteRow");

            expect(check.state == NSControlStateValueOn, "预填后「连接远程服务端」已勾选");
            expect([host.stringValue isEqualToString:@"10.108.17.55"], "预填后地址正确");
            expect([port.stringValue isEqualToString:@"9527"], "预填后端口正确");
            expect([user.stringValue isEqualToString:@"admin"], "预填后账号正确");
            expect(row.hidden == NO, "预填后地址/端口行已显示");

            // 没有 --remote 时，登录界面应自带默认部署地址，勾上就能登录。
            // 先摘掉「记住的地址」，测完再还原，避免动到用户自己的设置。
            NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
            id savedHost = [defaults objectForKey:@"smartpark.remote.host"];
            id savedPort = [defaults objectForKey:@"smartpark.remote.port"];
            [defaults removeObjectForKey:@"smartpark.remote.host"];
            [defaults removeObjectForKey:@"smartpark.remote.port"];

            LoginViewController *plain = [[LoginViewController alloc]
                initWithUserStore:loginUsers.get()];
            (void)plain.view;
            NSTextField *plainHost = (__bridge NSTextField *)readPointerIvar(plain, "_hostField");
            NSTextField *plainPort = (__bridge NSTextField *)readPointerIvar(plain, "_portField");
            expect([plainHost.stringValue isEqualToString:@"10.108.17.55"],
                   "未指定 --remote 时地址默认为 10.108.17.55");
            expect([plainPort.stringValue isEqualToString:@"9527"],
                   "未指定 --remote 时端口默认为 9527");

            if (savedHost != nil){ [defaults setObject:savedHost forKey:@"smartpark.remote.host"]; }
            if (savedPort != nil){ [defaults setObject:savedPort forKey:@"smartpark.remote.port"]; }
        }
    }

    std::printf("\n%s (%d failure(s))\n",
                failures == 0 ? "ALL CHECKS PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
