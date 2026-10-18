#import "LineChartView.h"

@implementation LinePointData
@end

@implementation LineSeriesData
@end

@implementation LineChartView{
    NSArray<LineSeriesData *> *_series;
    NSString *_unit;
    NSInteger _valueDecimals;
    double _axisMax;
}

- (instancetype)initWithFrame:(NSRect)frameRect{
    if ((self = [super initWithFrame:frameRect])){
        _series = @[];
        _unit = @"%";
        _valueDecimals = 0;
        _axisMax = 0.0;
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

// 纵轴默认上限：数据没长到这个高度时按它画（刻度固定，小数据不会被拉满整张图），
// 数据超过它时自动继续往上加。0 表示纯自动（按数据最大值取整）。
- (void)setAxisMax:(double)axisMax{
    _axisMax = MAX(0.0, axisMax);
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
            NSFontAttributeName: [NSFont systemFontOfSize:12],
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

    // 刻度取整：收入（元）、流量（辆）、占用率（%）都是整数单位，轴上不该出现
    // 「0.29 元」这种刻度——那只是把 1.15（数据最大值 1.0 加 15% 余量）四等分的
    // 结果。整数单位就用整数刻度：在 2~5 等分里挑「总高度最小、又能装下数据」
    // 的一档，步长取 1／2／5／25… 这类整齐的整数。显式要求小数的图
    // （setValueDecimals: 大于 0）保留原来的 15% 余量。
    auto niceStep = [](double x){
        if (x <= 1.0){
            return 1.0;
        }
        double base = pow(10.0, floor(log10(x)));
        const double multipliers[] = {1.0, 2.0, 2.5, 5.0, 10.0};
        for (double multiplier : multipliers){
            double step = multiplier * base;
            if (step < 1.0 || step != floor(step)){
                continue;
            }
            if (step >= x){
                return step;
            }
        }
        return 10.0 * base;
    };
    NSInteger divisions = 4;
    if (_valueDecimals <= 0){
        // _axisMax 是「默认上限」：数据没长到那儿就按它铺满刻度（小数据不会被
        // 拉满整张图），数据超过它时继续往上加，不会截断。
        double rawHigh = MAX(high, _axisMax);
        double bestSpan = 0.0;
        for (NSInteger d = 2; d <= 5; ++d){
            double span = niceStep(rawHigh / (double)d) * (double)d;
            if (bestSpan <= 0.0 || span <= bestSpan){
                bestSpan = span;
                divisions = d;
            }
        }
        high = bestSpan;
    } else{
        high = MAX(high * 1.15, _axisMax);
    }

    NSInteger maxPoints = 0;
    for (LineSeriesData *series in _series){
        maxPoints = MAX(maxPoints, (NSInteger)series.points.count);
    }

    NSDictionary *tickAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0],
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
    for (NSInteger i = 0; i <= divisions; ++i){
        double ratio = (double)i / (double)divisions;
        CGFloat y = NSMaxY(plot) - ratio * plot.size.height;
        NSBezierPath *line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(plot.origin.x, y)];
        [line lineToPoint:NSMakePoint(NSMaxX(plot), y)];
        line.lineWidth = 1.0;
        [gridColor setStroke];
        [line stroke];

        double value = low + (high - low) * ratio;
        NSString *text = [NSString stringWithFormat:@"%.*f%@", (int)_valueDecimals, value, _unit];
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
        NSFontAttributeName: [NSFont systemFontOfSize:11.0],
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
            NSFontAttributeName: [NSFont systemFontOfSize:11.0],
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
