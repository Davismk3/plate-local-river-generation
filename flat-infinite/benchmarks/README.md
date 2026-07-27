# Evaluation benchmarks

Scripts that produced the numbers, tables, and figures in Section 4
("Evaluation and Experimental Results") of the paper. Each imports
`flat-infinite`'s `src/` package directly and calls only its public API
(`cache.rivercache.ensurePlateRiverCache`, etc.) — nothing in `src/` was
modified to support these measurements.

Run any script in place (paths are resolved relative to this directory):

```
python3 script1_pool_and_resolution.py   # Sections 4.2 (scaling) and 4.3 (hydrology)
python3 script2_border_margin.py         # Section 4.4, border-margin vs. drainage density
python3 script3_boundary_warp.py         # Section 4.4, geometric/pointwise boundary mismatch
python3 script4_cache_eviction.py        # Section 4.5, cache eviction simulation (run after script1)
python3 make_figures.py                  # regenerates figures/*.pdf from results/*.json
```

Notes:

- `script1_pool_and_resolution.py` builds the native `_platewisegrid_native`
  extension's Python bindings on import; build it first for your
  interpreter if needed (`python3 setup.py build_ext --inplace` from the
  repo root) so timings reflect the compiled backend rather than the pure
  Python fallback.
- `config.SEED` is baked into several `@njit`-compiled dispatchers at
  first invocation and does not update on plain reassignment, so these
  scripts substitute statistical replication across distinct, widely
  separated plate-lattice identifiers under one fixed seed instead of
  re-seeding (see `OwnerIdGenerator` in `common.py`). `script3` sweeps
  `config.PLT_STRETCHING`/`PLT_BDR_SHAPE` instead, and forces
  `pointwisefields.plateOwnerIndex.recompile()` after each change to
  correctly pick up the new values.
- `script4_cache_eviction.py` is a simulation grounded in the per-plate
  build latencies and byte sizes measured by `script1`, replayed against
  synthetic camera paths — the shipped cache (`cache/rivercache.py`) has
  no eviction policy to instrument directly.
- `results/*.json` are the exact outputs the paper's numbers were drawn
  from; `figures/*.pdf` are copied into `latex/figures/` for the paper.
