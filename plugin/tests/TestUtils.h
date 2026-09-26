#pragma once

#include <cmath>
#include <random>
#include <vector>

namespace nova::test
{

constexpr double kPi = 3.14159265358979323846;

inline std::vector<float> sine (double freq, double sr, int n, float amp = 0.5f, double phase = 0.0)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = amp * (float) std::sin (2.0 * kPi * freq * i / sr + phase);
    return v;
}

inline std::vector<float> whiteNoise (int n, float amp = 0.25f, unsigned seed = 1234)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.f, 1.f);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = amp * d (rng);
    return v;
}

inline double rms (const float* x, int n)
{
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (1, n));
}

inline double rmsDb (const float* x, int n) { return 20.0 * std::log10 (rms (x, n) + 1e-12); }

inline double peak (const float* x, int n)
{
    double p = 0;
    for (int i = 0; i < n; ++i) p = std::max (p, (double) std::abs (x[i]));
    return p;
}

} // namespace nova::test
