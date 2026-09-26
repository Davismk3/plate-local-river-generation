#include "flat_infinite/visualizer.hpp"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace flat_infinite {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Scene {
    explicit Scene(Config config) : world(std::move(config)) {}
    World world;
    TerrainGrid terrain;
    std::vector<PlateIndex> owners;
    double minimum_height = 0.0;
    double maximum_height = 0.0;
    std::size_t river_segments = 0;
    std::size_t river_nodes = 0;
};

struct ProjectedPoint {
    NSPoint point;
    double depth;
};

struct Triangle {
    ProjectedPoint a;
    ProjectedPoint b;
    ProjectedPoint c;
    double height;
    double depth;
};

ProjectedPoint project(double x, double y, double z, NSSize size, double yaw, double elevation) {
    const double x1 = x * std::cos(yaw) - z * std::sin(yaw);
    const double z1 = x * std::sin(yaw) + z * std::cos(yaw);
    const double scale = std::min(size.width, size.height) * 0.34;
    return {
        NSMakePoint(
            size.width * 0.5 + x1 * scale,
            size.height * 0.68 + z1 * scale * std::sin(elevation)
                - y * scale * std::cos(elevation) * 0.88),
        z1 * std::cos(elevation) + y * std::sin(elevation),
    };
}

NSColor* lerpColor(const int a[3], const int b[3], double t) {
    t = std::clamp(t, 0.0, 1.0);
    return [NSColor colorWithCalibratedRed:(a[0] + (b[0] - a[0]) * t) / 255.0
                                     green:(a[1] + (b[1] - a[1]) * t) / 255.0
                                      blue:(a[2] + (b[2] - a[2]) * t) / 255.0
                                     alpha:1.0];
}

NSColor* terrainColor(double height) {
    static const int deep_water[3] = {33, 68, 96};
    static const int shallow_water[3] = {98, 144, 163};
    static const int lowland[3] = {101, 138, 99};
    static const int upland[3] = {150, 165, 116};
    static const int mountain[3] = {168, 143, 111};
    static const int peak[3] = {223, 220, 211};
    constexpr double sea_level = 0.35;
    if (height <= sea_level) return lerpColor(deep_water, shallow_water, height / sea_level);
    const double t = std::clamp((height - sea_level) / (1.0 - sea_level), 0.0, 1.0);
    if (t < 0.45) return lerpColor(lowland, upland, t / 0.45);
    if (t < 0.8) return lerpColor(upland, mountain, (t - 0.45) / 0.35);
    return lerpColor(mountain, peak, (t - 0.8) / 0.2);
}

NSString* nsString(const std::string& value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

} // namespace
} // namespace flat_infinite

@interface FlatInfiniteView : NSView {
@private
    std::shared_ptr<flat_infinite::Scene> _scene;
    flat_infinite::Config _config;
    flat_infinite::RiverOptions _options;
    double _centerX;
    double _centerY;
    double _plateCells;
    int _resolution;
    int _activeRadius;
    bool _includeRivers;
    double _yaw;
    double _elevation;
    BOOL _dragging;
    NSPoint _lastDrag;
    std::atomic<std::uint64_t> _requestId;
}
- (instancetype)initWithFrame:(NSRect)frame
                       centerX:(double)centerX
                       centerY:(double)centerY
                    plateCells:(double)plateCells
                    resolution:(int)resolution
                  activeRadius:(int)activeRadius
                 includeRivers:(bool)includeRivers
                       options:(flat_infinite::RiverOptions)options;
- (void)requestScene;
@end

@implementation FlatInfiniteView

