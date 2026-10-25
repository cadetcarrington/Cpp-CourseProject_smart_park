#import "AppDelegate.h"

#import "LoginViewController.h"
#import "MainMenu.h"
#import "MainWindowController.h"
#import "LocalRecognitionService.h"

#include "core/persistence/Persistence.h"
#include "core/service/UserStore.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QString>

#include <memory>

@implementation AppDelegate{
    // 账号库与停车数据共用同一个 SQLite 文件（与 Qt 版一致）。
    std::unique_ptr<smartpark::UserStore> userStore_;
    QString databasePath_;
    // --layout 读到的布局文本；为空表示用内置车库布局。
    QString layoutDescription_;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification{
    // 先把主菜单装上：没有「编辑」菜单项，⌘C / ⌘V / ⌘X / ⌘A / ⌘Z 在任何一个
    // 输入框里都不会生效（登录窗口也一样），⌘Q / ⌘M 等系统快捷键也一起缺失。
    SmartParkInstallMainMenu();

    // 自定义参数在 main.mm 里就摘出来了（QCoreApplication 只拿到 argv[0]，
    // 下面这个 parser 看不到它们），这里只作为兜底保留。
    QCommandLineParser parser;
    const QCommandLineOption databaseOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QStringLiteral("本地 SQLite 数据库路径"), QStringLiteral("path"));
    parser.addOption(databaseOption);
    parser.process(*QCoreApplication::instance());
    if (_launchDatabasePath.length > 0){
        databasePath_ = QString::fromNSString(_launchDatabasePath);
    } else {
        databasePath_ = parser.isSet(databaseOption)
            ? parser.value(databaseOption)
            : smartpark::Persistence::defaultDatabasePath();
    }

    // --layout：本地模式用指定的布局开库。服务端库/自定义图纸的布局签名与内置
    // 布局不同，不传这个参数会被服务端库拒之门外（"persisted layout differs"）。
    if (_launchLayoutPath.length > 0){
        QFile layoutFile(QString::fromNSString(_launchLayoutPath));
        if (!layoutFile.open(QIODevice::ReadOnly | QIODevice::Text)){
            [self failWithTitle:@"布局文件打不开"
                         detail:[NSString stringWithFormat:@"%@：%@",
                                 _launchLayoutPath,
                                 layoutFile.errorString().toNSString()]];
            return;
        }
        layoutDescription_ = QString::fromUtf8(layoutFile.readAll());
        if (layoutDescription_.trimmed().isEmpty()){
            [self failWithTitle:@"布局文件是空的" detail:_launchLayoutPath];
            return;
        }
    }
    [self showLogin];
    [LocalRecognitionService preloadDefault];
}

// 启动期致命错误：弹窗说明原因后退出，不要带着半截配置继续跑。
- (void)failWithTitle:(NSString *)title detail:(NSString *)detail{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = detail;
    alert.alertStyle = NSAlertStyleCritical;
    [alert addButtonWithTitle:@"退出"];
    if (self.loginWindowController.window != nil){
        [alert beginSheetModalForWindow:self.loginWindowController.window completionHandler:nil];
    } else {
        [alert runModal];
    }
    [NSApp terminate:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{
    return YES;
}

#pragma mark - 登录界面

- (void)showLogin{
    // 本地库不播种演示账号：空库首启走「创建管理员账号」注册页
    // （LoginViewController 首启流程），已有账号的库不受影响。
    userStore_ = std::make_unique<smartpark::UserStore>(
        databasePath_, smartpark::UserStore::SeedPolicy::NoSeed);
    if (!userStore_->lastError().isEmpty()){
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"账号数据库不可用";
        alert.informativeText = [NSString stringWithFormat:
            @"无法打开 %@：\n%@", databasePath_.toNSString(),
            userStore_->lastError().toNSString()];
        alert.alertStyle = NSAlertStyleCritical;
        [alert addButtonWithTitle:@"退出"];
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }

    LoginViewController *login = [[LoginViewController alloc]
        initWithUserStore:userStore_.get()];
    if (_launchRemoteHost.length > 0){
        [login prefillRemoteHost:_launchRemoteHost
                            port:_launchRemotePort
                            user:_launchUserName];
    }
    __weak AppDelegate *weakSelf = self;
    login.onAuthenticated = ^(NSString *userName, NSString *remoteHost,
                              NSInteger remotePort, NSString *remotePassword){
        [weakSelf showMainForUser:userName
                       remoteHost:remoteHost
                             port:remotePort
                         password:remotePassword];
    };

    // 高度随内容收缩：登录页去掉品牌图标后少了 66pt，保持底部留白不变。
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 420, 494)
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskFullSizeContentView)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.title = @"智能停车系统";
    window.accessibilityIdentifier = @"smartpark.login.window";
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    // 透明窗口 + 全尺寸内容视图：让登录页根部的 NSVisualEffectView
    // 以 BehindWindow 混合模式透出桌面，实现 macOS 原生毛玻璃。
    window.opaque = NO;
    window.backgroundColor = [NSColor clearColor];
    window.movableByWindowBackground = YES;
    window.contentViewController = login;
    [window center];

    NSWindowController *controller =
        [[NSWindowController alloc] initWithWindow:window];
    // 先把登录窗口显示出来，再关掉主窗口：避免出现「零窗口」瞬间
    // 触发 applicationShouldTerminateAfterLastWindowClosed 而直接退出。
    [controller showWindow:nil];
    self.loginWindowController = controller;

    [self.mainWindowController close];
    self.mainWindowController = nil;
    self.currentUser = nil;

    [NSApp activateIgnoringOtherApps:YES];
}

