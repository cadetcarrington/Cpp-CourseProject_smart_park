#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface RecordsViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
