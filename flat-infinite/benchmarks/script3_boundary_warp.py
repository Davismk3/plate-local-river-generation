"""Quantify the symmetric-difference area between the straight geometric
plate domain (Omega_i^phi, unwarped Voronoi-like borders) and the true
noise-warped pointwise footprint (nearest owner under the warped metric),
and how that error scales with the warp amplitude (PLT_STRETCHING) and
warp frequency (PLT_BDR_SHAPE).

PLT_STRETCHING/PLT_BDR_SHAPE are read as globals inside the @njit
`plateOwnerIndex` dispatcher, which numba bakes in at first compile time;
plain reassignment of config.* would silently not take effect on the
compiled path. We force numba to pick up each new value with an explicit
`.recompile()` after each config change (verified to match the pure-Python
`.py_func` fallback bit-for-bit on a spot check, and roughly 4x faster).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import numpy as np  # noqa: E402
import common  # noqa: E402
from app import config  # noqa: E402
from helpers import geometry  # noqa: E402
from platewise import platewisegeometry  # noqa: E402
from pointwise import pointwisefields  # noqa: E402

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
os.makedirs(RESULTS_DIR, exist_ok=True)

N_PLATES = 8
RASTER_RES = 120
DEFAULT_STRETCHING = config.PLT_STRETCHING
DEFAULT_BDR_SHAPE = config.PLT_BDR_SHAPE

AMPLITUDE_FACTORS = [0.0, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0]
FREQUENCY_FACTORS = [0.25, 0.5, 1.0, 1.5, 2.0, 3.0]


def symmetric_difference_fraction(owner_idx, n_samples=RASTER_RES):
    plate_owner_index = pointwisefields.plateOwnerIndex

    polygon = platewisegeometry.geometricRepresentation(owner_idx)
    if len(polygon.points) < 3:
        return None

    xs = [p.x for p in polygon.points]
    ys = [p.y for p in polygon.points]
    pad = 0.15 * max(max(xs) - min(xs), max(ys) - min(ys), 1e-6)
    min_x, max_x = min(xs) - pad, max(xs) + pad
    min_y, max_y = min(ys) - pad, max(ys) + pad

    grid_x = np.linspace(min_x, max_x, n_samples)
    grid_y = np.linspace(min_y, max_y, n_samples)
    cell_area = (max_x - min_x) * (max_y - min_y) / (n_samples * n_samples)

    mismatch = 0
    union = 0
    for py in grid_y:
        for px in grid_x:
            geo_inside = polygon.point_inside(geometry.Point(x=float(px), y=float(py)))
            world_x = px / config.PLT_SCALE
            world_y = py / config.PLT_SCALE
            owner = plate_owner_index(world_x, world_y)
            pointwise_inside = (int(owner[0]), int(owner[1])) == tuple(owner_idx)
            if geo_inside or pointwise_inside:
                union += 1
                if geo_inside != pointwise_inside:
                    mismatch += 1

    geometric_area = polygon.area()
    mismatch_area = mismatch * cell_area
    union_area = union * cell_area
    return {
        "geometric_area_plate_cells2": geometric_area,
        "symmetric_difference_area_plate_cells2": mismatch_area,
        "symmetric_difference_fraction_of_geometric_area": mismatch_area / geometric_area if geometric_area > 0 else None,
        "jaccard_index": (union - mismatch) / union if union > 0 else None,
    }


def run_sweep(factors, param_name, default_value):
    gen = common.OwnerIdGenerator(start_block=900000)
    owner_indices = [gen.next() for _ in range(N_PLATES)]
    results = []
    for factor in factors:
        setattr(config, param_name, default_value * factor if factor > 0 else 1e-9)
        pointwisefields.plateOwnerIndex.recompile()
        per_plate = []
        for owner_idx in owner_indices:
            r = symmetric_difference_fraction(owner_idx)
            if r is not None:
                per_plate.append(r)
        fractions = [p["symmetric_difference_fraction_of_geometric_area"] for p in per_plate if p["symmetric_difference_fraction_of_geometric_area"] is not None]
        results.append({
            "factor": factor,
            "value": getattr(config, param_name),
            "fraction_summary": common.summarize(fractions),
        })
        setattr(config, param_name, default_value)
        print(f"[{param_name} factor={factor}] done", file=sys.stderr)
    return results


def main():
    amplitude_results = run_sweep(AMPLITUDE_FACTORS, "PLT_STRETCHING", DEFAULT_STRETCHING)
    frequency_results = run_sweep(FREQUENCY_FACTORS, "PLT_BDR_SHAPE", DEFAULT_BDR_SHAPE)

    with open(os.path.join(RESULTS_DIR, "study3_boundary_warp.json"), "w") as f:
        json.dump({
            "n_plates": N_PLATES,
            "raster_resolution": RASTER_RES,
            "default_plt_stretching": DEFAULT_STRETCHING,
            "default_plt_bdr_shape": DEFAULT_BDR_SHAPE,
            "amplitude_sweep": amplitude_results,
            "frequency_sweep": frequency_results,
        }, f, indent=2)
    print("DONE", file=sys.stderr)


if __name__ == "__main__":
    main()
