#import <Cocoa/Cocoa.h>

@interface DonutSliceData : NSObject
@property (nonatomic, copy) NSString *label;
@property (nonatomic, assign) double value;
@property (nonatomic, strong) NSColor *color;
@end

// 环形占比图（等价于 Qt 版 DonutChartWidget）。
@interface DonutChartView : NSView
- (void)setSlices:(NSArray<DonutSliceData *> *)slices;
- (void)setCenterTitle:(NSString *)title;
- (void)setCenterValue:(NSString *)value;
@end