- (instancetype)initWithFrame:(NSRect)frame
                       centerX:(double)centerX
                       centerY:(double)centerY
                    plateCells:(double)plateCells
                    resolution:(int)resolution
                  activeRadius:(int)activeRadius
                 includeRivers:(bool)includeRivers
                       options:(flat_infinite::RiverOptions)options {
    self = [super initWithFrame:frame];
    if (self) {
        _centerX = centerX;
        _centerY = centerY;
        _plateCells = plateCells;
        _resolution = std::max(16, resolution);
        _activeRadius = std::max(0, activeRadius);
        _includeRivers = includeRivers;
        _options = options;
        _yaw = -38.0 * flat_infinite::kPi / 180.0;
        _elevation = 24.0 * flat_infinite::kPi / 180.0;
        _dragging = NO;
        _requestId = 0;
        [self requestScene];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)requestScene {
    const std::uint64_t request = ++_requestId;
    const double centerX = _centerX;
    const double centerY = _centerY;
    const double plateCells = _plateCells;
    const int resolution = _resolution;
    const int activeRadius = _activeRadius;
    const bool includeRivers = _includeRivers;
    const flat_infinite::RiverOptions options = _options;
    const flat_infinite::Config config = _config;
    [self setNeedsDisplay:YES];

    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, 120 * NSEC_PER_MSEC),
        dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        if (request != _requestId.load()) return;
        auto scene = std::make_shared<flat_infinite::Scene>(config);
        scene->terrain = scene->world.terrainGrid(
            centerX, centerY, plateCells, resolution, includeRivers, activeRadius, options);
        scene->owners = scene->world.activePlateIndices(centerX, centerY, activeRadius);
        if (!scene->terrain.heights.empty()) {
            const auto bounds = std::minmax_element(
                scene->terrain.heights.begin(), scene->terrain.heights.end());
            scene->minimum_height = *bounds.first;
            scene->maximum_height = *bounds.second;
        }
        if (includeRivers) {
            for (const flat_infinite::PlateIndex owner : scene->owners) {
                if (const auto* plate = scene->world.cachedPlate(owner)) {
                    scene->river_segments += plate->segments.size();
                    scene->river_nodes += plate->nodes.size();
                }
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (request != _requestId.load()) return;
            _scene = std::move(scene);
            [self setNeedsDisplay:YES];
        });
    });
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor colorWithCalibratedRed:12.0 / 255.0 green:15.0 / 255.0 blue:20.0 / 255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);
    const NSSize size = self.bounds.size;
    const auto scene = _scene;
    if (!scene) {
        NSDictionary* attributes = @{
            NSFontAttributeName: [NSFont monospacedSystemFontOfSize:15 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.9 alpha:1.0],
        };
        [@"Generating terrain and river cache…" drawAtPoint:NSMakePoint(16, 16) withAttributes:attributes];
        return;
    }

    const int resolution = scene->terrain.resolution;
    std::vector<flat_infinite::Triangle> triangles;
    triangles.reserve(static_cast<std::size_t>((resolution - 1) * (resolution - 1) * 2));
    auto gridCoordinate = [resolution](int value) {
        return -1.0 + 2.0 * static_cast<double>(value) / static_cast<double>(resolution - 1);
    };
    auto heightAt = [&](int row, int col) {
        return scene->terrain.heights[static_cast<std::size_t>(row * resolution + col)];
    };
    for (int row = 0; row + 1 < resolution; ++row) {
        for (int col = 0; col + 1 < resolution; ++col) {
            const double x0 = gridCoordinate(col);
            const double x1 = gridCoordinate(col + 1);
            const double z0 = gridCoordinate(row);
            const double z1 = gridCoordinate(row + 1);
            const double h0 = heightAt(row, col);
            const double h1 = heightAt(row, col + 1);
            const double h2 = heightAt(row + 1, col + 1);
            const double h3 = heightAt(row + 1, col);
            const auto p0 = flat_infinite::project(x0, std::clamp(h0 * 0.1, 0.0, 1.0), z0, size, _yaw, _elevation);
            const auto p1 = flat_infinite::project(x1, std::clamp(h1 * 0.1, 0.0, 1.0), z0, size, _yaw, _elevation);
            const auto p2 = flat_infinite::project(x1, std::clamp(h2 * 0.1, 0.0, 1.0), z1, size, _yaw, _elevation);
            const auto p3 = flat_infinite::project(x0, std::clamp(h3 * 0.1, 0.0, 1.0), z1, size, _yaw, _elevation);
            triangles.push_back({p0, p1, p2, (h0 + h1 + h2) / 3.0, (p0.depth + p1.depth + p2.depth) / 3.0});
            triangles.push_back({p0, p2, p3, (h0 + h2 + h3) / 3.0, (p0.depth + p2.depth + p3.depth) / 3.0});
        }
    }
    std::sort(triangles.begin(), triangles.end(), [](const auto& a, const auto& b) { return a.depth < b.depth; });
    for (const auto& triangle : triangles) {
        [flat_infinite::terrainColor(triangle.height) setFill];
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:triangle.a.point];
        [path lineToPoint:triangle.b.point];
        [path lineToPoint:triangle.c.point];
        [path closePath];
        [path fill];
    }

    if (_includeRivers) {
        const double worldWidth = _plateCells / _config.plate_scale;
        const double minX = _centerX - worldWidth * 0.5;
        const double minY = _centerY - worldWidth * 0.5;
        for (const auto owner : scene->owners) {
            const auto* plate = scene->world.cachedPlate(owner);
            if (!plate) continue;
            for (const auto& segment : plate->segments) {
                const auto& from = plate->nodes[segment.from];
                const auto& to = plate->nodes[segment.to];
                const double ax = (from.world_point.x - minX) / worldWidth * 2.0 - 1.0;
                const double az = (from.world_point.y - minY) / worldWidth * 2.0 - 1.0;
                const double bx = (to.world_point.x - minX) / worldWidth * 2.0 - 1.0;
                const double bz = (to.world_point.y - minY) / worldWidth * 2.0 - 1.0;
                if ((ax < -1.1 && bx < -1.1) || (ax > 1.1 && bx > 1.1)
                    || (az < -1.1 && bz < -1.1) || (az > 1.1 && bz > 1.1)) continue;
                const auto a = flat_infinite::project(ax, std::clamp(from.river_height * 0.1, 0.0, 1.0) + 0.012, az, size, _yaw, _elevation);
                const auto b = flat_infinite::project(bx, std::clamp(to.river_height * 0.1, 0.0, 1.0) + 0.012, bz, size, _yaw, _elevation);
                NSBezierPath* river = [NSBezierPath bezierPath];
                [river moveToPoint:a.point];
                [river lineToPoint:b.point];
                [river setLineWidth:std::max(1.0, 1.2 + 0.85 * (segment.strahler_order - 1))];
                [[NSColor colorWithCalibratedRed:24.0 / 255.0 green:112.0 / 255.0 blue:214.0 / 255.0 alpha:1.0] setStroke];
                [river stroke];
            }
        }
    }

    NSDictionary* attributes = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.92 alpha:1.0],
    };
    std::ostringstream hud;
    hud.setf(std::ios::fixed);
    hud.precision(0);
    hud << "center=(" << _centerX << ", " << _centerY << ")\n";
    hud.precision(2);
    hud << "plate_cells=" << _plateCells << "  resolution=" << resolution << "\n";
    hud.precision(3);
    hud << "height=" << scene->minimum_height << ".." << scene->maximum_height << "\n";
    hud << "rivers=" << scene->river_segments << " segments  " << scene->river_nodes << " nodes\n";
    hud.precision(1);
    hud << "yaw=" << _yaw * 180.0 / flat_infinite::kPi
        << "  elevation=" << _elevation * 180.0 / flat_infinite::kPi << "\n";
    hud << "arrow keys: pan   left-drag: rotate   Esc: close";
    [flat_infinite::nsString(hud.str()) drawAtPoint:NSMakePoint(14, 12) withAttributes:attributes];
}

