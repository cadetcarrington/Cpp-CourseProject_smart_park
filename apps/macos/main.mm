#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

#include <QCoreApplication>

#include <string>

// 自定义启动参数：--remote <host[:port]> 与 --user <name>。
// 部署后可以直接
//   open smartpark_admin_macos.app --args --remote 10.0.0.5:9527 --user admin
// 登录界面会预填好远程设置，只剩口令要输。
struct LaunchOptions{
    NSString *remoteHost = nil;
    NSInteger remotePort = 9527;
    NSString *userName = nil;
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

    @autoreleasepool{
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        // 静态持有 delegate，避免 NSApplication.delegate 的弱引用在启动后悬空。
        static AppDelegate *delegate;
        delegate = [[AppDelegate alloc] init];
        delegate.launchRemoteHost = options.remoteHost;
        delegate.launchRemotePort = options.remotePort;
        delegate.launchUserName = options.userName;
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
