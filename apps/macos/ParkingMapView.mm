#import "ParkingMapView.h"

#import "TextUtil.h"
#import "bridge/ParkingBridge.h"

#include "core/model/ParkingLayout.h"
#include "core/model/ParkingSpot.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace{
NSColor *rgba(double r, double g, double b, double a){
    return [NSColor colorWithSRGBRed:r / 255.0
                               green:g / 255.0
                                blue:b / 255.0
                               alpha:a / 255.0];
}

// 与 Qt 版 stallFillColor 保持一致的颜色映射。
NSColor *stallFill(const smartpark::ParkingSpot &spot){
    switch (spot.status()){
    case smartpark::SpotStatus::Reserved:
        return rgba(181, 71, 8, 140);
    case smartpark::SpotStatus::Occupied:
        return rgba(180, 35, 24, 255);  // 占用保持不透明红色
    case smartpark::SpotStatus::Disabled:
        return rgba(102, 112, 133, 140);
    case smartpark::SpotStatus::Available:
    default:
        break;
    }
    switch (spot.type()){
    case smartpark::SpotType::Accessible:
        return rgba(79, 70, 229, 140);
    case smartpark::SpotType::Charging:
        return rgba(2, 106, 162, 140);
    case smartpark::SpotType::Vip:
        return rgba(124, 58, 237, 140);
    case smartpark::SpotType::Normal:
    default:
        return rgba(15, 118, 110, 140);
    }
}

NSColor *labelColor(smartpark::SpotStatus status){
    return status == smartpark::SpotStatus::Occupied
        ? [NSColor whiteColor]
        : [NSColor blackColor];
}

const char *spotTypeText(smartpark::SpotType type){
    switch (type){
    case smartpark::SpotType::Accessible:
        return "无障碍";
    case smartpark::SpotType::Charging:
        return "充电";
    case smartpark::SpotType::Vip:
        return "VIP";
    case smartpark::SpotType::Normal:
    default:
        return "普通";
    }
}

const char *vehicleTypeText(smartpark::VehicleType type){
    switch (type){
    case smartpark::VehicleType::Motorcycle:
        return "摩托车";
    case smartpark::VehicleType::Truck:
        return "卡车";
    case smartpark::VehicleType::Electric:
        return "电动车";
    case smartpark::VehicleType::Car:
    default:
        return "轿车";
    }
}

NSString *spotLabel(const smartpark::ParkingSpot &spot){
    NSString *label = smartpark_ui::toNSString(spot.identifier());
    if (spot.parkedVehicle()){
        label = [label stringByAppendingFormat:@"\n%@",
                 smartpark_ui::toNSString(spot.parkedVehicle()->plateNumber())];
    } else if (spot.type() != smartpark::SpotType::Normal
               && spot.status() == smartpark::SpotStatus::Available){
        label = [label stringByAppendingFormat:@"\n%@",
                 smartpark_ui::toNSString(spotTypeText(spot.type()))];
    }
    return label;
}

bool isGarageFloorplan(const smartpark::ParkingLayout &layout){
    return std::fabs(layout.siteWidth() - 58.0) < 0.25
        && std::fabs(layout.siteHeight() - 42.4) < 0.25;
}

struct MapTransform{
    double scale;
    double ox;
    double oy;

    NSPoint modelPoint(NSPoint point) const{
        return NSMakePoint((point.x - ox) / scale, (point.y - oy) / scale);
    }
};

MapTransform mapTransform(NSRect bounds, const smartpark::ParkingLayout &layout){
    const double width = layout.siteWidth();
    const double height = layout.siteHeight();
    if (width <= 0.0 || height <= 0.0
        || bounds.size.width <= 80.0 || bounds.size.height <= 80.0){
        return {0.0, 0.0, 0.0};
    }
    const double scale = MIN((bounds.size.width - 80.0) / width,
                             (bounds.size.height - 80.0) / height);
    return {scale, bounds.origin.x + (bounds.size.width - width * scale) / 2.0,
            bounds.origin.y + (bounds.size.height - height * scale) / 2.0};
}

