#include "finite_world/viewer.hpp"

#import <Cocoa/Cocoa.h>

#include <memory>
#include <sstream>
#include <string>

namespace finite_world {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kStep = 6.0 * kPi / 180.0;

// Everything the window shows: the world with its lazy river caches, the
// planet mesh, and the camera.
struct Scene {
    explicit Scene(const ViewerOptions& options)
        : world(options.config), mesh(buildMesh(world, options.mesh_resolution)) {
        view.show_plates = options.show_plates;
        rotateView(view, options.yaw_degrees * kPi / 180.0, options.pitch_degrees * kPi / 180.0);
        refreshNearestPlate();
    }

    // Builds and caches rivers only for the plate facing the camera.
    void refreshNearestPlate() {
        nearest = nearestPlate(world, view);
        view.highlight_plate = nearest;
        if (!world.cachedPlate(nearest)) {
            world.ensurePlate(nearest);
            applyRivers(world, mesh, nearest);
        }
    }

    World world;
    Mesh mesh;
    View view;
    int nearest = 0;
};

} // namespace
} // namespace finite_world

@interface PlanetView : NSView {
@private
    std::unique_ptr<finite_world::Scene> _scene;
}
- (instancetype)initWithFrame:(NSRect)frame options:(const finite_world::ViewerOptions&)options;
@end

@implementation PlanetView

- (instancetype)initWithFrame:(NSRect)frame options:(const finite_world::ViewerOptions&)options {
    self = [super initWithFrame:frame];
    if (self) _scene = std::make_unique<finite_world::Scene>(options);
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const NSSize size = self.bounds.size;
    const int width = std::max(16, static_cast<int>(size.width));
    const int height = std::max(16, static_cast<int>(size.height));
    const finite_world::Image image = finite_world::render(_scene->world, _scene->mesh, _scene->view, width, height);

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CFDataRef data = CFDataCreate(nullptr, image.rgb.data(), static_cast<CFIndex>(image.rgb.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGImageRef cg = CGImageCreate(width, height, 8, 24, static_cast<size_t>(width) * 3, space,
        kCGBitmapByteOrderDefault | kCGImageAlphaNone, provider, nullptr, false, kCGRenderingIntentDefault);
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, 0, height);
    CGContextScaleCTM(context, 1, -1);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cg);
    CGContextRestoreGState(context);
    CGImageRelease(cg);
    CGDataProviderRelease(provider);
    CFRelease(data);
    CGColorSpaceRelease(space);

    const finite_world::World& world = _scene->world;
    const int plate = _scene->nearest;
    const finite_world::PlateRiverCache* cache = world.cachedPlate(plate);
    std::ostringstream hud;
    hud << (world.config().surface == finite_world::Surface::Cube ? "Cube" : "Sphere")
        << " planet, " << world.plateCount() << " plates\n"
        << "Plate facing camera: " << plate
        << (world.plateType(plate) == finite_world::PlateType::Continental ? " (continental)" : " (oceanic)") << "\n"
        << "Its rivers: " << (cache ? cache->paths.size() : 0) << " paths, "
        << (cache ? cache->segments.size() : 0) << " segments\n"
        << "Cached plates: " << world.cachedPlateCount() << " / " << world.plateCount() << "\n"
        << "Arrows: rotate   P: plates   R: rivers   +/-: zoom   Esc: close";
    NSDictionary* attributes = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedWhite:0.92 alpha:1.0],
    };
    [[NSString stringWithUTF8String:hud.str().c_str()] drawAtPoint:NSMakePoint(14, 12) withAttributes:attributes];
}

- (void)keyDown:(NSEvent*)event {
    finite_world::View& view = _scene->view;
    bool rotated = false;
    switch (event.keyCode) {
        case 123: finite_world::rotateView(view, finite_world::kStep, 0.0); rotated = true; break;   // left
        case 124: finite_world::rotateView(view, -finite_world::kStep, 0.0); rotated = true; break;  // right
        case 125: finite_world::rotateView(view, 0.0, -finite_world::kStep); rotated = true; break;  // down
        case 126: finite_world::rotateView(view, 0.0, finite_world::kStep); rotated = true; break;   // up
        case 53: [[self window] close]; return;
        default: {
            NSString* characters = [[event charactersIgnoringModifiers] lowercaseString];
            if ([characters isEqualToString:@"p"]) view.show_plates = !view.show_plates;
            else if ([characters isEqualToString:@"r"]) view.show_rivers = !view.show_rivers;
            else if ([characters isEqualToString:@"="] || [characters isEqualToString:@"+"]) view.zoom = std::min(4.0, view.zoom * 1.15);
            else if ([characters isEqualToString:@"-"]) view.zoom = std::max(0.5, view.zoom / 1.15);
            else { [super keyDown:event]; return; }
        }
    }
    if (rotated) _scene->refreshNearestPlate();
    [self setNeedsDisplay:YES];
}

@end

@interface PlanetWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation PlanetWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [NSApp terminate:nil];
}
@end

namespace finite_world {

int runViewer(const ViewerOptions& options) {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSMenu* menu_bar = [[NSMenu alloc] init];
        NSMenuItem* app_item = [[NSMenuItem alloc] init];
        [menu_bar addItem:app_item];
        [application setMainMenu:menu_bar];
        NSMenu* app_menu = [[NSMenu alloc] init];
        [app_menu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"]];
        [app_item setSubmenu:app_menu];
        [application finishLaunching];

        const NSRect frame = NSMakeRect(0, 0, 960, 820);
        __attribute__((objc_precise_lifetime)) NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                              | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window setTitle:options.config.surface == Surface::Cube ? @"PL-RGA — Cube Planet" : @"PL-RGA — Sphere Planet"];
        [window center];
        __attribute__((objc_precise_lifetime)) PlanetWindowDelegate* delegate = [[PlanetWindowDelegate alloc] init];
        [window setDelegate:delegate];
        __attribute__((objc_precise_lifetime)) PlanetView* view = [[PlanetView alloc] initWithFrame:frame options:options];
        [window setContentView:view];
        [window makeFirstResponder:view];
        [window makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];
        [application run];
    }
    return 0;
}

} // namespace finite_world
