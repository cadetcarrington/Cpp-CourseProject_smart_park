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
