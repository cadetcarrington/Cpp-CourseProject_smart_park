#import <Cocoa/Cocoa.h>

#ifdef __cplusplus
class QString;
#endif

// 主窗口：侧边栏 + 7 个业务页面。由 AppDelegate 在登录成功后创建。
@interface MainWindowController : NSWindowController

- (instancetype)initWithUserName:(NSString *)userName;
- (instancetype)initWithUserName:(NSString *)userName databasePath:(const QString &)databasePath;
// layoutDescription 非空时按这份布局启动本地模式（`--layout <文件>` 读到的内容）；
// 传空则用内置车库布局，与旧行为一致。
- (instancetype)initWithUserName:(NSString *)userName
                    databasePath:(const QString &)databasePath
               layoutDescription:(const QString &)layoutDescription;
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

// 主菜单用：按索引切页 / 刷新当前页。菜单动作由 AppDelegate 接收后转到这里。
- (void)selectPage:(NSInteger)index;
- (void)refreshCurrentPage;

// 当前页面索引（0 仪表盘 … 6 设施配置），「显示 › 页面」菜单用它打勾。
@property (nonatomic, readonly) NSInteger currentPageIndex;

// 主菜单「操作」的四个入口：入库 / 出库 / 寻车 / 预约。
// 出库与寻车会先弹出「选择或输入在场车牌」对话框，取消则什么都不做。
- (void)beginCheckIn;
- (void)beginCheckOut;
- (void)beginCarFinder;
- (void)beginReservation;

// 当前登录账号，显示在侧边栏底部。
@property (nonatomic, copy) NSString *userName;
// 用户确认「退出登录」后回调，由 AppDelegate 切回登录界面。
@property (nonatomic, copy) void (^logoutHandler)(void);
@end
