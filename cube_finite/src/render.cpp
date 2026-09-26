#include "finite_world/render.hpp"

#include "finite_world/noise.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>

namespace finite_world {
namespace {

constexpr double kExaggeration = 0.05;  // radial displacement per unit height
constexpr double kRiverLift = 0.004;
constexpr double kRiverDepthTolerance = 0.03;
// Camera distance from the planet center, in multiples of the planet's extent.
constexpr double kCameraDistance = 4.0;

struct Rgb { double r, g, b; };

// Face frames: normal, then two tangents spanning the face.
const Vec3 kFaceNormal[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const Vec3 kFaceU[6] = {{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}};
const Vec3 kFaceV[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

Rgb lerp(Rgb a, Rgb b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Desaturated cartographic palette shared with the flat world.
Rgb terrainColor(double height, double sea_level) {
    if (height <= sea_level) return lerp({33, 68, 96}, {98, 144, 163}, height / (sea_level + 1e-8));
    const double t = std::clamp((height - sea_level) / (1.0 - sea_level + 1e-8), 0.0, 1.0);
    if (t < 0.45) return lerp({101, 138, 99}, {150, 165, 116}, t / 0.45);
    if (t < 0.8) return lerp({150, 165, 116}, {168, 143, 111}, (t - 0.45) / 0.35);
    return lerp({168, 143, 111}, {223, 220, 211}, (t - 0.8) / 0.2);
}

Rgb plateColor(int plate, int seed) {
    const double h = (hash3(plate, 7, 0, seed + 55) + 1.0) * 0.5 * 6.0;
    const double s = 0.55, v = 0.9;
    const int sector = static_cast<int>(h) % 6;
    const double f = h - std::floor(h);
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    const double table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
    return {table[sector][0] * 255, table[sector][1] * 255, table[sector][2] * 255};
}

double displaced(double height, double sea_level) { return 1.0 + kExaggeration * std::max(height, sea_level); }

// Perspective camera on the view-space +z axis, looking at the planet center.
struct Screen {
    int width, height;
    double scale;
    double camera_distance;
    Vec3 project(const View& view, Vec3 world) const {
        const Vec3 v = view.orientation.apply(world);
        const double perspective = camera_distance / std::max(1e-9, camera_distance - v.z);
        return {width * 0.5 + v.x * scale * perspective, height * 0.5 - v.y * scale * perspective, v.z};
    }
};

struct Canvas {
    int width, height;
    std::vector<std::uint8_t> rgb;
    std::vector<double> depth;
    Canvas(int w, int h)
        : width(w), height(h), rgb(static_cast<std::size_t>(w) * h * 3, 0),
          depth(static_cast<std::size_t>(w) * h, -std::numeric_limits<double>::infinity()) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
            rgb[i * 3] = 12; rgb[i * 3 + 1] = 15; rgb[i * 3 + 2] = 20;
        }
    }
    void put(int x, int y, Rgb c) {
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
        rgb[i] = static_cast<std::uint8_t>(std::clamp(c.r, 0.0, 255.0));
        rgb[i + 1] = static_cast<std::uint8_t>(std::clamp(c.g, 0.0, 255.0));
        rgb[i + 2] = static_cast<std::uint8_t>(std::clamp(c.b, 0.0, 255.0));
    }
};

// Z-buffered triangle; larger depth is nearer the camera.
void fillTriangle(Canvas& canvas, Vec3 a, Vec3 b, Vec3 c, Rgb color) {
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1e-12) return;
    for (int y = y0; y <= y1; ++y) {
        const double py = y + 0.5;
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
            const double z = w0 * a.z + w1 * b.z + w2 * c.z;
            const std::size_t i = static_cast<std::size_t>(y) * canvas.width + x;
            if (z <= canvas.depth[i]) continue;
            canvas.depth[i] = z;
            canvas.put(x, y, color);
        }
    }
}

// Thick depth-tested line with flat ends.
void drawLine(Canvas& canvas, Vec3 a, Vec3 b, double width, Rgb color) {
    const double r = std::max(0.5, width * 0.5);
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length_squared = dx * dx + dy * dy;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - r)));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - r)));
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + r)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5 - a.x, py = y + 0.5 - a.y;
            double t = 0.0;
            if (length_squared > 1e-12) {
                t = (px * dx + py * dy) / length_squared;
                if (t < 0.0 || t > 1.0) continue;
                if (std::abs(px * dy - py * dx) / std::sqrt(length_squared) > r) continue;
            } else if (px * px + py * py > r * r) {
                continue;
            }
            const double z = a.z + (b.z - a.z) * t;
            const std::size_t i = static_cast<std::size_t>(y) * canvas.width + x;
            if (z < canvas.depth[i] - kRiverDepthTolerance) continue;
            canvas.put(x, y, color);
        }
    }
}

} // namespace