void drawFittedLabel(NSString *text, NSRect rect, NSColor *color,
                     CGFloat maxFontSize = 10.0){
    NSRect inner = NSInsetRect(rect, 1.5, 1.5);
    if (inner.size.width < 5.0 || inner.size.height < 5.0 || text.length == 0){
        return;
    }
    const BOOL vertical = inner.size.height > inner.size.width * 1.45;
    const NSSize area = vertical
        ? NSMakeSize(inner.size.height, inner.size.width) : inner.size;
    NSArray<NSString *> *lines = [text componentsSeparatedByString:@"\n"];
    CGFloat fontSize = maxFontSize;
    for (; fontSize >= 5.0; fontSize -= 0.5){
        NSFont *font = [NSFont fontWithName:@"PingFang SC" size:fontSize]
            ?: [NSFont systemFontOfSize:fontSize];
        NSDictionary *attrs = @{NSFontAttributeName: font};
        const CGFloat lineHeight = font.ascender - font.descender + font.leading;
        BOOL fits = lineHeight * lines.count <= area.height;
        for (NSString *line in lines){
            fits = fits && [line sizeWithAttributes:attrs].width <= area.width;
        }
        if (fits){
            break;
        }
    }
    fontSize = MAX(5.0, fontSize);
    NSFont *font = [NSFont fontWithName:@"PingFang SC" size:fontSize]
        ?: [NSFont systemFontOfSize:fontSize];
    const CGFloat lineHeight = font.ascender - font.descender + font.leading;
    NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = NSTextAlignmentCenter;
    style.lineBreakMode = NSLineBreakByClipping;
    NSDictionary *attrs = @{NSFontAttributeName: font,
                            NSForegroundColorAttributeName: color,
                            NSParagraphStyleAttributeName: style};
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:rect] addClip];
    if (vertical){
        NSAffineTransform *rotation = [NSAffineTransform transform];
        [rotation translateXBy:NSMidX(inner) yBy:NSMidY(inner)];
        [rotation rotateByDegrees:-90.0];
        [rotation concat];
    }
    const NSRect drawing = vertical
        ? NSMakeRect(-area.width / 2.0, -area.height / 2.0, area.width, area.height)
        : inner;
    CGFloat y = NSMidY(drawing) - lineHeight * lines.count / 2.0;
    for (NSString *line in lines){
        [line drawInRect:NSMakeRect(drawing.origin.x, y, drawing.size.width, lineHeight)
         withAttributes:attrs];
        y += lineHeight;
    }
    [NSGraphicsContext restoreGraphicsState];
}

// 在 path 中追加一段带缺口（入口/出口）的墙体线段。
void addWallSegment(NSBezierPath *path, NSPoint start, NSPoint end,
                    const std::vector<smartpark::Point> &gates, double gapWidth,
                    double scale, double ox, double oy){
    const bool horizontal = std::fabs(start.y - end.y) < 1e-6;
    const double wallStart = horizontal ? MIN(start.x, end.x) : MIN(start.y, end.y);
    const double wallEnd = horizontal ? MAX(start.x, end.x) : MAX(start.y, end.y);
    const double axis = horizontal ? start.y : start.x;

    std::vector<std::pair<double, double>> gaps;
    for (const smartpark::Point &gate : gates){
        const double gateAxis = horizontal ? gate.y : gate.x;
        const double gatePos = horizontal ? gate.x : gate.y;
        if (std::fabs(gateAxis - axis) > 0.6){
            continue;
        }
        const double from = MAX(wallStart, gatePos - gapWidth / 2.0);
        const double to = MIN(wallEnd, gatePos + gapWidth / 2.0);
        if (to > from + 0.2){
            gaps.push_back({from, to});
        }
    }
    std::sort(gaps.begin(), gaps.end());
    std::vector<std::pair<double, double>> merged;
    for (const auto &gap : gaps){
        if (merged.empty() || gap.first > merged.back().second + 0.05){
            merged.push_back(gap);
        } else{
            merged.back().second = MAX(merged.back().second, gap.second);
        }
    }

    auto addSegment = [&](double from, double to){
        if (to - from < 0.15){
            return;
        }
        if (horizontal){
            [path moveToPoint:NSMakePoint(ox + from * scale, oy + axis * scale)];
            [path lineToPoint:NSMakePoint(ox + to * scale, oy + axis * scale)];
        } else{
            [path moveToPoint:NSMakePoint(ox + axis * scale, oy + from * scale)];
            [path lineToPoint:NSMakePoint(ox + axis * scale, oy + to * scale)];
        }
    };

    double cursor = wallStart;
    for (const auto &gap : merged){
        addSegment(cursor, gap.first);
        cursor = gap.second;
    }
    addSegment(cursor, wallEnd);
}
} // namespace

@implementation ParkingMapView{
    NSTrackingArea *_trackingArea;
}

- (BOOL)isFlipped{
    return YES;  // 原点左上、y 向下，与 Qt 场景坐标一致
}

- (void)setFrameSize:(NSSize)newSize{
    [super setFrameSize:newSize];
    [self setNeedsDisplay:YES];
}

