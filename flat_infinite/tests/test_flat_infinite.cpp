#include "flat_infinite/noise.hpp"
#include "flat_infinite/world.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void close(double actual, double expected, double tolerance, const std::string& message) {
    check(std::abs(actual - expected) <= tolerance,
        message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

} // namespace

int main() {
    using namespace flat_infinite;

    // Values captured from helpers.noise.hash11 in github/flat-infinite.
    close(hash11(0, 0, 1), -0.6419580660955785, 1e-14, "hash at origin matches Python");
    close(hash11(1, 0, 802), 0.8956302394604451, 1e-14, "positive-index hash matches Python");
    close(hash11(-1, 0, 802), -0.2825304834556442, 1e-14, "negative-index hash matches Python");
    close(hash11(123, -456, 1), 0.11681353445947801, 1e-14, "mixed-index hash matches Python");
    close(brownianNoise(0.0, 0.0, 2, 5, 0.5, 2.0, 0.0005),
        0.18621485157192044, 1e-13, "Brownian X warp matches Python");
    close(brownianNoise(0.0, 0.0, 3, 5, 0.5, 2.0, 0.0005),
        -0.2863122825835036, 1e-13, "Brownian Y warp matches Python");

    // The pointwise reference values below were captured with the original warp
    // and planning attenuation, so these tests pin those two parameters.
    Config reference;
    reference.plate_stretching = 0.2;
    reference.base_terrain_mountain_scale = 0.35;
    World world(reference);
    const Point center = world.plateCenter({0, 0});
    close(center.x, 0.8002481544236256, 1e-12, "plate center X matches Python");
    close(center.y, 0.7762089501800368, 1e-12, "plate center Y matches Python");
    const Point negative_center = world.plateCenter({0, -1});
    close(negative_center.x, 0.6633113572203142, 1e-12, "negative plate center X matches Python");
    close(negative_center.y, -0.7418794234167843, 1e-12, "negative plate center Y matches Python");
    check(center.x > 0.0 && center.x < 1.0 && center.y > 0.0 && center.y < 1.0,
        "jittered center stays in its bounded owner neighborhood");
    const Polygon polygon = world.platePolygon({0, 0});
    check(polygon.points.size() >= 3, "plate polygon is non-degenerate");
    check(polygon.area() > 0.1, "plate polygon has positive area");

    // Fixed pointwise outputs captured from pointwisefields.py/pointwiseheight.py.
    const TerrainFields origin = world.terrainFields(0.0, 0.0);
    check(origin.owner == PlateIndex{0, -1}, "origin owner matches Python");
    close(origin.crust, 0.2, 1e-12, "origin crust matches Python");
    close(origin.land, 0.24041681629105108, 1e-11, "origin land matches Python");
    close(origin.mountains, 0.2800537485324826, 1e-11, "origin mountains match Python");
    close(origin.drift_scalar, 0.3031415689710728, 1e-11, "origin drift matches Python");
    close(origin.drift_vector.x, 0.476935763083722, 1e-12, "origin drift vector X matches Python");
    close(origin.drift_vector.y, 0.8789381536179016, 1e-12, "origin drift vector Y matches Python");
    close(world.firstHeight(0.0, 0.0), 0.53843562827742, 1e-11, "first height matches Python");
    close(world.baseHeight(0.0, 0.0), 0.7204705648235337, 1e-11, "final base height matches Python");

    const TerrainFields negative = world.terrainFields(-12345.0, 23456.0);
    check(negative.owner == PlateIndex{-2, 3}, "negative-coordinate owner matches Python");
    close(negative.land, 0.49348790836931944, 1e-10, "negative-coordinate land matches Python");
    close(negative.mountains, 0.21266213024340241, 1e-10, "negative-coordinate mountains match Python");
    close(world.baseHeight(-12345.0, 23456.0), 0.9061500386127219, 1e-10,
        "negative-coordinate height matches Python");

    const PlateIndex far_owner = world.plateOwner(-12345678.25, 9876543.5);
    const PlateIndex far_owner_again = world.plateOwner(-12345678.25, 9876543.5);
    check(far_owner == far_owner_again, "far coordinates are deterministic");
    const double far_height = world.baseHeight(-12345678.25, 9876543.5);
    check(std::isfinite(far_height), "far coordinates produce finite terrain");

    RiverOptions quick = defaultRiverOptions(world.config());
    quick.resolution = 32;
    quick.river_count = 8;
    quick.max_steps = 50;
    const PlateRiverCache first = world.buildPlateRivers({0, 0}, quick);
    const PlateRiverCache second = world.buildPlateRivers({0, 0}, quick);
    // Regression values for the reference terrain with the default border margin
    // and the river-aware source mask.
    check(first.nodes.size() == 23, "river node count regression");
    check(first.segments.size() == 15, "river segment count regression");
    check(first.paths.size() == 8, "river path count regression");
    close(first.grid.polygon.area(), 1.2218160832020848, 1e-11, "plate polygon area matches Python");
    check(!first.segments.empty() && first.segments[0].from == 0 && first.segments[0].to == 1,
        "first routed edge regression");
    close(first.segments[0].from_height, 0.7409265000992458, 1e-10,
        "first river upstream height regression");
    close(first.segments[0].to_height, 0.3033718483963016, 1e-10,
        "first river downstream height regression");
    // Every segment touching a node must agree on that node's river height.
    std::vector<double> node_height(first.nodes.size(), std::numeric_limits<double>::quiet_NaN());
    auto agree = [&](std::size_t node, double height) {
        if (std::isnan(node_height[node])) node_height[node] = height;
        check(std::abs(node_height[node] - height) < 1e-12, "segments agree on shared node height");
    };
    for (const RiverSegment& segment : first.segments) {
        agree(segment.from, segment.from_height);
        agree(segment.to, segment.to_height);
    }
    check(first.nodes.size() == second.nodes.size(), "river node count is deterministic");
    check(first.segments.size() == second.segments.size(), "river segment count is deterministic");
    check(first.grid.heights == second.grid.heights, "plate grid is deterministic");
    for (const RiverSegment& segment : first.segments) {
        check(segment.from < first.nodes.size() && segment.to < first.nodes.size(), "river endpoints are valid");
        check(segment.from_height + 1e-5 >= segment.to_height, "river does not rise downstream");
        check(segment.strahler_order >= 1, "Strahler order is positive");
    }

    // With the default warp, the warp displacement stays below the border margin,
    // so every point on every river channel is owned by the river's own plate.
    {
        World defaults;
        const RiverOptions options = defaultRiverOptions(defaults.config());
        for (int px = -1; px <= 1; ++px) {
            for (int py = -1; py <= 1; ++py) {
                const PlateRiverCache& cache = defaults.ensurePlate({px, py}, options);
                for (const RiverSegment& segment : cache.segments) {
                    const Point a = cache.nodes[segment.from].world_point;
                    const Point b = cache.nodes[segment.to].world_point;
                    for (int k = 0; k <= 8; ++k) {
                        const double t = k / 8.0;
                        check(defaults.plateOwner(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t) == cache.owner,
                            "river channel stays within its own plate");
                    }
                }
            }
        }
    }

    World cached_world;
    cached_world.ensureActive(0.0, 0.0, 1, quick);
    check(cached_world.cachedPlateCount() == 9, "3x3 active cache works");
    const TerrainGrid terrain = cached_world.terrainGrid(0.0, 0.0, 0.2, 12, true, 1, quick);
    check(terrain.heights.size() == 144, "terrain grid has requested dimensions");
    for (double value : terrain.heights) check(std::isfinite(value), "terrain sample is finite");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All flat-infinite tests passed\n";
    return EXIT_SUCCESS;
}
