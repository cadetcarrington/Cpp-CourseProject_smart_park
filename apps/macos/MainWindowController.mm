#import "MainWindowController.h"

#import "BookingViewController.h"
#import "DashboardViewController.h"
#import "OccupancyViewController.h"
#import "PlatePrompt.h"
#import "OperationsViewController.h"
#import "ParkingMapViewController.h"
#import "RecordsViewController.h"
#import "SettingsViewController.h"
#import "SidebarViewController.h"
#import "bridge/ParkingBridge.h"

#include "core/persistence/Persistence.h"

#include <memory>

namespace{
// 页面标题交给窗口工具栏显示（原生）：索引与侧边栏顺序一致。
NSString *pageTitleForIndex(NSInteger index){
    switch (index){
        case 0: return @"仪表盘";
        case 1: return @"车位图";
        case 2: return @"车辆出入库";
        case 3: return @"当前车位";
        case 4: return @"预约管理";
        case 5: return @"停车记录";
        case 6: return @"设施配置";
        default: return @"智能停车系统";
    }
}
} // namespace

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

@interface MainWindowController () <SidebarDelegate, NSToolbarDelegate>
@property (nonatomic, strong) PageHostViewController *pageHost;
@property (nonatomic, strong) NSArray<NSViewController *> *pages;
@property (nonatomic, strong) SidebarViewController *sidebar;
@property (nonatomic, strong) NSTextField *toolbarTitleLabel;
@property (nonatomic, assign) NSInteger currentPageIndex;
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
    return [self initWithUserName:userName
                     databasePath:databasePath
                layoutDescription:QString()];
}

- (instancetype)initWithUserName:(NSString *)userName
                    databasePath:(const QString &)databasePath
               layoutDescription:(const QString &)layoutDescription{
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1120, 720)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                   NSWindowStyleMaskFullSizeContentView)
        backing:NSBackingStoreBuffered
        defer:NO];
    if ((self = [super initWithWindow:window])){
        bridge_ = std::make_unique<ParkingBridge>(databasePath, layoutDescription);

        window.title = @"SmartPark Admin";
        window.accessibilityIdentifier = @"smartpark.main.window";
        // 原生标题栏 + 工具栏（与「系统设置」的详情区一致）：系统标题隐藏，
        // 改由工具栏里一个居中的标题项绘制，内容延伸到栏下面滚动。
        window.titlebarAppearsTransparent = NO;
        window.titleVisibility = NSWindowTitleHidden;
        window.contentMinSize = NSMakeSize(960, 600);

        NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"smartpark.main.toolbar"];
        toolbar.delegate = self;
        toolbar.displayMode = NSToolbarDisplayModeIconOnly;
        toolbar.allowsUserCustomization = NO;
        toolbar.autosavesConfiguration = NO;
        if (@available(macOS 11.0, *)){
            window.toolbarStyle = NSWindowToolbarStyleUnified;
        }
        window.toolbar = toolbar;

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
        // 允许折叠：「显示 › 显示/隐藏侧边栏」与工具栏左侧开关都靠它。
        sidebarItem.canCollapse = YES;
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

        // 初始显示仪表盘（标题由工具栏显示）。
        self.pageHost.childViewController = dashboard;
        [self applyPageTitleForIndex:0];

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

// 主菜单入口：刷新当前页。
- (void)refreshCurrentPage{
    [self refreshVisiblePage];
}

// 主菜单入口：按索引切页（侧边栏选中态由 showPage: 同步）。
- (void)selectPage:(NSInteger)index{
    [self showPage:index];
}

#pragma mark - 主菜单「操作」

- (OperationsViewController *)operationsPage{
    return (OperationsViewController *)self.pages[SmartParkPageOperations];
}

- (void)beginCheckIn{
    [self showPage:SmartParkPageOperations];
    [[self operationsPage] beginCheckIn];
}

- (void)beginCheckOut{
    NSString *plate = SmartParkPromptForParkedPlate(bridge_.get(), @"出库",
        @"选择或输入在场车牌，点「下一步」跳到车辆出入库办理出库。", @"下一步");
    if (plate == nil){
        return;
    }
    [self showPage:SmartParkPageOperations];
    [[self operationsPage] beginCheckOutWithPlate:plate];
}

