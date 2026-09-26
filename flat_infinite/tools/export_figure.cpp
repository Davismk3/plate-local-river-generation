// Paper figure and metrics exporter for the flat-infinite C++ implementation.
//
// One deterministic invocation builds the plate-local river caches for every
// plate visible in a square view, samples the final terrain over that view,
// measures the resulting networks, renders the figure (PNG), and writes a JSON
// summary plus a per-plate CSV. Every number reported in the paper's results
// section comes from this program.
//
// Timings are repeated on fresh World instances (no shared cache) and the
// median is reported; network statistics are taken from the first run and
// checked to be identical on every repeat.

#include "flat_infinite/world.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace fi = flat_infinite;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- options

struct Options {
    std::string output = "global_continent_rivers.png";
    std::string metrics_output;
    std::string plate_metrics_output;
    int width = 2400;
    int height = 1600;
    int supersample = 3;
    double plate_cells = 2.0;
    int resolution = 320;
    int timing_repeats = 5;
    int channel_samples = 16;
    std::int32_t seed = 1;
    // Camera, matching the interactive viewer but filling a wide paper figure.
    double yaw_deg = -38.0;
    double elevation_deg = 24.0;
    double screen_scale = 0.47;
    double screen_baseline = 0.72;
    double vertical_scale = 0.88;
    double height_scale = 0.1;
    double river_height_offset = 0.012;
};

void usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " OUTPUT.png [options]\n"
        << "  --metrics-output FILE        JSON summary\n"
        << "  --plate-metrics-output FILE  per-plate CSV\n"
        << "  --width N --height N         final image size (default 2400x1600)\n"
        << "  --supersample N              render scale before box downsampling (default 3)\n"
        << "  --plate-cells X              view width in plate cells (default 2)\n"
        << "  --resolution N               terrain samples per view side (default 320)\n"
        << "  --timing-repeats N           fresh-world timing repeats (default 5)\n"
        << "  --channel-samples N          final-terrain samples per river segment (default 16)\n"
        << "  --seed N                     world seed (default 1)\n";
}

Options parseOptions(int argc, char** argv) {
    Options options;
    bool have_output = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("missing value for " + argument);
            return argv[i];
        };
        if (argument == "--help") { usage(argv[0]); std::exit(0); }
        else if (argument == "--metrics-output") options.metrics_output = value();
        else if (argument == "--plate-metrics-output") options.plate_metrics_output = value();
        else if (argument == "--width") options.width = std::stoi(value());
        else if (argument == "--height") options.height = std::stoi(value());
        else if (argument == "--supersample") options.supersample = std::max(1, std::stoi(value()));
        else if (argument == "--plate-cells") options.plate_cells = std::stod(value());
        else if (argument == "--resolution") options.resolution = std::max(2, std::stoi(value()));
        else if (argument == "--timing-repeats") options.timing_repeats = std::max(1, std::stoi(value()));
        else if (argument == "--channel-samples") options.channel_samples = std::max(2, std::stoi(value()));
        else if (argument == "--seed") options.seed = std::stoi(value());
        else if (!argument.empty() && argument[0] == '-') throw std::runtime_error("unknown argument: " + argument);
        else { options.output = argument; have_output = true; }
    }
    if (!have_output) std::cerr << "note: no output path given, writing " << options.output << '\n';
    return options;
}

// ---------------------------------------------------------------- statistics

double seconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : 0.5 * (values[mid - 1] + values[mid]);
}

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

// Linear-interpolated percentile, matching numpy's default.
double percentile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = q / 100.0 * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min(lower + 1, values.size() - 1);
    return values[lower] + (values[upper] - values[lower]) * (position - static_cast<double>(lower));
}

// ---------------------------------------------------------------- view

struct View {
    double center_x = 0.0;
    double center_y = 0.0;
    double world_width = 0.0;
    double min_x = 0.0;
    double min_y = 0.0;
    int resolution = 0;

    double x(int col) const { return min_x + col * world_width / (resolution - 1); }
    double y(int row) const { return min_y + row * world_width / (resolution - 1); }
    bool inside(const fi::Point& p) const {
        return p.x >= min_x && p.x <= min_x + world_width && p.y >= min_y && p.y <= min_y + world_width;
    }
};

// Plates owning at least one terrain sample, ordered as the viewer loads them:
// the center plate first, then by descending sample count, then by distance.
std::vector<fi::PlateIndex> visibleOwners(const fi::World& world, const View& view) {
    const fi::PlateIndex center = world.plateOwner(view.center_x, view.center_y);
    std::map<fi::PlateIndex, int> counts;
    for (int row = 0; row < view.resolution; ++row) {
        for (int col = 0; col < view.resolution; ++col) ++counts[world.plateOwner(view.x(col), view.y(row))];
    }
    std::vector<fi::PlateIndex> owners;
    for (const auto& entry : counts) owners.push_back(entry.first);
    std::sort(owners.begin(), owners.end(), [&](const fi::PlateIndex& a, const fi::PlateIndex& b) {
        auto key = [&](const fi::PlateIndex& p) {
            const int dx = p.x - center.x;
            const int dy = p.y - center.y;
            return std::make_tuple(!(p == center), -counts[p], dx * dx + dy * dy, p.y, p.x);
        };
        return key(a) < key(b);
    });
    return owners;
}

// ---------------------------------------------------------------- one timed run

struct RunResult {
    std::vector<double> cache_build_seconds;  // per owner, in owner order
    double sample_with_rivers_seconds = 0.0;
    double sample_base_only_seconds = 0.0;
    std::vector<double> heights;              // row-major final heights
    std::size_t node_total = 0;
    std::size_t segment_total = 0;
};

