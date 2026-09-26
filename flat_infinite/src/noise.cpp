#include "flat_infinite/noise.hpp"

#include <cmath>
#include <cstdint>

namespace flat_infinite {
namespace {

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double valueNoiseSingle(double x, double y, std::int32_t seed) {
    const auto ix = static_cast<std::int32_t>(std::floor(x));
    const auto iy = static_cast<std::int32_t>(std::floor(y));
    const double fx = x - static_cast<double>(ix);
    const double fy = y - static_cast<double>(iy);
    const double u = fade(fx);
    const double v = fade(fy);
    const double v00 = hash11(ix, iy, seed);
    const double v10 = hash11(ix + 1, iy, seed);
    const double v01 = hash11(ix, iy + 1, seed);
    const double v11 = hash11(ix + 1, iy + 1, seed);
    const double x0 = (1.0 - u) * v00 + u * v10;
    const double x1 = (1.0 - u) * v01 + u * v11;
    return (1.0 - v) * x0 + v * x1;
}

double valueNoise(double x, double y, std::int32_t seed) {
    constexpr double c = 0.8320502943378437;
    constexpr double s = 0.5547001962252291;
    const double x2 = x * c - y * s + 19.1;
    const double y2 = x * s + y * c - 7.7;
    return 0.5 * (valueNoiseSingle(x, y, seed) + valueNoiseSingle(x2, y2, seed + 1013));
}

std::uint64_t arithmeticShiftRight(std::uint64_t value, unsigned amount) {
    if ((value & (UINT64_C(1) << 63U)) == 0) return value >> amount;
    return ~((~value) >> amount);
}

} // namespace

double hash11(std::int32_t x, std::int32_t y, std::int32_t seed) {
    // The Python/Numba source evaluates this expression as wrapping int64 and
    // uses arithmetic right shifts. Keep those semantics without signed-overflow UB.
    std::uint64_t h = static_cast<std::uint64_t>(static_cast<std::int64_t>(seed))
        ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(x)) * UINT64_C(374761393)
        ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(y)) * UINT64_C(668265263);
    h = (h ^ arithmeticShiftRight(h, 16U)) * UINT64_C(0x85EBCA6B);
    h = (h ^ arithmeticShiftRight(h, 13U)) * UINT64_C(0xC2B2AE35);
    h ^= arithmeticShiftRight(h, 16U);
    return (static_cast<double>(h & UINT64_C(0x7FFFFFFF)) / 2147483647.0) * 2.0 - 1.0;
}

double brownianNoise(
    double x,
    double y,
    std::int32_t seed,
    int octaves,
    double persistence,
    double lacunarity,
    double scale) {
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = scale;
    double maximum = 0.0;
    for (int octave = 0; octave < octaves; ++octave) {
        const double nx = x * frequency + static_cast<double>(octave) * 17.13;
        const double ny = y * frequency + static_cast<double>(octave) * 31.7;
        total += valueNoise(nx, ny, seed + octave * 137) * amplitude;
        maximum += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return maximum > 0.0 ? total / maximum : 0.0;
}

double positivePow(double value, double exponent) {
    return value > 0.0 ? std::pow(value, exponent) : 0.0;
}

double smoothStep(double value) {
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value * value * (3.0 - 2.0 * value);
}

} // namespace flat_infinite
