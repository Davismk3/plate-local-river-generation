#include "finite_world/viewer.hpp"

#include <iostream>

namespace finite_world {

int runViewer(const ViewerOptions& options) {
    (void)options;
    std::cerr << "The interactive viewer is only available on macOS. Use --snapshot FILE.ppm instead.\n";
    return 1;
}

} // namespace finite_world
