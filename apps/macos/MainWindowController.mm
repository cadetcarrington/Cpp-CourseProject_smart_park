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
@end

@implementation MainWindowController{
    std::unique_ptr<ParkingBridge> bridge_;
}

- (instancetype)init{
    return [self initWithUserName:nil];
}

- (instancetype)initWithUserName:(NSString *)userName{
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1120, 720)
        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                   NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                   NSWindowStyleMaskFullSizeContentView)
        backing:NSBackingStoreBuffered
        defer:NO];
    if ((self = [super initWithWindow:window])){
        bridge_ = std::make_unique<ParkingBridge>();

        window.title = @"SmartPark Admin";
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

        // 构建 7 个页面，共享同一个 bridge。
        DashboardViewController *dashboard = [[DashboardViewController alloc] init];
        dashboard.bridge = bridge_.get();
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

- (void)dataChanged:(NSNotification *)notification{
    for (NSViewController *vc in self.pages){
        if ([vc respondsToSelector:@selector(refresh)]){
            [vc performSelector:@selector(refresh)];
        }
    }
}

@end
