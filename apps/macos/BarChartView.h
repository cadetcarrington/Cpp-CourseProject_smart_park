#import <Cocoa/Cocoa.h>

@interface BarSliceData : NSObject
@property (nonatomic, copy) NSString *label;
@property (nonatomic, assign) double value;
@property (nonatomic, strong) NSColor *color;
@property (nonatomic, copy) NSString *valueText;
@end

// 水平条形图（等价于 Qt 版 BarChartWidget）。
@interface BarChartView : NSView
- (void)setBars:(NSArray<BarSliceData *> *)bars;
// 条形长度的默认满格值（0 = 按最大值自动）：数据超过它时继续按比例画满。
- (void)setAxisMax:(double)axisMax;
@end
