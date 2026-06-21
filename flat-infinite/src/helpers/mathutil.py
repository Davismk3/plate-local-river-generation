from numba import njit
from app import config


@njit(fastmath=True, cache=config.CACHING)
def positivePow(value, exponent):
    """Power function that clamps non-positive inputs to zero."""
    return value**exponent if value > 0.0 else 0.0


@njit(fastmath=True, cache=config.CACHING)
def smoothStep(t):
    """Smooth step function clamping to [0, 1]."""
    if t <= 0.0:
        return 0.0
    if t >= 1.0:
        return 1.0
    return t**2 * (3.0 - 2.0 * t)
