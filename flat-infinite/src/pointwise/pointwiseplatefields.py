from helpers import geometry, mathutil
from platewise import platewiseregions
from app import config
from numba import njit
import numpy as np
from pointwise import pointwisefields


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


@njit(fastmath=True, cache=config.CACHING)
def riverFields(x, y, packed_cache):
    plt_owner_idx = pointwisefields.plateOwnerIndex(x, y)
    plate_slot = _plateSlot(plt_owner_idx[0], plt_owner_idx[1], packed_cache)
    if plate_slot < 0:
        return 0.0, 1.0

    region_id = _nearestRiverRegionId(plate_slot, x, y, packed_cache)
    if region_id >= 0:
        region_candidate_starts = packed_cache[REGION_CANDIDATE_STARTS_IDX]
        return _riverFieldsFromSegmentIndexRange(
            region_candidate_starts[region_id],
            region_candidate_starts[region_id + 1],
            True,
            x,
            y,
            packed_cache,
        )

    plate_segment_starts = packed_cache[PLATE_SEGMENT_STARTS_IDX]
    return _riverFieldsFromSegmentIndexRange(
        plate_segment_starts[plate_slot],
        plate_segment_starts[plate_slot + 1],
        False,
        x,
        y,
        packed_cache,
    )


@njit(fastmath=True, cache=config.CACHING)
def _riverFieldsFromSegmentIndexRange(start, end, use_candidate_ids, x, y, packed_cache):
    if start >= end:
        return 0.0, 1.0

    best_dist = 1e30
    best_height = 0.0
    lake_surface_dist = 1e30
    lake_height = 0.0
    zero_height_sum = 0.0
    zero_count = 0
    height_weight_sum = 0.0
    weight_sum = 0.0
    candidate_ids = packed_cache[REGION_CANDIDATE_SEGMENT_IDS_IDX]

    for idx in range(start, end):
        segment_id = idx
        if use_candidate_ids:
            segment_id = candidate_ids[idx]

        height, dist, segment_lake_surface_dist = _riverSegmentHeightAndDistance(
            segment_id, x, y, packed_cache
        )
        if dist < best_dist:
            best_dist = dist
            best_height = height

        if segment_lake_surface_dist <= 0.0:
            if lake_surface_dist > 0.0 or segment_lake_surface_dist < lake_surface_dist:
                lake_surface_dist = segment_lake_surface_dist
                lake_height = height
        elif dist <= 1e-12:
            zero_height_sum += height
            zero_count += 1
        elif dist < config.RIVER_HEIGHT_BLEND_DISTANCE:
            fade = 1.0 - mathutil.smoothStep(
                dist / (config.RIVER_HEIGHT_BLEND_DISTANCE + 1e-12)
            )
            weight = (fade * fade) / ((dist + 1e-9) * (dist + 1e-9))
            height_weight_sum += height * weight
            weight_sum += weight

    if lake_surface_dist <= 0.0:
        river_height_field = lake_height
    elif zero_count > 0:
        river_height_field = zero_height_sum / zero_count
    elif weight_sum > 0.0:
        river_height_field = height_weight_sum / weight_sum
    else:
        river_height_field = best_height

    river_distance_field = _riverDistanceFieldFromDistance(best_dist)
    return river_height_field, river_distance_field


@njit(fastmath=True, cache=config.CACHING)
def _plateSlot(owner_x, owner_y, packed_cache):
    plate_owner_indices = packed_cache[PLATE_OWNER_INDICES_IDX]
    for idx in range(plate_owner_indices.shape[0]):
        if (
            plate_owner_indices[idx, 0] == owner_x
            and plate_owner_indices[idx, 1] == owner_y
        ):
            return idx
    return -1


@njit(fastmath=True, cache=config.CACHING)
def _nearestRiverRegionId(plate_slot, x, y, packed_cache):
    plate_region_starts = packed_cache[PLATE_REGION_STARTS_IDX]
    start = plate_region_starts[plate_slot]
    end = plate_region_starts[plate_slot + 1]

    for region_id in range(start, end):
        if _pointInRegion(region_id, x, y, packed_cache):
            return region_id
    return -1


@njit(fastmath=True, cache=config.CACHING)
def _pointInRegion(region_id, x, y, packed_cache):
    region_plane_starts = packed_cache[REGION_PLANE_STARTS_IDX]
    plane_points = packed_cache[PLANE_POINTS_IDX]
    plane_normals = packed_cache[PLANE_NORMALS_IDX]
    start = region_plane_starts[region_id]
    end = region_plane_starts[region_id + 1]

    for plane_id in range(start, end):
        dx = x - plane_points[plane_id, 0]
        dy = y - plane_points[plane_id, 1]
        dot = dx * plane_normals[plane_id, 0] + dy * plane_normals[plane_id, 1]
        if dot < -config.RIVER_POINT_IN_REGION_EPSILON:
            return False
    return True


@njit(fastmath=True, cache=config.CACHING)
def _riverSegmentHeightAndDistance(segment_id, x, y, packed_cache):
    segment_from_heights = packed_cache[SEGMENT_FROM_HEIGHTS_IDX]
    segment_to_heights = packed_cache[SEGMENT_TO_HEIGHTS_IDX]
    t, dist_sqrd = _segmentParameterAndDistanceSquared(segment_id, x, y, packed_cache)

    height_from = segment_from_heights[segment_id]
    height_to = segment_to_heights[segment_id]
    river_height = height_from + (height_to - height_from) * t

    return _lakeAdjustedSegmentFields(
        segment_id,
        x,
        y,
        np.sqrt(dist_sqrd),
        river_height,
        height_from,
        height_to,
        packed_cache,
    )


