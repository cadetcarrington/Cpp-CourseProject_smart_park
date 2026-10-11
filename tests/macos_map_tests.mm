#import <Cocoa/Cocoa.h>

#import "ParkingMapView.h"
#import "PlatePhotoArchive.h"
#include "bridge/ParkingBridge.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

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
    // 与 ParkingMapView.mm 的 mapTransform 保持一致：贴左上角 + 四周 12px 边距。
    const double margin = 12.0;
    const double scale = std::min((size.width - 2.0 * margin) / layout.siteWidth(),
                                  (size.height - 2.0 * margin) / layout.siteHeight());
    return {scale, margin, margin};
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
    // 场地底色改成纯白后，视图底色也是白的，「采样场地填充色」这条不再成立；
    // 改采西墙中点：墙只有真的渲染了才会出现在这个像素上，同样能挡住"空白地图"。
    const NSPoint wall = t.point(0.0, bridge.layout().siteHeight() / 2.0);
    require(distance(pixel(image, 2, 2), pixel(image, static_cast<int>(wall.x), static_cast<int>(wall.y))) > 0.20,
            "map is blank or site wall is missing");

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
// 悬停到「已预约未锁位」的车位上：提示必须说明这是预约、并给出预约车牌，
// 否则现场会把这个位子当成空闲位。
void checkPendingHover(ParkingMapView *view, const ParkingBridge &bridge,
                       NSWindow *window, const std::string &spotId,
                       const std::string &plate){
    const auto &spots = bridge.spots();
    const auto it = std::find_if(spots.begin(), spots.end(), [&spotId](const auto &spot){
        return spot.identifier() == spotId;
    });
    require(it != spots.end(), "pending reservation spot not found");
    const auto &b = it->bounds();
    const Transform t = expectedTransform(view.bounds.size, bridge.layout());
    const NSPoint local = t.point(b.origin.x + b.width / 2.0, b.origin.y + b.height / 2.0);
    const NSPoint inWindow = [view convertPoint:local toView:nil];
    NSEvent *event = [NSEvent mouseEventWithType:NSEventTypeMouseMoved
        location:inWindow modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
        context:nil eventNumber:0 clickCount:0 pressure:0];
    require(event != nil, "cannot create mouse event for pending reservation");
    [view mouseMoved:event];
    require(view.toolTip != nil, "pending reservation stall produced no tooltip");
    require([view.toolTip containsString:@"已预约"], "tooltip does not mark the stall as reserved");
    require([view.toolTip containsString:[NSString stringWithUTF8String:plate.c_str()]],
            "tooltip does not carry the reserving plate");
    std::printf("map hover on pending reservation: %s\n", view.toolTip.UTF8String);
}
} // namespace

