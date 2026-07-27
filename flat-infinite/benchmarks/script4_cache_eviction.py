"""Simulate cache memory under LRU-capacity and TTL eviction policies while
a camera crosses the infinite plate lattice.

The shipped implementation caches plate river networks in an unbounded
dict (see cache/rivercache.py: `_river_cache_by_key` is never evicted), so
there is no real eviction policy to instrument directly. This script builds
a grounded simulation instead: real per-plate cache-build latencies and
byte sizes (measured in script1's 150-plate pool) drive a synthetic replay
of camera movement across the plate lattice, under two candidate eviction
policies. This is clearly a simulation, not a measurement of the shipped
runtime -- called out explicitly in the writeup.
"""
import json
import os

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")

BYTES_PER_SEGMENT = None  # filled from study4_byte_breakdown.json
ACTIVE_RADIUS = 1  # matches config.TERRAIN_RIVER_ACTIVE_RADIUS default


def load_plate_cost_model():
    with open(os.path.join(RESULTS_DIR, "plate_pool_res100.json")) as f:
        pool = json.load(f)["records"]
    with open(os.path.join(RESULTS_DIR, "study4_byte_breakdown.json")) as f:
        byte_info = json.load(f)
    bytes_per_segment = byte_info["bytes_per_segment"]

    plates = {}
    for i, r in enumerate(pool):
        bytes_estimate = max(1.0, r["segment_count"] * bytes_per_segment)
        plates[i] = {
            "build_seconds": r["elapsed_seconds"],
            "bytes": bytes_estimate,
        }
    return plates, bytes_per_segment


def active_block(center_ix, center_iy, radius=ACTIVE_RADIUS):
    return [
        (center_ix + dx, center_iy + dy)
        for dy in range(-radius, radius + 1)
        for dx in range(-radius, radius + 1)
    ]


def plate_cost_for(owner_idx, plates, cost_cycle):
    # Deterministically map an (ix, iy) lattice cell to one of the measured
    # pool plates, so repeated visits to the same lattice cell reuse a
    # stable, consistent cost (as the real system would for a fixed seed).
    key = hash(owner_idx) % len(plates)
    return plates[key]


def linear_path(speed_plates_per_second):
    return lambda t: speed_plates_per_second * t


def oscillating_path(speed_plates_per_second, half_range_plates):
    # Models a player exploring and backtracking within a local region:
    # triangle-wave position with the given peak speed and amplitude.
    period = 4.0 * half_range_plates / max(speed_plates_per_second, 1e-9)

    def pos(t):
        phase = (t % period) / period  # in [0, 1)
        # Triangle wave in [-half_range, half_range]
        tri = 4 * abs(phase - 0.5) - 1  # in [-1, 1], starts at -1
        return half_range_plates * tri

    return pos


def simulate(plates, path_fn, duration_seconds, dt, policy, policy_param):
    """policy: 'lru' with policy_param=capacity (# resident plate caches),
    or 'ttl' with policy_param=seconds-since-last-access before eviction."""
    resident = {}  # owner_idx -> {"bytes":..., "last_access": t}
    timeline = []  # (t, resident_bytes, resident_count, cumulative_misses, cumulative_build_seconds)
    misses = 0
    hits = 0
    cumulative_build_seconds = 0.0

    t = 0.0
    while t <= duration_seconds:
        camera_pos = path_fn(t)
        center_ix = int(round(camera_pos))
        center_iy = 0
        required = active_block(center_ix, center_iy)

        for owner_idx in required:
            if owner_idx in resident:
                resident[owner_idx]["last_access"] = t
                hits += 1
            else:
                cost = plate_cost_for(owner_idx, plates, None)
                resident[owner_idx] = {"bytes": cost["bytes"], "last_access": t}
                cumulative_build_seconds += cost["build_seconds"]
                misses += 1

        required_set = set(required)
        if policy == "lru":
            capacity = policy_param
            if len(resident) > capacity:
                evictable = [k for k in resident if k not in required_set]
                evictable.sort(key=lambda k: resident[k]["last_access"])
                n_to_evict = len(resident) - capacity
                for k in evictable[:n_to_evict]:
                    del resident[k]
        elif policy == "ttl":
            ttl = policy_param
            for k in [k for k in resident if k not in required_set]:
                if t - resident[k]["last_access"] > ttl:
                    del resident[k]
        elif policy == "unbounded":
            pass
        else:
            raise ValueError(policy)

        resident_bytes = sum(v["bytes"] for v in resident.values())
        timeline.append({
            "t": t,
            "resident_bytes": resident_bytes,
            "resident_count": len(resident),
            "cumulative_misses": misses,
            "cumulative_hits": hits,
            "cumulative_build_seconds": cumulative_build_seconds,
        })
        t += dt

    return timeline


def _summarize_run(tl):
    return {
        "final_resident_bytes": tl[-1]["resident_bytes"],
        "final_resident_count": tl[-1]["resident_count"],
        "total_misses": tl[-1]["cumulative_misses"],
        "total_hits": tl[-1]["cumulative_hits"],
        "hit_rate": tl[-1]["cumulative_hits"] / max(1, tl[-1]["cumulative_hits"] + tl[-1]["cumulative_misses"]),
        "total_build_seconds": tl[-1]["cumulative_build_seconds"],
        "peak_resident_bytes": max(p["resident_bytes"] for p in tl),
        "timeline_sample": tl[::20],
    }


def main():
    plates, bytes_per_segment = load_plate_cost_model()
    mean_bytes = sum(p["bytes"] for p in plates.values()) / len(plates)
    mean_build = sum(p["build_seconds"] for p in plates.values()) / len(plates)

    duration = 180.0
    dt = 0.25
    lru_capacities = [9, 25, 49, 100]
    ttl_values = [5.0, 15.0, 45.0]

    scenarios = {
        "linear_slow": linear_path(0.25),
        "linear_fast": linear_path(4.0),
        "oscillating_slow": oscillating_path(0.5, half_range_plates=6),
        "oscillating_fast": oscillating_path(3.0, half_range_plates=6),
    }

    results = {
        "mean_plate_bytes": mean_bytes,
        "mean_plate_build_seconds": mean_build,
        "bytes_per_segment": bytes_per_segment,
        "active_radius": ACTIVE_RADIUS,
        "duration_seconds": duration,
        "dt": dt,
        "scenarios": {},
    }

    for name, path_fn in scenarios.items():
        entry = {"unbounded": None, "lru": {}, "ttl": {}}
        entry["unbounded"] = _summarize_run(simulate(plates, path_fn, duration, dt, "unbounded", None))
        for cap in lru_capacities:
            entry["lru"][str(cap)] = _summarize_run(simulate(plates, path_fn, duration, dt, "lru", cap))
        for ttl in ttl_values:
            entry["ttl"][str(ttl)] = _summarize_run(simulate(plates, path_fn, duration, dt, "ttl", ttl))
        results["scenarios"][name] = entry
        print(f"[{name}] done")

    with open(os.path.join(RESULTS_DIR, "study4_cache_eviction.json"), "w") as f:
        json.dump(results, f, indent=2)
    print("DONE")


if __name__ == "__main__":
    main()
