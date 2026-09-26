#pragma once

#include "finite_world/config.hpp"
#include "finite_world/math.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace finite_world {

enum class PlateType : std::uint8_t { Oceanic = 0, Continental = 1 };

struct TerrainFields {
    double crust = 0.0;
    double islands = 0.0;
    double land = 0.0;
    double mountains = 0.0;
    double plateau = 0.0;
    double drift_scalar = 0.0;
    int owner = 0;
    PlateType owner_type = PlateType::Oceanic;
};

enum class RiverNodeType : std::uint8_t { Channel = 0, Source = 1, SeaOutlet = 2, LocalMinimum = 3 };

struct RiverNode {
    Vec3 world_point;  // on the planet surface
    Vec2 chart_point;  // in the plate's tangent-plane chart (world units)
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

// Tangent plane at the plate's seed direction. A chart point (a, b) maps to the
// surface by radial projection of center * plane_distance + a * e1 + b * e2, so
// straight chart segments map to great-circle arcs on a sphere and to planar
// cuts on a cube.
struct PlateChart {
    Vec3 center;
    Vec3 e1;
    Vec3 e2;
    double plane_distance = 1.0;
};

struct PlateGrid {
    int owner = 0;
    int resolution = 0;
    std::vector<Vec2> chart_points;
    std::vector<Vec3> world_points;
    std::vector<double> heights;
    std::vector<std::uint8_t> valid;
    std::vector<std::uint8_t> border_safe;
    std::vector<std::uint8_t> lake_safe;

    std::size_t index(int row, int col) const { return static_cast<std::size_t>(row * resolution + col); }
};

struct PlateRiverCache {
    int owner = 0;
    PlateChart chart;
    PlateGrid grid;
    std::vector<RiverNode> nodes;
    std::vector<RiverSegment> segments;
    std::vector<std::vector<std::size_t>> paths;
};

struct RiverFields {
    double height = 0.0;
    double normalized_distance = 1.0;
};

class World {
public:
    explicit World(Config config = {});

    const Config& config() const { return config_; }
    int plateCount() const { return static_cast<int>(centers_.size()); }
    double plateScale() const { return plate_scale_; }                // plate cells per world unit
    const Vec3& plateCenter(int plate) const { return centers_[plate]; }  // plate space
    PlateType plateType(int plate) const { return types_[plate]; }

    // Radial projection of a direction onto the planet surface.
    Vec3 surfacePoint(Vec3 direction) const;

    // Pointwise representation.
    Vec3 warpedPlatePoint(Vec3 x) const;
    int plateOwner(Vec3 x) const;       // nearest seed to the warped point
    int geometricOwner(Vec3 x) const;   // nearest seed to the unwarped point
    // Signed distance (plate cells) from x to the border of the plate's geometric
    // domain: the smallest distance to any bisector plane, positive inside.
    double borderDistance(int plate, Vec3 x) const;
    TerrainFields terrainFields(Vec3 x) const;
    double firstHeight(Vec3 x) const;   // river planning height H_plan
    double baseHeight(Vec3 x) const;    // pre-river height H_pre

    // Geometric representation and plate-local rivers.
    PlateChart plateChart(int plate) const;
    Vec3 chartToSurface(const PlateChart& chart, Vec2 point) const;
    bool surfaceToChart(const PlateChart& chart, Vec3 x, Vec2& point) const;
    PlateGrid buildPlateGrid(int plate) const;
    PlateRiverCache buildPlateRivers(int plate) const;

    // Lazy per-plate cache.
    const PlateRiverCache& ensurePlate(int plate);
    const PlateRiverCache* cachedPlate(int plate) const;
    std::size_t cachedPlateCount() const { return cache_.size(); }

    // River fields from the owning plate's cache only (no cache is built).
    RiverFields riverFields(Vec3 x) const;
    // Final height. Rivers come from the owning plate's cache when it exists;
    // with build_missing, a missing cache is built first.
    double height(Vec3 x, bool include_rivers = true, bool build_missing = false);
    double cachedHeight(Vec3 x) const;

private:
    Config config_;
    double plate_scale_ = 1.0;
    std::vector<Vec3> centers_;
    std::vector<Vec3> drift_axes_;
    std::vector<PlateType> types_;
    std::map<int, PlateRiverCache> cache_;
};

} // namespace finite_world
