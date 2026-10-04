#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

#include <QCoreApplication>

int main(int argc, char *argv[]){
    // Qt 非 GUI 基础设施（SQLite 持久化的 QSqlDatabase / Qt Network）需要
    // QCoreApplication 实例。这里仅构造用于初始化 Qt，事件循环仍由 NSApplication
    // 驱动（QCoreApplication 不调用 exec）。
    QCoreApplication qtApp(argc, argv);
    QCoreApplication::setApplicationName("SmartPark Admin");
    QCoreApplication::setOrganizationName("SmartPark");

    @autoreleasepool{
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        // 静态持有 delegate，避免 NSApplication.delegate 的弱引用在启动后悬空。
        static AppDelegate *delegate;
        delegate = [[AppDelegate alloc] init];
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
