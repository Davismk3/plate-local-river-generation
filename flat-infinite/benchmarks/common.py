"""Shared helpers for the paper's evaluation benchmarks.

Imports the flat-infinite src/ package directly (no changes made to the
library itself). Run any script in this directory in place, e.g.:

    python3 benchmarks/script1_pool_and_resolution.py
"""
import itertools
import math
import os
import sys

REPO_SRC = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
if REPO_SRC not in sys.path:
    sys.path.insert(0, REPO_SRC)

import numpy as np  # noqa: E402

from app import config  # noqa: E402
from cache import rivercache  # noqa: E402
from helpers import ids  # noqa: E402
from platewise import platewisegrid  # noqa: E402


def assert_native_backend():
    status = platewisegrid.nativeBackendStatus()
    if not status["available"]:
        raise RuntimeError(f"native grid backend unavailable: {status}")
    return status


# ---------------------------------------------------------------------------
# Deterministic, collision-free plate-owner-index generator.
#
# Each experiment needs many "independent worlds" without touching
# config.SEED (which is baked into several @njit dispatchers at compile
# time and will not update on plain reassignment). Instead we walk a widely
# spaced deterministic lattice of plate identifiers: the underlying hash911
# already scrambles (ix, iy) into pseudo-independent plate placement/type/
# drift, so distinct, well-separated lattice cells behave as distinct
# statistical samples of the same generative process.
# ---------------------------------------------------------------------------
class OwnerIdGenerator:
    def __init__(self, start_block=0, stride=97, block_size=9973):
        self._block = start_block
        self._stride = stride
        self._block_size = block_size

    def next(self):
        value = self._block
        self._block += self._stride
        ix = (value * 340573) % self._block_size
        iy = (value * 912337) % self._block_size
        return (100000 + ix, 100000 + iy)

    def batch(self, n):
        return [self.next() for _ in range(n)]


def build_cache(owner_idx, resolution=config.RIVER_GRID_RES, border_margin=config.RIVR_BORDER_DIST,
                 river_count=config.RIVER_COUNT, force=True, time_it=False):
    import time
    t0 = time.perf_counter()
    cache = rivercache.ensurePlateRiverCache(
        owner_idx,
        resolution=resolution,
        river_count=river_count,
        source_min_height=config.SOURCE_MIN_HEIGHT,
        min_source_spacing=config.MIN_SOURCE_SPACING,
        step_size=config.STEP_SIZE,
        max_steps=config.MAX_RIVER_STEPS,
        border_margin=border_margin,
        force=force,
    )
    elapsed = time.perf_counter() - t0
    if time_it:
        return cache, elapsed
    return cache


def plate_land_area_plate_cells2(cache):
    grid = cache["grid"]
    polygon = grid[ids.polygon_id]
    valid_mask = grid[ids.valid_mask_id]
    heights = grid[ids.heights_id]
    valid_heights = heights[valid_mask]
    if valid_heights.size == 0:
        return 0.0, 0.0
    land_fraction = float(np.count_nonzero(valid_heights > config.SEA_LEVEL_FRACTION)) / float(valid_heights.size)
    domain_area = polygon.area()
    return domain_area * land_fraction, domain_area


def network_arrays(cache):
    network = cache["network"]
    return network[ids.nodes_id], network[ids.segments_id], network[ids.paths_id]


def segment_length_plate_cells(segment):
    return math.hypot(segment.b.x - segment.a.x, segment.b.y - segment.a.y) * config.PLT_SCALE


def path_length_plate_cells(nodes, path):
    length = 0.0
    for i in range(len(path) - 1):
        a = nodes[int(path[i])]
        b = nodes[int(path[i + 1])]
        length += math.hypot(b.x - a.x, b.y - a.y) * config.PLT_SCALE
    return length


# ---------------------------------------------------------------------------
# Strahler order via topological "link" collapsing (junction-to-junction
# reaches), matching the classical definition rather than raw grid-segment
# counts. The network the codebase builds is a directed in-forest: every
# segment points from its upstream (from_node) to its downstream (to_node)
# endpoint, and every node has out-degree <= 1 (a node continues downstream
# along at most one path) by construction.
# ---------------------------------------------------------------------------
def strahler_links(nodes, segments):
    n = len(nodes)
    in_edges = [[] for _ in range(n)]
    out_edges = [[] for _ in range(n)]
    for seg_idx, segment in enumerate(segments):
        out_edges[int(segment.from_node)].append(seg_idx)
        in_edges[int(segment.to_node)].append(seg_idx)

    def is_junction(node_id):
        return len(in_edges[node_id]) != 1 or len(out_edges[node_id]) != 1

    junctions = [i for i in range(n) if is_junction(i)]
    junction_set = set(junctions)

    links = []  # dict(start, end, segment_ids, length)
    for j in junctions:
        for seg_idx in out_edges[j]:
            chain = [seg_idx]
            current = int(segments[seg_idx].to_node)
            while current not in junction_set:
                nxt = out_edges[current][0]
                chain.append(nxt)
                current = int(segments[nxt].to_node)
            length = sum(segment_length_plate_cells(segments[s]) for s in chain)
            links.append({"start": j, "end": current, "segments": chain, "length": length})

    # Links ending at each junction, for the combine rule.
    incoming_by_junction = {}
    for link in links:
        incoming_by_junction.setdefault(link["end"], []).append(link)
    outgoing_by_junction = {}
    for link in links:
        outgoing_by_junction.setdefault(link["start"], []).append(link)

    order = {}
    pending = {j: len(incoming_by_junction.get(j, [])) for j in junctions}
    queue = [j for j in junctions if pending[j] == 0]
    for j in queue:
        order[j] = 1
    processed = set()

    while queue:
        j = queue.pop()
        if j in processed:
            continue
        processed.add(j)
        for link in outgoing_by_junction.get(j, []):
            link["order"] = order.get(j, 1)
            end = link["end"]
            pending[end] -= 1
            if pending[end] == 0 and end not in processed:
                incoming = incoming_by_junction.get(end, [])
                if incoming:
                    orders = [order[l["start"]] for l in incoming]
                    m = max(orders)
                    order[end] = m + 1 if orders.count(m) >= 2 else m
                else:
                    order[end] = 1
                queue.append(end)

    # Any link whose "order" never got set (shouldn't happen in a well
    # formed DAG, but guard defensively against isolated/rollback debris).
    for link in links:
        link.setdefault("order", order.get(link["start"], 1))

    return links


def bifurcation_counts(links):
    counts = {}
    for link in links:
        counts[link["order"]] = counts.get(link["order"], 0) + 1
    return counts


def summarize(values):
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return {"n": 0, "mean": None, "median": None, "std": None, "p95": None, "min": None, "max": None}
    return {
        "n": int(values.size),
        "mean": float(np.mean(values)),
        "median": float(np.median(values)),
        "std": float(np.std(values, ddof=1)) if values.size > 1 else 0.0,
        "p95": float(np.percentile(values, 95)),
        "min": float(np.min(values)),
        "max": float(np.max(values)),
    }
