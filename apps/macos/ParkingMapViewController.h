#import <Cocoa/Cocoa.h>

class ParkingBridge;

// 仪表盘「反向寻车」弹窗确认后发出：userInfo[SmartParkFindCarPlateKey] = 车牌。
extern NSNotificationName const SmartParkFindCarRequestNotification;
extern NSString *const SmartParkFindCarPlateKey;

@interface ParkingMapViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
// 外部（仪表盘弹窗）直接触发一次寻车；视图还没加载时先加载。
- (void)findRouteForPlate:(NSString *)plate;
@end
