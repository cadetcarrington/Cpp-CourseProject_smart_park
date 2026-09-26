#import <AppKit/AppKit.h>

#include "NativeEffects.h"
#include <QWidget>

namespace smartpark_ui{
bool applyNativeVibrancy(QWidget *window, bool darkAppearance){
    if (window == nullptr || getenv("SMARTPARK_NO_VIBRANCY") != nullptr){
        return false;
    }
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (view == nil || view.window == nil){
        return false;
    }
    NSWindow *nsWindow = view.window;
    // 让窗口自身透明，Qt 层透明区域才能透出后面的毛玻璃。
    nsWindow.opaque = NO;
    nsWindow.backgroundColor = [NSColor clearColor];

    NSVisualEffectView *vibrancy = [[NSVisualEffectView alloc]
        initWithFrame:view.bounds];
    vibrancy.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    vibrancy.material = darkAppearance
        ? NSVisualEffectMaterialHUDWindow
        : NSVisualEffectMaterialUnderWindowBackground;
    vibrancy.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    vibrancy.state = NSVisualEffectStateActive;
    [view addSubview:vibrancy positioned:NSWindowBelow relativeTo:nil];
    [vibrancy release];
    return true;
}
} // namespace smartpark_ui