RunResult timedRun(fi::World& world, const View& view, const std::vector<fi::PlateIndex>& owners) {
    RunResult result;
    const fi::RiverOptions river_options = fi::defaultRiverOptions(world.config());
    for (const fi::PlateIndex& owner : owners) {
        const auto start = Clock::now();
        const fi::PlateRiverCache& cache = world.ensurePlate(owner, river_options);
        result.cache_build_seconds.push_back(seconds(start, Clock::now()));
        result.node_total += cache.nodes.size();
        result.segment_total += cache.segments.size();
    }

    result.heights.resize(static_cast<std::size_t>(view.resolution) * view.resolution);
    auto start = Clock::now();
    for (int row = 0; row < view.resolution; ++row) {
        for (int col = 0; col < view.resolution; ++col) {
            result.heights[static_cast<std::size_t>(row) * view.resolution + col] =
                world.height(view.x(col), view.y(row), true);
        }
    }
    result.sample_with_rivers_seconds = seconds(start, Clock::now());

    // Same grid, pointwise terrain only, to isolate the cost of river queries.
    volatile double sink = 0.0;
    start = Clock::now();
    for (int row = 0; row < view.resolution; ++row) {
        for (int col = 0; col < view.resolution; ++col) sink = sink + world.height(view.x(col), view.y(row), false);
    }
    result.sample_base_only_seconds = seconds(start, Clock::now());
    (void)sink;
    return result;
}

// ---------------------------------------------------------------- network metrics

const char* nodeTypeName(fi::RiverNodeType type) {
    switch (type) {
        case fi::RiverNodeType::Source: return "source";
        case fi::RiverNodeType::SeaOutlet: return "outlet_sea";
        case fi::RiverNodeType::LocalMinimum: return "outlet_local_minimum";
        case fi::RiverNodeType::Channel: default: return "channel";
    }
}

struct PlateMetrics {
    fi::PlateIndex owner;
    double cache_build_seconds = 0.0;
    std::size_t grid_valid_cells = 0;
    double grid_land_fraction = 0.0;
    std::size_t node_count = 0, segment_count = 0, path_count = 0;
    std::size_t source_count = 0, sea_outlet_count = 0, local_minimum_outlet_count = 0, channel_node_count = 0;
    std::size_t confluence_count = 0, connected_component_count = 0;
    double total_channel_length = 0.0, mean_segment_length = 0.0;
    double mean_path_length = 0.0, max_path_length = 0.0;
    double mean_segment_height_drop = 0.0, min_segment_height_drop = 0.0;
    std::size_t downhill_violation_count = 0;
    int max_strahler_order = 0;
    std::size_t network_bytes = 0, grid_bytes = 0;
};

double plateCells(const fi::Point& a, const fi::Point& b, double plate_scale) {
    return fi::distance(a, b) * plate_scale;
}

PlateMetrics plateMetrics(const fi::PlateRiverCache& cache, double build_seconds, const fi::Config& config) {
    PlateMetrics m;
    m.owner = cache.owner;
    m.cache_build_seconds = build_seconds;
    const std::size_t n = cache.nodes.size();

    std::size_t land = 0;
    for (std::size_t i = 0; i < cache.grid.valid.size(); ++i) {
        if (!cache.grid.valid[i]) continue;
        ++m.grid_valid_cells;
        if (cache.grid.heights[i] > config.sea_level_fraction) ++land;
    }
    m.grid_land_fraction = m.grid_valid_cells ? static_cast<double>(land) / m.grid_valid_cells : 0.0;

    m.node_count = n;
    m.segment_count = cache.segments.size();
    m.path_count = cache.paths.size();
    for (const fi::RiverNode& node : cache.nodes) {
        switch (node.type) {
            case fi::RiverNodeType::Source: ++m.source_count; break;
            case fi::RiverNodeType::SeaOutlet: ++m.sea_outlet_count; break;
            case fi::RiverNodeType::LocalMinimum: ++m.local_minimum_outlet_count; break;
            case fi::RiverNodeType::Channel: ++m.channel_node_count; break;
        }
    }

    std::vector<std::size_t> degree(n, 0), parent(n);
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    auto find = [&](std::size_t v) {
        while (parent[v] != v) { parent[v] = parent[parent[v]]; v = parent[v]; }
        return v;
    };
    std::vector<double> lengths, drops;
    for (const fi::RiverSegment& s : cache.segments) {
        ++degree[s.from];
        ++degree[s.to];
        parent[find(s.to)] = find(s.from);
        lengths.push_back(plateCells(cache.nodes[s.from].world_point, cache.nodes[s.to].world_point, config.plate_scale));
        drops.push_back(s.from_height - s.to_height);
        m.max_strahler_order = std::max(m.max_strahler_order, s.strahler_order);
    }
    for (std::size_t d : degree) m.confluence_count += d >= 3;
    std::vector<std::size_t> roots;
    for (std::size_t v = 0; v < n; ++v) roots.push_back(find(v));
    std::sort(roots.begin(), roots.end());
    m.connected_component_count = static_cast<std::size_t>(std::unique(roots.begin(), roots.end()) - roots.begin());

    m.total_channel_length = std::accumulate(lengths.begin(), lengths.end(), 0.0);
    m.mean_segment_length = mean(lengths);
    m.mean_segment_height_drop = mean(drops);
    m.min_segment_height_drop = drops.empty() ? 0.0 : *std::min_element(drops.begin(), drops.end());
    for (double drop : drops) m.downhill_violation_count += drop < -1e-9;

    std::vector<double> path_lengths;
    for (const auto& path : cache.paths) {
        double length = 0.0;
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            length += plateCells(cache.nodes[path[i]].world_point, cache.nodes[path[i + 1]].world_point, config.plate_scale);
        }
        path_lengths.push_back(length);
    }
    m.mean_path_length = mean(path_lengths);
    m.max_path_length = path_lengths.empty() ? 0.0 : *std::max_element(path_lengths.begin(), path_lengths.end());

    std::size_t path_entries = 0;
    for (const auto& path : cache.paths) path_entries += path.size();
    m.network_bytes = n * sizeof(fi::RiverNode) + cache.segments.size() * sizeof(fi::RiverSegment)
        + path_entries * sizeof(std::size_t);
    const std::size_t cells = cache.grid.heights.size();
    m.grid_bytes = cells * (2 * sizeof(fi::Point) + sizeof(double) + 3 * sizeof(std::uint8_t))
        + cache.grid.polygon.points.size() * sizeof(fi::Point);
    return m;
}

