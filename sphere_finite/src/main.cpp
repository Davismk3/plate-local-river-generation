#include "finite_world/viewer.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;

void usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "With no --snapshot, the interactive viewer opens.\n\n"
        << "  --seed N          World seed (default 1)\n"
        << "  --plates N        Number of plates (default 40)\n"
        << "  --mesh N          Planet mesh vertices per cube-face side (default 144)\n"
        << "  --yaw DEG         Initial yaw (default 30)\n"
        << "  --pitch DEG       Initial pitch (default 20)\n"
        << "  --show-plates     Start with plate colouring on\n"
        << "  --snapshot FILE   Render one frame to a PPM image and exit\n"
        << "  --width N         Snapshot width (default 960)\n"
        << "  --height N        Snapshot height (default 820)\n"
        << "  --help            Show this message\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        finite_world::ViewerOptions options;
        std::string snapshot;
        int width = 960, height = 820;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc) throw std::runtime_error("missing value for " + argument);
                return argv[i];
            };
            if (argument == "--help") { usage(argv[0]); return 0; }
            else if (argument == "--seed") options.config.seed = std::stoi(value());
            else if (argument == "--plates") options.config.plate_count = std::max(4, std::stoi(value()));
            else if (argument == "--mesh") options.mesh_resolution = std::max(8, std::stoi(value()));
            else if (argument == "--yaw") options.yaw_degrees = std::stod(value());
            else if (argument == "--pitch") options.pitch_degrees = std::stod(value());
            else if (argument == "--show-plates") options.show_plates = true;
            else if (argument == "--snapshot") snapshot = value();
            else if (argument == "--width") width = std::max(64, std::stoi(value()));
            else if (argument == "--height") height = std::max(64, std::stoi(value()));
            else throw std::runtime_error("unknown argument: " + argument);
        }

        if (snapshot.empty()) return finite_world::runViewer(options);

        finite_world::World world(options.config);
        finite_world::Mesh mesh = finite_world::buildMesh(world, options.mesh_resolution);
        finite_world::View view;
        view.show_plates = options.show_plates;
        finite_world::rotateView(view, options.yaw_degrees * kPi / 180.0, options.pitch_degrees * kPi / 180.0);
        const int plate = finite_world::nearestPlate(world, view);
        view.highlight_plate = plate;
        const finite_world::PlateRiverCache& cache = world.ensurePlate(plate);
        finite_world::applyRivers(world, mesh, plate);
        if (!finite_world::writePpm(snapshot, finite_world::render(world, mesh, view, width, height))) {
            throw std::runtime_error("could not write " + snapshot);
        }
        std::cout << "Wrote " << snapshot << ": plate " << plate << " facing the camera, "
                  << cache.paths.size() << " river paths, " << cache.segments.size() << " segments\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage(argv[0]);
        return 1;
    }
}
