#import "DonutChartView.h"

@implementation DonutSliceData
@end

@implementation DonutChartView{
    NSArray<DonutSliceData *> *_slices;
    NSString *_centerTitle;
    NSString *_centerValue;
}

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _slices = @[];
        _centerTitle = @"总计";
        _centerValue = @"0";
    }
    return self;
}

- (BOOL)isFlipped{
    return YES;
}

- (void)setSlices:(NSArray<DonutSliceData *> *)slices{
    _slices = [slices copy];
    [self setNeedsDisplay:YES];
}

- (void)setCenterTitle:(NSString *)title{
    _centerTitle = [title copy];
    [self setNeedsDisplay:YES];
}

- (void)setCenterValue:(NSString *)value{
    _centerValue = [value copy];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect{
    [super drawRect:dirtyRect];
    NSRect area = NSInsetRect(self.bounds, 10, 10);
    if (area.size.width <= 40 || area.size.height <= 40){
        return;
    }

    double total = 0.0;
    for (DonutSliceData *slice in _slices){
        total += MAX(0.0, slice.value);
    }

    NSInteger legendRows = MAX(1, (NSInteger)_slices.count);
    CGFloat legendHeight = MIN(72.0, legendRows * 16.0 + 4.0);
    NSRect donutArea = NSMakeRect(area.origin.x, area.origin.y + 8.0,
                                  area.size.width,
                                  area.size.height - 8.0 - legendHeight - 6.0);
    if (donutArea.size.width <= 40 || donutArea.size.height <= 40){
        return;
    }

    CGFloat diameter = MIN(donutArea.size.width, donutArea.size.height);
    CGFloat ring = MAX(14.0, diameter / 4.5);
    CGFloat side = diameter - ring - 4.0;
    NSRect donutRect = NSMakeRect(NSMidX(donutArea) - side / 2.0,
                                  NSMidY(donutArea) - side / 2.0, side, side);

    if (total <= 0.0 || _slices.count == 0){
        NSBezierPath *empty = [NSBezierPath bezierPathWithOvalInRect:donutRect];
        empty.lineWidth = ring;
        empty.lineCapStyle = NSLineCapStyleButt;
        [[NSColor colorWithSRGBRed:0.9 green:0.9 blue:0.9 alpha:1.0] setStroke];
        [empty stroke];
    } else{
        double angle = 90.0;
        for (DonutSliceData *slice in _slices){
            double sweep = -MAX(0.0, slice.value) / total * 360.0;
            if (fabs(sweep) < 0.001){
                continue;
            }
            NSBezierPath *arc = [NSBezierPath bezierPath];
            [arc appendBezierPathWithArcWithCenter:NSMakePoint(NSMidX(donutRect), NSMidY(donutRect))
                                            radius:side / 2.0
                                        startAngle:angle
                                          endAngle:angle + sweep
                                         clockwise:YES];
            arc.lineWidth = ring;
            arc.lineCapStyle = NSLineCapStyleButt;
            [slice.color setStroke];
            [arc stroke];
            angle += sweep;
        }
    }

    // 中心文字：苹方 + 原生语义色，上下分层避免冲突。
    NSRect inner = NSInsetRect(donutRect, ring, ring);
    CGFloat midY = NSMidY(inner);
    NSDictionary *titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };
    NSDictionary *valueAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:20.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };
    [_centerValue drawInRect:NSMakeRect(inner.origin.x, midY - 8.0, inner.size.width, 24.0)
              withAttributes:valueAttrs];
    [_centerTitle drawInRect:NSMakeRect(inner.origin.x, midY + 18.0, inner.size.width, 14.0)
              withAttributes:titleAttrs];

    // 图例（两列、紧凑）：苹方 + 次要色。
    CGFloat columnWidth = area.size.width / 2.0;
    CGFloat rowHeight = 16.0;
    NSDictionary *legendAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };
    for (NSUInteger i = 0; i < _slices.count; ++i){
        DonutSliceData *slice = _slices[i];
        NSUInteger column = i % 2;
        NSUInteger row = i / 2;
        CGFloat x = area.origin.x + column * columnWidth;
        CGFloat y = NSMaxY(area) - legendHeight + 2.0 + row * rowHeight;
        [slice.color setFill];
        NSRectFill(NSMakeRect(x, y + 1.0, 10.0, 10.0));
        NSString *text = [NSString stringWithFormat:@"%@ %.0f", slice.label,
                          MAX(0.0, slice.value)];
        [text drawInRect:NSMakeRect(x + 15.0, y, columnWidth - 18.0, rowHeight)
          withAttributes:legendAttrs];
    }
}

@end
