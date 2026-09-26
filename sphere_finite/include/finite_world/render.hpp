#pragma once

#include "finite_world/world.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace finite_world {

// Planet surface mesh: six cube faces, each subdivided into a grid, projected
// onto the world surface. Heights include rivers only for cached plates.
struct Mesh {
    int resolution = 0;                // vertices per face side
    std::vector<Vec3> surface_points;  // on the undisplaced surface
    std::vector<int> owners;           // pointwise plate owner of each vertex
    std::vector<double> heights;

    std::size_t index(int face, int row, int col) const {
        return (static_cast<std::size_t>(face) * resolution + row) * resolution + col;
    }
};

Mesh buildMesh(const World& world, int resolution);
// Recomputes the heights of vertices owned by `plate` from its river cache.
void applyRivers(const World& world, Mesh& mesh, int plate);

struct View {
    Mat3 orientation;          // world -> view rotation
    double zoom = 1.0;
    bool show_plates = false;
    bool show_rivers = true;
    int highlight_plate = -1;  // outlined in the plate view
};

// Rotation by the arrow keys, about the screen axes.
void rotateView(View& view, double yaw, double pitch);
// Surface point facing the camera and the plate that owns it.
Vec3 cameraSurfacePoint(const World& world, const View& view);
int nearestPlate(const World& world, const View& view);

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
};

Image render(const World& world, const Mesh& mesh, const View& view, int width, int height);
bool writePpm(const std::string& path, const Image& image);

} // namespace finite_world
