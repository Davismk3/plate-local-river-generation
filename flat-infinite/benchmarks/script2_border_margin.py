"""Impact of the RIVR_BORDER_DIST preprocessing exclusion margin on drainage
density (total channel length per unit land area), holding 20 plates fixed
and sweeping the margin. border_margin is a plain function argument to
ensurePlateRiverCache, so no numba recompilation concerns apply here.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import common  # noqa: E402
from app import config  # noqa: E402
from cache import rivercache  # noqa: E402

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
os.makedirs(RESULTS_DIR, exist_ok=True)

N_PLATES = 20
MARGINS = [0.0, 25.0, 50.0, 100.0, 150.0, 200.0, 300.0]


def main():
    common.assert_native_backend()
    gen = common.OwnerIdGenerator(start_block=500000)
    common.build_cache(gen.next(), resolution=100)  # warmup

    owner_indices = [gen.next() for _ in range(N_PLATES)]

    results = []
    for margin in MARGINS:
        per_plate = []
        for owner_idx in owner_indices:
            cache, elapsed = common.build_cache(owner_idx, resolution=100, border_margin=margin, time_it=True)
            land_area, domain_area = common.plate_land_area_plate_cells2(cache)
            nodes, segments, paths = common.network_arrays(cache)
            total_len = sum(common.segment_length_plate_cells(s) for s in segments)
            density = total_len / land_area if land_area > 0 else None
            per_plate.append({
                "owner_idx": list(owner_idx),
                "land_area_plate_cells2": land_area,
                "total_channel_length_plate_cells": total_len,
                "drainage_density": density,
                "path_count": len(paths),
                "segment_count": len(segments),
                "elapsed_seconds": elapsed,
            })
            rivercache.clearRiverCache(owner_idx)

        densities = [p["drainage_density"] for p in per_plate if p["drainage_density"] is not None]
        path_counts = [p["path_count"] for p in per_plate]
        results.append({
            "border_margin": margin,
            "drainage_density_summary": common.summarize(densities),
            "path_count_summary": common.summarize(path_counts),
            "per_plate": per_plate,
        })
        print(f"[margin={margin}] done", file=sys.stderr)

    with open(os.path.join(RESULTS_DIR, "study3_border_margin.json"), "w") as f:
        json.dump({"n_plates": N_PLATES, "margins": MARGINS, "results": results}, f, indent=2)
    print("DONE", file=sys.stderr)


if __name__ == "__main__":
    main()