int main(int argc, char **argv){
    QCoreApplication qtApp(argc, argv);
    @autoreleasepool{
        try{
            QTemporaryDir directory;
            require(directory.isValid(), "cannot create temporary database directory");
            ParkingBridge bridge(directory.filePath("map-test.sqlite"));
            // 预约规划出的路线（下面用来断言车位图真的会画出来）。
            smartpark::Route plannedEntry;
            smartpark::Route plannedExit;
            // 延迟锁位期间「已预约」的目标车位（下面断言车位图标出来了）。
            std::string pendingSpotId;
            require(bridge.ready() && !bridge.memoryOnly(),
                    "temporary database did not open: " + bridge.lastError());
            require(bridge.totalSpots() > 0, "garage layout has no stalls");

            // 本地模式的预约写路径：与远程模式同一组接口（创建 → 到场 → 取消）。
            // 表单从第一版 Booking 切到 Reservation 后这里最容易悄悄坏掉，
            // 所以本地模式也要有一条断言，不能只靠远程集成测试。
            {
                const auto rule = bridge.reservationRule();
                require(rule.minLeadTimeMin > 0 && rule.minDurationMin > 0
                            && rule.deposit > 0.0,
                        "local reservation rule is empty");
                const auto start = smartpark::ParkingRecord::Clock::now()
                    + std::chrono::minutes(rule.minLeadTimeMin)
                    + std::chrono::seconds(1);
                auto created = bridge.createReservation(
                    "京A70001", smartpark::VehicleType::Car, start,
                    std::chrono::minutes(60), false);
                require(created.has_value(),
                        "local createReservation failed: " + bridge.lastError());
                require(!created->reservation.spotId().empty()
                            && !created->entryRoute.points.empty(),
                        "local reservation did not plan a route");
                require(!bridge.reservations().empty(),
                        "created reservation is not listed");
                // 到场窗口自「开始前 lockLeadTime 分钟」起：等窗口打开再确认。
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                auto arrived = bridge.checkInReservation("京A70001");
                require(arrived.has_value(),
                        "local checkInReservation failed: " + bridge.lastError());
                require(arrived->status() == smartpark::ReservationStatus::CheckedIn,
                        "local reservation status is not checked-in");
                bool occupied = false;
                for (const smartpark::ParkingSpot &spot : bridge.spots()){
                    occupied = occupied || (spot.identifier() == arrived->spotId()
                                            && spot.status() == smartpark::SpotStatus::Occupied);
                }
                require(occupied, "local check-in did not occupy the reserved spot");
                bool recorded = false;
                for (const smartpark::ParkingRecord &record : bridge.records()){
                    recorded = recorded || (record.plateNumber() == "京A70001"
                                            && !record.isClosed());
                }
                require(recorded, "local check-in did not create a parking record");

                auto second = bridge.createReservation(
                    "京A70002", smartpark::VehicleType::Car,
                    smartpark::ParkingRecord::Clock::now()
                        + std::chrono::minutes(rule.minLeadTimeMin + 10),
                    std::chrono::minutes(60), false);
                require(second.has_value(), "second local reservation failed");
                require(bridge.cancelReservation("京A70002"),
                        "local cancelReservation failed: " + bridge.lastError());
                plannedEntry = created->entryRoute;
                plannedExit = created->exitRoute;
                // 再建一笔两小时后开始的预约：它不在锁位窗口内，
                // 车位状态仍是「空闲」，正好用来验证车位图的「已预约」标记。
                auto far = bridge.createReservation(
                    "京A70003", smartpark::VehicleType::Car,
                    smartpark::ParkingRecord::Clock::now() + std::chrono::hours(2),
                    std::chrono::minutes(60), false);
                require(far.has_value(), "far-future reservation failed");
                pendingSpotId = far->reservation.spotId();
                require(!bridge.pendingReservations().empty(),
                        "pending reservation is not exposed for the map");
                std::printf("local booking flow: create/checkin/cancel OK "
                            "(spot %s)\n", created->reservation.spotId().c_str());
            }

            // --layout / 自定义布局开库 + 重新打开：换库或换图纸后的核心场景。
            // 修之前这里会锁死——客户端固定用内置布局开库，签名对不上就报
            // 「persisted layout differs」，重启后彻底打不开（只能删库）。
            {
                // 注意：SMARTPARK_DATA_DIR 是宏，不能直接塞进 QStringLiteral
                // （它要求单个字面量）；相邻字面量拼接后在 fromUtf8 里才成立。
                const QString layoutPath =
                    QString::fromUtf8(SMARTPARK_DATA_DIR "/garage-3tower.txt");
                QFile layoutFile(layoutPath);
                require(layoutFile.open(QIODevice::ReadOnly | QIODevice::Text),
                        "cannot open the sample layout file");
                const QString description = QString::fromUtf8(layoutFile.readAll());
                const QString customDb = directory.filePath("custom-layout.sqlite");
                int customSpots = 0;
                {
                    ParkingBridge custom(customDb, description);
                    require(custom.ready(),
                            "custom layout bridge not ready: " + custom.lastError());
                    require(!custom.memoryOnly(), "custom layout fell back to memory mode");
                    customSpots = custom.totalSpots();
                    require(customSpots > 0, "custom layout has no spots");
                }
                {
                    // 第二次打开同一个库（= 下次启动）：必须还能开。
                    ParkingBridge reopened(customDb, description);
                    require(reopened.ready(),
                            "reopening the db with the same layout failed: "
                                + reopened.lastError());
                    require(reopened.totalSpots() == customSpots,
                            "spot count changed on reopen");
                    require(reopened.layoutDescription() == description.toStdString(),
                            "bridge did not keep the custom layout text");
                }
                {
                    // 布局不匹配时必须明确失败，不能悄悄用内置布局去写别的图纸的库。
                    ParkingBridge mismatched(customDb);
                    require(!mismatched.ready(),
                            "a mismatching layout must not open the db");
                }
                std::printf("custom layout: %d spots, reopen OK, mismatch rejected\n",
                            customSpots);
            }

            // 车牌照片留档：入库归档一张、同车牌再归档一张，出库按车牌取回最新的那张。
            // 目录用环境变量指到临时目录，测试不写用户的应用数据目录。
            {
                QTemporaryDir photoRoot;
                require(photoRoot.isValid(), "cannot create plate photo root");
                const QString photoDir = photoRoot.filePath(QStringLiteral("plates"));
                setenv("SMARTPARK_PLATE_PHOTO_DIR", photoDir.toUtf8().constData(), 1);

                // QTemporaryDir::filePath 给的是 QString，归档接口要 NSString。
                NSString *first = photoRoot.filePath(QStringLiteral("crop-a.jpg")).toNSString();
                NSString *second = photoRoot.filePath(QStringLiteral("crop-b.jpg")).toNSString();
                [[NSData dataWithBytes:"a" length:1] writeToFile:first atomically:YES];
                [[NSData dataWithBytes:"b" length:1] writeToFile:second atomically:YES];

                NSError *archiveError = nil;
                NSString *firstArchived = [PlatePhotoArchive archiveImageAtPath:first
                                                                          plate:@"京A12345"
                                                                          error:&archiveError];
                require(firstArchived.length > 0, "archiving the plate photo failed");
                require([firstArchived.lastPathComponent hasPrefix:@"京A12345_"],
                        "archived file name lost the plate prefix");
                NSString *secondArchived = [PlatePhotoArchive archiveImageAtPath:second
                                                                           plate:@"京A12345"
                                                                           error:&archiveError];
                require(secondArchived.length > 0, "second archive failed");
                require(![secondArchived isEqualToString:firstArchived],
                        "same-second archive overwrote the previous photo");
                require([PlatePhotoArchive photosForPlate:@"京A12345"].count == 2,
                        "photosForPlate did not list both photos");
                require([[PlatePhotoArchive latestPhotoForPlate:@"京A12345"]
                            isEqualToString:secondArchived],
                        "latest lookup returned an older photo");
                require([PlatePhotoArchive latestPhotoForPlate:@"京B00000"].length == 0,
                        "unknown plate should have no archived photo");
                unsetenv("SMARTPARK_PLATE_PHOTO_DIR");
                std::printf("plate photo archive: 2 archived, latest lookup OK (%s)\n",
                            secondArchived.lastPathComponent.UTF8String);
            }

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
            // 「已预约（未锁位）」标记：车位状态还是空闲，但车位图上必须看得出来，
            // 悬停要给出预约车牌；取消预约后标记消失。
            if (!pendingSpotId.empty()){
                NSAppearance *aqua = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
                require(bridge.pendingReservations().size() == 1,
                        "pending reservation list should hold exactly one order");
                checkPendingHover(view, bridge, window, pendingSpotId, "京A70003");
                NSBitmapImageRep *marked = render(view, 960, 600, aqua);
                require(bridge.cancelReservation("京A70003"),
                        "cancelling the pending reservation failed");
                require(bridge.pendingReservations().empty(),
                        "pending reservation survived cancellation");
                NSBitmapImageRep *cleared = render(view, 960, 600, aqua);
                int changed = 0;
                for (int y = 0; y < 600; ++y){
                    for (int x = 0; x < 960; ++x){
                        if (distance(pixel(marked, x, y), pixel(cleared, x, y)) > 0.05){
                            ++changed;
                        }
                    }
                }
                require(changed > 30, "pending-reservation marker did not draw anything");
                std::printf("pending-reservation marker: %d px changed after cancel\n",
                            changed);
            } else {
                require(false, "no pending reservation spot to check");
            }

            // 预期路线覆盖层：设置路线后车位图上必须多出一条线（与 Qt 版
            // MainWindow 画 lastAllocation_ 的行为一致），清空后恢复原样。
            {
                NSAppearance *aqua = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
                NSBitmapImageRep *plain = render(view, 960, 600, aqua);
                bridge.setPlannedRoute(plannedEntry, plannedExit);
                require(bridge.plannedRoute().valid,
                        "planned route was not stored (nothing to draw)");
                NSBitmapImageRep *routed = render(view, 960, 600, aqua);
                int changed = 0;
                for (int y = 0; y < 600; ++y){
                    for (int x = 0; x < 960; ++x){
                        if (distance(pixel(plain, x, y), pixel(routed, x, y)) > 0.05){
                            ++changed;
                        }
                    }
                }
                require(changed > 30, "planned route overlay did not draw anything");
                bridge.clearPlannedRoute();
                NSBitmapImageRep *cleared = render(view, 960, 600, aqua);
                int residual = 0;
                for (int y = 0; y < 600; ++y){
                    for (int x = 0; x < 960; ++x){
                        if (distance(pixel(plain, x, y), pixel(cleared, x, y)) > 0.05){
                            ++residual;
                        }
                    }
                }
                require(residual == 0, "cleared route left ink on the map");
                std::printf("route overlay: %d px drawn, %d px left after clear\n",
                            changed, residual);
            }
            view.bridge = nullptr;
            return 0;
        } catch (const std::exception &error){
            std::fprintf(stderr, "map test failed: %s\n", error.what());
            return 1;
        }
    }
}