- (void)beginCarFinder{
    NSString *plate = SmartParkPromptForParkedPlate(bridge_.get(), @"反向寻车",
        @"选择或输入在场车牌，点「寻找」跳到车位地图查看步行路线。", @"寻找");
    if (plate == nil){
        return;
    }
    [self showPage:SmartParkPageMap];
    if (plate.length > 0){
        // 先切页（地图页此时已加载），再让地图页按车牌画路线。
        [[NSNotificationCenter defaultCenter]
            postNotificationName:SmartParkFindCarRequestNotification
                          object:nil
                        userInfo:@{SmartParkFindCarPlateKey: plate}];
    }
}

- (void)beginReservation{
    [self showPage:SmartParkPageBooking];
}

- (BOOL)isDatabaseReady{
    return bridge_ != nullptr && bridge_->ready();
}

- (NSString *)databaseError{
    if (bridge_ == nullptr){
        return @"无法创建停车数据服务。";
    }
    NSString *reason = [NSString stringWithUTF8String:bridge_->lastError().c_str()]
        ?: @"无法打开停车数据库。";
    // 库的布局签名与当前布局不符，是换库/换图纸后最常见的失败；把解法直接写进
    // 提示，否则用户只看到一句「persisted layout differs」无从下手。
    if ([reason containsString:@"persisted layout"]){
        return [NSString stringWithFormat:
            @"%@\n\n这个库是用另一份布局建的（例如服务端的三塔楼图纸）。"
             "加上启动参数 --layout <布局文件> 指定同一份布局即可打开，"
             "例如：--db ~/smartpark-data/smartpark-3tower.db "
             "--layout data/garage-3tower.txt", reason];
    }
    return reason;
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
    [self applyPageTitleForIndex:index];
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

#pragma mark - 工具栏（原生）

static NSString *const kSmartParkRefreshItem = @"smartpark.toolbar.refresh";
static NSString *const kSmartParkTitleItem = @"smartpark.toolbar.title";

// 页面标题同时写进窗口标题（Dock / 窗口菜单 / 辅助功能）和工具栏里的居中标题项。
- (void)applyPageTitleForIndex:(NSInteger)index{
    self.currentPageIndex = index;
    NSString *title = pageTitleForIndex(index);
    self.window.title = title;
    if (self.toolbarTitleLabel != nil){
        self.toolbarTitleLabel.stringValue = title;
    }
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar{
    return @[ NSToolbarToggleSidebarItemIdentifier, kSmartParkTitleItem,
              kSmartParkRefreshItem, NSToolbarFlexibleSpaceItemIdentifier ];
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar{
    // 侧边栏开关在最左、刷新在最右，标题居中——与 Finder / 系统设置的栏结构一致。
    return @[ NSToolbarToggleSidebarItemIdentifier,
              NSToolbarFlexibleSpaceItemIdentifier, kSmartParkTitleItem,
              NSToolbarFlexibleSpaceItemIdentifier, kSmartParkRefreshItem ];
}

- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
     itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
     willBeInsertedIntoToolbar:(BOOL)flag{
    if ([itemIdentifier isEqualToString:kSmartParkTitleItem]){
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
        if (self.toolbarTitleLabel == nil){
            NSTextField *label = [NSTextField labelWithString:pageTitleForIndex(self.currentPageIndex)];
            label.font = [NSFont systemFontOfSize:16.0 weight:NSFontWeightSemibold];
            label.textColor = [NSColor labelColor];
            label.alignment = NSTextAlignmentCenter;
            label.accessibilityIdentifier = @"smartpark.toolbar.title";
            self.toolbarTitleLabel = label;
        }
        item.view = self.toolbarTitleLabel;
        item.label = @"标题";
        item.paletteLabel = @"标题";
        return item;
    }
    if ([itemIdentifier isEqualToString:NSToolbarToggleSidebarItemIdentifier]){
        // 系统标准项：AppKit 自带图标与 toggleSidebar: 动作，无需自己接线。
        return [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
    }
    if (![itemIdentifier isEqualToString:kSmartParkRefreshItem]){
        return nil;
    }
    NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
    item.label = @"刷新";
    item.paletteLabel = @"刷新";
    item.toolTip = @"重新加载当前页面的数据";
    item.target = self;
    item.action = @selector(refreshToolbarItem:);
    if (@available(macOS 11.0, *)){
        item.image = [NSImage imageWithSystemSymbolName:@"arrow.clockwise"
                              accessibilityDescription:@"刷新"];
    }
    return item;
}

- (void)refreshToolbarItem:(id)sender{
    [self refreshVisiblePage];
}

@end
