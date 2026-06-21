from helpers import ids, geometry
from app import config

def plateRegions(platewise_grid, platewise_network):
    """
    This divides the plate polygon into regions that contain all points nearest to a given river segment.

    The significance of this is that it is done once per plate, so no work is needed pointwise to 
    find the nearest river segment before computing its distance and building the river fields.
    """
    world_polygon = _worldPolygon(platewise_grid[ids.polygon_id])
    segments = platewise_network[ids.segments_id]
    segment_polygons = []

    for segment in segments:
        region = _segmentRegion(segment, segments, world_polygon)
        if region is not None:
            segment_polygons.append(region)

    return {
        "world_polygon": world_polygon,
        "segments": segments,
        "segment_polygons": segment_polygons,
        }


def _worldPolygon(plate_polygon):
    """
    Scale up the polygon for world space.
    """
    return geometry.Polygon(points=tuple(
        geometry.Point(
            x=point.x / config.PLT_SCALE,
            y=point.y / config.PLT_SCALE,
        )
        for point in plate_polygon.points
    ))


def _segmentRegion(segment, segments, world_polygon):
    """
    Clip a polygon around the given segment. 
    
    This contains all points that are nearest to this segment.
    """
    segment_polygon = world_polygon
    midpoint = segment.midpoint()

    for other_segment in segments:
        if other_segment.id == segment.id:
            continue

        other_midpoint = other_segment.midpoint()
        if midpoint.distance_to_point(other_midpoint) <= config.RIVER_REGION_EPSILON:
            continue

        bisector_point = geometry.Segment(a=midpoint, b=other_midpoint).midpoint()
        bisector_norm = other_midpoint.vector_to_point(nbr_point=midpoint)

        segment_polygon = segment_polygon.clip_polygon(plane_point=bisector_point, normal_dir=bisector_norm)
        if len(segment_polygon.points) == 0:
            return None
    
    return {
        "segment_id": segment.id,
        "segment": segment,
        "polygon": segment_polygon,
        }
