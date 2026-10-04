#import "AppDelegate.h"

#import "LoginViewController.h"
#import "MainWindowController.h"

#include "core/persistence/Persistence.h"
#include "core/service/UserStore.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>

#include <memory>

@implementation AppDelegate{
    // 账号库与停车数据共用同一个 SQLite 文件（与 Qt 版一致）。
    std::unique_ptr<smartpark::UserStore> userStore_;
    QString databasePath_;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification{
    QCommandLineParser parser;
    const QCommandLineOption databaseOption(
        {QStringLiteral("d"), QStringLiteral("db")},
        QStringLiteral("本地 SQLite 数据库路径"), QStringLiteral("path"));
    parser.addOption(databaseOption);
    parser.process(*QCoreApplication::instance());
    databasePath_ = parser.isSet(databaseOption)
        ? parser.value(databaseOption)
        : smartpark::Persistence::defaultDatabasePath();
    [self showLogin];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{
    return YES;
}

#pragma mark - 登录界面

- (void)showLogin{
    userStore_ = std::make_unique<smartpark::UserStore>(databasePath_);
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
    __weak AppDelegate *weakSelf = self;
    login.onAuthenticated = ^(NSString *userName){
        [weakSelf showMainForUser:userName];
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
    MainWindowController *main =
        [[MainWindowController alloc] initWithUserName:userName databasePath:databasePath_];
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

@end
