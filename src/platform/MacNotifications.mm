#import <AppKit/AppKit.h>

#include "MacNotifications.h"

#include <cmath>
#include <cstdlib>

namespace{
constexpr CGFloat kBannerWidth = 320.0;
constexpr CGFloat kBannerMargin = 24.0;
constexpr CGFloat kBannerTopOffset = 56.0;
constexpr CGFloat kBannerGap = 8.0;

// 活动横幅队列：ARC 下横幅的唯一强持有者，同时决定多条横幅的层叠位置。
NSMutableArray<NSPanel *> *ActiveBanners(){
    static NSMutableArray<NSPanel *> *banners = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        banners = [NSMutableArray array];
    });
    return banners;
}

NSString *FromUTF8(const char *text){
    return text != nullptr ? [NSString stringWithUTF8String:text] : @"";
}

// 测试可缩自动消失时长；默认 5 秒。
NSTimeInterval AutoDismissSeconds(){
    const char *override = std::getenv("SMARTPARK_BANNER_SECONDS");
    if (override != nullptr){
        char *end = nullptr;
        const double value = std::strtod(override, &end);
        if (end != override && value > 0.0 && std::isfinite(value)){
            return value;
        }
    }
    return 5.0;
}
} // namespace

@interface SmartParkBannerPanel : NSPanel
- (instancetype)initWithTitle:(NSString *)title lines:(NSArray<NSString *> *)lines;
- (void)showAboveFrame:(NSRect)parentFrame;
- (void)dismissAnimated:(BOOL)animated;
@end

@implementation SmartParkBannerPanel{
    BOOL _dismissed;
}

+ (NSFont *)pingFangFontWithSize:(CGFloat)size weight:(NSFontWeight)weight{
    NSFontDescriptor *descriptor = [NSFontDescriptor fontDescriptorWithName:@"PingFang SC"
                                                                      size:size];
    NSFontDescriptor *weighted = [descriptor fontDescriptorByAddingAttributes:
        @{NSFontTraitsAttribute: @{NSFontWeightTrait: @(weight)}}];
    NSFont *font = [NSFont fontWithDescriptor:weighted size:size];
    return font != nil ? font : [NSFont systemFontOfSize:size weight:weight];
}

- (instancetype)initWithTitle:(NSString *)title lines:(NSArray<NSString *> *)lines{
    const CGFloat lineHeight = 19.0;
    const CGFloat height = 14.0 + 22.0 + lines.count * lineHeight + 14.0;
    const NSRect contentRect = NSMakeRect(0.0, 0.0, kBannerWidth, height);
    self = [super initWithContentRect:contentRect
                            styleMask:NSWindowStyleMaskBorderless
                                      | NSWindowStyleMaskNonactivatingPanel
                              backing:NSBackingStoreBuffered
                                defer:YES];
    if (self == nil){
        return nil;
    }
    self.level = NSStatusWindowLevel;
    self.opaque = NO;
    self.backgroundColor = NSColor.clearColor;
    self.hasShadow = YES;
    self.releasedWhenClosed = NO;
    self.hidesOnDeactivate = NO;
    self.animationBehavior = NSWindowAnimationBehaviorNone;

    NSVisualEffectView *effect = [[NSVisualEffectView alloc] initWithFrame:contentRect];
    effect.material = NSVisualEffectMaterialHUDWindow;
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateActive;
    effect.wantsLayer = YES;
    effect.layer.cornerRadius = 12.0;
    effect.layer.masksToBounds = YES;
    effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.contentView = effect;

    NSClickGestureRecognizer *click = [[NSClickGestureRecognizer alloc]
        initWithTarget:self action:@selector(handleClick:)];
    [effect addGestureRecognizer:click];

    CGFloat y = height - 14.0 - 20.0;
    [self addLabel:title
              font:[SmartParkBannerPanel pingFangFontWithSize:13.0 weight:NSFontWeightSemibold]
             color:NSColor.labelColor
            toView:effect
             frame:NSMakeRect(16.0, y, kBannerWidth - 32.0, 20.0)];
    y -= 22.0;
    for (NSString *line in lines){
        [self addLabel:line
                  font:[SmartParkBannerPanel pingFangFontWithSize:12.0 weight:NSFontWeightRegular]
                 color:NSColor.secondaryLabelColor
                toView:effect
                 frame:NSMakeRect(16.0, y, kBannerWidth - 32.0, lineHeight - 2.0)];
        y -= lineHeight;
    }
    return self;
}

- (void)addLabel:(NSString *)text font:(NSFont *)font color:(NSColor *)color
          toView:(NSView *)parent frame:(NSRect)frame{
    NSTextField *label = [NSTextField labelWithString:text];
    label.font = font;
    label.textColor = color;
    label.lineBreakMode = NSLineBreakByTruncatingTail;
    label.frame = frame;
    [parent addSubview:label];
}

- (void)handleClick:(NSGestureRecognizer *)sender{
    [self dismissAnimated:YES];
}

- (void)showAboveFrame:(NSRect)parentFrame{
    const NSRect frame = self.frame;
    const NSUInteger stackIndex = ActiveBanners().count;
    const NSRect target = NSMakeRect(
        NSMaxX(parentFrame) - frame.size.width - kBannerMargin,
        NSMaxY(parentFrame) - kBannerTopOffset
            - static_cast<CGFloat>(stackIndex) * (frame.size.height + kBannerGap),
        frame.size.width, frame.size.height);
    [self setFrame:target display:NO];
    self.alphaValue = 0.0;
    [self orderFrontRegardless];
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context){
        context.duration = 0.25;
        self.animator.alphaValue = 1.0;
    }];
    __weak SmartParkBannerPanel *weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 static_cast<int64_t>(AutoDismissSeconds() * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        [weakSelf dismissAnimated:YES];
    });
}

- (void)dismissAnimated:(BOOL)animated{
    if (_dismissed){
        return;
    }
    _dismissed = YES;
    __weak SmartParkBannerPanel *weakSelf = self;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context){
        context.duration = animated ? 0.25 : 0.0;
        context.completionHandler = ^{
            SmartParkBannerPanel *strongSelf = weakSelf;
            if (strongSelf == nil){
                return;
            }
            [ActiveBanners() removeObjectIdenticalTo:strongSelf];
            [strongSelf orderOut:nil];
            [strongSelf close];
        };
        self.animator.alphaValue = 0.0;
    }];
}

@end

void macnotify::showExitBanner(const void *parentNSWindow, const char *title,
                               const std::vector<std::string> &lines){
    if (title == nullptr){
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        NSWindow *parent = (__bridge NSWindow *)parentNSWindow;
        NSRect parentFrame = NSZeroRect;
        if (parent != nil && parent.isVisible){
            parentFrame = parent.frame;
        } else if (NSScreen.mainScreen != nil){
            parentFrame = NSScreen.mainScreen.visibleFrame;
        }
        NSMutableArray<NSString *> *bannerLines = [NSMutableArray array];
        for (const std::string &line : lines){
            [bannerLines addObject:FromUTF8(line.c_str())];
        }
        SmartParkBannerPanel *banner =
            [[SmartParkBannerPanel alloc] initWithTitle:FromUTF8(title)
                                                lines:bannerLines];
        [ActiveBanners() addObject:banner];
        [banner showAboveFrame:parentFrame];
    });
}
