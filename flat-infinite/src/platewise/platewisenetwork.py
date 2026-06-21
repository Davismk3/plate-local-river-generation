import numpy as np
from app import config
from helpers import geometry, ids

RIVER_JOIN_DISTANCE_WEIGHT = 1e-6
DEFAULT_RIVER_COUNT = getattr(config, "RIVER_COUNT", 50)
DEFAULT_SOURCE_MIN_HEIGHT = getattr(config, "SOURCE_MIN_HEIGHT", 0.5)
DEFAULT_MIN_SOURCE_SPACING = getattr(config, "MIN_SOURCE_SPACING", 5)
DEFAULT_STEP_SIZE = getattr(config, "STEP_SIZE", 2)
DEFAULT_MAX_RIVER_STEPS = getattr(config, "MAX_RIVER_STEPS", 160)
SEA_LEVEL_FRACTION = getattr(config, "SEA_LEVEL_FRACTION", 0.35)
RIVER_MIN_DROP = getattr(config, "RIVER_MIN_DROP", 1e-6)
RIVER_PATH_DISTANCE_WEIGHT = getattr(config, "RIVER_PATH_DISTANCE_WEIGHT", 1e-5)
RIVR_HGHT_SLOPE_DROP = getattr(config, "RIVR_HGHT_SLOPE_DROP", 0.005)


def plateNetwork(
    platewise_grid,
    river_count=DEFAULT_RIVER_COUNT,
    source_min_height=DEFAULT_SOURCE_MIN_HEIGHT,
    min_source_spacing=DEFAULT_MIN_SOURCE_SPACING,
    step_size=DEFAULT_STEP_SIZE,
    max_steps=DEFAULT_MAX_RIVER_STEPS,
    ):
    """
    Build a plate-local river network.

    The network stores river channels as geometry.Segment objects. Segment
    endpoints are geometry.Point instances with river-node metadata attached:
    id, type, pixel, plate_point, and height.
    """
    river_count = max(0, int(river_count))
    step_size = max(1, int(step_size))
    max_steps = max(1, int(max_steps))

    heights = platewise_grid[ids.heights_id]
    valid_mask = platewise_grid[ids.valid_mask_id]
    border_safe_mask = platewise_grid[ids.border_safe_mask_id]
    lake_safe_mask = platewise_grid[ids.lake_safe_mask_id]

    nodes = []
    segments = []
    paths = []
    node_by_pixel = {}

    candidate_source_pixels = _sourcePixels(
        heights,
        valid_mask,
        border_safe_mask,
        river_count,
        source_min_height,
        min_source_spacing,
    )
    source_pixels = []

    for source_pixel in candidate_source_pixels:
        node_start = len(nodes)
        segment_start = len(segments)
        path = []
        path_seen = set()
        previous_river_nodes = set(node_by_pixel.values())
        pixel = source_pixel
        source_node = _nodeForPixel(pixel, "source", nodes, node_by_pixel, platewise_grid)
        path.append(source_node)
        path_seen.add(pixel)

        for _ in range(max_steps):
            if heights[pixel] <= SEA_LEVEL_FRACTION:
                _setNodeType(path[-1], "outlet_sea", nodes)
                break

            existing_node = _nearbyDownhillRiverNode(
                pixel,
                previous_river_nodes,
                nodes,
                heights,
                step_size,
                path[-1],
                segments,
            )
            if existing_node is not None:
                _addSegment(path[-1], existing_node, segments, nodes)
                path.append(existing_node)
                break

            next_pixel = _downhillNeighbor(
                pixel,
                heights,
                valid_mask,
                border_safe_mask,
                step_size,
                path[-1],
                segments,
                nodes,
                node_by_pixel,
            )
            if next_pixel is None:
                if _hasUnsafeDownhillNeighbor(
                    pixel,
                    heights,
                    valid_mask,
                    border_safe_mask,
                    step_size,
                ):
                    _rollbackRiver(
                        node_start,
                        segment_start,
                        nodes,
                        segments,
                        node_by_pixel,
                    )
                    path = []
                    break
                if not lake_safe_mask[pixel]:
                    _rollbackRiver(
                        node_start,
                        segment_start,
                        nodes,
                        segments,
                        node_by_pixel,
                    )
                    path = []
                    break
                _setNodeType(path[-1], "outlet_local_minimum", nodes)
                break

            joins_existing_path = next_pixel in node_by_pixel
            next_type = (
                "outlet_sea"
                if heights[next_pixel] <= SEA_LEVEL_FRACTION
                else "channel"
            )
            next_node = _nodeForPixel(next_pixel, next_type, nodes, node_by_pixel, platewise_grid)
            _addSegment(path[-1], next_node, segments, nodes)
            path.append(next_node)

            if next_type == "outlet_sea":
                break
            if next_pixel in path_seen:
                if not lake_safe_mask[next_pixel]:
                    _rollbackRiver(
                        node_start,
                        segment_start,
                        nodes,
                        segments,
                        node_by_pixel,
                    )
                    path = []
                    break
                _setNodeType(path[-1], "outlet_local_minimum", nodes)
                break
            if joins_existing_path and len(path) >= 2 and next_node != path[-2]:
                break

            path_seen.add(next_pixel)
            pixel = next_pixel

        if len(path) > 0:
            source_pixels.append(source_pixel)
            paths.append(path)

    _applyRiverHeightSlopeDrop(nodes, segments, paths)

    plate_network = (
        platewise_grid[ids.plt_owner_idx_id],
        platewise_grid[ids.resolution_id],
        river_count,
        source_pixels,
        nodes,
        segments,
        paths,
        )

    return plate_network