- (void)viewDidChangeEffectiveAppearance{
    [super viewDidChangeEffectiveAppearance];
    [self setNeedsDisplay:YES];
}

- (void)updateTrackingAreas{
    [super updateTrackingAreas];
    if (_trackingArea){
        [self removeTrackingArea:_trackingArea];
    }
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)mouseMoved:(NSEvent *)event{
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const MapTransform transform = mapTransform(self.bounds, self.bridge->layout());
    if (transform.scale <= 0.0){
        return;
    }
    const NSPoint model = transform.modelPoint(p);
    for (const smartpark::ParkingSpot &spot : self.bridge->spots()){
        const smartpark::Rectangle &b = spot.bounds();
        if (model.x >= b.origin.x && model.x <= b.origin.x + b.width
            && model.y >= b.origin.y && model.y <= b.origin.y + b.height){
            NSString *plate = spot.parkedVehicle()
                ? smartpark_ui::toNSString(spot.parkedVehicle()->plateNumber())
                : @"-";
            NSString *vehicle = spot.parkedVehicle()
                ? smartpark_ui::toNSString(vehicleTypeText(spot.parkedVehicle()->type()))
                : @"-";
            self.toolTip = [NSString stringWithFormat:@"%@ | %@ | %@ | %@ | %@",
                smartpark_ui::toNSString(spot.identifier()),
                smartpark_ui::toNSString(spotTypeText(spot.type())),
                (spot.status() == smartpark::SpotStatus::Occupied ? @"占用"
                 : spot.status() == smartpark::SpotStatus::Reserved ? @"预订"
                 : spot.status() == smartpark::SpotStatus::Disabled ? @"停用" : @"空闲"),
                plate, vehicle];
            return;
        }
    }
    self.toolTip = nil;
}

