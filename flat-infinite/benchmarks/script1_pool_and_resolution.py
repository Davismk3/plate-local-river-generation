"""Phase A: paired grid-resolution scaling (50 plates x {50,100,200,400}).
Phase B: extend the resolution=100 column into a 150-plate pool.
Phase C: from that live pool, compute plate-density bootstrap stats,
         Strahler/bifurcation + Hack's-law hydrological validation, and a
         packed-cache byte-category breakdown.

All numbers are saved as JSON under results/. No source files in the
flat-infinite repo are modified; this only imports and calls its public
cache-build API.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import common  # noqa: E402
from app import config  # noqa: E402
from cache import rivercache  # noqa: E402

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
os.makedirs(RESULTS_DIR, exist_ok=True)

RESOLUTIONS = [50, 100, 200, 400]
N_SEEDS = 50
POOL_EXTRA = 100  # additional plates at resolution=100, on top of the 50 paired ones


def main():
    common.assert_native_backend()
    gen = common.OwnerIdGenerator(start_block=0)

    # Warm up numba dispatchers (first call pays JIT compile cost).
    common.build_cache(gen.next(), resolution=50)

    plate_seeds = [gen.next() for _ in range(N_SEEDS)]

    # ---------------- Phase A: resolution scaling ----------------
    records = []
    for resolution in RESOLUTIONS:
        for seed_idx, owner_idx in enumerate(plate_seeds):
            cache, elapsed = common.build_cache(owner_idx, resolution=resolution, time_it=True)
            nodes, segments, paths = common.network_arrays(cache)
            records.append({
                "resolution": resolution,
                "grid_cells": resolution * resolution,
                "seed_idx": seed_idx,
                "owner_idx": list(owner_idx),
                "elapsed_seconds": elapsed,
                "node_count": len(nodes),
                "segment_count": len(segments),
                "path_count": len(paths),
            })
            rivercache.clearRiverCache(owner_idx)
        print(f"[phase A] resolution={resolution} done", file=sys.stderr)

    with open(os.path.join(RESULTS_DIR, "study1_resolution_scaling.json"), "w") as f:
        json.dump({"records": records, "n_seeds": N_SEEDS, "resolutions": RESOLUTIONS}, f, indent=2)

    # ---------------- Phase B: build the res=100 pool (live, kept in memory) ----------------
    pool_owner_indices = list(plate_seeds)  # reuse the 50 res=100 seeds
    for _ in range(POOL_EXTRA):
        pool_owner_indices.append(gen.next())

    pool_caches = []
    pool_records = []
    for owner_idx in pool_owner_indices:
        cache, elapsed = common.build_cache(owner_idx, resolution=100, time_it=True)
        pool_caches.append(cache)
        land_area, domain_area = common.plate_land_area_plate_cells2(cache)
        nodes, segments, paths = common.network_arrays(cache)
        max_path_len = max((common.path_length_plate_cells(nodes, p) for p in paths), default=0.0)
        total_channel_len = sum(common.segment_length_plate_cells(s) for s in segments)
        pool_records.append({
            "owner_idx": list(owner_idx),
            "elapsed_seconds": elapsed,
            "land_area_plate_cells2": land_area,
            "domain_area_plate_cells2": domain_area,
            "node_count": len(nodes),
            "segment_count": len(segments),
            "path_count": len(paths),
            "max_path_length_plate_cells": max_path_len,
            "total_channel_length_plate_cells": total_channel_len,
        })
    print(f"[phase B] built pool of {len(pool_caches)} plates at resolution=100", file=sys.stderr)

    with open(os.path.join(RESULTS_DIR, "plate_pool_res100.json"), "w") as f:
        json.dump({"records": pool_records}, f, indent=2)

    # ---------------- Phase C1: plate-density bootstrap (uses only elapsed times) ----------------
    import numpy as np
    rng = np.random.default_rng(12345)
    times = np.array([r["elapsed_seconds"] for r in pool_records])
    density_levels = [5, 10, 20, 40, 60, 80, 100, 120, 140]
    bootstrap_reps = 500
    density_results = []
    for n_plates in density_levels:
        if n_plates > len(times):
            continue
        sums = []
        for _ in range(bootstrap_reps):
            idx = rng.choice(len(times), size=n_plates, replace=False)
            sums.append(float(np.sum(times[idx])))
        density_results.append({
            "n_plates": n_plates,
            "total_time_seconds": common.summarize(sums),
        })
    with open(os.path.join(RESULTS_DIR, "study1_density_scaling.json"), "w") as f:
        json.dump({"pool_size": len(times), "bootstrap_reps": bootstrap_reps, "density_results": density_results}, f, indent=2)
    print("[phase C1] density bootstrap done", file=sys.stderr)

    # ---------------- Phase C2: Strahler / bifurcation + Hack's law ----------------
    all_links = []
    hack_points = []
    for cache in pool_caches:
        nodes, segments, paths = common.network_arrays(cache)
        if len(segments) == 0:
            continue
        links = common.strahler_links(nodes, segments)
        all_links.extend(links)
        land_area, _ = common.plate_land_area_plate_cells2(cache)
        max_path_len = max((common.path_length_plate_cells(nodes, p) for p in paths), default=0.0)
        if land_area > 0 and max_path_len > 0:
            hack_points.append({"land_area_plate_cells2": land_area, "max_path_length_plate_cells": max_path_len})

    counts = common.bifurcation_counts(all_links)
    orders_sorted = sorted(counts.keys())
    ratios = []
    for a, b in zip(orders_sorted, orders_sorted[1:]):
        if counts[b] > 0:
            ratios.append({"order_low": a, "order_high": b, "n_low": counts[a], "n_high": counts[b], "ratio": counts[a] / counts[b]})

    # Per-plate bifurcation ratios too (for a distribution, not just pooled).
    per_plate_ratios = []
    for cache in pool_caches:
        nodes, segments, paths = common.network_arrays(cache)
        if len(segments) == 0:
            continue
        links = common.strahler_links(nodes, segments)
        c = common.bifurcation_counts(links)
        os_ = sorted(c.keys())
        rs = [c[a] / c[b] for a, b in zip(os_, os_[1:]) if c[b] > 0]
        if rs:
            per_plate_ratios.extend(rs)

    # Hack's law: log(L) = log(C) + h*log(A)
    import numpy as np
    A = np.array([p["land_area_plate_cells2"] for p in hack_points])
    L = np.array([p["max_path_length_plate_cells"] for p in hack_points])
    hacks_law = None
    if len(A) >= 3:
        logA = np.log(A)
        logL = np.log(L)
        slope, intercept = np.polyfit(logA, logL, 1)
        pred = slope * logA + intercept
        ss_res = np.sum((logL - pred) ** 2)
        ss_tot = np.sum((logL - np.mean(logL)) ** 2)
        r2 = 1 - ss_res / ss_tot if ss_tot > 0 else None
        hacks_law = {
            "n_plates": int(len(A)),
            "h_exponent": float(slope),
            "C_coefficient": float(np.exp(intercept)),
            "r_squared": float(r2) if r2 is not None else None,
        }

    with open(os.path.join(RESULTS_DIR, "study2_hydrology.json"), "w") as f:
        json.dump({
            "pool_size": len(pool_caches),
            "total_links": len(all_links),
            "order_counts": {str(k): v for k, v in counts.items()},
            "pooled_bifurcation_ratios": ratios,
            "per_plate_bifurcation_ratio_summary": common.summarize(per_plate_ratios),
            "hacks_law": hacks_law,
            "hack_points": hack_points,
        }, f, indent=2)
    print("[phase C2] hydrology done", file=sys.stderr)

    # ---------------- Phase C3: packed-cache byte breakdown ----------------
    owner_list = pool_owner_indices
    packed = rivercache.packRiverCache(owner_list)

    geometry_keys = ["segment_from_points", "segment_to_points", "segment_from_heights",
                      "segment_to_heights", "node_points", "node_heights"]
    topology_keys = ["plate_owner_indices", "plate_segment_starts", "plate_region_starts",
                      "plate_node_starts", "node_plate_slots", "node_types",
                      "segment_from_lake_nodes", "segment_to_lake_nodes",
                      "region_segment_ids", "region_candidate_starts", "region_candidate_segment_ids"]
    polygon_keys = ["plane_points", "plane_normals", "region_plane_starts"]

    def group_bytes(keys):
        total = 0
        breakdown = {}
        for k in keys:
            v = packed.get(k)
            if v is None:
                continue
            nbytes = int(v.nbytes)
            breakdown[k] = nbytes
            total += nbytes
        return total, breakdown

    geometry_bytes, geometry_breakdown = group_bytes(geometry_keys)
    topology_bytes, topology_breakdown = group_bytes(topology_keys)
    polygon_bytes, polygon_breakdown = group_bytes(polygon_keys)
    total_bytes = geometry_bytes + topology_bytes + polygon_bytes
    all_segment_count = sum(r["segment_count"] for r in pool_records)

    with open(os.path.join(RESULTS_DIR, "study4_byte_breakdown.json"), "w") as f:
        json.dump({
            "n_plates": len(owner_list),
            "all_segment_count": all_segment_count,
            "total_bytes": total_bytes,
            "bytes_per_segment": total_bytes / all_segment_count if all_segment_count else None,
            "geometry_bytes": geometry_bytes,
            "geometry_breakdown": geometry_breakdown,
            "topology_bytes": topology_bytes,
            "topology_breakdown": topology_breakdown,
            "polygon_bytes": polygon_bytes,
            "polygon_breakdown": polygon_breakdown,
            "per_plate_bytes_estimate": [
                {"owner_idx": r["owner_idx"], "segment_count": r["segment_count"],
                 "node_count": r["node_count"]}
                for r in pool_records
            ],
        }, f, indent=2)
    print("[phase C3] byte breakdown done", file=sys.stderr)

    print("ALL PHASES COMPLETE", file=sys.stderr)


if __name__ == "__main__":
    main()
