import numpy as np
from numba import njit
from app import config
from helpers import mathutil as mathhelper
from helpers import noise
import plateownership

"""
This script holds the core logic for generating the continental landmasses.
"""

@njit(fastmath=True, cache=config.CACHING)
def plateOwnerIndex(x, y):
    """
    Return the nearest plate owner index for a world-space point.

    This is the owner-selection part of pointwiseRepresentation, exposed so
    cache and visualizer code do not need to duplicate it in pure Python.
    """
    plt_nbr_number = 8

    plt_bdr_shape = config.PLT_BDR_SHAPE * config.PLT_SCALE / config.PLT_REFERENCE_SCALE
    warp_x = (
        noise.basicBrownianNoise(
            x,
            y,
            scale=plt_bdr_shape,
            octaves=config.PLT_ROUGHNESS,
            seed=config.SEED + 1,
        )
        * config.PLT_STRETCHING
    )
    warp_y = (
        noise.basicBrownianNoise(
            x,
            y,
            scale=plt_bdr_shape,
            octaves=config.PLT_ROUGHNESS,
            seed=config.SEED + 2,
        )
        * config.PLT_STRETCHING
    )
    plt_x = x * config.PLT_SCALE + warp_x
    plt_y = y * config.PLT_SCALE + warp_y

    plt_ix = np.int32(np.floor(plt_x))
    plt_iy = np.int32(np.floor(plt_y))
    plt_owner_ix = plt_ix
    plt_owner_iy = plt_iy
    best_dist = 1e18

    for dy in (-1, 0, 1):
        niy = plt_iy + dy
        for dx in (-1, 0, 1):
            nix = plt_ix + dx
            plt_center_x, plt_center_y = plateownership.plateCenter((nix, niy))
            ddx = plt_x - plt_center_x
            ddy = plt_y - plt_center_y
            plt_center_dist = ddx**2 + ddy**2

            if plt_center_dist < best_dist:
                best_dist = plt_center_dist
                plt_owner_ix = nix
                plt_owner_iy = niy

    return plt_owner_ix, plt_owner_iy


