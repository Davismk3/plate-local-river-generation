import plateownership
from app import config
from helpers import geometry

def geometricRepresentation(plt_owner_idx):
    eps = 1e-8

    owner_center_coords = plateownership.plateCenter(plt_owner_idx)
    owner_center = geometry.Point(
        x=float(owner_center_coords[0]),
        y=float(owner_center_coords[1]),
    )
    centers = _centersToConsider(owner_center, plt_owner_idx)
    polygon = _initialPolygon(owner_center, centers)

    for nbr_center in centers:
        if owner_center.distance_to_point(nbr_point=nbr_center) <= eps:
            continue
        midway_pos = geometry.Segment(a=owner_center, b=nbr_center).midpoint()
        normal_dir = nbr_center.vector_to_point(nbr_point=owner_center)
        
        polygon = polygon.clip_polygon(plane_point=midway_pos, normal_dir=normal_dir)

    return polygon


def _centersToConsider(owner_center, plt_owner_idx):
    ix, iy = plt_owner_idx
    centers = []

    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            center = plateownership.plateCenter((ix + dx, iy + dy))
            centers.append(geometry.Point(x=center[0], y=center[1]))

    centers[4] = owner_center

    return centers


def _initialPolygon(owner_center, centers):
    """
    Return a padded starting square that contains the local centers.
    """

    all_points = list(centers) + [owner_center]
    min_x = min(point.x for point in all_points) - config.PLATE_GEOMETRY_PADDING_CELLS
    max_x = max(point.x for point in all_points) + config.PLATE_GEOMETRY_PADDING_CELLS
    min_y = min(point.y for point in all_points) - config.PLATE_GEOMETRY_PADDING_CELLS
    max_y = max(point.y for point in all_points) + config.PLATE_GEOMETRY_PADDING_CELLS

    return geometry.Polygon(points=(
            geometry.Point(x=min_x, y=min_y), 
            geometry.Point(x=max_x, y=min_y),
            geometry.Point(x=max_x, y=max_y),
            geometry.Point(x=min_x, y=max_y),
            )
        )
