#include "finite_world/noise.hpp"

namespace finite_world {
namespace {

double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

double lerp(double a, double b, double t) { return a + (b - a) * t; }

} // namespace

double hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(seed) * 0x9E3779B1u;
    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA77u;
    h = (h << 13U) | (h >> 19U);
    h ^= static_cast<std::uint32_t>(y) * 0xC2B2AE3Du;
    h = (h << 13U) | (h >> 19U);
    h ^= static_cast<std::uint32_t>(z) * 0x27D4EB2Fu;
    h ^= h >> 15U;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12U;
    h *= 0x297A2D39u;
    h ^= h >> 15U;
    return static_cast<double>(h & 0xFFFFFFu) / static_cast<double>(0xFFFFFFu) * 2.0 - 1.0;
}

double valueNoise3(Vec3 p, std::int32_t seed) {
    const auto ix = static_cast<std::int32_t>(std::floor(p.x));
    const auto iy = static_cast<std::int32_t>(std::floor(p.y));
    const auto iz = static_cast<std::int32_t>(std::floor(p.z));
    const double u = fade(p.x - ix);
    const double v = fade(p.y - iy);
    const double w = fade(p.z - iz);
    auto corner = [&](int dx, int dy, int dz) { return hash3(ix + dx, iy + dy, iz + dz, seed); };
    const double x00 = lerp(corner(0, 0, 0), corner(1, 0, 0), u);
    const double x10 = lerp(corner(0, 1, 0), corner(1, 1, 0), u);
    const double x01 = lerp(corner(0, 0, 1), corner(1, 0, 1), u);
    const double x11 = lerp(corner(0, 1, 1), corner(1, 1, 1), u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

double fbm3(Vec3 p, std::int32_t seed, int octaves, double persistence, double lacunarity) {
    double total = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double maximum = 0.0;
    for (int octave = 0; octave < octaves; ++octave) {
        const Vec3 q{p.x * frequency + octave * 17.13, p.y * frequency + octave * 31.7, p.z * frequency + octave * 7.91};
        total += valueNoise3(q, seed + octave * 137) * amplitude;
        maximum += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return maximum > 0.0 ? total / maximum : 0.0;
}

double smoothStep(double value) {
    if (value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value * value * (3.0 - 2.0 * value);
}

double positivePow(double value, double exponent) {
    return value > 0.0 ? std::pow(value, exponent) : 0.0;
}

} // namespace finite_world
