#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

#include "bridge/ParkingBridge.h"
#include "core/persistence/Persistence.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>
#include <string>

// 自定义启动参数：
//   --remote <host[:port]> / --user <name>   登录界面预填远程模式
//   --db <path>                              本地模式打开哪个 SQLite（默认库）
//   --layout <file>                          本地模式用哪份布局（服务端库/自定义
//                                            图纸的库必须配对的布局）
//   --check-db                               只校验「库+布局能否打开」后退出（无窗口）
struct LaunchOptions{
    NSString *remoteHost = nil;
    NSInteger remotePort = 9527;
    NSString *userName = nil;
    NSString *databasePath = nil;
    NSString *layoutPath = nil;
    BOOL checkDatabase = NO;
};

static LaunchOptions parseLaunchOptions(int argc, char *argv[]){
    LaunchOptions options;
    for (int i = 1; i < argc; ++i){
        const std::string arg = argv[i];
        if (arg == "--remote" && i + 1 < argc){
            NSString *endpoint = [NSString stringWithUTF8String:argv[++i]];
            NSArray<NSString *> *parts = [endpoint componentsSeparatedByString:@":"];
            options.remoteHost = parts.firstObject;
            if (parts.count > 1){
                options.remotePort = parts[1].integerValue;
            }
        } else if (arg == "--user" && i + 1 < argc){
            options.userName = [NSString stringWithUTF8String:argv[++i]];
        } else if ((arg == "--db" || arg == "-d") && i + 1 < argc){
            options.databasePath = [NSString stringWithUTF8String:argv[++i]];
        } else if ((arg == "--layout" || arg == "-l") && i + 1 < argc){
            options.layoutPath = [NSString stringWithUTF8String:argv[++i]];
        } else if (arg == "--check-db"){
            options.checkDatabase = YES;
        }
    }
    return options;
}

int main(int argc, char *argv[]){
    // 先摘出自定义参数。QCoreApplication 遇到不认识的选项会直接报
    // "Unknown options: ..." 并退出，所以只把 argv[0] 交给它——本进程不用
    // Qt 的参数解析（没有 QCommandLineParser）。
    const LaunchOptions options = parseLaunchOptions(argc, argv);

    // Qt 非 GUI 基础设施（SQLite 持久化的 QSqlDatabase / Qt Network）需要
    // QCoreApplication 实例。这里仅构造用于初始化 Qt，事件循环仍由 NSApplication
    // 驱动（QCoreApplication 不调用 exec）。
    int qtArgc = 1;
    QCoreApplication qtApp(qtArgc, argv);
    QCoreApplication::setApplicationName("SmartPark Admin");
    QCoreApplication::setOrganizationName("SmartPark");

    // --check-db：只回答「这份库配这份布局能不能打开」，不开窗口。走的是和主窗口
    // 同一个 ParkingBridge，所以结论与界面里一致（换库/换布局后先跑这个）。
    if (options.checkDatabase){
        const QString databasePath = options.databasePath != nil
            ? QString::fromNSString(options.databasePath)
            : smartpark::Persistence::defaultDatabasePath();
        QString layoutText;
        const char *layoutLabel = "builtin";
        if (options.layoutPath != nil){
            QFile layoutFile(QString::fromNSString(options.layoutPath));
            if (!layoutFile.open(QIODevice::ReadOnly | QIODevice::Text)){
                std::fprintf(stderr, "layout file not readable: %s (%s)\n",
                             options.layoutPath.UTF8String,
                             layoutFile.errorString().toUtf8().constData());
                return 2;
            }
            layoutText = QString::fromUtf8(layoutFile.readAll());
            layoutLabel = "custom";
        }
        // ParkingBridge 在全局命名空间（只有它内部用的领域类型在 smartpark::）。
        ::ParkingBridge bridge(databasePath, layoutText);
        if (!bridge.ready()){
            std::fprintf(stderr, "NOT READY: %s\n", bridge.lastError().c_str());
            return 1;
        }
        std::printf("READY db=%s layout=%s spots=%d records=%zu\n",
                    databasePath.toUtf8().constData(), layoutLabel,
                    bridge.totalSpots(), bridge.records().size());
        return 0;
    }

    @autoreleasepool{
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        // 静态持有 delegate，避免 NSApplication.delegate 的弱引用在启动后悬空。
        static AppDelegate *delegate;
        delegate = [[AppDelegate alloc] init];
        delegate.launchRemoteHost = options.remoteHost;
        delegate.launchRemotePort = options.remotePort;
        delegate.launchUserName = options.userName;
        delegate.launchDatabasePath = options.databasePath;
        delegate.launchLayoutPath = options.layoutPath;
        app.delegate = delegate;

        // 远程模式用的 QTcpSocket / QTimer（ServerSession 的应答、心跳与重连）
        // 靠 Qt 自己的事件队列投递，而 NSApplication 的循环只驱动 Cocoa。
        // 不抽干 Qt 队列的话，连接会停在「正在连接」永不推进。这里挂一个
        // 轻量 NSTimer 定期 processEvents()，让两套事件循环共存。
        [NSTimer scheduledTimerWithTimeInterval:0.01
                                        repeats:YES
                                          block:^(NSTimer *timer){
            QCoreApplication::processEvents();
        }];

        [app run];
    }
    return 0;
}