- (void)keyDown:(NSEvent*)event {
    const double movement = 0.05 / _config.plate_scale;
    bool moved = true;
    switch (event.keyCode) {
        case 123: _centerX -= movement; break;
        case 124: _centerX += movement; break;
        case 125: _centerY += movement; break;
        case 126: _centerY -= movement; break;
        case 53: [[self window] close]; return;
        default: moved = false; [super keyDown:event]; break;
    }
    if (moved) [self requestScene];
}

- (void)mouseDown:(NSEvent*)event {
    _dragging = YES;
    _lastDrag = [self convertPoint:event.locationInWindow fromView:nil];
}

- (void)mouseDragged:(NSEvent*)event {
    if (!_dragging) return;
    const NSPoint current = [self convertPoint:event.locationInWindow fromView:nil];
    _yaw -= (current.x - _lastDrag.x) * 0.006;
    _elevation = std::clamp(_elevation + (current.y - _lastDrag.y) * 0.006,
        8.0 * flat_infinite::kPi / 180.0, 72.0 * flat_infinite::kPi / 180.0);
    _lastDrag = current;
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    _dragging = NO;
}

@end

@interface FlatInfiniteWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation FlatInfiniteWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [NSApp terminate:nil];
}
@end

namespace flat_infinite {

int runInteractiveVisualizer(
    double center_x,
    double center_y,
    double plate_cells,
    int resolution,
    int active_radius,
    bool include_rivers,
    RiverOptions options) {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSMenu* menu_bar = [[NSMenu alloc] init];
        NSMenuItem* app_item = [[NSMenuItem alloc] init];
        [menu_bar addItem:app_item];
        [application setMainMenu:menu_bar];
        NSMenu* app_menu = [[NSMenu alloc] init];
        NSMenuItem* quit_item = [[NSMenuItem alloc]
            initWithTitle:@"Quit Flat Infinite WorldGen"
                   action:@selector(terminate:)
            keyEquivalent:@"q"];
        [app_menu addItem:quit_item];
        [app_item setSubmenu:app_menu];
        [application finishLaunching];

        const NSRect frame = NSMakeRect(0, 0, 1000, 760);
        __attribute__((objc_precise_lifetime)) NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                              | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window setTitle:@"WorldGen — Flat Infinite Terrain"];
        [window setSharingType:NSWindowSharingReadOnly];
        [window center];
        __attribute__((objc_precise_lifetime)) FlatInfiniteWindowDelegate* delegate =
            [[FlatInfiniteWindowDelegate alloc] init];
        [window setDelegate:delegate];
        __attribute__((objc_precise_lifetime)) FlatInfiniteView* view = [[FlatInfiniteView alloc]
            initWithFrame:frame
                  centerX:center_x
                  centerY:center_y
               plateCells:plate_cells
               resolution:resolution
             activeRadius:active_radius
            includeRivers:include_rivers
                  options:options];
        [window setContentView:view];
        [window makeFirstResponder:view];
        [window makeKeyAndOrderFront:nil];
        [window orderFrontRegardless];
        [application activateIgnoringOtherApps:YES];
        [[NSRunningApplication currentApplication]
            activateWithOptions:NSApplicationActivateAllWindows];
        [application run];
    }
    return 0;
}

} // namespace flat_infinite
