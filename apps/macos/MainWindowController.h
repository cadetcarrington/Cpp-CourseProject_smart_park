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

// 切换为远程模式：以 TCP 连接服务端，本机数据库不再作为数据源。
// 连接与登录结果异步到达，期间界面显示连接状态。
- (void)connectToRemoteHost:(NSString *)host port:(NSInteger)port
                       user:(NSString *)user password:(NSString *)password;
// 当前是否远程模式。
- (BOOL)isRemote;
// 远程模式下的连接/快照状态文案，供侧边栏底部或窗口副标题显示。
- (NSString *)remoteStatusText;

// 当前登录账号，显示在侧边栏底部。
@property (nonatomic, copy) NSString *userName;
// 用户确认「退出登录」后回调，由 AppDelegate 切回登录界面。
@property (nonatomic, copy) void (^logoutHandler)(void);
@end
