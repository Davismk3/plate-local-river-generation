#include "finite_world/world.hpp"

#include "finite_world/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace finite_world {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kNeighborCount = 9;

struct Neighbor {
    double distance = 1e18;
    int plate = 0;
};

double skewWeight(double distance_value, double width, double interior_skew, double border_skew) {
    const double t = distance_value / (width + 1e-8);
    if (t <= 0.0 || t >= 1.0) return 0.0;
    const double coefficient = 1.0 / (
        std::pow(interior_skew / (interior_skew + border_skew), interior_skew)
        * std::pow(border_skew / (interior_skew + border_skew), border_skew));
    return coefficient * std::pow(t, interior_skew) * std::pow(1.0 - t, border_skew);
}

double driftCoverage(double drift_scalar, double coverage) {
    return std::max(0.0, (drift_scalar + 2.0 * coverage - 1.0) / (2.0 * coverage + 1e-8));
}

// Local tangent drift direction of a plate: a_i x n(x), normalized.
Vec3 localDrift(Vec3 axis, Vec3 normal) { return normalize(cross(axis, normal)); }

} // namespace

World::World(Config config) : config_(std::move(config)) {
    const int count = std::max(2, config_.plate_count);
    const double area = config_.surface == Surface::Sphere
        ? 4.0 * kPi * config_.radius * config_.radius
        : 24.0 * config_.radius * config_.radius;
    // One plate cell is roughly the spacing between neighboring seeds.
    plate_scale_ = std::sqrt(static_cast<double>(count) / area);

    const double golden_angle = kPi * (3.0 - std::sqrt(5.0));
    for (int i = 0; i < count; ++i) {
        const double y = 1.0 - (2.0 * i + 1.0) / count;
        const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double theta = golden_angle * i;
        const Vec3 direction{r * std::cos(theta), y, r * std::sin(theta)};
        const Vec3 jitter{
            hash3(i, 0, 0, config_.seed + 801),
            hash3(i, 0, 0, config_.seed + 802),
            hash3(i, 0, 0, config_.seed + 803)};
        const Vec3 center = surfacePoint(direction) * plate_scale_ + jitter * config_.plate_jitter;
        centers_.push_back(center);

        const Vec3 random{
            hash3(i, 0, 0, config_.seed + 3101),
            hash3(i, 0, 0, config_.seed + 3102),
            hash3(i, 0, 0, config_.seed + 3103)};
        drift_axes_.push_back(normalize(cross(center, random)));

        const double value = (hash3(i, 0, 0, config_.seed + 2001) + 1.0) * 0.5;
        types_.push_back(value > config_.continents_fraction ? PlateType::Oceanic : PlateType::Continental);
    }
}

Vec3 World::surfacePoint(Vec3 direction) const {
    if (config_.surface == Surface::Sphere) return normalize(direction) * config_.radius;
    const double m = std::max({std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)});
    return m > 1e-300 ? direction * (config_.radius / m) : Vec3{config_.radius, 0.0, 0.0};
}

Vec3 World::warpedPlatePoint(Vec3 x) const {
    const Vec3 p = x * plate_scale_;
    const Vec3 q = p * config_.warp_frequency;
    const Vec3 warp{
        fbm3(q, config_.seed + 1, config_.warp_octaves),
        fbm3(q, config_.seed + 2, config_.warp_octaves),
        fbm3(q, config_.seed + 3, config_.warp_octaves)};
    return p + warp * config_.warp_amplitude;
}

int World::plateOwner(Vec3 x) const {
    const Vec3 p = warpedPlatePoint(x);
    int best = 0;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < plateCount(); ++i) {
        const Vec3 d = p - centers_[i];
        const double squared = dot(d, d);
        if (squared < best_distance) { best_distance = squared; best = i; }
    }
    return best;
}

int World::geometricOwner(Vec3 x) const {
    const Vec3 p = x * plate_scale_;
    int best = 0;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < plateCount(); ++i) {
        const Vec3 d = p - centers_[i];
        const double squared = dot(d, d);
        if (squared < best_distance) { best_distance = squared; best = i; }
    }
    return best;
}

double World::borderDistance(int plate, Vec3 x) const {
    const Vec3 p = x * plate_scale_;
    const Vec3 c = centers_[plate];
    double best = std::numeric_limits<double>::infinity();
    for (int j = 0; j < plateCount(); ++j) {
        if (j == plate) continue;
        const Vec3 normal = normalize(centers_[j] - c);
        const Vec3 midpoint = (c + centers_[j]) * 0.5;
        best = std::min(best, dot(midpoint - p, normal));
    }
    return best;
}

