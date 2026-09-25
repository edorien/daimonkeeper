#ifndef FE_NOISE_H
#define FE_NOISE_H

#include <stdint.h>
// Cheap deterministic value noise + fBm, for the procedural marble surface
// passes (frontgui_ingame_relief.cpp's panel face, frontgui_widgets.cpp's
// menu-list background). Header-only / inline -- both call sites keep their
// own marble *parameters* (frequency, warp amp, colours), just share the
// field. docs/refactor/ingame-gui/10-maintainability-refactors.md §2.
//
// C++ only.

#ifdef __cplusplus

#include <cmath>

namespace fe {

inline double noise_hash(int64_t x, int64_t y)
{
    uint64_t h = (uint64_t)x * 374761393u + (uint64_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (double)(h & 0xFFFFu) * (1.0 / 65535.0);
}

// Bilinear-interpolated value noise, one period per integer cell.
inline double value_noise(double x, double y)
{
    const double fx = std::floor(x), fy = std::floor(y);
    const int64_t xi = (int64_t)fx, yi = (int64_t)fy;
    const double xf = x - fx, yf = y - fy;
    const double u = xf * xf * (3.0 - 2.0 * xf);
    const double v = yf * yf * (3.0 - 2.0 * yf);
    const double a = noise_hash(xi, yi),     b = noise_hash(xi + 1, yi);
    const double c = noise_hash(xi, yi + 1), d = noise_hash(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b + d - c) * u * v;
}

// 3-octave fractal Brownian motion, ~0..1.
inline double fbm(double x, double y)
{
    return 0.6  * value_noise(x, y)
         + 0.3  * value_noise(x * 2.1 + 5.2, y * 2.1 + 1.3)
         + 0.15 * value_noise(x * 4.3 + 9.1, y * 4.3 + 7.7);
}

} // namespace fe

#endif // __cplusplus
#endif // FE_NOISE_H
