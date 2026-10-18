#import <Cocoa/Cocoa.h>

@interface LinePointData : NSObject
@property (nonatomic, copy) NSString *label;
@property (nonatomic, assign) double value;
@end

@interface LineSeriesData : NSObject
@property (nonatomic, copy) NSString *name;
@property (nonatomic, strong) NSColor *color;
@property (nonatomic, copy) NSArray<LinePointData *> *points;
@end

// 折线图（等价于 Qt 版 LineChartWidget）。
@interface LineChartView : NSView
- (void)setSeries:(NSArray<LineSeriesData *> *)series;
- (void)setUnit:(NSString *)unit;
- (void)setValueDecimals:(NSInteger)decimals;
// 纵轴默认上限（0 = 纯自动）：数据没到就按它铺满刻度，超过就继续往上加。
- (void)setAxisMax:(double)axisMax;
@end