def _sourcePixels(
    heights,
    valid_mask,
    source_safe_mask,
    river_count,
    source_min_height,
    min_source_spacing,
):
    candidates = []
    source_min_height = max(float(source_min_height), float(SEA_LEVEL_FRACTION))

    for row in range(heights.shape[0]):
        for col in range(heights.shape[1]):
            if not valid_mask[row, col] or not source_safe_mask[row, col]:
                continue
            height = float(heights[row, col])
            if height < source_min_height:
                continue
            candidates.append((height, row, col))

    candidates.sort(reverse=True)
    selected = []
    spacing_sqrd = float(min_source_spacing) ** 2

    for _, row, col in candidates:
        if len(selected) >= river_count:
            break
        too_close = False
        for selected_row, selected_col in selected:
            drow = row - selected_row
            dcol = col - selected_col
            if drow * drow + dcol * dcol < spacing_sqrd:
                too_close = True
                break
        if not too_close:
            selected.append((int(row), int(col)))

    return selected


def _downhillNeighbor(
    pixel,
    heights,
    valid_mask,
    route_safe_mask,
    step_size,
    from_node,
    segments,
    nodes,
    node_by_pixel,
):
    row, col = pixel
    current_height = float(heights[row, col])
    best_pixel = None
    best_score = current_height

    min_row = max(0, row - step_size)
    max_row = min(heights.shape[0] - 1, row + step_size)
    min_col = max(0, col - step_size)
    max_col = min(heights.shape[1] - 1, col + step_size)

    for next_row in range(min_row, max_row + 1):
        for next_col in range(min_col, max_col + 1):
            if next_row == row and next_col == col:
                continue
            if not valid_mask[next_row, next_col] or not route_safe_mask[
                next_row, next_col
            ]:
                continue
            next_height = float(heights[next_row, next_col])
            if next_height >= current_height - RIVER_MIN_DROP:
                continue
            to_node = node_by_pixel.get((int(next_row), int(next_col)))
            if _wouldCrossExistingSegments(
                from_node,
                (int(next_row), int(next_col)),
                segments,
                nodes,
                to_node,
            ):
                continue
            drow = next_row - row
            dcol = next_col - col
            score = next_height + RIVER_PATH_DISTANCE_WEIGHT * (
                drow * drow + dcol * dcol
            )
            if score < best_score:
                best_score = score
                best_pixel = (int(next_row), int(next_col))

    return best_pixel


