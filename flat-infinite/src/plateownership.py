import numpy as np
from numba import njit
from app import config
from helpers import noise


@njit(fastmath=True)
def plateCenter(plt_owner_idx):
    """
    Deterministic owner and neighbor plate center position by a deterministic jitter.
    
    This algorithm is semi-arbitrary.
     
    For infinite worlds, the current implementation is recommended.

    For finite worlds, direct control over the plate count is possible, 
    and all plate centers may be computed on runtime. The golden ratio 
    sphere of points is recommended in this case for closed world geometries.
    """
    
    ix, iy = plt_owner_idx

    plt_center_x = (
        ix + 0.5 + noise.hash11(ix, iy, config.SEED + 801) * config.PLT_SHAPE
    )
    plt_center_y = (
        iy + 0.5 + noise.hash11(ix, iy, config.SEED + 802) * config.PLT_SHAPE
    )

    return plt_center_x, plt_center_y
