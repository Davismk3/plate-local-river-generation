import math

import numpy as np

from app import config
from helpers import ids
from platewise import platewisegrid, platewisenetwork, platewiseregions
from pointwise import pointwisefields, pointwiseheight, pointwiseplatefields


PLATE_OWNER_INDICES_IDX = 0
PLATE_SEGMENT_STARTS_IDX = 1
PLATE_REGION_STARTS_IDX = 2
REGION_SEGMENT_IDS_IDX = 3
REGION_PLANE_STARTS_IDX = 4
REGION_CANDIDATE_STARTS_IDX = 5
REGION_CANDIDATE_SEGMENT_IDS_IDX = 6
PLANE_POINTS_IDX = 7
PLANE_NORMALS_IDX = 8
SEGMENT_FROM_POINTS_IDX = 9
SEGMENT_TO_POINTS_IDX = 10
SEGMENT_FROM_HEIGHTS_IDX = 11
SEGMENT_TO_HEIGHTS_IDX = 12
SEGMENT_FROM_LAKE_NODES_IDX = 13
SEGMENT_TO_LAKE_NODES_IDX = 14


_river_cache_by_key = {}
_latest_cache_key_by_plate = {}
_active_owner_indices = ()
_packed_cache = None


def plateOwnerIndex(x, y):
    return _ownerKey(pointwisefields.plateOwnerIndex(float(x), float(y)))


def activePlateOwnerIndices(center_x, center_y, radius=1):
    owner_x, owner_y = plateOwnerIndex(center_x, center_y)
    radius = max(0, int(radius))

    owner_indices = []
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            owner_indices.append((owner_x + dx, owner_y + dy))

    return owner_indices


def ensurePlateRiverCache(
    plt_owner_idx,
    resolution=config.RIVER_GRID_RES,
    river_count=config.RIVER_COUNT,
    source_min_height=config.SOURCE_MIN_HEIGHT,
    min_source_spacing=config.MIN_SOURCE_SPACING,
    step_size=config.STEP_SIZE,
    max_steps=config.MAX_RIVER_STEPS,
    border_margin=config.RIVR_BORDER_DIST,
    force=False,
):
    owner_idx = _ownerKey(plt_owner_idx)
    options = _riverOptions(
        resolution=resolution,
        river_count=river_count,
        source_min_height=source_min_height,
        min_source_spacing=min_source_spacing,
        step_size=step_size,
        max_steps=max_steps,
        border_margin=border_margin,
    )
    cache_key = _cacheKey(owner_idx, options)

    if not force and cache_key in _river_cache_by_key:
        return _river_cache_by_key[cache_key]

    grid = platewisegrid.plateGrid(
        owner_idx,
        resolution=options["resolution"],
        border_margin=options["border_margin"],
    )
    network = platewisenetwork.plateNetwork(
        grid,
        river_count=options["river_count"],
        source_min_height=options["source_min_height"],
        min_source_spacing=options["min_source_spacing"],
        step_size=options["step_size"],
        max_steps=options["max_steps"],
    )
    regions = platewiseregions.plateRegions(grid, network)

    cache = {
        "plt_owner_idx": owner_idx,
        "options": options,
        "grid": grid,
        "network": network,
        "regions": regions,
    }
    _river_cache_by_key[cache_key] = cache
    _latest_cache_key_by_plate[owner_idx] = cache_key
    return cache


def getPlateRiverCache(plt_owner_idx):
    owner_idx = _ownerKey(plt_owner_idx)
    cache_key = _latest_cache_key_by_plate.get(owner_idx)
    if cache_key is None:
        return None
    return _river_cache_by_key.get(cache_key)


def ensureActiveRiverCacheForWorldPosition(
    center_x,
    center_y,
    active_radius=1,
    resolution=config.RIVER_GRID_RES,
    river_count=config.RIVER_COUNT,
    source_min_height=config.SOURCE_MIN_HEIGHT,
    min_source_spacing=config.MIN_SOURCE_SPACING,
    step_size=config.STEP_SIZE,
    max_steps=config.MAX_RIVER_STEPS,
    border_margin=config.RIVR_BORDER_DIST,
):
    global _active_owner_indices

    owner_indices = activePlateOwnerIndices(center_x, center_y, active_radius)
    caches = []
    for owner_idx in owner_indices:
        caches.append(
            ensurePlateRiverCache(
                owner_idx,
                resolution=resolution,
                river_count=river_count,
                source_min_height=source_min_height,
                min_source_spacing=min_source_spacing,
                step_size=step_size,
                max_steps=max_steps,
                border_margin=border_margin,
            )
        )

    _active_owner_indices = tuple(owner_indices)
    publishPackedRiverCache(owner_indices)
    return caches


def cachedPlateOwnerIndices():
    return sorted(_latest_cache_key_by_plate.keys())


