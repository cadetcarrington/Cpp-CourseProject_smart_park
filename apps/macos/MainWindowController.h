#import <Cocoa/Cocoa.h>

#ifdef __cplusplus
class QString;
#endif

// 主窗口：侧边栏 + 7 个业务页面。由 AppDelegate 在登录成功后创建。
@interface MainWindowController : NSWindowController

- (instancetype)initWithUserName:(NSString *)userName;
- (instancetype)initWithUserName:(NSString *)userName databasePath:(const QString &)databasePath;
- (BOOL)isDatabaseReady;
- (NSString *)databaseError;

// 当前登录账号，显示在侧边栏底部。
@property (nonatomic, copy) NSString *userName;
// 用户确认「退出登录」后回调，由 AppDelegate 切回登录界面。
@property (nonatomic, copy) void (^logoutHandler)(void);
@end
