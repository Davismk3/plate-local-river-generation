#pragma once

#include "flat_infinite/config.hpp"
#include "flat_infinite/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace flat_infinite {

struct PlateIndex {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const PlateIndex& other) const { return x == other.x && y == other.y; }
    bool operator<(const PlateIndex& other) const {
        return x < other.x || (x == other.x && y < other.y);
    }
};

enum class PlateType : std::uint8_t { Oceanic = 0, Continental = 1 };

struct TerrainFields {
    double crust = 0.0;
    double islands = 0.0;
    double land = 0.0;
    double mountains = 0.0;
    double plateau = 0.0;
    double drift_scalar = 0.0;
    Point drift_vector;
    PlateIndex owner;
    PlateType owner_type = PlateType::Oceanic;
};

enum class RiverNodeType : std::uint8_t {
    Channel = 0,
    Source = 1,
    SeaOutlet = 2,
    LocalMinimum = 3,
};

struct RiverNode {
    Point world_point;
    Point plate_point;
    int row = 0;
    int col = 0;
    double height = 0.0;
    double river_height = 0.0;
    RiverNodeType type = RiverNodeType::Channel;
};

struct RiverSegment {
    std::size_t from = 0;
    std::size_t to = 0;
    double from_height = 0.0;
    double to_height = 0.0;
    int strahler_order = 1;
};

struct RiverOptions {
    int resolution = 100;
    int river_count = 50;
    double source_min_height = 0.5;
    int min_source_spacing = 5;
    int step_size = 2;
    int max_steps = 160;
    double border_margin = 100.0;
};

struct PlateGrid {
    PlateIndex owner;
    int resolution = 0;
    Polygon polygon;
    std::vector<Point> world_points;
    std::vector<Point> plate_points;
    std::vector<double> heights;
    std::vector<std::uint8_t> valid;
    std::vector<std::uint8_t> border_safe;
    std::vector<std::uint8_t> lake_safe;

    std::size_t index(int row, int col) const {
        return static_cast<std::size_t>(row * resolution + col);
    }
};

struct PlateRiverCache {
    PlateIndex owner;
    RiverOptions options;
    PlateGrid grid;
    std::vector<RiverNode> nodes;
    std::vector<RiverSegment> segments;
    std::vector<std::vector<std::size_t>> paths;
};

struct RiverFields {
    double height = 0.0;
    double normalized_distance = 1.0;
};

struct TerrainGrid {
    int resolution = 0;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    std::vector<double> heights;
};

class World {
public:
    explicit World(Config config = {});

    const Config& config() const { return config_; }
    Point plateCenter(PlateIndex index) const;
    PlateIndex plateOwner(double world_x, double world_y) const;
    TerrainFields terrainFields(double world_x, double world_y) const;
    double firstHeight(double world_x, double world_y) const;
    double baseHeight(double world_x, double world_y) const;
    double height(double world_x, double world_y, bool include_rivers = true);

    Polygon platePolygon(PlateIndex owner) const;
    PlateGrid buildPlateGrid(PlateIndex owner, const RiverOptions& options) const;
    PlateRiverCache buildPlateRivers(PlateIndex owner, const RiverOptions& options) const;
    const PlateRiverCache& ensurePlate(PlateIndex owner, const RiverOptions& options = {});
    RiverFields riverFields(double world_x, double world_y) const;

    std::vector<PlateIndex> activePlateIndices(double world_x, double world_y, int radius = 1) const;
    void ensureActive(double world_x, double world_y, int radius = 1, const RiverOptions& options = {});
    TerrainGrid terrainGrid(
        double center_x,
        double center_y,
        double plate_cells,
        int resolution,
        bool include_rivers = true,
        int active_radius = 1,
        const RiverOptions& options = {});
    void clearCache();
    std::size_t cachedPlateCount() const { return cache_.size(); }
    const PlateRiverCache* cachedPlate(PlateIndex owner) const;

private:
    Config config_;
    std::map<PlateIndex, PlateRiverCache> cache_;

    PlateType plateType(PlateIndex index) const;
    Point plateDrift(PlateIndex index) const;
};

RiverOptions defaultRiverOptions(const Config& config);

} // namespace flat_infinite