// Segments of the same plate that lie on a common line and share a stretch of
// positive length. Along such a stretch two different interpolated river
// heights exist, which the height blend averages.
std::vector<std::uint8_t> overlappingSegments(const fi::PlateRiverCache& cache) {
    const std::size_t count = cache.segments.size();
    std::vector<std::uint8_t> overlaps(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const fi::Point a = cache.nodes[cache.segments[i].from].world_point;
        const fi::Point b = cache.nodes[cache.segments[i].to].world_point;
        const double ux = b.x - a.x, uy = b.y - a.y;
        const double length = std::hypot(ux, uy);
        if (length < 1e-12) continue;
        for (std::size_t j = i + 1; j < count; ++j) {
            const fi::Point c = cache.nodes[cache.segments[j].from].world_point;
            const fi::Point d = cache.nodes[cache.segments[j].to].world_point;
            const double off_c = std::abs(ux * (c.y - a.y) - uy * (c.x - a.x)) / length;
            const double off_d = std::abs(ux * (d.y - a.y) - uy * (d.x - a.x)) / length;
            if (off_c > 1e-6 || off_d > 1e-6) continue;
            const double tc = ((c.x - a.x) * ux + (c.y - a.y) * uy) / (length * length);
            const double td = ((d.x - a.x) * ux + (d.y - a.y) * uy) / (length * length);
            const double shared = std::min(1.0, std::max(tc, td)) - std::max(0.0, std::min(tc, td));
            if (shared * length > 1e-6) overlaps[i] = overlaps[j] = 1;
        }
    }
    return overlaps;
}

// Samples the final terrain along every river segment and checks that it never
// rises downstream. Samples whose warped owner is not the segment's plate use
// that other plate's rivers, so they are tallied separately, as are segments
// that collinearly overlap another segment.
struct ChannelCheck {
    std::size_t segments_checked = 0;
    std::size_t samples = 0;
    std::size_t foreign_owner_samples = 0;
    std::size_t segments_with_foreign_samples = 0;
    std::size_t overlapping_segments = 0;
    std::size_t violating_segments = 0;
    std::size_t violating_segments_owned = 0;  // among segments whose samples are all owned by its plate
    std::size_t violating_segments_owned_overlapping = 0;
    double max_rise = 0.0;
    double max_rise_owned = 0.0;
    double max_rise_owned_non_overlapping = 0.0;
};

ChannelCheck checkChannels(fi::World& world, const std::vector<const fi::PlateRiverCache*>& caches, int samples_per_segment) {
    ChannelCheck check;
    for (const fi::PlateRiverCache* cache : caches) {
        const std::vector<std::uint8_t> overlaps = overlappingSegments(*cache);
        for (std::size_t index = 0; index < cache->segments.size(); ++index) {
            const fi::RiverSegment& segment = cache->segments[index];
            check.overlapping_segments += overlaps[index];
            const fi::Point a = cache->nodes[segment.from].world_point;
            const fi::Point b = cache->nodes[segment.to].world_point;
            bool foreign = false;
            double previous = 0.0, rise = 0.0;
            for (int k = 0; k < samples_per_segment; ++k) {
                const double t = static_cast<double>(k) / (samples_per_segment - 1);
                const double x = a.x + (b.x - a.x) * t;
                const double y = a.y + (b.y - a.y) * t;
                if (!(world.plateOwner(x, y) == cache->owner)) { foreign = true; ++check.foreign_owner_samples; }
                const double h = world.height(x, y, true);
                if (k > 0) rise = std::max(rise, h - previous);
                previous = h;
                ++check.samples;
            }
            ++check.segments_checked;
            check.segments_with_foreign_samples += foreign;
            const bool violates = rise > 1e-9;
            check.violating_segments += violates;
            check.max_rise = std::max(check.max_rise, rise);
            if (!foreign) {
                check.violating_segments_owned += violates;
                check.violating_segments_owned_overlapping += violates && overlaps[index];
                check.max_rise_owned = std::max(check.max_rise_owned, rise);
                if (!overlaps[index]) {
                    check.max_rise_owned_non_overlapping = std::max(check.max_rise_owned_non_overlapping, rise);
                }
            }
        }
    }
    return check;
}

// ---------------------------------------------------------------- rendering

struct Rgb { std::uint8_t r, g, b; };

struct Canvas {
    int width, height;
    std::vector<Rgb> pixels;
    std::vector<float> depth;          // terrain depth of the visible face
    std::vector<std::uint8_t> covered; // terrain rasterized here

    Canvas(int w, int h, Rgb background)
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * h, background),
          depth(static_cast<std::size_t>(w) * h, 0.0f), covered(static_cast<std::size_t>(w) * h, 0) {}
    std::size_t at(int x, int y) const { return static_cast<std::size_t>(y) * width + x; }
};

struct Projected { double x, y, depth; };

struct Camera {
    int width, height;
    double yaw, elevation, scale, baseline, vertical;
    Projected project(double x, double y, double z) const {
        const double x1 = x * std::cos(yaw) - z * std::sin(yaw);
        const double z1 = x * std::sin(yaw) + z * std::cos(yaw);
        const double s = std::min(width, height) * scale;
        return {
            width * 0.5 + x1 * s,
            height * baseline + z1 * s * std::sin(elevation) - y * s * std::cos(elevation) * vertical,
            z1 * std::cos(elevation) + y * std::sin(elevation),
        };
    }
};