def _hasUnsafeDownhillNeighbor(pixel, heights, valid_mask, route_safe_mask, step_size):
    row, col = pixel
    current_height = float(heights[row, col])

    min_row = max(0, row - step_size)
    max_row = min(heights.shape[0] - 1, row + step_size)
    min_col = max(0, col - step_size)
    max_col = min(heights.shape[1] - 1, col + step_size)

    for next_row in range(min_row, max_row + 1):
        for next_col in range(min_col, max_col + 1):
            if next_row == row and next_col == col:
                continue
            if not valid_mask[next_row, next_col]:
                continue
            if route_safe_mask[next_row, next_col]:
                continue
            if (
                float(heights[next_row, next_col])
                < current_height - RIVER_MIN_DROP
            ):
                return True

    return False


def _nearbyDownhillRiverNode(
    pixel, previous_river_nodes, nodes, heights, step_size, from_node, segments
):
    if len(previous_river_nodes) == 0:
        return None

    row, col = pixel
    current_height = float(heights[row, col])
    best_node = None
    best_score = 1e30
    max_dist_sqrd = step_size * step_size

    for node_id in previous_river_nodes:
        if int(node_id) == int(from_node):
            continue
        node = nodes[int(node_id)]
        if node.type == "source":
            continue
        node_row, node_col = node.pixel
        drow = node_row - row
        dcol = node_col - col
        dist_sqrd = drow * drow + dcol * dcol
        if dist_sqrd == 0 or dist_sqrd > max_dist_sqrd:
            continue
        if float(node.height) >= current_height - RIVER_MIN_DROP:
            continue
        if _segmentExists(from_node, int(node_id), segments):
            continue
        if _wouldCrossExistingSegments(
            from_node,
            node.pixel,
            segments,
            nodes,
            int(node_id),
        ):
            continue
        score = float(node.height) + RIVER_JOIN_DISTANCE_WEIGHT * dist_sqrd
        if score < best_score:
            best_score = score
            best_node = int(node_id)

    return best_node


def _nodeForPixel(pixel, node_type, nodes, node_by_pixel, platewise_grid):
    if pixel in node_by_pixel:
        node_id = node_by_pixel[pixel]
        if node_type.startswith("outlet"):
            _setNodeType(node_id, node_type, nodes)
        return node_id

    row, col = pixel
    world_points = platewise_grid[ids.world_points_id]
    plate_points = platewise_grid[ids.plate_points_id]
    heights = platewise_grid[ids.heights_id]
    node_id = len(nodes)
    node = geometry.Point(
        x=float(world_points[row, col, 0]),
        y=float(world_points[row, col, 1]),
    )
    node.id = node_id
    node.type = node_type
    node.pixel = (int(row), int(col))
    node.plate_point = geometry.Point(
        x=float(plate_points[row, col, 0]),
        y=float(plate_points[row, col, 1]),
    )
    node.height = float(heights[row, col])
    node.river_height = node.height

    nodes.append(node)
    node_by_pixel[pixel] = node_id
    return node_id


def _addSegment(from_node, to_node, segments, nodes):
    if from_node == to_node or _segmentExists(from_node, to_node, segments):
        return

    from_point = nodes[int(from_node)]
    to_point = nodes[int(to_node)]
    segment = geometry.Segment(a=from_point, b=to_point)
    segment.id = len(segments)
    segment.from_node = int(from_node)
    segment.to_node = int(to_node)
    segment.from_pixel = from_point.pixel
    segment.to_pixel = to_point.pixel
    segment.from_height = float(from_point.height)
    segment.to_height = float(to_point.height)
    segments.append(segment)


def _rollbackRiver(node_start, segment_start, nodes, segments, node_by_pixel):
    del segments[int(segment_start) :]
    for node in nodes[int(node_start) :]:
        node_by_pixel.pop(node.pixel, None)
    del nodes[int(node_start) :]


def _segmentExists(from_node, to_node, segments):
    for segment in segments:
        if int(segment.from_node) == int(from_node) and int(segment.to_node) == int(
            to_node
        ):
            return True
        if int(segment.from_node) == int(to_node) and int(segment.to_node) == int(
            from_node
        ):
            return True
    return False