#pragma mark - 主窗口

- (void)showMainForUser:(NSString *)userName{
    [self showMainForUser:userName remoteHost:nil port:0 password:nil];
}

- (void)showMainForUser:(NSString *)userName
             remoteHost:(NSString *)remoteHost
                   port:(NSInteger)remotePort
               password:(NSString *)remotePassword{
    MainWindowController *main =
        [[MainWindowController alloc] initWithUserName:userName
                                          databasePath:databasePath_
                                     layoutDescription:layoutDescription_];

    if (remoteHost.length > 0){
        // 远程模式：本机库不作为数据源，失败与否由连接状态决定。
        [main connectToRemoteHost:remoteHost port:remotePort
                             user:userName password:remotePassword];
        [self presentMainWindow:main forUser:userName];
        return;
    }

    if (![main isDatabaseReady]){
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = @"停车数据库不可用";
        alert.informativeText = [main databaseError];
        alert.alertStyle = NSAlertStyleCritical;
        [alert addButtonWithTitle:@"确定"];
        [alert beginSheetModalForWindow:self.loginWindowController.window
                     completionHandler:nil];
        return;
    }
    [self presentMainWindow:main forUser:userName];
}

- (void)presentMainWindow:(MainWindowController *)main forUser:(NSString *)userName{
    __weak AppDelegate *weakSelf = self;
    main.logoutHandler = ^{
        [weakSelf showLogin];
    };

    [main showWindow:nil];
    self.mainWindowController = main;
    self.currentUser = userName;

    // 主窗口已经就位，此时再收起登录窗口。
    [self.loginWindowController close];
    self.loginWindowController = nil;

    [NSApp activateIgnoringOtherApps:YES];
}

#pragma mark - 主菜单动作

// 下面几个 action 的 target 都留空，由 AppKit 沿响应者链送到 App 委托。
- (void)smartparkSelectPage:(NSMenuItem *)sender{
    [self.mainWindowController selectPage:sender.tag];
}

- (void)smartparkRefreshPage:(id)sender{
    [self.mainWindowController refreshCurrentPage];
}

- (void)smartparkLogout:(id)sender{
    void (^logout)(void) = self.mainWindowController.logoutHandler;
    if (logout != nil){
        logout();
    }
}

- (void)smartparkCheckOut:(id)sender{
    [self.mainWindowController beginCheckOut];
}

- (void)smartparkCheckIn:(id)sender{
    [self.mainWindowController beginCheckIn];
}

- (void)smartparkFindCar:(id)sender{
    [self.mainWindowController beginCarFinder];
}

- (void)smartparkNewReservation:(id)sender{
    [self.mainWindowController beginReservation];
}

- (void)smartparkShowShortcuts:(id)sender{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"快捷键";
    alert.informativeText =
        @"操作　　　　入库 ⌘I · 出库 ⌘O · 寻车 ⌘F · 预约 ⌘N\n"
         "页面　　　　⌘1 仪表盘 · ⌘2 车位图 · ⌘3 车辆出入库\n"
         "　　　　　　⌘4 当前车位 · ⌘5 预约管理 · ⌘6 停车记录 · ⌘7 设施配置\n"
         "刷新当前页　⌘R　　显示/隐藏侧边栏 ⌃⌘S　　全屏 ⌃⌘F\n"
         "退出登录　　⇧⌘W　　关闭窗口 ⌘W（关闭主窗口即退出）\n\n"
         "编辑：⌘C 拷贝 · ⌘V 粘贴 · ⌘X 剪切 · ⌘A 全选 · ⌘Z 撤销 · ⇧⌘Z 重做";
    [alert addButtonWithTitle:@"好"];
    NSWindow *window = self.mainWindowController.window ?: self.loginWindowController.window;
    if (window != nil){
        [alert beginSheetModalForWindow:window completionHandler:nil];
    } else{
        [alert runModal];
    }
}

// 没有主窗口（还在登录页）时，把「页面切换 / 刷新 / 退出登录」变灰。
- (BOOL)validateMenuItem:(NSMenuItem *)item{
    const SEL action = item.action;
    if (action == @selector(smartparkSelectPage:) || action == @selector(smartparkRefreshPage:) ||
        action == @selector(smartparkLogout:) || action == @selector(smartparkCheckIn:) ||
        action == @selector(smartparkCheckOut:) || action == @selector(smartparkFindCar:) ||
        action == @selector(smartparkNewReservation:)){
        if (self.mainWindowController == nil){
            return NO;
        }
        if (action == @selector(smartparkSelectPage:)){
            item.state = (item.tag == self.mainWindowController.currentPageIndex)
                ? NSControlStateValueOn : NSControlStateValueOff;
        }
        return YES;
    }
    return YES;
}

@end
