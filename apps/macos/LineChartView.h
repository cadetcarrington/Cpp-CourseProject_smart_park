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
@end
