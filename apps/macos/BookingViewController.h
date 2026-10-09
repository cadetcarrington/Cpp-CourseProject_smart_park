#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface BookingViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
