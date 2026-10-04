// 原生通知横幅冒烟测试：展示一条横幅，断言其作为可见窗口存在，
// 并在缩短的自动消失时限后断言其已关闭。
#import <AppKit/AppKit.h>

#include "MacNotifications.h"

#include <cstdio>
#import <objc/runtime.h>

namespace{
bool PanelVisible(){
    bool visible = false;
    for (NSWindow *window in NSApp.windows){
        if ([window isKindOfClass:NSPanel.class] && window.isVisible){
            visible = true;
        }
    }
    return visible;
}

void PumpRunLoop(NSTimeInterval seconds){
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([[NSDate date] compare:deadline] == NSOrderedAscending){
        [NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode
                           beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
}
} // namespace

int main(){
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        application.activationPolicy = NSApplicationActivationPolicyAccessory;
        setenv("SMARTPARK_BANNER_SECONDS", "1", 1);

        macnotify::showExitBanner(nullptr, "车辆已离场 · 京A12345",
                                  {"车位：A001", "停车时长：1 小时 25 分", "费用：10.00 元"});
        PumpRunLoop(0.6);
        if (!PanelVisible()){
            std::fprintf(stderr, "FAILED: banner panel is not visible after show\n");
            return 1;
        }

        // 自动消失（1 秒）+ 淡出（0.25 秒）后不应再有可见横幅。
        PumpRunLoop(2.2);
        if (PanelVisible()){
            std::fprintf(stderr, "FAILED: banner panel is still visible after auto-dismiss\n");
            return 1;
        }
        std::printf("platform banner test passed\n");
        return 0;
    }
}