def _setNodeType(node_id, node_type, nodes):
    node = nodes[int(node_id)]
    if node.type == "source" and node_type != "outlet_sea":
        return
    node.type = node_type


def _wouldCrossExistingSegments(from_node, to_pixel, segments, nodes, to_node=None):
    from_pixel = nodes[int(from_node)].pixel

    for segment in segments:
        segment_from = int(segment.from_node)
        segment_to = int(segment.to_node)
        if segment_from == int(from_node) or segment_to == int(from_node):
            continue
        if to_node is not None and (
            segment_from == int(to_node) or segment_to == int(to_node)
        ):
            continue

        if _segmentsCross(
            from_pixel,
            to_pixel,
            nodes[segment_from].pixel,
            nodes[segment_to].pixel,
        ):
            return True

    return False


def _segmentsCross(a, b, c, d):
    if _samePixel(a, b) or _samePixel(c, d):
        return False
    if _samePixel(a, c) or _samePixel(a, d) or _samePixel(b, c) or _samePixel(b, d):
        return False

    o1 = _orientation(a, b, c)
    o2 = _orientation(a, b, d)
    o3 = _orientation(c, d, a)
    o4 = _orientation(c, d, b)

    if o1 == 0 and _onSegment(a, c, b):
        return True
    if o2 == 0 and _onSegment(a, d, b):
        return True
    if o3 == 0 and _onSegment(c, a, d):
        return True
    if o4 == 0 and _onSegment(c, b, d):
        return True

    return ((o1 > 0) != (o2 > 0)) and ((o3 > 0) != (o4 > 0))


def _orientation(a, b, c):
    return (int(b[0]) - int(a[0])) * (int(c[1]) - int(a[1])) - (
        int(b[1]) - int(a[1])
    ) * (int(c[0]) - int(a[0]))


def _onSegment(a, p, b):
    return (
        min(int(a[0]), int(b[0])) <= int(p[0]) <= max(int(a[0]), int(b[0]))
        and min(int(a[1]), int(b[1])) <= int(p[1]) <= max(int(a[1]), int(b[1]))
    )


def _samePixel(a, b):
    return int(a[0]) == int(b[0]) and int(a[1]) == int(b[1])


def _applyRiverHeightSlopeDrop(nodes, segments, paths):
    drop = max(0.0, float(RIVR_HGHT_SLOPE_DROP))
    if len(nodes) == 0 or len(segments) == 0:
        return

    segment_by_nodes = {}
    for segment in segments:
        segment_by_nodes[(int(segment.from_node), int(segment.to_node))] = segment

    for path in paths:
        if len(path) < 2:
            continue

        source_id = int(path[0])
        outlet_id = int(path[-1])
        source_raw = float(nodes[source_id].height)
        outlet_raw = float(nodes[outlet_id].height)
        outlet_height = float(getattr(nodes[outlet_id], "river_height", outlet_raw))
        raw_span = source_raw - outlet_raw

        if raw_span <= RIVER_MIN_DROP:
            source_height = outlet_height
        else:
            source_height = source_raw - drop
            if source_height < outlet_height + RIVER_MIN_DROP:
                source_height = outlet_height + RIVER_MIN_DROP

        adjusted_span = source_height - outlet_height
        path_heights = {}

        for node_id_raw in path:
            node_id = int(node_id_raw)
            raw_height = float(nodes[node_id].height)
            if raw_span <= RIVER_MIN_DROP:
                river_height = outlet_height
            else:
                t = (raw_height - outlet_raw) / raw_span
                t = max(0.0, min(1.0, t))
                river_height = outlet_height + adjusted_span * t

            nodes[node_id].river_height = float(river_height)
            path_heights[node_id] = float(river_height)

        for idx in range(len(path) - 1):
            from_id = int(path[idx])
            to_id = int(path[idx + 1])
            segment = segment_by_nodes.get((from_id, to_id))
            if segment is None:
                continue
            segment.from_height = path_heights[from_id]
            segment.to_height = path_heights[to_id]