def clearRiverCache(plt_owner_idx=None):
    global _packed_cache, _active_owner_indices

    if plt_owner_idx is None:
        _river_cache_by_key.clear()
        _latest_cache_key_by_plate.clear()
        _active_owner_indices = ()
        _packed_cache = None
        return

    owner_idx = _ownerKey(plt_owner_idx)
    cache_key = _latest_cache_key_by_plate.pop(owner_idx, None)
    if cache_key is not None:
        _river_cache_by_key.pop(cache_key, None)
    _packed_cache = None


def publishPackedRiverCache(owner_indices=None):
    global _packed_cache
    _packed_cache = packRiverCache(owner_indices)
    return _packed_cache


def currentPackedRiverCache():
    return _packed_cache


def packRiverCache(owner_indices=None):
    if owner_indices is None:
        owner_indices = _active_owner_indices or tuple(cachedPlateOwnerIndices())
    owner_indices = [_ownerKey(owner_idx) for owner_idx in owner_indices]

    plate_owner_indices = np.asarray(owner_indices, dtype=np.int32).reshape((-1, 2))
    plate_segment_starts = np.zeros(len(owner_indices) + 1, dtype=np.int32)
    plate_region_starts = np.zeros(len(owner_indices) + 1, dtype=np.int32)
    plate_node_starts = np.zeros(len(owner_indices) + 1, dtype=np.int32)

    segment_from_points = []
    segment_to_points = []
    segment_from_heights = []
    segment_to_heights = []
    segment_from_lake_nodes = []
    segment_to_lake_nodes = []
    region_segment_ids = []
    region_plane_starts = [0]
    region_candidate_starts = [0]
    region_candidate_segment_ids = []
    plane_points = []
    plane_normals = []
    node_plate_slots = []
    node_points = []
    node_heights = []
    node_types = []

    for slot, owner_idx in enumerate(owner_indices):
        cache = getPlateRiverCache(owner_idx)
        segment_offset = len(segment_from_points)
        plate_segment_starts[slot] = len(segment_from_points)
        plate_region_starts[slot] = len(region_segment_ids)
        plate_node_starts[slot] = len(node_points)

        if cache is None:
            continue

        network = cache["network"]
        nodes = network[ids.nodes_id]
        for node in nodes:
            node_plate_slots.append(slot)
            node_points.append(_pointTuple(node))
            node_heights.append(float(getattr(node, "river_height", node.height)))
            node_types.append(_nodeTypeId(node.type))

        regions = cache["regions"]
        for segment in regions["segments"]:
            segment_from_points.append(_pointTuple(segment.a))
            segment_to_points.append(_pointTuple(segment.b))
            segment_from_heights.append(float(segment.from_height))
            segment_to_heights.append(float(segment.to_height))
            segment_from_lake_nodes.append(segment.a.type == "outlet_local_minimum")
            segment_to_lake_nodes.append(segment.b.type == "outlet_local_minimum")

        for region in regions["segment_polygons"]:
            region_segment_ids.append(segment_offset + int(region["segment_id"]))
            for plane_point, plane_normal in _polygonPlanes(region["polygon"]):
                plane_points.append(plane_point)
                plane_normals.append(plane_normal)
            region_plane_starts.append(len(plane_points))
            region_candidate_segment_ids.append(segment_offset + int(region["segment_id"]))
            region_candidate_starts.append(len(region_candidate_segment_ids))

    plate_segment_starts[len(owner_indices)] = len(segment_from_points)
    plate_region_starts[len(owner_indices)] = len(region_segment_ids)
    plate_node_starts[len(owner_indices)] = len(node_points)

    return {
        "plate_owner_indices": plate_owner_indices,
        "plate_segment_starts": plate_segment_starts,
        "plate_region_starts": plate_region_starts,
        "plate_node_starts": plate_node_starts,
        "segment_from_points": _array2D(segment_from_points, np.float64),
        "segment_to_points": _array2D(segment_to_points, np.float64),
        "segment_from_heights": _array1D(segment_from_heights, np.float64),
        "segment_to_heights": _array1D(segment_to_heights, np.float64),
        "segment_from_lake_nodes": _array1D(segment_from_lake_nodes, np.bool_),
        "segment_to_lake_nodes": _array1D(segment_to_lake_nodes, np.bool_),
        "region_segment_ids": _array1D(region_segment_ids, np.int32),
        "region_plane_starts": np.asarray(region_plane_starts, dtype=np.int32),
        "region_candidate_starts": np.asarray(region_candidate_starts, dtype=np.int32),
        "region_candidate_segment_ids": _array1D(
            region_candidate_segment_ids, np.int32
        ),
        "plane_points": _array2D(plane_points, np.float64),
        "plane_normals": _array2D(plane_normals, np.float64),
        "node_plate_slots": _array1D(node_plate_slots, np.int32),
        "node_points": _array2D(node_points, np.float64),
        "node_heights": _array1D(node_heights, np.float64),
        "node_types": _array1D(node_types, np.int8),
    }


