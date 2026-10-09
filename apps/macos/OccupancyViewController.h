#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface OccupancyViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
