#include "flat_infinite/world.hpp"

#include "flat_infinite/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace flat_infinite {
namespace {

struct Neighbor {
    double distance = 1e18;
    PlateIndex index;
    Point center;
    Point drift;
    PlateType type = PlateType::Oceanic;
};

double plateDistance(
    const std::array<Neighbor, 9>& neighbors,
    PlateType type_a,
    std::optional<PlateType> type_b) {
    for (std::size_t i = 1; i < neighbors.size(); ++i) {
        if (neighbors[i].type == type_a || (type_b && neighbors[i].type == *type_b)) {
            return neighbors[i].distance - neighbors[0].distance;
        }
    }
    return 1e18;
}

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

double plateauWeight(const std::array<Neighbor, 9>& neighbors, const Config& config) {
    const double bridge_width = config.plateau_bridge_width;
    double ocean_distance = 1e18;
    const double nearest_distance = neighbors[0].distance;
    for (const Neighbor& neighbor : neighbors) {
        if (neighbor.type == PlateType::Oceanic) {
            ocean_distance = std::min(ocean_distance, neighbor.distance - nearest_distance);
        }
    }
    const double ocean_weight = smoothStep(ocean_distance / (bridge_width + 1e-8));
    if (ocean_weight <= 0.0) return 0.0;

    double result = 0.0;
    for (std::size_t plate = 0; plate + 1 < neighbors.size(); ++plate) {
        if (neighbors[plate].type == PlateType::Oceanic) continue;
        const double pair_distance = neighbors[plate].distance - nearest_distance;
        const double pair_near_weight = 1.0 - smoothStep(pair_distance / (bridge_width + 1e-8));
        if (pair_near_weight <= 0.0) continue;

        for (std::size_t nbr = plate + 1; nbr < neighbors.size(); ++nbr) {
            if (neighbors[nbr].type == PlateType::Oceanic) continue;
            const double border_distance = neighbors[nbr].distance - neighbors[plate].distance;
            const double border_weight = 1.0 - smoothStep(border_distance / (bridge_width + 1e-8));
            if (border_weight <= 0.0) continue;

            Point direction{
                neighbors[nbr].center.x - neighbors[plate].center.x,
                neighbors[nbr].center.y - neighbors[plate].center.y};
            const double magnitude = std::hypot(direction.x, direction.y) + 1e-8;
            direction.x /= magnitude;
            direction.y /= magnitude;
            const double plate_closing = neighbors[plate].drift.x * direction.x
                + neighbors[plate].drift.y * direction.y;
            const double neighbor_closing = -(neighbors[nbr].drift.x * direction.x
                + neighbors[nbr].drift.y * direction.y);
            if (plate_closing <= 0.0 || neighbor_closing <= 0.0) continue;

            const double score = border_weight * pair_near_weight * plate_closing * neighbor_closing
                * smoothStep(plate_closing / (config.plateau_closing_fade_width + 1e-8))
                * smoothStep(neighbor_closing / (config.plateau_closing_fade_width + 1e-8));
            result = std::max(result, score);
        }
    }
    return result * ocean_weight;
}

} // namespace

World::World(Config config) : config_(std::move(config)) {}

RiverOptions defaultRiverOptions(const Config& config) {
    return {
        config.river_grid_resolution,
        config.river_count,
        config.source_min_height,
        config.min_source_spacing,
        config.river_step_size,
        config.max_river_steps,
        config.river_border_distance,
    };
}

Point World::plateCenter(PlateIndex index) const {
    return {
        static_cast<double>(index.x) + 0.5 + hash11(index.x, index.y, config_.seed + 801) * config_.plate_shape,
        static_cast<double>(index.y) + 0.5 + hash11(index.x, index.y, config_.seed + 802) * config_.plate_shape,
    };
}

PlateType World::plateType(PlateIndex index) const {
    const double value = (hash11(index.x, index.y, config_.seed + 2001) + 1.0) * 0.5;
    return value > config_.continents_fraction ? PlateType::Oceanic : PlateType::Continental;
}

Point World::plateDrift(PlateIndex index) const {
    double x = hash11(index.x, index.y, config_.seed + 3101);
    double y = hash11(index.x, index.y, config_.seed + 3102);
    const double magnitude = std::hypot(x, y) + 1e-12;
    return {x / magnitude, y / magnitude};
}