Rgb lerpColor(Rgb a, Rgb b, double t) {
    auto channel = [t](int u, int v) { return static_cast<std::uint8_t>(u + (v - u) * t); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

// Desaturated cartographic palette shared with the interactive viewer.
Rgb terrainColor(double height, double sea_level) {
    if (height <= sea_level) {
        return lerpColor({33, 68, 96}, {98, 144, 163}, std::clamp(height / (sea_level + 1e-8), 0.0, 1.0));
    }
    const double t = std::clamp((height - sea_level) / (1.0 - sea_level + 1e-8), 0.0, 1.0);
    if (t < 0.45) return lerpColor({101, 138, 99}, {150, 165, 116}, t / 0.45);
    if (t < 0.8) return lerpColor({150, 165, 116}, {168, 143, 111}, (t - 0.45) / 0.35);
    return lerpColor({168, 143, 111}, {223, 220, 211}, (t - 0.8) / 0.2);
}

void fillTriangle(Canvas& canvas, const Projected& a, const Projected& b, const Projected& c, Rgb color, float depth) {
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1e-12) return;
    const double sign = area > 0 ? 1.0 : -1.0;
    for (int y = y0; y <= y1; ++y) {
        const double py = y + 0.5;
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double w0 = sign * ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x));
            const double w1 = sign * ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x));
            const double w2 = sign * ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x));
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const std::size_t i = canvas.at(x, y);
            canvas.pixels[i] = color;
            canvas.depth[i] = depth;
            canvas.covered[i] = 1;
        }
    }
}

// A river point is hidden when the visible terrain at its pixel is nearer the
// camera (larger depth) by more than a small epsilon.
bool occluded(const Canvas& canvas, double x, double y, double depth) {
    const int ix = static_cast<int>(std::lround(x));
    const int iy = static_cast<int>(std::lround(y));
    if (ix < 0 || iy < 0 || ix >= canvas.width || iy >= canvas.height) return false;
    const std::size_t i = canvas.at(ix, iy);
    return canvas.covered[i] && depth < canvas.depth[i] - 0.05;
}

// Thick line with flat ends, so consecutive pieces do not paint over each
// other's highlight.
void drawLine(Canvas& canvas, double ax, double ay, double bx, double by, double width, Rgb color) {
    const double r = std::max(0.5, width * 0.5);
    const double dx = bx - ax, dy = by - ay;
    const double length_squared = dx * dx + dy * dy;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - r)));
    const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(std::max(ax, bx) + r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - r)));
    const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(std::max(ay, by) + r)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5 - ax, py = y + 0.5 - ay;
            if (length_squared < 1e-12) {
                if (px * px + py * py <= r * r) canvas.pixels[canvas.at(x, y)] = color;
                continue;
            }
            const double t = (px * dx + py * dy) / length_squared;
            if (t < 0.0 || t > 1.0) continue;
            if (std::abs(px * dy - py * dx) / std::sqrt(length_squared) <= r) canvas.pixels[canvas.at(x, y)] = color;
        }
    }
}

void drawDisc(Canvas& canvas, double cx, double cy, double radius, Rgb color) {
    drawLine(canvas, cx, cy, cx, cy, radius * 2.0, color);
}

struct DrawnNetwork {
    std::size_t node_count = 0;
    std::size_t segment_count = 0;
    std::map<std::string, std::size_t> node_types;
    std::vector<double> segment_lengths;
};

