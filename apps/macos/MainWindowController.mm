#import "MainWindowController.h"

#import "BookingViewController.h"
#import "DashboardViewController.h"
#import "OccupancyViewController.h"
#import "OperationsViewController.h"
#import "ParkingMapViewController.h"
#import "RecordsViewController.h"
#import "SettingsViewController.h"
#import "SidebarViewController.h"
#import "bridge/ParkingBridge.h"

#include "core/persistence/Persistence.h"

#include <memory>

// 内容区容器：在 Sidebar 切换时替换当前页面 VC。
@interface PageHostViewController : NSViewController
@property (nonatomic, strong) NSViewController *childViewController;
@end

@implementation PageHostViewController

- (void)setChildViewController:(NSViewController *)child{
    if (child == _childViewController){
        return;
    }
    [_childViewController.view removeFromSuperview];
    [_childViewController removeFromParentViewController];
    _childViewController = child;
    if (child){
        [self addChildViewController:child];
        child.view.frame = self.view.bounds;
        child.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [self.view addSubview:child.view];
    }
}

@end

@interface MainWindowController () <SidebarDelegate>
@property (nonatomic, strong) PageHostViewController *pageHost;
@property (nonatomic, strong) NSArray<NSViewController *> *pages;
@property (nonatomic, strong) SidebarViewController *sidebar;
@end

@implementation MainWindowController{
    std::unique_ptr<ParkingBridge> bridge_;
    BOOL _remoteOnline;
    NSString *_remoteStatus;
}

- (instancetype)init{
    return [self initWithUserName:nil];
}

- (instancetype)initWithUserName:(NSString *)userName{
    return [self initWithUserName:userName
                    databasePath:smartpark::Persistence::defaultDatabasePath()];
}

- (instancetype)initWithUserName:(NSString *)userName databasePath:(const QString &)databasePath{
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1120, 720)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                   NSWindowStyleMaskFullSizeContentView)
        backing:NSBackingStoreBuffered
        defer:NO];
    if ((self = [super initWithWindow:window])){
        bridge_ = std::make_unique<ParkingBridge>(databasePath);

        window.title = @"SmartPark Admin";
        window.accessibilityIdentifier = @"smartpark.main.window";
        window.titlebarAppearsTransparent = YES;
        window.titleVisibility = NSWindowTitleHidden;
        window.contentMinSize = NSMakeSize(960, 600);

        NSSplitViewController *split = [[NSSplitViewController alloc] init];
        split.splitView.vertical = YES;
        split.splitView.dividerStyle = NSSplitViewDividerStyleThin;

        SidebarViewController *sidebar = [[SidebarViewController alloc] init];
        sidebar.delegate = self;
        sidebar.userName = userName;
        __weak MainWindowController *weakSelf = self;
        sidebar.logoutHandler = ^{
            MainWindowController *strongSelf = weakSelf;
            if (strongSelf != nil && strongSelf.logoutHandler != nil){
                strongSelf.logoutHandler();
            }
        };
        NSSplitViewItem *sidebarItem =
            [NSSplitViewItem sidebarWithViewController:sidebar];
        sidebarItem.minimumThickness = 200;
        sidebarItem.maximumThickness = 260;
        sidebarItem.canCollapse = NO;
        self.sidebar = sidebar;

        // 构建 7 个页面，共享同一个 bridge。
        DashboardViewController *dashboard = [[DashboardViewController alloc] init];
        dashboard.bridge = bridge_.get();
        // 仪表盘快捷操作卡片：切到目标页并同步侧边栏选中态。
        dashboard.quickActionHandler = ^(SmartParkPage page){
            [weakSelf showPage:(NSInteger)page];
        };
        ParkingMapViewController *map = [[ParkingMapViewController alloc] init];
        map.bridge = bridge_.get();
        OperationsViewController *operations = [[OperationsViewController alloc] init];
        operations.bridge = bridge_.get();
        OccupancyViewController *occupancy = [[OccupancyViewController alloc] init];
        occupancy.bridge = bridge_.get();
        BookingViewController *booking = [[BookingViewController alloc] init];
        booking.bridge = bridge_.get();
        RecordsViewController *records = [[RecordsViewController alloc] init];
        records.bridge = bridge_.get();
        SettingsViewController *settings = [[SettingsViewController alloc] init];
        settings.bridge = bridge_.get();

        self.pages = @[dashboard, map, operations, occupancy, booking, records, settings];

        self.pageHost = [[PageHostViewController alloc] init];
        NSSplitViewItem *contentItem =
            [NSSplitViewItem splitViewItemWithViewController:self.pageHost];

        [split addSplitViewItem:sidebarItem];
        [split addSplitViewItem:contentItem];

        window.contentViewController = split;
        [window center];

        // 初始显示仪表盘。
        self.pageHost.childViewController = dashboard;

        // 车辆作业/预约等操作后广播数据变更，驱动各页面刷新。
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(dataChanged:)
                                                     name:@"SmartParkDataChanged"
                                                   object:nil];
    }
    return self;
}

