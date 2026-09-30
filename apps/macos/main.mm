#import <Cocoa/Cocoa.h>
#import "AppDelegate.h"

#include <QCoreApplication>

int main(int argc, char *argv[]){
    // Qt 非 GUI 基础设施（SQLite 持久化的 QSqlDatabase / Qt Network）需要
    // QCoreApplication 实例。这里仅构造用于初始化 Qt，事件循环仍由 NSApplication
    // 驱动，两者互不冲突（QCoreApplication 不调用 exec）。
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
        [app run];
    }
    return 0;
}