def packedCacheTuple(packed_cache=None):
    if packed_cache is None:
        packed_cache = _packed_cache
    if packed_cache is None:
        packed_cache = packRiverCache(())

    return (
        packed_cache["plate_owner_indices"],
        packed_cache["plate_segment_starts"],
        packed_cache["plate_region_starts"],
        packed_cache["region_segment_ids"],
        packed_cache["region_plane_starts"],
        packed_cache["region_candidate_starts"],
        packed_cache["region_candidate_segment_ids"],
        packed_cache["plane_points"],
        packed_cache["plane_normals"],
        packed_cache["segment_from_points"],
        packed_cache["segment_to_points"],
        packed_cache["segment_from_heights"],
        packed_cache["segment_to_heights"],
        packed_cache["segment_from_lake_nodes"],
        packed_cache["segment_to_lake_nodes"],
    )


def riverFields(x, y, river_cache=None):
    if river_cache is None:
        return _riverFieldsFromPlateCache(
            ensurePlateRiverCache(plateOwnerIndex(x, y)),
            x,
            y,
        )

    if _isPlateCache(river_cache):
        return _riverFieldsFromPlateCache(river_cache, x, y)

    if _isRegionsCache(river_cache):
        return pointwiseplatefields.plateRiverFields(x, y, river_cache)

    if _isPackedCacheTuple(river_cache):
        return pointwiseplatefields.riverFields(float(x), float(y), river_cache)

    if isinstance(river_cache, dict) and "plate_caches" in river_cache:
        return riverFieldsFromCaches(x, y, river_cache["plate_caches"])

    if isinstance(river_cache, (list, tuple)) and (
        len(river_cache) == 0 or _isPlateCache(river_cache[0])
    ):
        return riverFieldsFromCaches(x, y, river_cache)

    raise TypeError("river_cache must be None, a plate cache, a regions cache, or a cache list")


def riverFieldsFromCaches(x, y, river_caches):
    best_height = 0.0
    best_distance = 1.0

    for cache in river_caches:
        if not _isPlateCache(cache):
            continue
        river_height, river_distance = _riverFieldsFromPlateCache(cache, x, y)
        if river_distance < best_distance:
            best_height = river_height
            best_distance = river_distance

    return best_height, best_distance


def heightField(x, y, river_cache=None):
    if river_cache is None or _isPackedCacheTuple(river_cache):
        return float(pointwiseheight.finalHeightField(float(x), float(y), river_cache))

    height = float(pointwiseheight.finalHeightField(float(x), float(y), None))
    river_height, river_distance = riverFields(x, y, river_cache)
    if river_distance >= 1.0:
        return height

    terrain_weight = river_distance**config.RIVER_BLEND_EXP
    return height * terrain_weight + river_height * (1.0 - terrain_weight)


def terrainHeightGrid(
    center_x,
    center_y,
    plate_cells=config.TERRAIN_PLATE_CELLS,
    resolution=config.TERRAIN_RESOLUTION,
    include_rivers=True,
    active_radius=config.TERRAIN_RIVER_ACTIVE_RADIUS,
):
    resolution = max(2, int(resolution))
    world_width = float(plate_cells) / float(config.PLT_SCALE)
    min_x = float(center_x) - world_width * 0.5
    max_x = float(center_x) + world_width * 0.5
    min_y = float(center_y) - world_width * 0.5
    max_y = float(center_y) + world_width * 0.5

    river_caches = None
    river_height_cache = None
    if include_rivers:
        river_caches = ensureActiveRiverCacheForWorldPosition(
            center_x,
            center_y,
            active_radius=active_radius,
        )
        river_height_cache = packedCacheTuple(_packed_cache)

    xs = np.linspace(min_x, max_x, resolution, dtype=np.float64)
    ys = np.linspace(min_y, max_y, resolution, dtype=np.float64)
    heights = np.zeros((resolution, resolution), dtype=np.float64)

    for row, y in enumerate(ys):
        for col, x in enumerate(xs):
            heights[row, col] = heightField(x, y, river_height_cache)

    return {
        "center": (float(center_x), float(center_y)),
        "plate_cells": float(plate_cells),
        "world_bounds": (min_x, max_x, min_y, max_y),
        "xs": xs,
        "ys": ys,
        "heights": heights,
        "river_caches": river_caches,
    }


