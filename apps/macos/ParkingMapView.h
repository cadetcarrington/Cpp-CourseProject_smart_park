#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "core/model/Geometry.h"

class ParkingBridge;

// 反向寻车的步行路线叠加（模型坐标，单位 m）。
struct ParkingWalkRoute{
    std::vector<smartpark::Point> entryPoints;  // 最近入口 → 车位（灰色）
    std::vector<smartpark::Point> exitPoints;   // 车位 → 最近出口（蓝色）
    std::string spotId;                         // 目标车位编号，用于标注
    smartpark::Point target{};                  // 车位取车点（画标记用）
};

// 自定义 NSView，用 AppKit 绘制实时车位地图（等价于 Qt 版的 QGraphicsScene 车位图）。
@interface ParkingMapView : NSView
@property (nonatomic, assign) ParkingBridge *bridge;
// 叠加一条反向寻车的步行路线；传空/调用 clearWalkRoute 即清除。内部会重绘。
- (void)setWalkRoute:(const ParkingWalkRoute &)route;
- (void)clearWalkRoute;
@end
