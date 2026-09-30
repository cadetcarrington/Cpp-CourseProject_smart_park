#import <Cocoa/Cocoa.h>

// 应用协调者：登录界面 ⇄ 主窗口的切换（对应 Qt 版 main.cpp 的登录循环）。
// 未通过 UserStore 认证前不会创建任何业务页面。
@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindowController *mainWindowController;
@property (nonatomic, strong) NSWindowController *loginWindowController;
@property (nonatomic, copy) NSString *currentUser;
@end