- (void)connectToRemoteHost:(NSString *)host port:(NSInteger)port
                       user:(NSString *)user password:(NSString *)password{
    if (bridge_ == nullptr){
        return;
    }
    __weak MainWindowController *weakSelf = self;
    // 连接状态变化：登录失败要如实显示，不能只把界面停在「连接中」。
    bridge_->onRemoteStateChanged = [weakSelf](bool online, const QString &detail){
        MainWindowController *strongSelf = weakSelf;
        if (strongSelf == nil){
            return;
        }
        strongSelf->_remoteOnline = online;
        strongSelf->_remoteStatus = [NSString stringWithUTF8String:
            detail.toUtf8().constData()] ?: @"";
        [strongSelf applyRemoteCapabilities];
    };
    // 快照刷新后让当前页面重画（页面自身从 bridge 取数）。
    bridge_->onRemoteDataChanged = [weakSelf]{
        MainWindowController *strongSelf = weakSelf;
        if (strongSelf == nil){
            return;
        }
        [strongSelf refreshVisiblePage];
    };
    bridge_->connectRemote(QString::fromUtf8(host.UTF8String),
                           static_cast<quint16>(port),
                           QString::fromUtf8((user ?: @"admin").UTF8String),
                           QString::fromUtf8((password ?: @"").UTF8String));
}

- (BOOL)isRemote{
    return bridge_ != nullptr && bridge_->remoteMode();
}

- (NSString *)remoteStatusText{
    return _remoteStatus;
}

// 远程模式下协议覆盖不到的页面从侧边栏移除，避免点进去是一片空表。
- (void)applyRemoteCapabilities{
    if (bridge_ == nullptr || !bridge_->remoteMode()){
        return;
    }
    const auto caps = bridge_->capabilities();
    NSMutableIndexSet *hidden = [NSMutableIndexSet indexSet];
    if (!caps.bookings){
        [hidden addIndex:(NSUInteger)SmartParkPageBooking];
    }
    if (!caps.records){
        [hidden addIndex:(NSUInteger)SmartParkPageRecords];
    }
    if (!caps.layoutEditing){
        [hidden addIndex:(NSUInteger)SmartParkPageSettings];
    }
    [self.sidebar setHiddenPages:hidden];
    // 当前页若被隐藏，退回仪表盘。
    const NSInteger current = [self.pages indexOfObject:self.pageHost.childViewController];
    if (current != NSNotFound && [hidden containsIndex:(NSUInteger)current]){
        [self showPage:0];
    }
}

- (void)refreshVisiblePage{
    NSViewController *current = self.pageHost.childViewController;
    if ([current respondsToSelector:@selector(refresh)]){
        [current performSelector:@selector(refresh)];
    }
}

- (BOOL)isDatabaseReady{
    return bridge_ != nullptr && bridge_->ready();
}

- (NSString *)databaseError{
    if (bridge_ == nullptr){
        return @"无法创建停车数据服务。";
    }
    return [NSString stringWithUTF8String:bridge_->lastError().c_str()] ?: @"无法打开停车数据库。";
}

- (void)dealloc{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (NSViewController *)pageForIndex:(NSInteger)index{
    if (index >= 0 && index < (NSInteger)self.pages.count){
        return self.pages[(NSUInteger)index];
    }
    return self.pages.firstObject;
}

- (void)sidebar:(SidebarViewController *)sidebar didSelectIndex:(NSInteger)index{
    NSViewController *vc = [self pageForIndex:index];
    self.pageHost.childViewController = vc;
    if ([vc respondsToSelector:@selector(refresh)]){
        [vc performSelector:@selector(refresh)];
    }
}

// 程序化切页（仪表盘快捷操作卡片）：同步侧边栏选中态后直接切页。
// 侧边栏若已选中同一行不会再回调，因此这里显式调用一次。
- (void)showPage:(NSInteger)index{
    [self.sidebar selectIndex:index];
    [self sidebar:self.sidebar didSelectIndex:index];
}

- (void)dataChanged:(NSNotification *)notification{    for (NSViewController *vc in self.pages){
        if ([vc respondsToSelector:@selector(refresh)]){
            [vc performSelector:@selector(refresh)];
        }
    }
}

@end
