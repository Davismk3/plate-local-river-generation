#pragma once

#include "flat_infinite/world.hpp"

namespace flat_infinite {

// Opens the native interactive terrain viewer. Returns after the window closes.
int runInteractiveVisualizer(
    double center_x,
    double center_y,
    double plate_cells,
    int resolution,
    int active_radius,
    bool include_rivers,
    RiverOptions options);

} // namespace flat_infinite
