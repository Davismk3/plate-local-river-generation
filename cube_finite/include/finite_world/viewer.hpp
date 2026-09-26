#pragma once

#include "finite_world/render.hpp"

namespace finite_world {

struct ViewerOptions {
    Config config;
    int mesh_resolution = 144;
    double yaw_degrees = 30.0;
    double pitch_degrees = 20.0;
    bool show_plates = false;
};

// Opens the native planet viewer (macOS). Returns after the window closes.
int runViewer(const ViewerOptions& options);

} // namespace finite_world