TerrainFields World::terrainFields(Vec3 x) const {
    const Vec3 p = warpedPlatePoint(x);

    // The nearest few seeds, sorted by distance in warped plate space.
    std::array<Neighbor, kNeighborCount> neighbors{};
    std::size_t count = 0;
    for (int i = 0; i < plateCount(); ++i) {
        const double d = length(p - centers_[i]);
        if (count == kNeighborCount && d >= neighbors[count - 1].distance) continue;
        std::size_t at = count < kNeighborCount ? count++ : kNeighborCount - 1;
        while (at > 0 && d < neighbors[at - 1].distance) {
            neighbors[at] = neighbors[at - 1];
            --at;
        }
        neighbors[at] = {d, i};
    }

    TerrainFields fields;
    const int owner = neighbors[0].plate;
    fields.owner = owner;
    fields.owner_type = types_[owner];
    fields.crust = config_.ocean_height;

    const Vec3 normal = normalize(x);
    const Vec3 drift = localDrift(drift_axes_[owner], normal);
    fields.drift_scalar = dot(p - centers_[owner], drift);

    const double border_distance = neighbors[1].distance - neighbors[0].distance;
    double continental_distance = 1e18;
    double ocean_distance = 1e18;
    for (std::size_t i = 1; i < count; ++i) {
        const double margin = neighbors[i].distance - neighbors[0].distance;
        if (types_[neighbors[i].plate] == PlateType::Continental) continental_distance = std::min(continental_distance, margin);
        else ocean_distance = std::min(ocean_distance, margin);
    }

    if (fields.owner_type == PlateType::Oceanic) {
        if (continental_distance < 1e17) {
            const double blend = 1.0 - continental_distance / (config_.ocean_blend_width + 1e-8);
            fields.crust = config_.ocean_height + (config_.continent_height - config_.ocean_height)
                * positivePow(blend, config_.ocean_blend_exponent);
        }
        return fields;
    }

    fields.crust = config_.continent_height;
    if (border_distance <= config_.land_width) {
        fields.land = config_.land_amplitude
            * skewWeight(border_distance, config_.land_width, config_.land_interior_skew, config_.land_border_skew)
            * driftCoverage(fields.drift_scalar, config_.land_plate_coverage);
    }
    if (border_distance <= config_.islands_width) {
        fields.islands = config_.islands_amplitude
            * skewWeight(border_distance, config_.islands_width, config_.islands_interior_skew, config_.islands_border_skew)
            * driftCoverage(fields.drift_scalar, config_.islands_plate_coverage);
    }
    if (border_distance <= config_.mountains_width) {
        fields.mountains = config_.mountains_amplitude
            * skewWeight(border_distance, config_.mountains_width, config_.mountains_interior_skew, config_.mountains_border_skew)
            * driftCoverage(fields.drift_scalar, config_.mountains_plate_coverage);
    }

    // Plateaus where two continental plates converge.
    double plateau_response = 0.0;
    const double bridge = config_.plateau_bridge_width;
    const double ocean_weight = smoothStep(ocean_distance / (bridge + 1e-8));
    if (ocean_weight > 0.0) {
        for (std::size_t a = 0; a + 1 < count; ++a) {
            const int plate_a = neighbors[a].plate;
            if (types_[plate_a] == PlateType::Oceanic) continue;
            const double near_weight = 1.0 - smoothStep((neighbors[a].distance - neighbors[0].distance) / (bridge + 1e-8));
            if (near_weight <= 0.0) continue;
            for (std::size_t b = a + 1; b < count; ++b) {
                const int plate_b = neighbors[b].plate;
                if (types_[plate_b] == PlateType::Oceanic) continue;
                const double border_weight = 1.0 - smoothStep((neighbors[b].distance - neighbors[a].distance) / (bridge + 1e-8));
                if (border_weight <= 0.0) continue;
                Vec3 separation = centers_[plate_b] - centers_[plate_a];
                separation = normalize(separation - normal * dot(separation, normal));
                const double closing_a = dot(localDrift(drift_axes_[plate_a], normal), separation);
                const double closing_b = -dot(localDrift(drift_axes_[plate_b], normal), separation);
                if (closing_a <= 0.0 || closing_b <= 0.0) continue;
                const double score = border_weight * near_weight * closing_a * closing_b
                    * smoothStep(closing_a / (config_.plateau_closing_fade_width + 1e-8))
                    * smoothStep(closing_b / (config_.plateau_closing_fade_width + 1e-8));
                plateau_response = std::max(plateau_response, score);
            }
        }
    }
    fields.plateau = config_.plateau_amplitude * smoothStep(4.0 * plateau_response * ocean_weight)
        * std::max(0.0, config_.land_amplitude - fields.land)
        * std::max(0.0, config_.mountains_amplitude - fields.mountains);

    const double drift_start = 1.0 - 2.0 * config_.land_plate_coverage + config_.sea_level_fraction;
    const double drift_length = 2.0 * (config_.islands_plate_coverage - config_.land_plate_coverage) + 1e-8;
    const double islands_fade = std::max(
        0.0, 1.0 + config_.sea_level_fraction - (fields.drift_scalar - drift_start) / drift_length);
    fields.islands *= islands_fade * (1.0 - std::min(1.0, fields.plateau));
    return fields;
}

