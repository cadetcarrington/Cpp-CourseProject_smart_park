#import <Cocoa/Cocoa.h>

class ParkingBridge;

// 自定义 NSView，用 AppKit 绘制实时车位地图（等价于 Qt 版的 QGraphicsScene 车位图）。
@interface ParkingMapView : NSView
@property (nonatomic, assign) ParkingBridge *bridge;
@end
