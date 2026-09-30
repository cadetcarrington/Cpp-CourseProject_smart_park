#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface OperationsViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