Mesh buildMesh(const World& world, int resolution) {
    Mesh mesh;
    mesh.resolution = std::max(2, resolution);
    const std::size_t count = static_cast<std::size_t>(6) * mesh.resolution * mesh.resolution;
    mesh.surface_points.resize(count);
    mesh.owners.resize(count);
    mesh.heights.resize(count);
    for (int face = 0; face < 6; ++face) {
        for (int row = 0; row < mesh.resolution; ++row) {
            const double b = -1.0 + 2.0 * row / (mesh.resolution - 1);
            for (int col = 0; col < mesh.resolution; ++col) {
                const double a = -1.0 + 2.0 * col / (mesh.resolution - 1);
                const Vec3 cube = kFaceNormal[face] + kFaceU[face] * a + kFaceV[face] * b;
                const Vec3 x = world.surfacePoint(cube);
                const std::size_t i = mesh.index(face, row, col);
                mesh.surface_points[i] = x;
                mesh.owners[i] = world.plateOwner(x);
                mesh.heights[i] = world.cachedHeight(x);
            }
        }
    }
    return mesh;
}

void applyRivers(const World& world, Mesh& mesh, int plate) {
    for (std::size_t i = 0; i < mesh.surface_points.size(); ++i) {
        if (mesh.owners[i] == plate) mesh.heights[i] = world.cachedHeight(mesh.surface_points[i]);
    }
}

void rotateView(View& view, double yaw, double pitch) {
    view.orientation = rotationX(pitch) * rotationY(yaw) * view.orientation;
}

Vec3 cameraSurfacePoint(const World& world, const View& view) {
    return world.surfacePoint(view.orientation.applyTransposed({0.0, 0.0, 1.0}));
}

int nearestPlate(const World& world, const View& view) {
    return world.plateOwner(cameraSurfacePoint(world, view));
}

