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

#import "LoginViewController.h"
#import "MainWindowController.h"
#import "TextUtil.h"   // smartpark_ui::toNSString：中文安全的 QString -> NSString

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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

        // 「预约管理」页还要显示时段预约（网页端创建的那种）。
        [sidebar selectIndex:4];
        spin([]{ return false; }, 450);
        [main.contentViewController.view layoutSubtreeIfNeeded];
        expect(viewContainsText(main.contentViewController.view, reservationNeedle),
               external ? "「预约管理」页渲染出时段预约区块"
                        : "「预约管理」页显示时段预约 京A20001（网页端创建的那种）");
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
