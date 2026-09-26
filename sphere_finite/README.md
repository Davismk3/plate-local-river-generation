# Plate-Local River Generation: Finite Sphere Planet (C++)

A dependency-free C++17 toy example of the Plate-Local River Generation
Algorithm (PL-RGA) on a finite, spherical planet. It applies the paper's
finite-world variant (see [`../davis2026_plate_local_river_generation.pdf`](../davis2026_plate_local_river_generation.pdf)) and shares its river construction
with the flat infinite example in `../flat_infinite/`.

The interactive viewer (macOS) shows the planet with its tectonic plates. As
you rotate it, rivers are generated and cached lazily for the plate facing the
camera only. A plate's rivers are built the first time it faces the camera, and
revisiting it reuses the cache.

## Build and run

Run these from this directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
build/sphere_finite
```

| Key | Action |
|---|---|
| Arrow keys | Rotate the planet |
| `P` | Toggle plate colouring |
| `R` | Toggle rivers |
| `+` / `-` | Zoom |
| Esc | Close |

The border darkening shows plate borders in both modes. In the plate view,
the plate facing the camera is brightened.

Options: `--seed N`, `--plates N` (default 40), `--mesh N` (planet mesh
resolution), `--yaw DEG`, `--pitch DEG`, `--show-plates`. On any platform,
`--snapshot FILE.ppm` renders a single frame to an image instead of opening a
window.

## How the paper's finite-world techniques are applied

- **Plate seeds:** a golden-angle sequence of directions on the sphere,
  jittered (`World::World`).
- **Pointwise representation:** each point is owned by the nearest seed after
  a 3D noise warp. Land, island, mountain, plateau, and continental-shelf
  fields use the same formulas as the flat world. Each plate's drift axis is
  a_i = normalize(c_i x r_i), and its local drift is a_i x n(x), with the
  surface normal n(x) (`terrain.cpp`).
- **Geometric representation:** a plate's domain is the intersection of the
  bisector half-spaces with the sphere. It is sampled through a tangent-plane
  chart at the plate seed and projected radially onto the sphere. This is a
  gnomonic projection, so straight chart segments map to great-circle arcs.
- **Rivers:** the same tracing, height adjustment, proximity mask, lakes,
  Strahler orders, and height blend as the flat world (`rivers.cpp`).
- **Border guarantee:** the warp moves a point by at most sqrt(3) x
  `warp_amplitude` plate cells (0.087), which is less than
  `river_border_margin` (0.1). Every river channel is therefore owned by its
  own plate, and the final terrain never rises along a channel. The tests check
  both.

All parameters are in `include/finite_world/config.hpp`. The code is identical
to `../cube_finite/` except for the surface selected in `config.hpp`.

## Layout

- `include/finite_world/`: public headers
- `src/`: world, rivers, renderer, command-line tool, and macOS viewer
- `tests/`: seed determinism, plate-domain, and downhill-guarantee checks
