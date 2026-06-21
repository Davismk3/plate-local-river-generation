from app import config
from numba import njit
from helpers import noise
from pointwise import pointwisefields, pointwiseplatefields


@njit(fastmath=True, cache=config.CACHING)
def firstHeightField(x, y):
    """
    Base height field sampled by the low-resolution river construction grid.

    BASE_TERRAIN_MOUNTAIN_SCALE keeps mountains and plateaus lower than in the
    final terrain, so river paths can blend below the final high-relief surface.
    """
    crust_weight, islands_weight, land_weight, mountains_weight, plateau_weight, drift_scalar, drift_vector = (
        pointwisefields.pointwiseRepresentation(x, y)
    )

    height = (
        crust_weight
        + land_weight * config.BASE_TERRAIN_LAND_SCALE
        + (mountains_weight + plateau_weight + islands_weight) * config.BASE_TERRAIN_MOUNTAIN_SCALE
    )

    return height


@njit(fastmath=True, cache=config.CACHING)
def finalHeightField(x, y, packed_river_cache=None):
    """
    Final terrain height, optionally blended with packed river fields.

    This sample uses crust, land, mountain, and plateau weights for height.
    """

    crust_weight, islands_weight, land_weight, mountains_weight, plateau_weight, drift_scalar, drift_vector = (
        pointwisefields.pointwiseRepresentation(x, y)
    )

    height = (
        crust_weight
        + land_weight
        + mountains_weight
        + plateau_weight
        + islands_weight * (noise.basicBrownianNoise(x, y, octaves=1, scale=0.01) + 1) / 2  # The island chains look best with noise.
    )
    if packed_river_cache is None:
        return height

    river_height, river_distance = pointwiseplatefields.riverFields(
        x,
        y,
        packed_river_cache,
    )

    # The river distance field is normalized so values >= 1 are outside the
    # river influence area.
    if river_distance >= 1.0:
        return height

    # Blend river height back to terrain height as distance from the river grows.
    terrain_weight = river_distance**config.RIVER_BLEND_EXP
    return height * terrain_weight + river_height * (1.0 - terrain_weight)
