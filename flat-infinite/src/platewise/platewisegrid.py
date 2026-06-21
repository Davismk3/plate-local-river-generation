import numpy as np
from app import config
import platewise.platewisegeometry as platewisegeometry
from pointwise import pointwiseheight

def plateGrid(
    plt_owner_idx,
    resolution=config.RIVER_GRID_RES,
    border_margin=config.RIVR_BORDER_DIST,
    ):
    polygon = platewisegeometry.geometricRepresentation(plt_owner_idx)
    resolution = max(2, int(resolution))

    heights = np.zeros((resolution, resolution), dtype=np.float64)
    valid_mask = np.zeros((resolution, resolution), dtype=bool)
    border_safe_mask = np.zeros((resolution, resolution), dtype=bool)
    lake_safe_mask = np.zeros((resolution, resolution), dtype=bool)
    world_points = np.zeros((resolution, resolution, 2), dtype=np.float64)
    plate_points = np.zeros((resolution, resolution, 2), dtype=np.float64)

    if len(polygon.points) < 3:
        return (
            plt_owner_idx,
            resolution,
            polygon,
            world_points,
            plate_points,
            heights,
            valid_mask,
            border_safe_mask,
            lake_safe_mask,
            )

    min_x = float(min(point.x for point in polygon.points))
    max_x = float(max(point.x for point in polygon.points))
    min_y = float(min(point.y for point in polygon.points))
    max_y = float(max(point.y for point in polygon.points))
    polygon_points = _polygonArray(polygon)

    # No Rivers & Lakes Near Plate Border
    border_margin_plate = border_margin * config.PLT_SCALE
    lake_margin_plate = (border_margin + config.RIVR_LAKE_RADIUS + config.RIVR_DIST_WIDTH) * config.PLT_SCALE

    for row in range(resolution):
        fy = 0.0 if resolution == 1 else row / (resolution - 1)
        plate_y = min_y + fy * (max_y - min_y)
        for col in range(resolution):
            fx = 0.0 if resolution == 1 else col / (resolution - 1)
            plate_x = min_x + fx * (max_x - min_x)

            world_x = plate_x / config.PLT_SCALE
            world_y = plate_y / config.PLT_SCALE

            plate_points[row, col, 0] = plate_x
            plate_points[row, col, 1] = plate_y
            world_points[row, col, 0] = world_x
            world_points[row, col, 1] = world_y

            # Avoid Outside Grid Points 
            if not _pointInPolygon(plate_x, plate_y, polygon_points):
                continue
            valid_mask[row, col] = True

            border_distance = _distanceToPolygonBorder(plate_x, plate_y, polygon_points)

            # Avoid Near-Border Grid Points 
            if border_distance >= border_margin_plate:
                border_safe_mask[row, col] = True
            if border_distance >= lake_margin_plate:
                lake_safe_mask[row, col] = True

            # Pre-River Height
            heights[row, col] = float(pointwiseheight.firstHeightField(world_x, world_y))
    
    platewise_grid = (
        plt_owner_idx,
        resolution,
        polygon,
        world_points,
        plate_points,
        heights,
        valid_mask,
        border_safe_mask,
        lake_safe_mask,
        )
    
    return platewise_grid


def _polygonArray(polygon):
    """The polygon object is bad for performance pointwise, even at the low-res for the plate grid, so it is converted to an array here."""
    return np.asarray(
        [(float(point.x), float(point.y)) for point in polygon.points],
        dtype=np.float64,
    )


def _pointInPolygon(x, y, polygon):
    """The polygon object is bad for performance pointwise, even at the low-res for the plate grid, so its methods are not used here."""
    inside = False
    prev = polygon[-1]

    for curr in polygon:
        x0, y0 = float(prev[0]), float(prev[1])
        x1, y1 = float(curr[0]), float(curr[1])
        if ((y0 > y) != (y1 > y)) and (
            x < (x1 - x0) * (y - y0) / (y1 - y0 + 1e-12) + x0
        ):
            inside = not inside
        prev = curr

    return inside


def _distanceToPolygonBorder(px, py, polygon):
    """
    Not using the 'geometry.py' module's 'border_distance_to_point()' method here since it was too slow.
    """
    best_sqrd = 1e30

    for idx in range(len(polygon)):
        ax, ay = float(polygon[idx][0]), float(polygon[idx][1])
        bx, by = float(polygon[(idx + 1) % len(polygon)][0]), float(
            polygon[(idx + 1) % len(polygon)][1]
        )
        abx = bx - ax
        aby = by - ay
        denom = abx * abx + aby * aby
        t = 0.0
        if denom > 1e-18:
            t = ((px - ax) * abx + (py - ay) * aby) / denom
            t = max(0.0, min(1.0, t))
        cx = ax + t * abx
        cy = ay + t * aby
        dx = px - cx
        dy = py - cy
        dist_sqrd = dx**2 + dy**2
        if dist_sqrd < best_sqrd:
            best_sqrd = dist_sqrd

    return best_sqrd**0.5
