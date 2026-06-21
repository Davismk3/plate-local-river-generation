# Plate-Local River Generation Algorithm

This repository contains a flat, infinite-world implementation of a Plate-Local River Generation Algorithm (PL-RGA).

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
