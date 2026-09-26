#include "finite_world/world.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
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

} // namespace

int main() {
    using namespace finite_world;
    World world;
    const Config& config = world.config();

    check(world.plateCount() == config.plate_count, "plate count");
    const World again;
    for (int i = 0; i < world.plateCount(); ++i) {
        const Vec3 a = world.plateCenter(i);
        const Vec3 b = again.plateCenter(i);
        check(a.x == b.x && a.y == b.y && a.z == b.z, "plate seeds are deterministic");
    }

    // The border guarantee: the warp moves a point by at most sqrt(3) * amplitude.
    check(std::sqrt(3.0) * config.warp_amplitude < config.river_border_margin,
        "border margin exceeds the maximum warp displacement");

    for (int plate = 0; plate < world.plateCount(); plate += 5) {
        const PlateRiverCache& cache = world.ensurePlate(plate);

        std::size_t valid = 0;
        for (std::size_t i = 0; i < cache.grid.valid.size(); ++i) {
            if (!cache.grid.valid[i]) continue;
            ++valid;
            check(world.geometricOwner(cache.grid.world_points[i]) == plate, "grid samples lie in the plate domain");
        }
        check(valid > 100, "plate grid covers the plate");

        std::vector<double> node_height(cache.nodes.size(), std::nan(""));
        for (const RiverSegment& segment : cache.segments) {
            check(segment.from_height > segment.to_height, "stored segment descends");
            for (auto [node, h] : {std::pair{segment.from, segment.from_height}, std::pair{segment.to, segment.to_height}}) {
                if (std::isnan(node_height[node])) node_height[node] = h;
                check(std::abs(node_height[node] - h) < 1e-12, "segments agree on shared node heights");
            }

            // Along the channel, the final terrain belongs to this plate and never rises.
            const Vec2 a = cache.nodes[segment.from].chart_point;
            const Vec2 b = cache.nodes[segment.to].chart_point;
            double previous = 0.0;
            for (int k = 0; k <= 8; ++k) {
                const double t = k / 8.0;
                const Vec3 x = world.chartToSurface(cache.chart, {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
                check(world.plateOwner(x) == plate, "river channel stays within its own plate");
                const double h = world.cachedHeight(x);
                if (k > 0) check(h <= previous + 1e-9, "final terrain never rises along a channel");
                previous = h;
            }
        }
    }

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All finite-world tests passed\n";
    return EXIT_SUCCESS;
}