- (void)drawRect:(NSRect)dirtyRect{
    [super drawRect:dirtyRect];
    if (self.bridge == nullptr || !self.bridge->ready()){
        return;
    }
    const smartpark::ParkingLayout &layout = self.bridge->layout();
    const std::vector<smartpark::ParkingSpot> &spots = self.bridge->spots();
    const double siteW = layout.siteWidth();
    const double siteH = layout.siteHeight();
    if (siteW <= 0.0 || siteH <= 0.0){
        return;
    }

    const MapTransform transform = mapTransform(self.bounds, layout);
    if (transform.scale <= 0.0){
        return;
    }
    const double scale = transform.scale;
    const double ox = transform.ox;
    const double oy = transform.oy;

    auto tx = [&](double x){ return ox + x * scale; };
    auto ty = [&](double y){ return oy + y * scale; };
    auto rectFor = [&](const smartpark::Rectangle &r){
        return NSMakeRect(tx(r.origin.x), ty(r.origin.y),
                          r.width * scale, r.height * scale);
    };

    const bool garage = isGarageFloorplan(layout);

    // 场地背景
    [rgba(232, 234, 229, 255) setFill];
    NSRectFill(NSMakeRect(ox, oy, siteW * scale, siteH * scale));

    // 网格 + 坐标轴标签
    if (garage){
        const double xAxes[] = {0.0, 9.0, 18.0, 27.0, 36.0, 45.0, 54.0, 58.0};
        const char *xLabels[] = {"6-1", "6-2", "6-3", "6-4", "6-5", "6-6", "6-7", "6-8"};
        const double yAxes[] = {0.0, 12.2, 24.4, 33.4, 42.4};
        const char *yLabels[] = {"6-E", "6-D", "6-C", "6-B", "6-A"};
        [rgba(186, 190, 184, 255) setStroke];
        for (double x : xAxes){
            NSBezierPath *line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(tx(x), ty(0.0))];
            [line lineToPoint:NSMakePoint(tx(x), ty(siteH))];
            [line stroke];
        }
        for (double y : yAxes){
            NSBezierPath *line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(tx(0.0), ty(y))];
            [line lineToPoint:NSMakePoint(tx(siteW), ty(y))];
            [line stroke];
        }
        NSDictionary *axisAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName: [NSColor labelColor],
        };
        for (int i = 0; i < 8; ++i){
            [[NSString stringWithUTF8String:xLabels[i]]
                drawInRect:NSMakeRect(tx(xAxes[i]) - 12, ty(0.0) - 16, 24, 14)
             withAttributes:axisAttrs];
        }
        for (int i = 0; i < 5; ++i){
            [[NSString stringWithUTF8String:yLabels[i]]
                drawInRect:NSMakeRect(tx(0.0) - 24, ty(yAxes[i]) - 7, 22, 14)
             withAttributes:axisAttrs];
        }
    } else{
        [rgba(198, 204, 210, 255) setStroke];
        for (double x = 5.0; x < siteW; x += 5.0){
            NSBezierPath *line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(tx(x), ty(0.0))];
            [line lineToPoint:NSMakePoint(tx(x), ty(siteH))];
            [line stroke];
        }
        for (double y = 5.0; y < siteH; y += 5.0){
            NSBezierPath *line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(tx(0.0), ty(y))];
            [line lineToPoint:NSMakePoint(tx(siteW), ty(y))];
            [line stroke];
        }
    }

    // 内置车库的区域包络包含通道，只绘制普通布局的区域边界。
    if (!garage){
        for (const smartpark::Rectangle &region : layout.regions()){
            NSRect r = rectFor(region);
            [rgba(226, 230, 235, 40) setFill];
            NSRectFill(r);
            NSBezierPath *p = [NSBezierPath bezierPathWithRect:r];
            [rgba(170, 178, 188, 255) setStroke];
            [p stroke];
        }
    }

    // 障碍物
    for (const smartpark::LayoutObstacle &obstacle : layout.obstacles()){
        NSRect r = rectFor(obstacle.bounds);
        [rgba(210, 214, 218, 255) setFill];
        NSRectFill(r);
        NSBezierPath *p = [NSBezierPath bezierPathWithRect:r];
        [rgba(92, 96, 102, 255) setStroke];
        [p stroke];
        drawFittedLabel(smartpark_ui::toNSString(obstacle.name), r,
                        rgba(48, 52, 56, 255), 12.0);
    }

    // 入口 / 出口标记
    for (const smartpark::Point &entrance : layout.entrances()){
        if (entrance.y <= 0.6){
            NSRect r = NSMakeRect(tx(entrance.x - 3.2), ty(0.05), 6.4 * scale, 2.4 * scale);
            [rgba(176, 178, 172, 255) setFill];
            NSRectFill(r);
        }
    }
    for (const smartpark::Point &exit : layout.exits()){
        if (exit.y <= 0.6){
            NSRect r = NSMakeRect(tx(exit.x - 2.6), ty(0.05), 5.2 * scale, 2.2 * scale);
            [rgba(176, 178, 172, 255) setFill];
            NSRectFill(r);
        }
    }

    // 车位
    for (const smartpark::ParkingSpot &spot : spots){
        NSRect r = rectFor(spot.bounds());
        NSColor *fill = stallFill(spot);
        [fill setFill];
        NSBezierPath *p = [NSBezierPath bezierPathWithRect:r];
        [p fill];
        [rgba(48, 52, 56, 255) setStroke];
        p.lineWidth = 0.5;
        [p stroke];

        drawFittedLabel(spotLabel(spot), r, labelColor(spot.status()));
    }

    // 墙体（带入口/出口缺口）
    std::vector<smartpark::Point> gates = layout.entrances();
    gates.insert(gates.end(), layout.exits().begin(), layout.exits().end());
    NSBezierPath *wall = [NSBezierPath bezierPath];
    addWallSegment(wall, NSMakePoint(0.0, 0.0), NSMakePoint(siteW, 0.0), gates, 4.6, scale, ox, oy);
    addWallSegment(wall, NSMakePoint(0.0, siteH), NSMakePoint(siteW, siteH), gates, 4.6, scale, ox, oy);
    addWallSegment(wall, NSMakePoint(0.0, 0.0), NSMakePoint(0.0, siteH), gates, 4.6, scale, ox, oy);
    addWallSegment(wall, NSMakePoint(siteW, 0.0), NSMakePoint(siteW, siteH), gates, 4.6, scale, ox, oy);
    [rgba(46, 50, 54, 255) setStroke];
    wall.lineWidth = 2.0;
    [wall stroke];

    // 入口 / 出口文字
    NSDictionary *entryAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: rgba(36, 72, 160, 255),
    };
    NSDictionary *exitAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: rgba(176, 84, 24, 255),
    };
    for (const smartpark::Point &entrance : layout.entrances()){
        [@"入口" drawInRect:NSMakeRect(tx(entrance.x) - 14, ty(0.0) - 26, 28, 12)
             withAttributes:entryAttrs];
    }
    for (const smartpark::Point &exit : layout.exits()){
        [@"出口" drawInRect:NSMakeRect(tx(exit.x) - 14, ty(0.0) - 26, 28, 12)
             withAttributes:exitAttrs];
    }
}

@end
