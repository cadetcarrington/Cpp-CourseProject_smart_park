#import <Cocoa/Cocoa.h>

class ParkingBridge;

// 仪表盘页面的页码（与侧边栏顺序一致），供快捷操作卡片跳转使用。
typedef NS_ENUM(NSInteger, SmartParkPage){
    SmartParkPageDashboard = 0,
    SmartParkPageMap = 1,
    SmartParkPageOperations = 2,
    SmartParkPageOccupancy = 3,
    SmartParkPageBooking = 4,
    SmartParkPageRecords = 5,
    SmartParkPageSettings = 6,
};

@interface DashboardViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
// 点击快捷操作卡片时回调目标页码，由 MainWindowController 负责切页。
@property (nonatomic, copy) void (^quickActionHandler)(SmartParkPage page);
- (void)refresh;
@end