Image render(const World& world, const Mesh& mesh, const View& view, int width, int height) {
    Canvas canvas(width, height);
    const Config& config = world.config();
    const double sea = config.sea_level_fraction;
    const double extent = (config.surface == Surface::Cube ? std::sqrt(3.0) : 1.0) * config.radius * (1.0 + kExaggeration * 1.5);
    const double camera_distance = kCameraDistance * extent;
    // Scale so the planet's nearest extent still fits the frame under perspective.
    const double fit = (camera_distance - extent) / camera_distance;
    const Screen screen{width, height, std::min(width, height) * 0.47 * view.zoom * fit / extent, camera_distance};
    const Vec3 eye{0.0, 0.0, camera_distance};
    const Vec3 light = normalize({-0.45, 0.55, 0.75});

    auto vertex = [&](std::size_t i) {
        return mesh.surface_points[i] * displaced(mesh.heights[i], sea);
    };

    const int n = mesh.resolution;
    for (int face = 0; face < 6; ++face) {
        for (int row = 0; row + 1 < n; ++row) {
            for (int col = 0; col + 1 < n; ++col) {
                const std::size_t i00 = mesh.index(face, row, col);
                const std::size_t i01 = mesh.index(face, row, col + 1);
                const std::size_t i10 = mesh.index(face, row + 1, col);
                const std::size_t i11 = mesh.index(face, row + 1, col + 1);
                const std::size_t triangles[2][3] = {{i00, i01, i11}, {i00, i11, i10}};
                for (const auto& tri : triangles) {
                    const Vec3 wa = vertex(tri[0]), wb = vertex(tri[1]), wc = vertex(tri[2]);
                    Vec3 normal = normalize(cross(wb - wa, wc - wa));
                    if (dot(normal, wa) < 0.0) normal = normal * -1.0;
                    const Vec3 view_normal = view.orientation.apply(normal);
                    // Skip triangles facing away from the eye (the far side of the planet).
                    if (dot(view_normal, eye - view.orientation.apply(wa)) < 0.0) continue;
                    const double shade = 0.35 + 0.65 * std::max(0.0, dot(view_normal, light));

                    const double h = (mesh.heights[tri[0]] + mesh.heights[tri[1]] + mesh.heights[tri[2]]) / 3.0;
                    Rgb color = terrainColor(h, sea);
                    const int owner = mesh.owners[tri[0]];
                    const bool border = owner != mesh.owners[tri[1]] || owner != mesh.owners[tri[2]];
                    if (view.show_plates) {
                        color = lerp(color, plateColor(owner, config.seed), 0.55);
                        if (owner == view.highlight_plate) color = lerp(color, {255, 255, 255}, 0.25);
                        if (border) color = {35, 35, 40};
                    } else if (border) {
                        color = lerp(color, {30, 30, 35}, 0.45);
                    }
                    color = {color.r * shade, color.g * shade, color.b * shade};
                    fillTriangle(canvas, screen.project(view, wa), screen.project(view, wb), screen.project(view, wc), color);
                }
            }
        }
    }

    if (view.show_rivers) {
        const double px = std::max(1.0, std::min(width, height) / 700.0);
        for (int plate = 0; plate < world.plateCount(); ++plate) {
            const PlateRiverCache* cache = world.cachedPlate(plate);
            if (!cache) continue;
            auto riverPoint = [&](const RiverNode& node) {
                return screen.project(view, node.world_point * (displaced(node.river_height, sea) + kRiverLift));
            };
            std::map<std::size_t, int> degree;
            for (const RiverSegment& s : cache->segments) {
                ++degree[s.from];
                ++degree[s.to];
                const Vec3 a = riverPoint(cache->nodes[s.from]);
                const Vec3 b = riverPoint(cache->nodes[s.to]);
                const double w = px * (1.6 + 0.9 * (s.strahler_order - 1));
                drawLine(canvas, a, b, w + px, {24, 112, 214});
                drawLine(canvas, a, b, std::max(1.0, w * 0.45), {188, 229, 255});
            }
            for (std::size_t i = 0; i < cache->nodes.size(); ++i) {
                const RiverNode& node = cache->nodes[i];
                Rgb color{34, 148, 242};
                double radius = 2.2 * px;
                switch (node.type) {
                    case RiverNodeType::Source: color = {248, 250, 252}; break;
                    case RiverNodeType::SeaOutlet: color = {72, 190, 255}; break;
                    case RiverNodeType::LocalMinimum: color = {255, 176, 64}; radius = 3.0 * px; break;
                    case RiverNodeType::Channel:
                        if (degree[i] < 3) continue;
                        radius = 2.8 * px;
                        break;
                }
                const Vec3 p = riverPoint(node);
                drawLine(canvas, p, p, radius * 2.0, color);
            }
        }
    }
    return {canvas.width, canvas.height, std::move(canvas.rgb)};
}

bool writePpm(const std::string& path, const Image& image) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    return static_cast<bool>(file);
}

} // namespace finite_world
