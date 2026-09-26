#pragma once

#include <cstdint>

namespace finite_world {

enum class Surface { Cube, Sphere };

// Lengths marked "plate cells" are in plate space, where neighboring plate
// seeds are about one unit apart. World space has planet radius `radius`.
struct Config {
    Surface surface = Surface::Sphere;
    std::int32_t seed = 1;

    // Plates: golden-angle seeds on the surface, jittered (plate cells).
    int plate_count = 40;
    double radius = 1.0;
    double plate_jitter = 0.15;

    // Border warp w(x) = warp_amplitude * (n1, n2, n3), so |w| <= sqrt(3) * warp_amplitude.
    double warp_amplitude = 0.05;
    double warp_frequency = 1.5;
    int warp_octaves = 5;

    // Pointwise terrain fields (same form as the flat infinite world).
    double continents_fraction = 0.7;
    double sea_level_fraction = 0.35;
    double ocean_height = 0.0;
    double continent_height = 0.2;
    double ocean_blend_width = 0.75;
    double ocean_blend_exponent = 3.0;

    double land_amplitude = 1.0;
    double land_width = 2.0;
    double land_interior_skew = 1.0;
    double land_border_skew = 4.0;
    double land_plate_coverage = 0.6;

    double islands_amplitude = 0.35;
    double islands_width = 0.1;
    double islands_interior_skew = 6.0;
    double islands_border_skew = 2.0;
    double islands_plate_coverage = 0.65;
    double island_noise_frequency = 12.0;

    double mountains_amplitude = 1.0;
    double mountains_width = 0.3;
    double mountains_interior_skew = 2.0;
    double mountains_border_skew = 2.0;
    double mountains_plate_coverage = 0.5;

    double plateau_amplitude = 1.0;
    double plateau_bridge_width = 0.15;
    double plateau_closing_fade_width = 0.25;

    // River planning height attenuation (a_land, a_relief in the paper).
    double base_terrain_land_scale = 1.0;
    double base_terrain_mountain_scale = 0.35;

    // Rivers.
    int river_grid_resolution = 100;
    int river_count = 30;
    double source_min_height = 0.5;
    int min_source_spacing = 5;
    int river_step_size = 2;
    int max_river_steps = 160;
    // Plate cells. Must exceed sqrt(3) * warp_amplitude so every river channel
    // is owned by its own plate after warping.
    double river_border_margin = 0.1;
    double river_blend_width = 0.03;
    double river_lake_radius = 0.04;
    double river_lake_surface_margin = 0.002;
    double river_lake_fade_distance = 0.03;
    double river_height_blend_distance = 0.03;
    double river_height_slope_drop = 0.005;
    double river_blend_exponent = 1.5;
    double river_min_drop = 1e-6;
    double river_path_distance_weight = 1e-5;
};

} // namespace finite_world