double World::firstHeight(Vec3 x) const {
    const TerrainFields f = terrainFields(x);
    return f.crust + f.land * config_.base_terrain_land_scale
        + (f.mountains + f.plateau + f.islands) * config_.base_terrain_mountain_scale;
}

double World::baseHeight(Vec3 x) const {
    const TerrainFields f = terrainFields(x);
    const double island_noise = (fbm3(x * (plate_scale_ * config_.island_noise_frequency), config_.seed + 97, 1) + 1.0) * 0.5;
    return f.crust + f.land + f.mountains + f.plateau + f.islands * island_noise;
}

PlateChart World::plateChart(int plate) const {
    PlateChart chart;
    chart.center = normalize(centers_[plate]);
    const Vec3 helper = std::abs(chart.center.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};
    chart.e1 = normalize(cross(helper, chart.center));
    chart.e2 = cross(chart.center, chart.e1);
    chart.plane_distance = length(surfacePoint(chart.center));
    return chart;
}

Vec3 World::chartToSurface(const PlateChart& chart, Vec2 point) const {
    return surfacePoint(chart.center * chart.plane_distance + chart.e1 * point.x + chart.e2 * point.y);
}

bool World::surfaceToChart(const PlateChart& chart, Vec3 x, Vec2& point) const {
    const double along = dot(x, chart.center);
    if (along <= 1e-12) return false;
    const Vec3 on_plane = x * (chart.plane_distance / along);
    point = {dot(on_plane, chart.e1), dot(on_plane, chart.e2)};
    return true;
}

PlateGrid World::buildPlateGrid(int plate) const {
    const int resolution = std::max(2, config_.river_grid_resolution);
    PlateGrid grid;
    grid.owner = plate;
    grid.resolution = resolution;
    const std::size_t size = static_cast<std::size_t>(resolution) * resolution;
    grid.chart_points.resize(size);
    grid.world_points.resize(size);
    grid.heights.assign(size, 0.0);
    grid.valid.assign(size, 0);
    grid.border_safe.assign(size, 0);
    grid.lake_safe.assign(size, 0);

    // Chart bounding rectangle of the plate domain, from a coarse scan that
    // grows until the domain no longer touches the scanned square.
    const PlateChart chart = plateChart(plate);
    constexpr int coarse = 64;
    double half = 1.5 / plate_scale_;
    double min_a = 0.0, max_a = 0.0, min_b = 0.0, max_b = 0.0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        bool found = false, touches_edge = false;
        min_a = min_b = std::numeric_limits<double>::infinity();
        max_a = max_b = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < coarse; ++i) {
            for (int j = 0; j < coarse; ++j) {
                const Vec2 q{-half + 2.0 * half * i / (coarse - 1), -half + 2.0 * half * j / (coarse - 1)};
                if (borderDistance(plate, chartToSurface(chart, q)) < 0.0) continue;
                found = true;
                touches_edge = touches_edge || i == 0 || j == 0 || i == coarse - 1 || j == coarse - 1;
                min_a = std::min(min_a, q.x); max_a = std::max(max_a, q.x);
                min_b = std::min(min_b, q.y); max_b = std::max(max_b, q.y);
            }
        }
        if (found && !touches_edge) break;
        half *= 1.5;
    }
    const double pad = 2.0 * half / (coarse - 1);
    min_a -= pad; max_a += pad; min_b -= pad; max_b += pad;

    const double lake_margin = config_.river_border_margin + config_.river_lake_radius + config_.river_blend_width;
    for (int row = 0; row < resolution; ++row) {
        const double fb = static_cast<double>(row) / (resolution - 1);
        for (int col = 0; col < resolution; ++col) {
            const double fa = static_cast<double>(col) / (resolution - 1);
            const Vec2 q{min_a + fa * (max_a - min_a), min_b + fb * (max_b - min_b)};
            const Vec3 x = chartToSurface(chart, q);
            const std::size_t index = grid.index(row, col);
            grid.chart_points[index] = q;
            grid.world_points[index] = x;
            const double border = borderDistance(plate, x);
            if (border < 0.0) continue;
            grid.valid[index] = 1;
            grid.border_safe[index] = border >= config_.river_border_margin;
            grid.lake_safe[index] = border >= lake_margin;
            grid.heights[index] = firstHeight(x);
        }
    }
    return grid;
}

} // namespace finite_world
