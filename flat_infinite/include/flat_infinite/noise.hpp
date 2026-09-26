#pragma once

#include <cstdint>

namespace flat_infinite {

double hash11(std::int32_t x, std::int32_t y, std::int32_t seed = 1);
double brownianNoise(
    double x,
    double y,
    std::int32_t seed = 1,
    int octaves = 6,
    double persistence = 0.5,
    double lacunarity = 2.0,
    double scale = 0.01);

double positivePow(double value, double exponent);
double smoothStep(double value);

} // namespace flat_infinite