@njit(fastmath=True, cache=config.CACHING)
def _riverDistanceFieldFromDistance(dist):
    if dist <= 0.0:
        return 0.0
    if dist >= config.RIVR_DIST_WIDTH:
        return 1.0
    return (
        dist / (config.RIVR_DIST_WIDTH + 1e-12)
    )**config.RIVER_DISTANCE_FIELD_POWER


@njit(fastmath=True, cache=config.CACHING)
def _lakeAdjustedSegmentFields(
    segment_id,
    x,
    y,
    segment_dist,
    segment_height,
    lake_height_from,
    lake_height_to,
    packed_cache,
    ):
    segment_from_lake_nodes = packed_cache[SEGMENT_FROM_LAKE_NODES_IDX]
    segment_to_lake_nodes = packed_cache[SEGMENT_TO_LAKE_NODES_IDX]
    segment_from_points = packed_cache[SEGMENT_FROM_POINTS_IDX]
    segment_to_points = packed_cache[SEGMENT_TO_POINTS_IDX]
    dist = segment_dist
    height = segment_height
    lake_surface_dist = 1e30

    if segment_from_lake_nodes[segment_id]:
        dx = x - segment_from_points[segment_id, 0]
        dy = y - segment_from_points[segment_id, 1]
        node_dist = np.sqrt(dx**2 + dy**2)
        lake_dist = node_dist - config.RIVR_LAKE_RADIUS
        surface_dist = lake_dist - config.RIVER_LAKE_SURFACE_MARGIN_DISTANCE
        if lake_dist < dist:
            dist = lake_dist
        if surface_dist <= 0.0:
            if surface_dist < lake_surface_dist:
                height = lake_height_from
                lake_surface_dist = surface_dist
        elif surface_dist < config.RIVER_LAKE_NODE_HEIGHT_FADE_DISTANCE:
            fade = 1.0 - mathutil.smoothStep(
                surface_dist / (config.RIVER_LAKE_NODE_HEIGHT_FADE_DISTANCE + 1e-12)
            )
            height = height * (1.0 - fade) + lake_height_from * fade

    if segment_to_lake_nodes[segment_id]:
        dx = x - segment_to_points[segment_id, 0]
        dy = y - segment_to_points[segment_id, 1]
        node_dist = np.sqrt(dx**2 + dy**2)
        lake_dist = node_dist - config.RIVR_LAKE_RADIUS
        surface_dist = lake_dist - config.RIVER_LAKE_SURFACE_MARGIN_DISTANCE
        if lake_dist < dist:
            dist = lake_dist
        if surface_dist <= 0.0:
            if surface_dist < lake_surface_dist:
                height = lake_height_to
                lake_surface_dist = surface_dist
        elif surface_dist < config.RIVER_LAKE_NODE_HEIGHT_FADE_DISTANCE:
            fade = 1.0 - mathutil.smoothStep(
                surface_dist / (config.RIVER_LAKE_NODE_HEIGHT_FADE_DISTANCE + 1e-12)
            )
            height = height * (1.0 - fade) + lake_height_to * fade

    return height, dist, lake_surface_dist


@njit(fastmath=True, cache=config.CACHING)
def _segmentParameterAndDistanceSquared(segment_id, x, y, packed_cache):
    from_points = packed_cache[SEGMENT_FROM_POINTS_IDX]
    to_points = packed_cache[SEGMENT_TO_POINTS_IDX]
    ax = from_points[segment_id, 0]
    ay = from_points[segment_id, 1]
    bx = to_points[segment_id, 0]
    by = to_points[segment_id, 1]
    abx = bx - ax
    aby = by - ay
    ab_len_sqrd = abx * abx + aby * aby

    if ab_len_sqrd <= 1e-18:
        t = 0.0
    else:
        t = ((x - ax) * abx + (y - ay) * aby) / ab_len_sqrd
        if t < 0.0:
            t = 0.0
        elif t > 1.0:
            t = 1.0

    cx = ax + abx * t
    cy = ay + aby * t
    dx = x - cx
    dy = y - cy
    return t, dx * dx + dy * dy


def plateRiverFields(x, y, platewise_regions):
    point = geometry.Point(x=x, y=y)

    candidate_segments = _candidateSegments(point, platewise_regions)
    return _riverFieldsFromSegments(point, candidate_segments)


def _candidateSegments(point, segment_polygons):
    for region in segment_polygons["segment_polygons"]:
        if region["polygon"].point_inside(point):
            return [
                region["segment"]
                ]
    
    # All Segments Fallback
    return segment_polygons["segments"]
    

def _riverFieldsFromSegments(point, segments):
    if not segments:
        return 0.0, 1.0

    best_distance = 1e30
    best_height = 0.0

    for segment in segments:
        height, distance = _segmentHeightAndDistance(point, segment)

        if distance < best_distance:
            best_distance = distance
            best_height = height

    return best_height, _riverDistanceField(best_distance)


def _segmentHeightAndDistance(point, segment):
    """
    Linear (or otherwise if you want) interpolation for height between segment nodes. 
    """
    t = segment.t_param_for_point(nbr_point=point)

    height_from = segment.from_height
    height_to = segment.to_height
    height = height_from + (height_to - height_from) * t

    distance = segment.distance_to_point(point)

    return height, distance


def _riverDistanceField(distance):
    width = config.RIVR_DIST_WIDTH

    if distance <= 0.0:
        return 0.0
    if distance >= width:
        return 1.0

    return (distance / (width + 1e-12))**config.RIVER_DISTANCE_FIELD_POWER