PlateIndex World::plateOwner(double world_x, double world_y) const {
    const double border_scale = config_.plate_border_shape * config_.plate_scale / config_.plate_reference_scale;
    const double plate_x = world_x * config_.plate_scale
        + brownianNoise(world_x, world_y, config_.seed + 1, config_.plate_roughness, 0.5, 2.0, border_scale)
            * config_.plate_stretching;
    const double plate_y = world_y * config_.plate_scale
        + brownianNoise(world_x, world_y, config_.seed + 2, config_.plate_roughness, 0.5, 2.0, border_scale)
            * config_.plate_stretching;
    const auto ix = static_cast<std::int32_t>(std::floor(plate_x));
    const auto iy = static_cast<std::int32_t>(std::floor(plate_y));
    PlateIndex owner{ix, iy};
    double best = 1e18;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const PlateIndex candidate{ix + dx, iy + dy};
            const Point center = plateCenter(candidate);
            const double squared = (plate_x - center.x) * (plate_x - center.x)
                + (plate_y - center.y) * (plate_y - center.y);
            if (squared < best) {
                best = squared;
                owner = candidate;
            }
        }
    }
    return owner;
}

TerrainFields World::terrainFields(double world_x, double world_y) const {
    const double border_scale = config_.plate_border_shape * config_.plate_scale / config_.plate_reference_scale;
    const double plate_x = world_x * config_.plate_scale
        + brownianNoise(world_x, world_y, config_.seed + 1, config_.plate_roughness, 0.5, 2.0, border_scale)
            * config_.plate_stretching;
    const double plate_y = world_y * config_.plate_scale
        + brownianNoise(world_x, world_y, config_.seed + 2, config_.plate_roughness, 0.5, 2.0, border_scale)
            * config_.plate_stretching;
    const auto ix = static_cast<std::int32_t>(std::floor(plate_x));
    const auto iy = static_cast<std::int32_t>(std::floor(plate_y));

    std::array<Neighbor, 9> neighbors{};
    std::size_t count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            Neighbor item;
            item.index = {ix + dx, iy + dy};
            item.center = plateCenter(item.index);
            item.drift = plateDrift(item.index);
            item.type = plateType(item.index);
            item.distance = std::hypot(plate_x - item.center.x, plate_y - item.center.y);
            std::size_t insert = count;
            while (insert > 0 && item.distance < neighbors[insert - 1].distance) {
                neighbors[insert] = neighbors[insert - 1];
                --insert;
            }
            neighbors[insert] = item;
            ++count;
        }
    }

    TerrainFields fields;
    fields.crust = config_.ocean_height;
    fields.owner = neighbors[0].index;
    fields.owner_type = neighbors[0].type;
    fields.drift_vector = neighbors[0].drift;
    fields.drift_scalar = (plate_x - neighbors[0].center.x) * fields.drift_vector.x
        + (plate_y - neighbors[0].center.y) * fields.drift_vector.y;

    const double border_distance = plateDistance(
        neighbors, PlateType::Oceanic, PlateType::Continental);
    const double continental_distance = plateDistance(neighbors, PlateType::Continental, std::nullopt);
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
    fields.plateau = config_.plateau_amplitude * smoothStep(4.0 * plateauWeight(neighbors, config_))
        * std::max(0.0, config_.land_amplitude - fields.land)
        * std::max(0.0, config_.mountains_amplitude - fields.mountains);

    const double drift_start = 1.0 - 2.0 * config_.land_plate_coverage + config_.sea_level_fraction;
    const double drift_length = 2.0 * (config_.islands_plate_coverage - config_.land_plate_coverage) + 1e-8;
    const double islands_fade = std::max(
        0.0, 1.0 + config_.sea_level_fraction - (fields.drift_scalar - drift_start) / drift_length);
    fields.islands *= islands_fade * (1.0 - std::min(1.0, fields.plateau));
    return fields;
}

double World::firstHeight(double world_x, double world_y) const {
    const TerrainFields fields = terrainFields(world_x, world_y);
    return fields.crust + fields.land * config_.base_terrain_land_scale
        + (fields.mountains + fields.plateau + fields.islands) * config_.base_terrain_mountain_scale;
}

