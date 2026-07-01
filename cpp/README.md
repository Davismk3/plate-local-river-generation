# Plate-Local River Generation Algorithm

This repository contains a flat, infinite-world implementation of a Plate-Local River Generation Algorithm (PL-RGA). The method builds plate-local river networks from a low-resolution terrain grid, converts those networks into packed data, and uses that packed cache for fast pointwise terrain sampling.

The approach is not specific to rivers. The same plate-local structure can be adapted for road networks, volcano placement, settlement placement, or other constrained topology features.

## Run

```bash
python3 -m pip install -r requirements.txt
python3 setup.py build_ext --inplace
PYTHONPATH=src python3 -m app.main
```

You can also run the entry point directly:

```bash
python3 src/app/main.py
```

## Notes

Pointwise functions are designed to stay Numba-compatible where performance matters. Platewise construction prioritizes readability and debuggability, then converts object-backed river data into packed arrays for fast terrain sampling.

`platewisegrid.py` uses an optional pybind11 extension for grid/mask construction. If the extension has not been built, the pure-Python fallback path is used.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the current module layout and data flow.
