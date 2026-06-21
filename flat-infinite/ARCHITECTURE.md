# Architecture

This project currently uses two complementary representations:

- Platewise generation is object-backed and easier to inspect/debug.
- Pointwise terrain sampling is Numba-friendly and uses packed arrays where performance matters.

The active implementation lives under `src/`.

## Directory Layout

```text
src/
|-- app/
|   |-- config.py                 Global tuning constants.
|   |-- main.py                   Application entry point.
|   `-- visualizer.py             Pygame terrain/river visualizer.
|
|-- cache/
|   |-- __init__.py               Public cache exports.
|   `-- rivercache.py             Plate cache construction, packed cache conversion,
|                                 active-view cache management, and sampling facade.
|
|-- helpers/
|   |-- geometry.py               Point, Segment, Polygon classes and geometry methods.
|   |-- ids.py                    Tuple index constants.
|   |-- mathutil.py               Small Numba math helpers.
|   `-- noise.py                  Deterministic hash/value/fractal noise.
|
|-- plateownership.py             Deterministic jittered plate-center function.
|
|-- platewise/
|   |-- platewisegeometry.py      Object polygon approximation of a plate domain.
|   |-- platewisegrid.py          Low-resolution plate-local terrain/routing grid.
|   |-- platewisenetwork.py       Plate-local river network generation.
|   `-- platewiseregions.py       Nearest-river segment regions for acceleration.
|
`-- pointwise/
    |-- pointwisefields.py        Numba pointwise continent/plate fields.
    |-- pointwiseheight.py        Base/final height functions.
    `-- pointwiseplatefields.py   River height/distance fields from packed cache,
                                  plus object-backed fallback helpers.
```

## Main Data Flow

```text
world point (x, y)
  |
  v
pointwise.pointwisefields
  - plateOwnerIndex(x, y)
  - pointwiseRepresentation(x, y)
  |
  v
pointwise.pointwiseheight
  - firstHeightField(x, y)
  - finalHeightField(x, y, packed_river_cache=None)
```

River-aware terrain adds a platewise precomputation stage:

```text
plate owner index
  |
  v
platewisegeometry.geometricRepresentation()
  |
  v
platewisegrid.plateGrid()
  |
  v
platewisenetwork.plateNetwork()
  |
  v
platewiseregions.plateRegions()
  |
  v
cache.rivercache.ensurePlateRiverCache()
  |
  v
cache.rivercache.packRiverCache()
  |
  v
pointwiseplatefields.riverFields()
  |
  v
pointwiseheight.finalHeightField()
```

## Representations

### Geometry Objects

`helpers.geometry` provides:

- `Point`
- `Segment`
- `Polygon`

These are used mainly by `platewise/` modules because they make the river network and plate polygons easier to reason about. River nodes are `Point` objects with metadata attached, and river paths are stored as `Segment` objects.

### Tuple Records

Some platewise outputs are tuples indexed with constants from `helpers.ids`:

- `plateGrid()` returns plate owner, resolution, polygon, world points, plate points, heights, and masks.
- `plateNetwork()` returns plate owner, resolution, river count, source pixels, nodes, segments, and paths.

This is a compromise between explicit objects and Numba-oriented array layouts.

### Dict Records

`plateRegions()` returns a dict:

```python
{
    "world_polygon": ...,
    "segments": ...,
    "segment_polygons": ...,
}
```

`rivercache.ensurePlateRiverCache()` returns a dict:

```python
{
    "plt_owner_idx": ...,
    "options": ...,
    "grid": ...,
    "network": ...,
    "regions": ...,
}
```

### Packed Cache

`cache.rivercache.packRiverCache()` converts object-backed plate caches into NumPy arrays. `packedCacheTuple()` exposes those arrays in the order expected by `pointwiseplatefields.riverFields()`.

The packed cache is the performance boundary: visualizer and terrain sampling should use this path for river-aware height sampling.

## Visualizer

`app.visualizer` is intentionally outside the generation modules.

It:

- computes the visible plate owner indices for the current view,
- loads plate river caches on a worker thread,
- debounces terrain reloads while arrow-key movement is active,
- converts loaded plate caches into packed arrays,
- samples final terrain height with the packed river cache,
- draws terrain, river segments, river nodes, and HUD text with Pygame.

Run with:

```bash
PYTHONPATH=src python3 -m app.main
```

or:

```bash
python3 src/app/main.py
```

## Performance Notes

The current important split is:

- Use object geometry for platewise construction and debugging.
- Use packed arrays for pointwise river-field sampling.

The visualizer should not call the object-backed `plateRiverFields()` path for every terrain pixel. That path is useful for clarity and fallback behavior, but it is too slow for interactive sampling. The fast path is:

```text
ensurePlateRiverCache()
packRiverCache()
packedCacheTuple()
pointwiseheight.finalHeightField(x, y, packed_cache)
```

Some platewise construction code still uses numeric helper loops in hot spots, even though the final stored outputs are geometry objects. This matches the old implementation's performance profile while keeping the newer object-oriented data model.
