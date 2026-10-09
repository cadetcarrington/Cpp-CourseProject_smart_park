#import "BarChartView.h"

@implementation BarSliceData
@end

@implementation BarChartView{
    NSArray<BarSliceData *> *_bars;
}

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _bars = @[];
    }
    return self;
}

- (BOOL)isFlipped{
    return YES;
}

- (void)setBars:(NSArray<BarSliceData *> *)bars{
    _bars = [bars copy];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect{
    [super drawRect:dirtyRect];
    NSRect area = NSInsetRect(self.bounds, 10, 10);
    if (area.size.width <= 40 || area.size.height <= 24){
        return;
    }
    if (_bars.count == 0){
        NSDictionary *attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:110/255.0 green:106/255.0 blue:97/255.0 alpha:1.0],
        };
        [@"暂无数据" drawInRect:area withAttributes:attrs];
        return;
    }

    NSDictionary *labelAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };
    NSDictionary *valueAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.0],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };

    CGFloat maxLabelWidth = 24.0;
    for (BarSliceData *slice in _bars){
        NSSize size = [slice.label sizeWithAttributes:labelAttrs];
        maxLabelWidth = MAX(maxLabelWidth, size.width);
    }
    CGFloat labelColumn = MIN(maxLabelWidth + 18.0, MAX(48.0, area.size.width / 4.0));

    CGFloat maxValueWidth = 24.0;
    for (BarSliceData *slice in _bars){
        NSString *text = [self valueTextFor:slice];
        NSSize size = [text sizeWithAttributes:valueAttrs];
        maxValueWidth = MAX(maxValueWidth, size.width);
    }
    CGFloat valueColumn = maxValueWidth + 14.0;

    CGFloat barLeft = area.origin.x + labelColumn;
    CGFloat barRight = NSMaxX(area) - valueColumn;
    CGFloat barWidth = MAX(4.0, barRight - barLeft);

    double maxValue = 0.0;
    for (BarSliceData *slice in _bars){
        maxValue = MAX(maxValue, fabs(slice.value));
    }
    if (maxValue <= 0.0){
        maxValue = 1.0;
    }

    NSInteger count = (NSInteger)_bars.count;
    CGFloat rowHeight = MIN(40.0, area.size.height / count);
    CGFloat barHeight = MAX(10.0, MIN(20.0, rowHeight - 12.0));
    CGFloat top = area.origin.y + MAX(0.0, (area.size.height - count * rowHeight) / 2.0);

    for (NSInteger i = 0; i < count; ++i){
        BarSliceData *slice = _bars[i];
        CGFloat y = top + i * rowHeight;
        CGFloat centerY = y + rowHeight / 2.0;

        // 标签
        NSRect labelRect = NSMakeRect(area.origin.x, centerY - 6.0,
                                      labelColumn - 8.0, 12.0);
        [slice.label drawInRect:labelRect withAttributes:labelAttrs];

        // 背景条
        CGFloat barTop = centerY - barHeight / 2.0;
        NSBezierPath *track = [NSBezierPath bezierPathWithRoundedRect:
            NSMakeRect(barLeft, barTop, barWidth, barHeight)
            xRadius:barHeight / 2.0 yRadius:barHeight / 2.0];
        [[NSColor colorWithSRGBRed:234/255.0 green:231/255.0 blue:224/255.0 alpha:1.0] setFill];
        [track fill];

        // 填充条
        double ratio = MIN(1.0, MAX(0.0, fabs(slice.value) / maxValue));
        CGFloat fillWidth = MAX(2.0, ratio * barWidth);
        if (fillWidth > 0){
            NSColor *fillColor = [slice.color colorWithAlphaComponent:0.92];
            NSBezierPath *fill = [NSBezierPath bezierPathWithRoundedRect:
                NSMakeRect(barLeft, barTop, MIN(fillWidth, barWidth), barHeight)
                xRadius:barHeight / 2.0 yRadius:barHeight / 2.0];
            [fillColor setFill];
            [fill fill];
        }

        // 数值
        NSString *valueText = [self valueTextFor:slice];
        NSRect valueRect = NSMakeRect(barRight + 8.0, centerY - 6.0,
                                      valueColumn - 8.0, 12.0);
        [valueText drawInRect:valueRect withAttributes:valueAttrs];
    }
}

- (NSString *)valueTextFor:(BarSliceData *)slice{
    if (slice.valueText.length > 0){
        return slice.valueText;
    }
    return [NSString stringWithFormat:@"%.1f", slice.value];
}

@end