double World::baseHeight(double world_x, double world_y) const {
    const TerrainFields fields = terrainFields(world_x, world_y);
    const double island_noise = (brownianNoise(world_x, world_y, config_.seed, 1, 0.5, 2.0, 0.01) + 1.0) * 0.5;
    return fields.crust + fields.land + fields.mountains + fields.plateau + fields.islands * island_noise;
}

Polygon World::platePolygon(PlateIndex owner) const {
    const Point owner_center = plateCenter(owner);
    std::array<Point, 9> centers{};
    std::size_t at = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) centers[at++] = plateCenter({owner.x + dx, owner.y + dy});
    }
    centers[4] = owner_center;
    double min_x = owner_center.x;
    double max_x = owner_center.x;
    double min_y = owner_center.y;
    double max_y = owner_center.y;
    for (const Point& center : centers) {
        min_x = std::min(min_x, center.x);
        max_x = std::max(max_x, center.x);
        min_y = std::min(min_y, center.y);
        max_y = std::max(max_y, center.y);
    }
    const double padding = config_.plate_geometry_padding_cells;
    Polygon polygon{{
        {min_x - padding, min_y - padding},
        {max_x + padding, min_y - padding},
        {max_x + padding, max_y + padding},
        {min_x - padding, max_y + padding},
    }};
    for (const Point& center : centers) {
        if (distance(owner_center, center) <= 1e-8) continue;
        const Point midpoint{(owner_center.x + center.x) * 0.5, (owner_center.y + center.y) * 0.5};
        polygon = polygon.clip(midpoint, {owner_center.x - center.x, owner_center.y - center.y});
    }
    return polygon;
}

PlateGrid World::buildPlateGrid(PlateIndex owner, const RiverOptions& raw_options) const {
    RiverOptions options = raw_options;
    options.resolution = std::max(2, options.resolution);
    PlateGrid grid;
    grid.owner = owner;
    grid.resolution = options.resolution;
    grid.polygon = platePolygon(owner);
    const std::size_t size = static_cast<std::size_t>(options.resolution * options.resolution);
    grid.world_points.resize(size);
    grid.plate_points.resize(size);
    grid.heights.assign(size, 0.0);
    grid.valid.assign(size, 0);
    grid.border_safe.assign(size, 0);
    grid.lake_safe.assign(size, 0);
    if (grid.polygon.points.size() < 3) return grid;

    double min_x = grid.polygon.points[0].x;
    double max_x = min_x;
    double min_y = grid.polygon.points[0].y;
    double max_y = min_y;
    for (const Point& point : grid.polygon.points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    const double border_margin = options.border_margin * config_.plate_scale;
    const double lake_margin = (options.border_margin + config_.river_lake_radius
        + config_.river_distance_width) * config_.plate_scale;
    for (int row = 0; row < options.resolution; ++row) {
        const double fy = static_cast<double>(row) / static_cast<double>(options.resolution - 1);
        for (int col = 0; col < options.resolution; ++col) {
            const double fx = static_cast<double>(col) / static_cast<double>(options.resolution - 1);
            const Point plate_point{min_x + fx * (max_x - min_x), min_y + fy * (max_y - min_y)};
            const Point world_point{plate_point.x / config_.plate_scale, plate_point.y / config_.plate_scale};
            const std::size_t index = grid.index(row, col);
            grid.plate_points[index] = plate_point;
            grid.world_points[index] = world_point;
            if (!grid.polygon.contains(plate_point)) continue;
            grid.valid[index] = 1;
            const double border_distance = grid.polygon.borderDistance(plate_point);
            grid.border_safe[index] = border_distance >= border_margin;
            grid.lake_safe[index] = border_distance >= lake_margin;
            grid.heights[index] = firstHeight(world_point.x, world_point.y);
        }
    }
    return grid;
}

std::vector<PlateIndex> World::activePlateIndices(double world_x, double world_y, int radius) const {
    const PlateIndex owner = plateOwner(world_x, world_y);
    radius = std::max(0, radius);
    std::vector<PlateIndex> result;
    result.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) result.push_back({owner.x + dx, owner.y + dy});
    }
    return result;
}

} // namespace flat_infinite

