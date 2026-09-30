#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface DashboardViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
