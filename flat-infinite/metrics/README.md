# Reference-configuration metrics

NOTE: AI was used to generate `export_figure.py`. This script is solely for visualization purposes, and plays no role in the PL-RGA itself. 

The exact JSON summary and per-plate CSV that Section 4.1 ("Reference
Configuration") and Table 1 of the paper report. Both are emitted by the
same deterministic exporter invocation that produced the paper's Figure 1
(`global_continent_rivers.png`) — nothing here is a separately curated or
re-run example.

- `global_continent_rivers_metrics.json` — aggregate terrain, network,
  packed-cache, and timing statistics across all 11 plate caches
  intersecting the figure's viewport, plus the clipped subset actually
  drawn in the figure.
- `global_continent_rivers_per_plate.csv` — the same network statistics
  broken out per individual plate cache.
- `export_figure.py` — the script that generates both files above (and the
  figure PNG itself). It imports `flat-infinite`'s `src/` package directly
  (`app.config`, `app.visualizer`, `cache.rivercache`, `helpers.ids`) and
  calls only its public API — nothing in `src/` was modified to support
  figure/metrics export. It runs the same terrain/river sampling and
  rendering path as the interactive app, just at export resolution with
  `--metrics-output`/`--plate-metrics-output` attached.

## Regenerating

```bash
PYTHONPATH=src python3 metrics/export_figure.py \
  metrics/global_continent_rivers.png \
  --width 2400 --height 1600 --plate-cells 2.0 --resolution 320 \
  --metrics-output metrics/global_continent_rivers_metrics.json \
  --plate-metrics-output metrics/global_continent_rivers_per_plate.csv
```

The run is deterministic in seed and network/terrain counts
(`config.SEED = 1` by default); only the reported wall-clock timings vary
with machine and compilation state, as noted in the paper.