DrawnNetwork renderFigure(
    const Options& options,
    const fi::Config& config,
    const View& view,
    const std::vector<double>& heights,
    const std::vector<const fi::PlateRiverCache*>& caches,
    Canvas& canvas) {
    const Camera camera{
        canvas.width, canvas.height,
        options.yaw_deg * kPi / 180.0, options.elevation_deg * kPi / 180.0,
        options.screen_scale, options.screen_baseline, options.vertical_scale};
    const int n = view.resolution;
    auto grid = [n](int i) { return -1.0 + 2.0 * i / (n - 1); };
    auto cube = [&](double h) { return std::clamp(h * options.height_scale, 0.0, 1.0); };

    // Terrain faces, painted back to front.
    struct Face { double depth; Projected a, b, c; Rgb color; };
    std::vector<Face> faces;
    faces.reserve(static_cast<std::size_t>(n - 1) * (n - 1) * 2);
    for (int row = 0; row + 1 < n; ++row) {
        for (int col = 0; col + 1 < n; ++col) {
            const double h0 = heights[static_cast<std::size_t>(row) * n + col];
            const double h1 = heights[static_cast<std::size_t>(row) * n + col + 1];
            const double h2 = heights[static_cast<std::size_t>(row + 1) * n + col + 1];
            const double h3 = heights[static_cast<std::size_t>(row + 1) * n + col];
            const Projected p0 = camera.project(grid(col), cube(h0), grid(row));
            const Projected p1 = camera.project(grid(col + 1), cube(h1), grid(row));
            const Projected p2 = camera.project(grid(col + 1), cube(h2), grid(row + 1));
            const Projected p3 = camera.project(grid(col), cube(h3), grid(row + 1));
            faces.push_back({(p0.depth + p1.depth + p2.depth) / 3.0, p0, p1, p2,
                             terrainColor((h0 + h1 + h2) / 3.0, config.sea_level_fraction)});
            faces.push_back({(p0.depth + p2.depth + p3.depth) / 3.0, p0, p2, p3,
                             terrainColor((h0 + h2 + h3) / 3.0, config.sea_level_fraction)});
        }
    }
    std::stable_sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.depth < b.depth; });
    for (const Face& f : faces) fillTriangle(canvas, f.a, f.b, f.c, f.color, static_cast<float>(f.depth));

    // River geometry clipped to the meshed view.
    struct Edge { const fi::RiverNode* a; const fi::RiverNode* b; int order; };
    std::vector<Edge> edges;
    std::vector<const fi::RiverNode*> nodes;
    std::map<const fi::RiverNode*, int> degree;
    DrawnNetwork drawn;
    for (const fi::PlateRiverCache* cache : caches) {
        for (const fi::RiverSegment& s : cache->segments) {
            const fi::RiverNode& a = cache->nodes[s.from];
            const fi::RiverNode& b = cache->nodes[s.to];
            if (!view.inside(a.world_point) || !view.inside(b.world_point)) continue;
            edges.push_back({&a, &b, s.strahler_order});
            ++degree[&a];
            ++degree[&b];
            drawn.segment_lengths.push_back(plateCells(a.world_point, b.world_point, config.plate_scale));
        }
        for (const fi::RiverNode& node : cache->nodes) {
            if (!view.inside(node.world_point)) continue;
            nodes.push_back(&node);
            ++drawn.node_types[nodeTypeName(node.type)];
        }
    }
    drawn.node_count = nodes.size();
    drawn.segment_count = edges.size();

    auto toScreen = [&](const fi::RiverNode& node) {
        const double x = (node.world_point.x - view.min_x) / view.world_width * 2.0 - 1.0;
        const double z = (node.world_point.y - view.min_y) / view.world_width * 2.0 - 1.0;
        Projected p = camera.project(x, cube(node.river_height) + options.river_height_offset, z);
        p.x = std::trunc(p.x);
        p.y = std::trunc(p.y);
        return p;
    };

    // Keep river symbols legible when the figure is reduced to page width.
    const double render_scale = std::max(1.0, std::min(canvas.width, canvas.height) / 760.0);
    const double channel_width = std::max(2.0, std::round(2.0 * render_scale));
    const double highlight_width = std::max(1.0, std::round(render_scale));
    const double endpoint_radius = std::max(2.0, std::round(2.0 * render_scale));
    const double confluence_radius = std::max(3.0, std::round(3.0 * render_scale));
    const Rgb channel_color{24, 112, 214}, highlight_color{188, 229, 255};

    for (const Edge& e : edges) {
        const Projected p0 = toScreen(*e.a);
        const Projected p1 = toScreen(*e.b);
        const double order_scale = 1.0 + 0.85 * std::min(std::max(0, e.order - 1), 4);
        const double w = std::max(1.0, std::round(channel_width * order_scale));
        const double hw = std::max(1.0, std::round(highlight_width * order_scale));
        // Test visibility at roughly two-pixel intervals so a channel crossing a
        // ridge is clipped exactly where the ridge hides it.
        const double length = std::hypot(p1.x - p0.x, p1.y - p0.y);
        const int pieces = std::max(1, static_cast<int>(std::ceil(length / 2.0)));
        for (int k = 0; k < pieces; ++k) {
            const double t0 = static_cast<double>(k) / pieces;
            const double t1 = static_cast<double>(k + 1) / pieces;
            const double tm = 0.5 * (t0 + t1);
            if (occluded(canvas, p0.x + (p1.x - p0.x) * tm, p0.y + (p1.y - p0.y) * tm,
                         p0.depth + (p1.depth - p0.depth) * tm)) continue;
            const double ax = p0.x + (p1.x - p0.x) * t0, ay = p0.y + (p1.y - p0.y) * t0;
            const double bx = p0.x + (p1.x - p0.x) * t1, by = p0.y + (p1.y - p0.y) * t1;
            drawLine(canvas, ax, ay, bx, by, w, channel_color);
            drawLine(canvas, ax, ay, bx, by, hw, highlight_color);
        }
    }

    // Mark only topologically meaningful nodes: sources, outlets, confluences.
    for (const fi::RiverNode* node : nodes) {
        const bool confluence = degree[node] >= 3;
        if (node->type == fi::RiverNodeType::Channel && !confluence) continue;
        const Projected p = toScreen(*node);
        if (occluded(canvas, p.x, p.y, p.depth)) continue;
        switch (node->type) {
            case fi::RiverNodeType::Source: drawDisc(canvas, p.x, p.y, endpoint_radius, {248, 250, 252}); break;
            case fi::RiverNodeType::LocalMinimum: drawDisc(canvas, p.x, p.y, endpoint_radius, {255, 176, 64}); break;
            case fi::RiverNodeType::SeaOutlet: drawDisc(canvas, p.x, p.y, endpoint_radius, {72, 190, 255}); break;
            case fi::RiverNodeType::Channel: drawDisc(canvas, p.x, p.y, confluence_radius, {34, 148, 242}); break;
        }
    }
    return drawn;
}

std::vector<Rgb> downsample(const Canvas& canvas, int factor) {
    const int w = canvas.width / factor;
    const int h = canvas.height / factor;
    std::vector<Rgb> out(static_cast<std::size_t>(w) * h);
    const double inv = 1.0 / (factor * factor);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double r = 0, g = 0, b = 0;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    const Rgb& p = canvas.pixels[canvas.at(x * factor + dx, y * factor + dy)];
                    r += p.r; g += p.g; b += p.b;
                }
            }
            out[static_cast<std::size_t>(y) * w + x] = {
                static_cast<std::uint8_t>(std::lround(r * inv)),
                static_cast<std::uint8_t>(std::lround(g * inv)),
                static_cast<std::uint8_t>(std::lround(b * inv))};
        }
    }
    return out;
}

