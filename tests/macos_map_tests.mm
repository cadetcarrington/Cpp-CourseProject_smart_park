#import <Cocoa/Cocoa.h>

#import "ParkingMapView.h"
#include "bridge/ParkingBridge.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace{
void require(bool condition, const std::string &message){
    if (!condition){
        throw std::runtime_error(message);
    }
}

struct Transform{
    double scale;
    double ox;
    double oy;

    NSPoint point(double x, double y) const{
        return NSMakePoint(ox + x * scale, oy + y * scale);
    }
};

Transform expectedTransform(NSSize size, const smartpark::ParkingLayout &layout){
    const double scale = std::min((size.width - 80.0) / layout.siteWidth(),
                                  (size.height - 80.0) / layout.siteHeight());
    return {scale, (size.width - layout.siteWidth() * scale) / 2.0,
            (size.height - layout.siteHeight() * scale) / 2.0};
}

struct RGB{
    double r;
    double g;
    double b;
};

RGB pixel(NSBitmapImageRep *image, int x, int y){
    NSColor *color = [[image colorAtX:x y:y]
        colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    require(color != nil, "bitmap pixel unavailable");
    return {color.redComponent, color.greenComponent, color.blueComponent};
}

double distance(RGB a, RGB b){
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

// Check ink away from stall borders; the flat fill and grid have no dark pixels here.
int darkInk(NSBitmapImageRep *image, NSRect rect){
    int count = 0;
    for (int y = std::max(0, static_cast<int>(NSMinY(rect)));
         y < std::min(static_cast<int>(image.pixelsHigh), static_cast<int>(NSMaxY(rect))); ++y){
        for (int x = std::max(0, static_cast<int>(NSMinX(rect)));
             x < std::min(static_cast<int>(image.pixelsWide), static_cast<int>(NSMaxX(rect))); ++x){
            const RGB p = pixel(image, x, y);
            if (p.r < 0.42 && p.g < 0.42 && p.b < 0.42){
                ++count;
            }
        }
    }
    return count;
}

NSBitmapImageRep *render(ParkingMapView *view, int width, int height, NSAppearance *appearance){
    [view setFrameSize:NSMakeSize(width, height)];
    view.appearance = appearance;
    NSBitmapImageRep *image = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
        colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    require(image != nil, "cannot create offscreen bitmap");
    NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:image];
    require(context != nil, "cannot create offscreen graphics context");
    [appearance performAsCurrentDrawingAppearance:^{
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        [[NSColor whiteColor] setFill];
        NSRectFill(view.bounds);
        [view drawRect:view.bounds];
        [context flushGraphics];
        [NSGraphicsContext restoreGraphicsState];
    }];
    return image;
}

void checkRender(ParkingMapView *view, const ParkingBridge &bridge,
                 int width, int height, NSAppearance *appearance){
    NSBitmapImageRep *image = render(view, width, height, appearance);
    if ([appearance.name isEqualToString:NSAppearanceNameAqua]){
        NSString *path = [NSString stringWithFormat:@"/tmp/smartpark-map-review-%d.png", width];
        NSData *png = [image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        require(png != nil && [png writeToFile:path atomically:YES], "could not export map PNG");
    }
    require(image.pixelsWide == width && image.pixelsHigh == height, "bitmap dimensions mismatch");
    const Transform t = expectedTransform(view.bounds.size, bridge.layout());
    const NSPoint site = t.point(20.0, 20.0);
    require(distance(pixel(image, 2, 2), pixel(image, static_cast<int>(site.x), static_cast<int>(site.y))) > 0.20,
            "map is blank or site background is missing");

    const auto &spots = bridge.spots();
    const auto it = std::find_if(spots.begin(), spots.end(), [](const auto &spot){
        return spot.type() == smartpark::SpotType::Charging
            && spot.bounds().height > spot.bounds().width;
    });
    require(it != spots.end(), "no vertical charging stall to inspect");
    const auto &b = it->bounds();
    const NSPoint origin = t.point(b.origin.x, b.origin.y);
    const NSRect interior = NSMakeRect(origin.x + 2.0, origin.y + 2.0,
                                     b.width * t.scale - 4.0, b.height * t.scale - 4.0);
    require(darkInk(image, interior) > 2, "charging stall label did not paint inside its bounds");

    std::printf("map %dx%d %s: site and stall label ink OK\n", width, height,
                appearance.name.UTF8String);
}

void checkHit(ParkingMapView *view, const ParkingBridge &bridge, NSWindow *window){
    const auto &spots = bridge.spots();
    require(!spots.empty(), "no stalls to hit-test");
    const auto &spot = spots.front();
    const auto &b = spot.bounds();
    const Transform t = expectedTransform(view.bounds.size, bridge.layout());
    auto move = [&](NSPoint local){
        const NSPoint inWindow = [view convertPoint:local toView:nil];
        NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeMouseMoved
            location:inWindow modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
            context:nil eventNumber:0 clickCount:0 pressure:0];
        require(event != nil, "cannot create mouse event");
        [view mouseMoved:event];
    };
    move(t.point(b.origin.x + b.width / 2.0, b.origin.y + b.height / 2.0));
    NSString *identifier = [NSString stringWithUTF8String:spot.identifier().c_str()];
    require(view.toolTip != nil && [view.toolTip hasPrefix:[identifier stringByAppendingString:@" | "]],
            "stall center hit did not select the expected spot");
    move(NSMakePoint(2.0, 2.0));
    require(view.toolTip == nil, "outside point retained a stall tooltip");
    std::printf("map %dx%d: stall center and outside hit OK\n",
                static_cast<int>(view.bounds.size.width), static_cast<int>(view.bounds.size.height));
}
} // namespace

int main(int argc, char **argv){
    QCoreApplication qtApp(argc, argv);
    @autoreleasepool{
        try{
            QTemporaryDir directory;
            require(directory.isValid(), "cannot create temporary database directory");
            ParkingBridge bridge(directory.filePath("map-test.sqlite"));
            require(bridge.ready() && !bridge.memoryOnly(),
                    "temporary database did not open: " + bridge.lastError());
            require(bridge.totalSpots() > 0, "garage layout has no stalls");

            [NSApplication sharedApplication];
            NSWindow *window = [[NSWindow alloc]
                initWithContentRect:NSMakeRect(0, 0, 1600, 900)
                styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
            require(window != nil, "cannot create hidden window for coordinate conversion");
            ParkingMapView *view = [[ParkingMapView alloc] initWithFrame:NSMakeRect(0, 0, 960, 600)];
            view.bridge = &bridge;
            [window.contentView addSubview:view];
            for (const auto &size : {NSMakeSize(960, 600), NSMakeSize(1120, 720), NSMakeSize(1600, 900)}){
                for (NSString *name in @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]){
                    checkRender(view, bridge, static_cast<int>(size.width), static_cast<int>(size.height),
                                [NSAppearance appearanceNamed:name]);
                }
                checkHit(view, bridge, window);
            }
            view.bridge = nullptr;
            return 0;
        } catch (const std::exception &error){
            std::fprintf(stderr, "map test failed: %s\n", error.what());
            return 1;
        }
    }
}
