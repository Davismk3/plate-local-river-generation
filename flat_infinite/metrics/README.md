# Reference-configuration metrics

The figure, JSON summary, and per-plate CSV behind the paper's results
section (Figure 1, Section 4.1, and Table 1). All three come from one
deterministic invocation of `flat_infinite_export`
([`tools/export_figure.cpp`](../tools/export_figure.cpp)). That program only
uses the public `flat_infinite::World` API; nothing in `src/` or `include/`
was changed to support it.

- `global_continent_rivers.png`: Figure 1, a 2400x1600 render of the
  two-plate-cell view centered on plate (1, 0). It is rendered at 3x
  supersampling with a software rasterizer, and river channels are
  depth-tested against the terrain.
- `global_continent_rivers_metrics.json`: terrain, network, final-terrain
  channel check, cache memory, and timing statistics across all plates that
  own at least one terrain sample in the view. It also includes the subset of
  the network drawn in the figure.
- `global_continent_rivers_per_plate.csv`: the same network statistics for
  each plate cache.

## Regenerating

```bash
# from the directory containing CMakeLists.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/flat_infinite_export metrics/global_continent_rivers.png \
  --width 2400 --height 1600 --plate-cells 2.0 --resolution 320 --timing-repeats 5 \
  --metrics-output metrics/global_continent_rivers_metrics.json \
  --plate-metrics-output metrics/global_continent_rivers_per_plate.csv
```

The exporter needs zlib, which it uses for PNG output.

## What is measured

- **Timings** are repeated on fresh `World` instances, so every repeat
  builds its caches cold. The reported value is the median across repeats.
  The exporter checks that heights and network sizes are bit-identical on
  every repeat, and exits with status 2 if they are not.
- **Terrain sampling** is timed twice over the same 320x320 grid: once with
  river queries (`height(x, y, true)`) and once with pointwise terrain only.
  The difference is the cost of the per-sample river query, which scans
  every segment of the owning plate.
- **Stored-segment downhill check**: every cached segment must satisfy
  `from_height - to_height >= -1e-9`, using its adjusted river heights.
- **Final-terrain channel check**: the final terrain `height(x, y, true)` is
  sampled at 16 evenly spaced points along every segment, and any downstream
  rise above 1e-9 is counted. Violations are split into two groups:
  - segments that have a sample whose warped plate owner is a neighboring
    plate (so that sample is composed with the neighbor's rivers), and
  - segments that collinearly overlap another segment of the same plate.
- **Cache memory** is the size of the cached nodes, segments, and paths
  (`network_bytes`), counted with the C++ `sizeof` of each record. The
  planning grid kept in each cache is reported separately (`grid_bytes`).
  Allocator and container overhead are not included.

Network statistics and the channel check are deterministic for a given seed
and configuration. Wall-clock timings vary with machine and load.
