#include "flat_infinite/world.hpp"
#include "flat_infinite/visualizer.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void usage(const char* executable) {
    std::cout
        << "Flat Infinite World Generator (C++17)\n\n"
        << "Usage: " << executable << " [options]\n"
        << "With no export flags, the native interactive viewer opens.\n\n"
        << "  --output FILE       Export a PGM heightmap instead of opening a window\n"
        << "  --csv FILE          Also write floating-point samples as CSV\n"
        << "  --center-x VALUE    View center X in world units (default: plate 1,0 center)\n"
        << "  --center-y VALUE    View center Y in world units (default: plate 1,0 center)\n"
        << "  --plate-cells N     View width in plate cells (default: 1)\n"
        << "  --resolution N      Square output resolution (default: 100)\n"
        << "  --river-grid N      Plate-local river resolution (default: 100)\n"
        << "  --river-count N     Sources attempted per plate (default: 50)\n"
        << "  --active-radius N   Cached plate radius (default: 1)\n"
        << "  --no-rivers         Render base terrain only\n"
        << "  --headless          Export to terrain.pgm without opening a window\n"
        << "  --interactive       Force the interactive window\n"
        << "  --help              Show this message\n";
}

double parseDouble(const char* value, const char* option) {
    char* end = nullptr;
    const double result = std::strtod(value, &end);
    if (end == value || *end != '\0') throw std::runtime_error(std::string("invalid value for ") + option);
    return result;
}

int parseInt(const char* value, const char* option) {
    char* end = nullptr;
    const long result = std::strtol(value, &end, 10);
    if (end == value || *end != '\0') throw std::runtime_error(std::string("invalid value for ") + option);
    return static_cast<int>(result);
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string output = "terrain.pgm";
        std::string csv_output;
        double center_x = 0.0;
        double center_y = 0.0;
        double plate_cells = 1.0;
        int resolution = 100;
        int active_radius = 1;
        bool include_rivers = true;
        bool export_requested = false;
        bool force_interactive = false;
        flat_infinite::Config config;
        flat_infinite::RiverOptions river_options = flat_infinite::defaultRiverOptions(config);
        const flat_infinite::Point initial_center = flat_infinite::World(config).plateCenter({1, 0});
        center_x = initial_center.x / config.plate_scale;
        center_y = initial_center.y / config.plate_scale;

        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&](const char* option) -> const char* {
                if (++i >= argc) throw std::runtime_error(std::string("missing value for ") + option);
                return argv[i];
            };
            if (argument == "--help") { usage(argv[0]); return 0; }
            if (argument == "--output") { output = value("--output"); export_requested = true; }
            else if (argument == "--csv") { csv_output = value("--csv"); export_requested = true; }
            else if (argument == "--center-x") center_x = parseDouble(value("--center-x"), "--center-x");
            else if (argument == "--center-y") center_y = parseDouble(value("--center-y"), "--center-y");
            else if (argument == "--plate-cells") plate_cells = parseDouble(value("--plate-cells"), "--plate-cells");
            else if (argument == "--resolution") resolution = parseInt(value("--resolution"), "--resolution");
            else if (argument == "--river-grid") river_options.resolution = parseInt(value("--river-grid"), "--river-grid");
            else if (argument == "--river-count") river_options.river_count = parseInt(value("--river-count"), "--river-count");
            else if (argument == "--active-radius") active_radius = parseInt(value("--active-radius"), "--active-radius");
            else if (argument == "--no-rivers") include_rivers = false;
            else if (argument == "--headless") export_requested = true;
            else if (argument == "--interactive") force_interactive = true;
            else throw std::runtime_error("unknown argument: " + argument);
        }
        if (resolution < 2) throw std::runtime_error("resolution must be at least 2");
        if (plate_cells <= 0.0) throw std::runtime_error("plate-cells must be positive");

        if (!export_requested || force_interactive) {
            return flat_infinite::runInteractiveVisualizer(
                center_x, center_y, plate_cells, resolution, active_radius, include_rivers, river_options);
        }

        flat_infinite::World world(config);
        const flat_infinite::TerrainGrid grid = world.terrainGrid(
            center_x, center_y, plate_cells, resolution, include_rivers, active_radius, river_options);
        const auto [minimum, maximum] = std::minmax_element(grid.heights.begin(), grid.heights.end());
        const double span = *maximum - *minimum;

        std::ofstream pgm(output, std::ios::binary);
        if (!pgm) throw std::runtime_error("could not open output file: " + output);
        pgm << "P5\n" << resolution << ' ' << resolution << "\n65535\n";
        for (double height : grid.heights) {
            const double normalized = span > 1e-15 ? (height - *minimum) / span : 0.0;
            const auto sample = static_cast<unsigned>(std::clamp(normalized, 0.0, 1.0) * 65535.0 + 0.5);
            pgm.put(static_cast<char>((sample >> 8U) & 0xFFU));
            pgm.put(static_cast<char>(sample & 0xFFU));
        }

        if (!csv_output.empty()) {
            std::ofstream csv(csv_output);
            if (!csv) throw std::runtime_error("could not open CSV file: " + csv_output);
            csv << std::setprecision(17) << "x,y,height\n";
            for (int row = 0; row < resolution; ++row) {
                const double y = grid.min_y + (grid.max_y - grid.min_y) * row / (resolution - 1.0);
                for (int col = 0; col < resolution; ++col) {
                    const double x = grid.min_x + (grid.max_x - grid.min_x) * col / (resolution - 1.0);
                    csv << x << ',' << y << ',' << grid.heights[static_cast<std::size_t>(row * resolution + col)] << '\n';
                }
            }
        }

        std::cout << "Wrote " << output << " (" << resolution << 'x' << resolution
                  << ", height range " << *minimum << ".." << *maximum
                  << ", cached plates " << world.cachedPlateCount() << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}