@njit(fastmath=True, cache=config.CACHING)
def pointwiseRepresentation(x, y):
    """
    the pointwise representation is the true world fields used for terrain composition.
    """

    # Finite Neighbor Count To Consider 
    plt_nbr_number = 8 # 8 is natural for 3x3 indexed neighbors.

    # Apply Pointwise Roughness
    plt_bdr_shape = config.PLT_BDR_SHAPE * config.PLT_SCALE / config.PLT_REFERENCE_SCALE
    warp_x = (
        noise.basicBrownianNoise(
            x,
            y,
            scale=plt_bdr_shape,
            octaves=config.PLT_ROUGHNESS,
            seed=config.SEED + 1,
        )
        * config.PLT_STRETCHING
    )
    warp_y = (
        noise.basicBrownianNoise(
            x,
            y,
            scale=plt_bdr_shape,
            octaves=config.PLT_ROUGHNESS,
            seed=config.SEED + 2,
        )
        * config.PLT_STRETCHING
    )
    plt_x = x * config.PLT_SCALE + warp_x
    plt_y = y * config.PLT_SCALE + warp_y

    # Floored Integer Lattice
    plt_ix = np.int32(np.floor(plt_x))
    plt_iy = np.int32(np.floor(plt_y))

    # Numba-Friendly Initialized Lists
    plt_dist_list = np.full(plt_nbr_number + 1, 1e18)
    plt_center_list = np.zeros((plt_nbr_number + 1, 2), dtype=np.float64)
    plt_drift_dir_list = np.zeros((plt_nbr_number + 1, 2), dtype=np.float64)
    plt_type_list = np.zeros(plt_nbr_number + 1, dtype=np.int8)

    # Deterministic Owner & Neighbor Plate Data Fill Algorithm
    for dy in (-1, 0, 1):
        niy = plt_iy + dy
        for dx in (-1, 0, 1):
            nix = plt_ix + dx

            plt_center_x, plt_center_y = plateownership.plateCenter((nix, niy))

            ddx = plt_x - plt_center_x
            ddy = plt_y - plt_center_y
            plt_center_dist = np.sqrt(ddx**2 + ddy**2)

            if plt_center_dist < plt_dist_list[plt_nbr_number]:
                i = plt_nbr_number
                while i > 0 and plt_center_dist < plt_dist_list[i - 1]:
                    plt_dist_list[i] = plt_dist_list[i - 1]
                    plt_center_list[i, 0] = plt_center_list[i - 1, 0]
                    plt_center_list[i, 1] = plt_center_list[i - 1, 1]
                    plt_drift_dir_list[i] = plt_drift_dir_list[i - 1]
                    plt_type_list[i] = plt_type_list[i - 1]
                    i -= 1

                plt_dist_list[i] = plt_center_dist
                plt_center_list[i, 0] = plt_center_x
                plt_center_list[i, 1] = plt_center_y
                plt_drift_dir_list[i] = _plateDrift(nix, niy)
                plt_type_list[i] = _plateType(nix, niy)

    # Initialize Weight Fields
    crust_weight = config.OCN_HGT
    islands_weight = 0.0
    land_weight = 0.0
    mountains_weight = 0.0
    plateau_weight = 0.0

    # Owner Data
    owner_center_x = plt_center_list[0, 0]
    owner_center_y = plt_center_list[0, 1]
    drift_vector = plt_drift_dir_list[0]

    # Drift Scalar = (Position Vector) • (Drift Vector)
    drift_scalar = (plt_x - owner_center_x) * drift_vector[0] + (
        plt_y - owner_center_y
    ) * drift_vector[1]

    # Neighbor Plate Distance
    border_dist = _plateDistance(
        plt_type_list,
        plt_dist_list,
        config.OCEANIC_PLATE_ID,
        config.CONTINENTAL_PLATE_ID,
    )
    continental_dist = _plateDistance(
        plt_type_list, plt_dist_list, config.CONTINENTAL_PLATE_ID, -1
    )

    # Owner Plate Type 
    owner_type = plt_type_list[0]

    # Oceanic Plate Fields
    if owner_type == config.OCEANIC_PLATE_ID:
        if continental_dist < 1e17:
            
            # Blend To Continental Plate Height
            blend = 1.0 - continental_dist / (config.OCN_BLD_WIDTH + 1e-8)
            crust_weight = config.OCN_HGT + (
                config.CONT_HGT - config.OCN_HGT
            ) * mathhelper.positivePow(blend, config.OCN_BLD_EXP)

    # Continental Plate Fields
    elif owner_type == config.CONTINENTAL_PLATE_ID:
        crust_weight = config.CONT_HGT

        # Land Weight Field
        if border_dist <= config.LAND_WIDTH:
            land_weight = (
                config.LAND_AMP
                * _skewWeight(
                    border_dist,
                    config.LAND_WIDTH,
                    config.LAND_INT_SKEW,
                    config.LAND_BDR_SKEW,
                )
                * _driftCoverage(drift_scalar, config.LAND_PLT_COVERAGE)
            )

        # Island Chain Weight Field
        if border_dist <= config.ISLS_WIDTH:
            islands_weight = (
                config.ISLS_AMP
                * _skewWeight(
                    border_dist,
                    config.ISLS_WIDTH,
                    config.ISLS_INT_SKEW,
                    config.ISLS_BDR_SKEW,
                )
                * _driftCoverage(drift_scalar, config.ISLS_PLT_COVERAGE)
            )

        # Mountain Range Weight Fields
        if border_dist <= config.MNTS_WIDTH:
            mountains_weight = (
                config.MNTS_AMP
                * _skewWeight(
                    border_dist,
                    config.MNTS_WIDTH,
                    config.MNTS_INT_SKEW,
                    config.MNTS_BDR_SKEW,
                )
                * _driftCoverage(drift_scalar, config.MNTS_PLT_COVERAGE)
            )

        # Plateau Weight Field
        plateau_weight = (
            config.PLTU_AMP
            * mathhelper.smoothStep(
                4.0  # 4.0 was chosen qualitatively
                * _plateauWeight(
                    plt_drift_dir_list, plt_type_list, plt_center_list, plt_dist_list
                )
            )
            * max(0.0, config.LAND_AMP - land_weight)
            * max(0.0, config.MNTS_AMP - mountains_weight)
        )

        # Island Chain Extra Handling
        drift_start = 1.0 - 2.0 * config.LAND_PLT_COVERAGE + config.SEA_LEVEL_FRACTION
        drift_length = (
            2.0 * (config.ISLS_PLT_COVERAGE - config.LAND_PLT_COVERAGE) + 1e-8
        )
        islands_fade = max(
            0.0,
            1.0
            + config.SEA_LEVEL_FRACTION
            - (drift_scalar - drift_start) / drift_length,
        )
        islands_weight *= islands_fade * (1.0 - min(1.0, plateau_weight))

    return (
        crust_weight,
        islands_weight,
        land_weight,
        mountains_weight,
        plateau_weight,
        drift_scalar,
        drift_vector,
        )


@njit(fastmath=True, cache=config.CACHING)
def _plateDistance(plt_type_list, plt_dist_list, plate_type_a, plate_type_b):
    """Approximate sample distance to the nearest selected plate-type border."""
    for i in range(1, np.size(plt_type_list)):
        plate_type = plt_type_list[i]
        if plate_type == plate_type_a or plate_type == plate_type_b:
            return plt_dist_list[i] - plt_dist_list[0]
    return 1e18


@njit(fastmath=True, nogil=True, cache=config.CACHING)
def _plateType(nix, niy):
    """
    Deterministic plate type. 

    More plate or no plate types can be used. 
    
    This is used to later determine which plates get continents or just sparse islands.
    """
    plt_hash = (noise.hash11(nix, niy, config.SEED + 2001) + 1) / 2

    if plt_hash > config.CONTINENTS_FRACT:
        return config.OCEANIC_PLATE_ID
    return config.CONTINENTAL_PLATE_ID


