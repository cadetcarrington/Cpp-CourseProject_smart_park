#import "LineChartView.h"

@implementation LinePointData
@end

@implementation LineSeriesData
@end

@implementation LineChartView{
    NSArray<LineSeriesData *> *_series;
    NSString *_unit;
    NSInteger _valueDecimals;
}

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _series = @[];
        _unit = @"%";
        _valueDecimals = 0;
    }
    return self;
}

- (BOOL)isFlipped{
    return YES;
}

- (void)setSeries:(NSArray<LineSeriesData *> *)series{
    _series = [series copy];
    [self setNeedsDisplay:YES];
}

- (void)setUnit:(NSString *)unit{
    _unit = [unit copy];
    [self setNeedsDisplay:YES];
}

- (void)setValueDecimals:(NSInteger)decimals{
    _valueDecimals = decimals;
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect{
    [super drawRect:dirtyRect];
    NSRect area = NSInsetRect(self.bounds, 12, 10);
    if (area.size.width <= 60 || area.size.height <= 40){
        return;
    }
    if (_series.count == 0){
        NSDictionary *attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11],
            NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:110/255.0 green:106/255.0 blue:97/255.0 alpha:1.0],
        };
        [@"暂无数据" drawInRect:area withAttributes:attrs];
        return;
    }

    // 自动缩放
    double low = 0.0, high = 1.0;
    BOOL found = NO;
    for (LineSeriesData *series in _series){
        for (LinePointData *point in series.points){
            if (!found){
                low = high = point.value;
                found = YES;
            } else{
                low = MIN(low, point.value);
                high = MAX(high, point.value);
            }
        }
    }
    if (!found){
        low = 0.0; high = 1.0;
    }
    if (low > 0.0){
        low = 0.0;
    }
    if (high <= low){
        high = low + 1.0;
    }
    high = high * 1.15;

    NSInteger maxPoints = 0;
    for (LineSeriesData *series in _series){
        maxPoints = MAX(maxPoints, (NSInteger)series.points.count);
    }

    NSDictionary *tickAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.0],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };

    CGFloat yLabelWidth = 46.0;
    CGFloat xLabelHeight = 20.0;
    CGFloat legendHeight = _series.count > 1 ? 22.0 : 0.0;
    NSRect plot = NSMakeRect(area.origin.x + yLabelWidth, area.origin.y + 4.0,
                             area.size.width - yLabelWidth - 4.0,
                             area.size.height - 4.0 - xLabelHeight - legendHeight - 2.0);
    if (plot.size.width <= 40 || plot.size.height <= 24){
        return;
    }

    // 水平网格 + y 轴刻度
    NSColor *gridColor = [NSColor colorWithSRGBRed:234/255.0 green:231/255.0 blue:224/255.0 alpha:1.0];
    for (int i = 0; i <= 4; ++i){
        double ratio = (double)i / 4.0;
        CGFloat y = NSMaxY(plot) - ratio * plot.size.height;
        NSBezierPath *line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(plot.origin.x, y)];
        [line lineToPoint:NSMakePoint(NSMaxX(plot), y)];
        line.lineWidth = 1.0;
        [gridColor setStroke];
        [line stroke];

        double value = low + (high - low) * ratio;
        double span = high - low;
        NSInteger decimals = span < 2.0 ? 2 : (span < 10.0 ? 1 : 0);
        NSString *text = [NSString stringWithFormat:@"%.*f%@", (int)decimals, value, _unit];
        [text drawInRect:NSMakeRect(area.origin.x, y - 6.0, yLabelWidth - 6.0, 12.0)
          withAttributes:tickAttrs];
    }

    auto mapX = ^CGFloat(NSInteger index){
        if (maxPoints <= 1){
            return NSMidX(plot);
        }
        return plot.origin.x + (double)index * plot.size.width / (maxPoints - 1);
    };
    auto mapY = ^CGFloat(double value){
        double clamped = MAX(low, MIN(high, value));
        double ratio = (clamped - low) / (high - low);
        return NSMaxY(plot) - ratio * plot.size.height;
    };

    // x 轴标签（用第一条线的 labels）
    NSDictionary *labelAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10.0],
        NSForegroundColorAttributeName: [NSColor labelColor],
    };
    LineSeriesData *labelSeries = _series.firstObject;
    for (NSInteger i = 0; i < maxPoints; ++i){
        CGFloat x = mapX(i);
        NSString *label = @"";
        if (i < (NSInteger)labelSeries.points.count){
            label = labelSeries.points[i].label;
        }
        NSRect labelRect = NSMakeRect(x - 32.0, NSMaxY(plot) + 4.0, 64.0, xLabelHeight - 2.0);
        [label drawInRect:labelRect withAttributes:labelAttrs];
    }

    // 折线 + 点
    for (LineSeriesData *series in _series){
        if (series.points.count == 0){
            continue;
        }
        NSBezierPath *path = [NSBezierPath bezierPath];
        for (NSInteger i = 0; i < (NSInteger)series.points.count; ++i){
            LinePointData *point = series.points[i];
            NSPoint p = NSMakePoint(mapX(i), mapY(point.value));
            if (i == 0){
                [path moveToPoint:p];
            } else{
                [path lineToPoint:p];
            }
        }
        path.lineWidth = 2.2;
        path.lineCapStyle = NSLineCapStyleRound;
        path.lineJoinStyle = NSLineJoinStyleRound;
        [series.color setStroke];
        [path stroke];

        [series.color setFill];
        for (NSInteger i = 0; i < (NSInteger)series.points.count; ++i){
            LinePointData *point = series.points[i];
            NSRect dot = NSMakeRect(mapX(i) - 3.4, mapY(point.value) - 3.4, 6.8, 6.8);
            NSBezierPath *dotPath = [NSBezierPath bezierPathWithOvalInRect:dot];
            [dotPath fill];
        }
    }

    // 图例（多条线时）
    if (_series.count > 1){
        NSDictionary *legendAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:10.0],
            NSForegroundColorAttributeName: [NSColor labelColor],
        };
        CGFloat cursorX = plot.origin.x;
        CGFloat rowY = NSMaxY(area) - legendHeight + 2.0;
        for (LineSeriesData *series in _series){
            CGFloat textWidth = [series.name sizeWithAttributes:legendAttrs].width + 8.0;
            [series.color setFill];
            NSRectFill(NSMakeRect(cursorX, rowY + 3.0, 10.0, 10.0));
            [series.name drawInRect:NSMakeRect(cursorX + 14.0, rowY, textWidth, legendHeight - 4.0)
                     withAttributes:legendAttrs];
            cursorX += 10.0 + 6.0 + textWidth;
        }
    }
}

@end
