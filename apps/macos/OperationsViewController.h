#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface OperationsViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
// 主菜单「操作」用：切到本页后直接进入入库/出库状态（车牌框聚焦、状态行给出提示）。
- (void)beginCheckIn;
- (void)beginCheckOutWithPlate:(NSString *)plate;
@end