@njit(fastmath=True, nogil=True, cache=config.CACHING)
def _plateDrift(nix, niy):
    """
    Deterministic plate drift direction.

    For more complex world geometries, like spheres or cubes, Euler pole rotation is recommended. 
    """
    rx = noise.hash11(nix, niy, config.SEED + 3101)
    ry = noise.hash11(nix, niy, config.SEED + 3102)

    r_magnitude = np.sqrt(rx**2 + ry**2) + 1e-12
    rx /= r_magnitude
    ry /= r_magnitude
    
    return np.array((rx, ry), dtype=np.float64)


@njit(fastmath=True, cache=config.CACHING)
def _plateauWeight(plt_drift_dir_list, plt_type_list, plt_center_list, plt_dist_list):
    """Score nearby colliding continental plate pairs, faded near oceanic plates."""
    bridge_width = config.PLATEAU_BRIDGE_WIDTH
    pair_fade_width = bridge_width
    ocean_fade_width = bridge_width
    closing_fade_width = config.PLATEAU_CLOSING_FADE_WIDTH
    plateau_weight = 0.0
    ocean_dist = 1e18
    nearest_dist = plt_dist_list[0]

    for plate in range(np.size(plt_type_list)):
        if plt_type_list[plate] == config.OCEANIC_PLATE_ID:
            border_dist = plt_dist_list[plate] - nearest_dist
            if border_dist < ocean_dist:
                ocean_dist = border_dist

    ocean_weight = mathhelper.smoothStep(ocean_dist / (ocean_fade_width + 1e-8))
    if ocean_weight <= 0.0:
        return 0.0

    for plate in range(np.size(plt_type_list) - 1):
        if plt_type_list[plate] == config.OCEANIC_PLATE_ID:
            continue

        pair_dist = plt_dist_list[plate] - nearest_dist
        pair_near_weight = 1.0 - mathhelper.smoothStep(pair_dist / (pair_fade_width + 1e-8))
        if pair_near_weight <= 0.0:
            continue

        plate_center_x = plt_center_list[plate, 0]
        plate_center_y = plt_center_list[plate, 1]
        plate_drift = plt_drift_dir_list[plate]

        for nbr in range(plate + 1, np.size(plt_type_list)):
            if plt_type_list[nbr] == config.OCEANIC_PLATE_ID:
                continue

            border_dist = plt_dist_list[nbr] - plt_dist_list[plate]
            border_weight = 1.0 - mathhelper.smoothStep(border_dist / (bridge_width + 1e-8))
            if border_weight <= 0.0:
                continue

            pos_x = plt_center_list[nbr, 0] - plate_center_x
            pos_y = plt_center_list[nbr, 1] - plate_center_y
            pos_mag = np.sqrt(pos_x**2 + pos_y**2) + 1e-8
            pos_x /= pos_mag
            pos_y /= pos_mag

            nbr_drift = plt_drift_dir_list[nbr]
            plate_closing = plate_drift[0] * pos_x + plate_drift[1] * pos_y
            nbr_closing = -(nbr_drift[0] * pos_x + nbr_drift[1] * pos_y)

            if plate_closing > 0.0 and nbr_closing > 0.0:
                plate_closing_weight = mathhelper.smoothStep(
                    plate_closing / (closing_fade_width + 1e-8)
                )
                nbr_closing_weight = mathhelper.smoothStep(
                    nbr_closing / (closing_fade_width + 1e-8)
                )
                pair_score = (
                    border_weight
                    * pair_near_weight
                    * plate_closing
                    * nbr_closing
                    * plate_closing_weight
                    * nbr_closing_weight
                )
                if pair_score > plateau_weight:
                    plateau_weight = pair_score

    return plateau_weight * ocean_weight


@njit(fastmath=True, cache=config.CACHING)
def _skewWeight(distance, width, interior_skew, border_skew):
    """Primary distance falloff; the coefficient normalizes the peak to 1."""
    t = distance / (width + 1e-8)
    if t <= 0.0 or t >= 1.0:
        return 0.0
    coeff = 1.0 / (
        (interior_skew / (interior_skew + border_skew)) ** interior_skew
        * (border_skew / (interior_skew + border_skew)) ** border_skew
    )
    return coeff * (t**interior_skew) * ((1.0 - t) ** border_skew)


@njit(fastmath=True, cache=config.CACHING)
def _driftCoverage(drift_scalar, plate_coverage):
    """Scale feature weights by drift-side coverage, clamping negatives to zero."""
    drift_coeff = (drift_scalar + 2.0 * plate_coverage - 1.0) / (
        2.0 * plate_coverage + 1e-8
    )
    return drift_coeff if drift_coeff > 0.0 else 0.0
