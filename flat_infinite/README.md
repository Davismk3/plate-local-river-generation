# Plate-Local River Generation: Flat Infinite World (C++)

This is a dependency-free C++17 toy example of the Plate-Local River Generation
Algorithm (PL-RGA) on a flat, infinite world. It is the implementation behind
the results and Figure 1 of the accompanying paper (see [`../davis2026_plate_local_river_generation.pdf`](../davis2026_plate_local_river_generation.pdf)).

It implements:

- deterministic jittered plate centers indexed by unbounded integer `(x, y)` keys;
- coordinate-derived value/fractal noise with deterministic 64-bit hashing;
- pointwise continent, island, mountain, plateau, and drift fields;
- clipped plate-local Voronoi polygons and low-resolution routing grids;
- downhill river construction with confluences, lakes, border protection, slope
  adjustment, and Strahler stream order;
- lazy per-plate river caching and river-aware point/terrain-grid sampling.

The library and command-line tool need no third-party dependencies. The
interactive viewer uses Cocoa on macOS; on other platforms the command-line tool
still exports heightmaps. The paper exporter additionally needs zlib.

## Build and test

Run these from this directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Interactive viewer

Running the executable without export flags opens the native terrain window
(macOS):

```bash
build/flat_infinite_cli
```

Use the arrow keys to move across the infinite plane, left-drag to rotate the
view, and Escape to close the window. Terrain and plate-local rivers regenerate
as the view moves. Pass `--no-rivers` for faster base-terrain-only viewing.

## Generate a heightmap

```bash
build/flat_infinite_cli \
  --output terrain.pgm \
  --csv terrain.csv \
  --center-x 0 --center-y 0 \
  --plate-cells 1 --resolution 256
```

The PGM is a portable 16-bit grayscale heightmap. The optional CSV contains
the unnormalized world coordinates and floating-point heights. Use
`--no-rivers` for the pointwise base terrain only; see `--help` for all flags.

## Reproduce the paper's figure and metrics

`build/flat_infinite_export` renders Figure 1 and writes the metrics reported
in the paper's results section. See [`metrics/README.md`](metrics/README.md)
for the exact command and for what each measurement means.

## Library API

```cpp
#include <flat_infinite/world.hpp>

flat_infinite::World world;

// Works at arbitrary positive or negative world coordinates.
auto owner = world.plateOwner(-250000.0, 810000.0);
double base = world.baseHeight(-250000.0, 810000.0);

// River data is generated lazily and cached by integer plate index.
double with_rivers = world.height(-250000.0, 810000.0, true);

// Preload a 3x3 neighborhood for dense view sampling.
auto options = flat_infinite::defaultRiverOptions(world.config());
world.ensureActive(0.0, 0.0, 1, options);
auto grid = world.terrainGrid(0.0, 0.0, 1.0, 256, true, 1, options);
```

The public API is in `include/flat_infinite/`, and all parameters with their
defaults are in `include/flat_infinite/config.hpp`.

## Layout

- `include/flat_infinite/`: public headers
- `src/`: library, command-line tool, and viewer
- `tools/export_figure.cpp`: paper figure and metrics exporter
- `tests/`: regression tests
- `metrics/`: committed figure, JSON summary, and per-plate CSV