void writePng(const std::string& path, int width, int height, const std::vector<Rgb>& pixels) {
    // Each row uses the "Sub" filter, which compresses smooth terrain well.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + 3 * width));
    for (int y = 0; y < height; ++y) {
        raw.push_back(1);
        Rgb previous{0, 0, 0};
        for (int x = 0; x < width; ++x) {
            const Rgb& p = pixels[static_cast<std::size_t>(y) * width + x];
            raw.push_back(static_cast<std::uint8_t>(p.r - previous.r));
            raw.push_back(static_cast<std::uint8_t>(p.g - previous.g));
            raw.push_back(static_cast<std::uint8_t>(p.b - previous.b));
            previous = p;
        }
    }
    uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK) {
        throw std::runtime_error("zlib compression failed");
    }
    compressed.resize(compressed_size);

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("could not open " + path);
    auto put32 = [&](std::uint32_t v) {
        const char bytes[4] = {static_cast<char>(v >> 24), static_cast<char>(v >> 16),
                               static_cast<char>(v >> 8), static_cast<char>(v)};
        file.write(bytes, 4);
    };
    auto chunk = [&](const char* type, const std::vector<std::uint8_t>& data) {
        put32(static_cast<std::uint32_t>(data.size()));
        file.write(type, 4);
        if (!data.empty()) file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(type), 4);
        if (!data.empty()) crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
        put32(static_cast<std::uint32_t>(crc));
    };
    const char signature[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    file.write(signature, 8);
    std::vector<std::uint8_t> header(13, 0);
    for (int i = 0; i < 4; ++i) {
        header[i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(width) >> (24 - 8 * i));
        header[4 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) >> (24 - 8 * i));
    }
    header[8] = 8;  // bit depth
    header[9] = 2;  // truecolor RGB
    chunk("IHDR", header);
    chunk("IDAT", compressed);
    chunk("IEND", {});
}

// ---------------------------------------------------------------- metadata + output

std::string platformDescription() {
    std::string text;
#if defined(__unix__) || defined(__APPLE__)
    utsname info{};
    if (uname(&info) == 0) text = std::string(info.sysname) + " " + info.release + " " + info.machine;
#endif
    return text.empty() ? "unknown" : text;
}

std::string cpuDescription() {
#if defined(__APPLE__)
    char buffer[256] = {};
    std::size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0) return buffer;
#endif
    return "unknown";
}

std::string compilerDescription() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