def riverGeometryForView(
    center_x,
    center_y,
    plate_cells,
    active_radius=1,
    resolution=config.RIVER_GRID_RES,
    river_count=config.RIVER_COUNT,
    source_min_height=config.SOURCE_MIN_HEIGHT,
    min_source_spacing=config.MIN_SOURCE_SPACING,
    step_size=config.STEP_SIZE,
    max_steps=config.MAX_RIVER_STEPS,
    border_margin=config.RIVR_BORDER_DIST,
):
    caches = ensureActiveRiverCacheForWorldPosition(
        center_x,
        center_y,
        active_radius=active_radius,
        resolution=resolution,
        river_count=river_count,
        source_min_height=source_min_height,
        min_source_spacing=min_source_spacing,
        step_size=step_size,
        max_steps=max_steps,
        border_margin=border_margin,
    )

    world_width = float(plate_cells) / float(config.PLT_SCALE)
    min_x = float(center_x) - world_width * 0.5
    max_x = float(center_x) + world_width * 0.5
    min_y = float(center_y) - world_width * 0.5
    max_y = float(center_y) + world_width * 0.5
    margin = world_width * config.RIVER_VIEW_MARGIN_FRACTION

    segments = []
    nodes = []
    for cache in caches:
        owner_idx = cache["plt_owner_idx"]
        network = cache["network"]

        for segment in network[ids.segments_id]:
            if not _segmentIntersectsBounds(
                _pointTuple(segment.a),
                _pointTuple(segment.b),
                min_x,
                max_x,
                min_y,
                max_y,
                margin,
            ):
                continue
            segments.append((segment, owner_idx))

        for node in network[ids.nodes_id]:
            x, y = _pointTuple(node)
            if min_x - margin <= x <= max_x + margin and min_y - margin <= y <= max_y + margin:
                nodes.append((node, owner_idx))

    return segments, nodes


def _riverFieldsFromPlateCache(cache, x, y):
    return pointwiseplatefields.plateRiverFields(x, y, cache["regions"])


def _isPlateCache(value):
    return isinstance(value, dict) and "grid" in value and "network" in value and "regions" in value


def _isRegionsCache(value):
    return isinstance(value, dict) and "segment_polygons" in value and "segments" in value


def _isPackedCacheTuple(value):
    return isinstance(value, tuple) and len(value) == 15


def _riverOptions(
    resolution,
    river_count,
    source_min_height,
    min_source_spacing,
    step_size,
    max_steps,
    border_margin,
):
    return {
        "resolution": max(2, int(resolution)),
        "river_count": max(0, int(river_count)),
        "source_min_height": float(source_min_height),
        "min_source_spacing": max(0, int(min_source_spacing)),
        "step_size": max(1, int(step_size)),
        "max_steps": max(1, int(max_steps)),
        "border_margin": float(border_margin),
    }


def _cacheKey(owner_idx, options):
    return (
        _ownerKey(owner_idx),
        options["resolution"],
        options["river_count"],
        options["source_min_height"],
        options["min_source_spacing"],
        options["step_size"],
        options["max_steps"],
        options["border_margin"],
    )


def _ownerKey(plt_owner_idx):
    return int(plt_owner_idx[0]), int(plt_owner_idx[1])


def _pointTuple(point):
    return float(point.x), float(point.y)


def _nodeTypeId(node_type):
    if node_type == "source":
        return 1
    if node_type == "outlet_sea":
        return 2
    if node_type == "outlet_local_minimum":
        return 3
    return 0


def _segmentIntersectsBounds(a, b, min_x, max_x, min_y, max_y, margin):
    ax, ay = a
    bx, by = b
    if max(ax, bx) < min_x - margin or min(ax, bx) > max_x + margin:
        return False
    if max(ay, by) < min_y - margin or min(ay, by) > max_y + margin:
        return False
    return True


def _polygonPlanes(polygon):
    points = polygon.points
    if len(points) < 3:
        return []

    centroid_x = sum(point.x for point in points) / len(points)
    centroid_y = sum(point.y for point in points) / len(points)
    planes = []

    for idx, a in enumerate(points):
        b = points[(idx + 1) % len(points)]
        edge_x = b.x - a.x
        edge_y = b.y - a.y
        normal_x = edge_y
        normal_y = -edge_x
        norm = math.sqrt(normal_x * normal_x + normal_y * normal_y)
        if norm <= config.RIVER_REGION_EPSILON:
            continue

        normal_x /= norm
        normal_y /= norm
        if (centroid_x - a.x) * normal_x + (centroid_y - a.y) * normal_y < 0.0:
            normal_x = -normal_x
            normal_y = -normal_y

        planes.append(((float(a.x), float(a.y)), (float(normal_x), float(normal_y))))

    return planes


def _array1D(values, dtype):
    return np.asarray(values, dtype=dtype)


def _array2D(values, dtype):
    if len(values) == 0:
        return np.zeros((0, 2), dtype=dtype)
    return np.asarray(values, dtype=dtype).reshape((-1, 2))
