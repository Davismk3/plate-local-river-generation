#include "flat_infinite/visualizer.hpp"

#include <iostream>

namespace flat_infinite {

int runInteractiveVisualizer(double, double, double, int, int, bool, RiverOptions) {
    std::cerr << "The interactive viewer currently uses the native macOS Cocoa backend.\n";
    return 1;
}

} // namespace flat_infinite