std::string jsonString(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

class Json {
public:
    explicit Json(std::ostream& out) : out_(out) { out_ << std::setprecision(17); }
    void open(const std::string& key = "") { comma(); if (!key.empty()) out_ << jsonString(key) << ": "; out_ << "{"; first_ = true; ++depth_; }
    void openArray(const std::string& key) { comma(); out_ << jsonString(key) << ": ["; first_ = true; ++depth_; }
    void close(char bracket = '}') { --depth_; out_ << "\n" << std::string(2 * depth_, ' ') << bracket; first_ = false; }
    template <typename T> void field(const std::string& key, const T& value) { comma(); out_ << jsonString(key) << ": " << value; }
    void field(const std::string& key, const std::string& value) { comma(); out_ << jsonString(key) << ": " << jsonString(value); }
    void field(const std::string& key, const char* value) { field(key, std::string(value)); }
    void field(const std::string& key, bool value) { comma(); out_ << jsonString(key) << ": " << (value ? "true" : "false"); }
private:
    void comma() { if (!first_) out_ << ","; if (depth_ > 0) out_ << "\n" << std::string(2 * depth_, ' '); first_ = false; }
    std::ostream& out_;
    bool first_ = true;
    int depth_ = 0;
};

void writePlateFields(Json& json, const PlateMetrics& m) {
    json.field("plate_x", m.owner.x);
    json.field("plate_y", m.owner.y);
    json.field("cache_build_seconds_median", m.cache_build_seconds);
    json.field("grid_valid_cells", m.grid_valid_cells);
    json.field("grid_land_fraction", m.grid_land_fraction);
    json.field("node_count", m.node_count);
    json.field("segment_count", m.segment_count);
    json.field("path_count", m.path_count);
    json.field("source_count", m.source_count);
    json.field("sea_outlet_count", m.sea_outlet_count);
    json.field("local_minimum_outlet_count", m.local_minimum_outlet_count);
    json.field("channel_node_count", m.channel_node_count);
    json.field("confluence_count", m.confluence_count);
    json.field("connected_component_count", m.connected_component_count);
    json.field("max_strahler_order", m.max_strahler_order);
    json.field("total_channel_length_plate_cells", m.total_channel_length);
    json.field("mean_segment_length_plate_cells", m.mean_segment_length);
    json.field("mean_path_length_plate_cells", m.mean_path_length);
    json.field("max_path_length_plate_cells", m.max_path_length);
    json.field("mean_segment_height_drop", m.mean_segment_height_drop);
    json.field("min_segment_height_drop", m.min_segment_height_drop);
    json.field("downhill_violation_count", m.downhill_violation_count);
    json.field("network_bytes", m.network_bytes);
    json.field("grid_bytes", m.grid_bytes);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = Clock::now();
        const Options options = parseOptions(argc, argv);
        fi::Config config;
        config.seed = options.seed;

        View view;
        {
            const fi::World probe(config);
            const fi::Point center = probe.plateCenter({1, 0});
            view.center_x = center.x / config.plate_scale;
            view.center_y = center.y / config.plate_scale;
        }
        view.world_width = options.plate_cells / config.plate_scale;
        view.min_x = view.center_x - view.world_width * 0.5;
        view.min_y = view.center_y - view.world_width * 0.5;
        view.resolution = options.resolution;

        std::vector<fi::PlateIndex> owners;
        {
            const fi::World probe(config);
            owners = visibleOwners(probe, view);
        }

        // Timing repeats, each on a fresh World so every cache is built cold.
        std::vector<RunResult> runs;
        fi::World world(config);
        for (int repeat = 0; repeat < options.timing_repeats; ++repeat) {
            if (repeat == 0) {
                runs.push_back(timedRun(world, view, owners));
            } else {
                fi::World fresh(config);
                runs.push_back(timedRun(fresh, view, owners));
            }
        }
        bool deterministic = true;
        for (const RunResult& run : runs) {
            deterministic = deterministic && run.heights == runs[0].heights
                && run.node_total == runs[0].node_total && run.segment_total == runs[0].segment_total;
        }
        const std::vector<double>& heights = runs[0].heights;

        std::vector<double> build_median(owners.size());
        std::vector<double> build_totals, sample_rivers, sample_base;
        for (std::size_t i = 0; i < owners.size(); ++i) {
            std::vector<double> samples;
            for (const RunResult& run : runs) samples.push_back(run.cache_build_seconds[i]);
            build_median[i] = median(samples);
        }
        for (const RunResult& run : runs) {
            build_totals.push_back(std::accumulate(run.cache_build_seconds.begin(), run.cache_build_seconds.end(), 0.0));
            sample_rivers.push_back(run.sample_with_rivers_seconds);
            sample_base.push_back(run.sample_base_only_seconds);
        }

        std::vector<const fi::PlateRiverCache*> caches;
        std::vector<PlateMetrics> per_plate;
        for (std::size_t i = 0; i < owners.size(); ++i) {
            caches.push_back(world.cachedPlate(owners[i]));
            per_plate.push_back(plateMetrics(*caches.back(), build_median[i], config));
        }

        const auto check_start = Clock::now();
        const ChannelCheck channels = checkChannels(world, caches, options.channel_samples);
        const double check_seconds = seconds(check_start, Clock::now());

        // Render and save.
        const auto render_start = Clock::now();
        Canvas canvas(options.width * options.supersample, options.height * options.supersample, {12, 15, 20});
        const DrawnNetwork drawn = renderFigure(options, config, view, heights, caches, canvas);
        const std::vector<Rgb> image = options.supersample > 1
            ? downsample(canvas, options.supersample) : canvas.pixels;
        const double render_seconds = seconds(render_start, Clock::now());
        const auto save_start = Clock::now();
        writePng(options.output, options.width, options.height, image);
        const double save_seconds = seconds(save_start, Clock::now());

        // Aggregate statistics.
        auto total = [&](auto member) {
            std::size_t sum = 0;
            for (const PlateMetrics& m : per_plate) sum += m.*member;
            return sum;
        };
        double total_length = 0.0;
        int max_order = 0;
        for (const PlateMetrics& m : per_plate) { total_length += m.total_channel_length; max_order = std::max(max_order, m.max_strahler_order); }
        const std::size_t segments = total(&PlateMetrics::segment_count);
        const std::size_t violations = total(&PlateMetrics::downhill_violation_count);
        const std::size_t network_bytes = total(&PlateMetrics::network_bytes);
        const std::size_t grid_bytes = total(&PlateMetrics::grid_bytes);
        const std::size_t requested = static_cast<std::size_t>(config.river_count) * owners.size();
        const std::size_t sample_count = heights.size();
        std::size_t land = 0;
        for (double h : heights) land += h > config.sea_level_fraction;
        std::vector<double> build_list(build_median);

        if (!options.metrics_output.empty()) {
            std::ofstream file(options.metrics_output);
            if (!file) throw std::runtime_error("could not open " + options.metrics_output);
            Json json(file);
            json.open();
            json.open("metadata");
            json.field("implementation", "flat-infinite C++17");
            json.field("seed", config.seed);
            json.field("compiler", compilerDescription());
#ifdef NDEBUG
            json.field("assertions", "disabled (NDEBUG)");
#else
            json.field("assertions", "enabled");
#endif
            json.field("platform", platformDescription());
            json.field("cpu", cpuDescription());
            json.field("timing_repeats", options.timing_repeats);
            json.field("deterministic_across_repeats", deterministic);
            json.close();

            json.open("figure_domain");
            json.field("image_width_pixels", options.width);
            json.field("image_height_pixels", options.height);
            json.field("supersample", options.supersample);
            json.field("plate_cell_width", options.plate_cells);
            json.field("world_coordinate_width", view.world_width);
            json.field("view_center_x", view.center_x);
            json.field("view_center_y", view.center_y);
            json.field("terrain_resolution", view.resolution);
            json.field("terrain_sample_count", sample_count);
            json.field("visible_plate_count", owners.size());
            json.close();

            json.open("generation_parameters");
            json.field("river_grid_resolution_per_plate", config.river_grid_resolution);
            json.field("maximum_requested_paths_per_plate", config.river_count);
            json.field("source_minimum_height", config.source_min_height);
            json.field("minimum_source_spacing_grid_cells", config.min_source_spacing);
            json.field("river_step_size_grid_cells", config.river_step_size);
            json.field("maximum_river_steps", config.max_river_steps);
            json.field("border_margin_world_units", config.river_border_distance);
            json.field("river_blend_width_world_units", config.river_distance_width);
            json.field("lake_radius_world_units", config.river_lake_radius);
            json.field("planning_relief_attenuation", config.base_terrain_mountain_scale);
            json.field("sea_level", config.sea_level_fraction);
            json.close();

            json.open("terrain");
            json.field("minimum_height", *std::min_element(heights.begin(), heights.end()));
            json.field("maximum_height", *std::max_element(heights.begin(), heights.end()));
            json.field("mean_height", mean(heights));
            json.field("height_percentile_05", percentile(heights, 5));
            json.field("height_percentile_50", percentile(heights, 50));
            json.field("height_percentile_95", percentile(heights, 95));
            json.field("land_sample_count", land);
            json.field("land_fraction", static_cast<double>(land) / sample_count);
            json.field("ocean_fraction", 1.0 - static_cast<double>(land) / sample_count);
            json.close();

            json.open("all_visible_plate_networks");
            json.field("node_count", total(&PlateMetrics::node_count));
            json.field("segment_count", segments);
            json.field("accepted_path_count", total(&PlateMetrics::path_count));
            json.field("requested_path_count", requested);
            json.field("path_acceptance_fraction", static_cast<double>(total(&PlateMetrics::path_count)) / requested);
            json.field("source_count", total(&PlateMetrics::source_count));
            json.field("sea_outlet_count", total(&PlateMetrics::sea_outlet_count));
            json.field("local_minimum_outlet_count", total(&PlateMetrics::local_minimum_outlet_count));
            json.field("confluence_count", total(&PlateMetrics::confluence_count));
            json.field("connected_component_count", total(&PlateMetrics::connected_component_count));
            json.field("max_strahler_order", max_order);
            json.field("total_channel_length_plate_cells", total_length);
            json.field("mean_channel_length_per_plate_cells", total_length / owners.size());
            json.field("stored_segment_downhill_violation_count", violations);
            json.field("stored_segment_downhill_compliance_fraction",
                       segments ? 1.0 - static_cast<double>(violations) / segments : 1.0);
            json.close();

            json.open("final_terrain_channel_check");
            json.field("samples_per_segment", options.channel_samples);
            json.field("segments_checked", channels.segments_checked);
            json.field("samples", channels.samples);
            json.field("foreign_owner_samples", channels.foreign_owner_samples);
            json.field("segments_with_foreign_owner_samples", channels.segments_with_foreign_samples);
            json.field("collinearly_overlapping_segments", channels.overlapping_segments);
            json.field("violating_segments", channels.violating_segments);
            json.field("violating_segments_fully_owned", channels.violating_segments_owned);
            json.field("violating_segments_fully_owned_overlapping", channels.violating_segments_owned_overlapping);
            json.field("max_downstream_rise", channels.max_rise);
            json.field("max_downstream_rise_fully_owned", channels.max_rise_owned);
            json.field("max_downstream_rise_fully_owned_non_overlapping", channels.max_rise_owned_non_overlapping);
            json.field("seconds", check_seconds);
            json.close();

            json.open("clipped_figure_network");
            json.field("node_count", drawn.node_count);
            json.field("segment_count", drawn.segment_count);
            json.open("node_type_counts");
            for (const auto& [name, count] : drawn.node_types) json.field(name, count);
            json.close();
            json.field("total_channel_length_plate_cells",
                       std::accumulate(drawn.segment_lengths.begin(), drawn.segment_lengths.end(), 0.0));
            json.field("mean_segment_length_plate_cells", mean(drawn.segment_lengths));
            json.close();

            json.open("cache_memory");
            json.field("network_bytes", network_bytes);
            json.field("network_mebibytes", network_bytes / (1024.0 * 1024.0));
            json.field("network_bytes_per_segment", segments ? static_cast<double>(network_bytes) / segments : 0.0);
            json.field("grid_bytes", grid_bytes);
            json.field("grid_mebibytes", grid_bytes / (1024.0 * 1024.0));
            json.field("sizeof_river_node", sizeof(fi::RiverNode));
            json.field("sizeof_river_segment", sizeof(fi::RiverSegment));
            json.close();

            json.open("timings");
            json.field("cache_build_total_seconds_median", median(build_totals));
            json.field("cache_build_per_plate_mean_seconds", mean(build_list));
            json.field("cache_build_per_plate_median_seconds", median(build_list));
            json.field("cache_build_per_plate_minimum_seconds", *std::min_element(build_list.begin(), build_list.end()));
            json.field("cache_build_per_plate_maximum_seconds", *std::max_element(build_list.begin(), build_list.end()));
            json.field("terrain_sampling_with_rivers_seconds_median", median(sample_rivers));
            json.field("terrain_sampling_base_only_seconds_median", median(sample_base));
            json.field("per_sample_with_rivers_microseconds", median(sample_rivers) / sample_count * 1e6);
            json.field("per_sample_base_only_microseconds", median(sample_base) / sample_count * 1e6);
            json.field("render_seconds", render_seconds);
            json.field("png_write_seconds", save_seconds);
            json.field("total_export_seconds", seconds(total_start, Clock::now()));
            json.close();

            json.openArray("per_plate");
            for (const PlateMetrics& m : per_plate) {
                json.open();
                writePlateFields(json, m);
                json.close();
            }
            json.close(']');
            json.close();
            file << '\n';
        }

        if (!options.plate_metrics_output.empty()) {
            std::ofstream csv(options.plate_metrics_output);
            if (!csv) throw std::runtime_error("could not open " + options.plate_metrics_output);
            csv << std::setprecision(17)
                << "plate_x,plate_y,cache_build_seconds_median,grid_valid_cells,grid_land_fraction,node_count,"
                   "segment_count,path_count,source_count,sea_outlet_count,local_minimum_outlet_count,"
                   "channel_node_count,confluence_count,connected_component_count,max_strahler_order,"
                   "total_channel_length_plate_cells,mean_segment_length_plate_cells,mean_path_length_plate_cells,"
                   "max_path_length_plate_cells,mean_segment_height_drop,min_segment_height_drop,"
                   "downhill_violation_count,network_bytes,grid_bytes\n";
            for (const PlateMetrics& m : per_plate) {
                csv << m.owner.x << ',' << m.owner.y << ',' << m.cache_build_seconds << ',' << m.grid_valid_cells << ','
                    << m.grid_land_fraction << ',' << m.node_count << ',' << m.segment_count << ',' << m.path_count << ','
                    << m.source_count << ',' << m.sea_outlet_count << ',' << m.local_minimum_outlet_count << ','
                    << m.channel_node_count << ',' << m.confluence_count << ',' << m.connected_component_count << ','
                    << m.max_strahler_order << ',' << m.total_channel_length << ',' << m.mean_segment_length << ','
                    << m.mean_path_length << ',' << m.max_path_length << ',' << m.mean_segment_height_drop << ','
                    << m.min_segment_height_drop << ',' << m.downhill_violation_count << ',' << m.network_bytes << ','
                    << m.grid_bytes << '\n';
            }
        }

        std::cout << "Saved " << options.output << " (" << options.width << 'x' << options.height << ", "
                  << options.plate_cells << " plate cells, " << owners.size() << " visible plates, "
                  << (deterministic ? "deterministic" : "NON-DETERMINISTIC") << " across "
                  << options.timing_repeats << " repeats)\n";
        return deterministic ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
