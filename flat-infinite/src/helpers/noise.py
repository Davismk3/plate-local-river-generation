import numpy as np
from numba import njit
from app import config


@njit(fastmath=True, nogil=True)
def basicBrownianNoise(
    x,
    y,
    seed=config.SEED,
    octaves=6,
    persistence=0.5,
    lacunarity=2.0,
    scale=0.01,
    ) -> float:
    """Deterministic fractal value noise in the range [-1.0, 1.0]."""
    total = 0.0
    amplitude = 1.0
    frequency = scale
    max_value = 0.0

    for i in range(octaves):
        nx = x * frequency + i * 17.13
        ny = y * frequency + i * 31.7
        total += _valueNoise(nx, ny, seed + i * 137) * amplitude
        max_value += amplitude
        amplitude *= persistence
        frequency *= lacunarity

    return total / max_value


@njit(fastmath=True, nogil=True, inline="always")
def hash11(x, y, seed=config.SEED) -> float:
    """Hash a lattice coordinate into a deterministic float in [-1.0, 1.0]."""
    h = seed ^ x * 374761393 ^ y * 668265263
    h = (h ^ (h >> 16)) * 0x85EBCA6B
    h = (h ^ (h >> 13)) * 0xC2B2AE35
    h ^= h >> 16
    return ((h & 0x7FFFFFFF) / 2147483647.0) * 2.0 - 1.0


@njit(fastmath=True, nogil=True, inline="always", cache=True)
def _fade(t: float) -> float:
    return t**3 * (t * (t * 6.0 - 15.0) + 10.0)


@njit(fastmath=True, nogil=True, inline="always", cache=True)
def _valueNoiseSingle(x: float, y: float, seed: int = config.SEED) -> float:
    ix = np.int32(np.floor(x))
    iy = np.int32(np.floor(y))
    fx = x - float(ix)
    fy = y - float(iy)

    u = _fade(fx)
    v = _fade(fy)
    v00 = hash11(ix, iy, seed)
    v10 = hash11(ix + 1, iy, seed)
    v01 = hash11(ix, iy + 1, seed)
    v11 = hash11(ix + 1, iy + 1, seed)

    x0 = (1.0 - u) * v00 + u * v10
    x1 = (1.0 - u) * v01 + u * v11
    return (1.0 - v) * x0 + v * x1


@njit(fastmath=True, nogil=True, inline="always", cache=True)
def _valueNoise(x: float, y: float, seed: int = config.SEED) -> float:
    c = 0.8320502943378437  # cos(33.0 degrees)
    s = 0.5547001962252291  # sin(33.0 degrees)
    x2 = x * c - y * s + 19.1
    y2 = x * s + y * c - 7.7

    n1 = _valueNoiseSingle(x, y, seed)
    n2 = _valueNoiseSingle(x2, y2, seed + 1013)
    return 0.5 * (n1 + n2)
