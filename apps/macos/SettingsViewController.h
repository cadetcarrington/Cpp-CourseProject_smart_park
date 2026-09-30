#import <Cocoa/Cocoa.h>

class ParkingBridge;

@interface SettingsViewController : NSViewController
@property (nonatomic, assign) ParkingBridge *bridge;
- (void)refresh;
@end
