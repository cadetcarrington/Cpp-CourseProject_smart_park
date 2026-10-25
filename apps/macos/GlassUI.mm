#import "GlassUI.h"

BOOL SmartParkGlassEnabled(void){
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    if (@available(macOS 26.0, *)){
        return YES;
    }
#endif
    return NO;
}

NSView * _Nullable SmartParkGlassWrap(NSView *content, CGFloat cornerRadius){
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    if (@available(macOS 26.0, *)){
        // NSGlassEffectView 只保证 contentView 位于玻璃效果内（其余子视图
        // 的 z 序不受承诺），因此这里只做 contentView 一层，不混排。
        NSGlassEffectView *glass = [[NSGlassEffectView alloc] init];
        glass.cornerRadius = cornerRadius;
        glass.contentView = content;
        return glass;
    }
#else
    (void)content;
    (void)cornerRadius;
#endif
    return nil;
}

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000

// 胶囊按钮的玻璃外壳：无边框按钮失去系统按压高亮后，用悬停着色补回
// 可交互反馈（Apple 液态玻璃示例里的 hover tint 手法）。
// API_AVAILABLE 声明：旧系统上该子类不注册、引用返回 Nil，
// 只会在 @available(macOS 26) 分支里被创建。
API_AVAILABLE(macos(26.0))
@interface SmartParkGlassHoverChrome : NSGlassEffectView
@end

@implementation SmartParkGlassHoverChrome{
    BOOL _hovered;
}

- (void)updateTrackingAreas{
    [super updateTrackingAreas];
    // 尺寸变化后重建跟踪区，避免旧矩形失效。
    for (NSTrackingArea *area in [self.trackingAreas copy]){
        [self removeTrackingArea:area];
    }
    [self addTrackingArea:[[NSTrackingArea alloc]
        initWithRect:self.bounds
              options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp
                 owner:self userInfo:nil]];
}

- (void)mouseEntered:(NSEvent *)event{
    _hovered = YES;
    self.tintColor = [NSColor colorWithWhite:0.0 alpha:0.10];
}

- (void)mouseExited:(NSEvent *)event{
    _hovered = NO;
    self.tintColor = nil;
}

@end

#endif  // __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000

NSView *SmartParkGlassButton(NSButton *button, CGFloat cornerRadius){
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
    if (@available(macOS 26.0, *)){
        button.bordered = NO;
        button.font = [NSFont systemFontOfSize:button.font.pointSize
                                       weight:NSFontWeightSemibold];
        button.contentTintColor = [NSColor controlAccentColor];
        // 胶囊高度略加余量，玻璃圆角≈高度一半即成胶囊形。
        [button.heightAnchor constraintGreaterThanOrEqualToConstant:26].active = YES;
        [button.widthAnchor constraintGreaterThanOrEqualToConstant:84].active = YES;

        SmartParkGlassHoverChrome *glass = [[SmartParkGlassHoverChrome alloc] init];
        glass.cornerRadius = cornerRadius;
        glass.contentView = button;
        return glass;
    }
#else
    (void)cornerRadius;
#endif
    return button;
}
