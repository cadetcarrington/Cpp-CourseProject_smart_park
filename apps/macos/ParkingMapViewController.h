#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface ParkingMapViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
