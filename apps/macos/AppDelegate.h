#import <Cocoa/Cocoa.h>

// 应用协调者：登录界面 ⇄ 主窗口的切换（对应 Qt 版 main.cpp 的登录循环）。
// 未通过 UserStore 认证前不会创建任何业务页面。
@interface AppDelegate : NSObject <NSApplicationDelegate>

// 启动参数（--remote host:port / --user name / --db path / --layout file）：
// 登录界面据此预填远程模式；--db/--layout 决定本地模式打开哪个库、用哪份布局。
@property (nonatomic, copy) NSString *launchRemoteHost;
@property (nonatomic) NSInteger launchRemotePort;
@property (nonatomic, copy) NSString *launchUserName;
@property (nonatomic, copy) NSString *launchDatabasePath;
@property (nonatomic, copy) NSString *launchLayoutPath;
@property (nonatomic, strong) NSWindowController *mainWindowController;
@property (nonatomic, strong) NSWindowController *loginWindowController;
@property (nonatomic, copy) NSString *currentUser;
@end
