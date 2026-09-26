#pragma once

#include <cstdint>

namespace flat_infinite {

struct Config {
    std::int32_t seed = 1;

    double plate_reference_scale = 0.00015;
    double plate_scale = 0.00015;
    // Border warp amplitude in plate cells. The warp moves a point by at most
    // plate_stretching * sqrt(2) cells (about 94 world units at 0.01), which is
    // below river_border_distance, so river channels never leave their plate.
    double plate_stretching = 0.01;
    int plate_roughness = 5;
    double plate_border_shape = 0.0005;
    double plate_shape = 1.0 / 3.0;
    double plate_geometry_padding_cells = 2.0;

    double continents_fraction = 0.85;
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

    double mountains_amplitude = 1.0;
    double mountains_width = 0.3;
    double mountains_interior_skew = 2.0;
    double mountains_border_skew = 2.0;
    double mountains_plate_coverage = 0.5;

    double plateau_amplitude = 1.0;
    double plateau_bridge_width = 0.15;
    double plateau_closing_fade_width = 0.25;

    double base_terrain_land_scale = 1.0;
    // Attenuation of mountains, plateaus, and islands in the river planning
    // height. Values below 1.0 plan rivers below the final terrain, carving
    // valleys; 1.0 disables it.
    double base_terrain_mountain_scale = 0.35;

    int river_grid_resolution = 100;
    int river_count = 50;
    double source_min_height = 0.5;
    int min_source_spacing = 5;
    int river_step_size = 2;
    int max_river_steps = 160;
    // World units kept between rivers and the geometric plate border.
    double river_border_distance = 100.0;
    double river_distance_width = 100.0;
    double river_lake_radius = 150.0;
    double river_height_slope_drop = 0.005;
    double river_blend_exponent = 1.5;
    double river_min_drop = 1e-6;
    double river_path_distance_weight = 1e-5;
    double river_distance_field_power = 1.0;
    double river_lake_surface_margin_distance = 7.0;
    double river_lake_node_height_fade_distance = 100.0;
    double river_height_blend_distance = 100.0;
};

} // namespace flat_infinite

