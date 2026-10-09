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

    // 图例按两列排布：真正占用的行数是 ceil(条数 / 2)，而不是条数本身。
    // 原来按条数预留高度，预留区（4 行 = 68pt）远高于实际图例（2 行 = 32pt），
    // 于是圆环被挤到上方、图例贴在下缘，第二列正好压在圆环下方造成重叠。
    const NSInteger legendColumns = 2;
    const CGFloat legendRowHeight = 16.0;
    const NSInteger legendRows = MAX(1, (NSInteger)(
        (_slices.count + (NSUInteger)legendColumns - 1) / (NSUInteger)legendColumns));
    const CGFloat legendHeight = legendRows * legendRowHeight + 4.0;
    const CGFloat legendGap = 8.0;
    NSRect donutArea = NSMakeRect(area.origin.x, area.origin.y,
                                  area.size.width,
                                  area.size.height - legendHeight - legendGap);
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

    // 中心文字：苹方 + 原生语义色，上下两层整体在圆环内居中。
    // drawInRect: 默认是左对齐，必须显式给居中的段落样式，否则文字会贴在
    // 内圆左缘（实测偏左 10pt），看起来就是「不在中间」。
    NSRect inner = NSInsetRect(donutRect, ring, ring);
    const CGFloat midY = NSMidY(inner);
    NSMutableParagraphStyle *centreStyle = [[NSMutableParagraphStyle alloc] init];
    centreStyle.alignment = NSTextAlignmentCenter;
    centreStyle.lineBreakMode = NSLineBreakByTruncatingTail;
    NSDictionary *titleAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor labelColor],
        NSParagraphStyleAttributeName: centreStyle,
    };
    NSDictionary *valueAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:20.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor labelColor],
        NSParagraphStyleAttributeName: centreStyle,
    };
    const CGFloat valueHeight = 24.0;
    const CGFloat titleHeight = 14.0;
    const CGFloat blockTop = midY - (valueHeight + titleHeight) / 2.0;
    [_centerValue drawInRect:NSMakeRect(inner.origin.x, blockTop,
                                        inner.size.width, valueHeight)
              withAttributes:valueAttrs];
    [_centerTitle drawInRect:NSMakeRect(inner.origin.x, blockTop + valueHeight,
                                        inner.size.width, titleHeight)
              withAttributes:titleAttrs];

    // 图例（两列、紧凑）：苹方 + 次要色，底边对齐 area 下缘。
    const CGFloat columnWidth = area.size.width / 2.0;
    NSDictionary *legendAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };
    const CGFloat legendTop = NSMaxY(area) - legendHeight + 2.0;
    for (NSUInteger i = 0; i < _slices.count; ++i){
        DonutSliceData *slice = _slices[i];
        const NSUInteger column = i % (NSUInteger)legendColumns;
        const NSUInteger row = i / (NSUInteger)legendColumns;
        const CGFloat x = area.origin.x + column * columnWidth;
        const CGFloat y = legendTop + row * legendRowHeight;
        [slice.color setFill];
        NSRectFill(NSMakeRect(x, y + 1.0, 10.0, 10.0));
        NSString *text = [NSString stringWithFormat:@"%@ %.0f", slice.label,
                          MAX(0.0, slice.value)];
        [text drawInRect:NSMakeRect(x + 15.0, y, columnWidth - 18.0, legendRowHeight)
          withAttributes:legendAttrs];
    }
}

@end
