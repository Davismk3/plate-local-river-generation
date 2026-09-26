#pragma once

#include "finite_world/math.hpp"

#include <cstdint>

namespace finite_world {

// Deterministic hash of an integer lattice point, in [-1, 1].
double hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t seed);

// Trilinear value noise in [-1, 1].
double valueNoise3(Vec3 p, std::int32_t seed);

// Normalized fractal (Brownian) value noise in [-1, 1].
double fbm3(Vec3 p, std::int32_t seed, int octaves, double persistence = 0.5, double lacunarity = 2.0);

double smoothStep(double value);
double positivePow(double value, double exponent);

} // namespace finite_world
