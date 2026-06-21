# Plate-Local River Generation Algorithm

This repository contains a flat, infinite-world implementation of a Plate-Local River Generation Algorithm (PL-RGA).

## Architecture

```text
flat-infinite/
├── ARCHITECTURE.md
├── README.md
├── requirements.txt
└── src/
    ├── app/
    │   ├── __init__.py
    │   ├── config.py               Global tuning constants.
    │   ├── main.py                 Application entry point.
    │   └── visualizer.py           Pygame terrain/river visualizer.
    ├── cache/
    │   ├── __init__.py             
    │   └── rivercache.py           Plate cache construction, packed cache conversion, active-view cache management, and sampling facade.
    ├── helpers/
    │   ├── __init__.py
    │   ├── geometry.py             Point, Segment, Polygon classes and geometry methods.
    │   ├── ids.py                  Tuple index constants.
    │   ├── mathutil.py             Small Numba math helpers.
    │   └── noise.py                Deterministic hash/value/fractal noise.
    ├── plateownership.py           Deterministic jittered plate-center function.
    ├── platewise/
    │   ├── __init__.py
    │   ├── platewisegeometry.py    Object polygon approximation of a plate domain.
    │   ├── platewisegrid.py        Low-resolution plate-local terrain/routing grid.
    │   ├── platewisenetwork.py     Plate-local river network generation.
    │   └── platewiseregions.py     Nearest-river segment regions for acceleration.
    └── pointwise/
        ├── __init__.py
        ├── pointwisefields.py      Numba pointwise continent/plate fields.
        ├── pointwiseheight.py      Base/final height functions.
        └── pointwiseplatefields.py River height/distance fields from packed cache, plus object-backed fallback helpers.
```

## Run

```bash
python3 -m pip install -r requirements.txt
PYTHONPATH=src python3 -m app.main
```

You can also run the entry point directly:

```bash
python3 src/app/main.py
```

## Notes

Pointwise functions are designed to stay Numba-compatible where performance matters. Platewise construction prioritizes readability and debuggability, then converts object-backed river data into packed arrays for fast terrain sampling.
