#pragma once

// Realtime-safe DSP building blocks. Nothing in this file allocates after prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

namespace nova::dsp
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxChannels = 2;

// Exact "value changed" test for cached control values (avoids -Wfloat-equal noise).
inline bool differs (float a, float b) noexcept { return std::abs (a - b) > 0.0f; }

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.f) noexcept
{
    return g > 0.f ? std::max (floorDb, 20.f * std::log10 (g)) : floorDb;
}
inline double dbToGainD (double db) noexcept { return std::pow (10.0, db * 0.05); }

// Time constant -> one-pole coefficient (reaches ~63% in `ms`)
inline float timeCoeff (float ms, double sampleRate) noexcept
{
    if (ms <= 0.f) return 0.f;
    return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate));
}

//==============================================================================
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs identity() { return {}; }
    static BiquadCoeffs peak (double sr, double f, double q, double gainDb);
    static BiquadCoeffs lowShelf (double sr, double f, double q, double gainDb);
    static BiquadCoeffs highShelf (double sr, double f, double q, double gainDb);
    static BiquadCoeffs lowPass (double sr, double f, double q);
    static BiquadCoeffs highPass (double sr, double f, double q);
    static BiquadCoeffs bandPass (double sr, double f, double q);   // constant 0 dB peak gain

    // |H| at frequency f (linear)
    double magnitude (double sr, double f) const noexcept;
};

inline double clampFreq (double sr, double f) { return std::clamp (f, 5.0, 0.49 * sr); }

inline BiquadCoeffs normalised (double b0, double b1, double b2, double a0, double a1, double a2)
{
    BiquadCoeffs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

inline BiquadCoeffs BiquadCoeffs::peak (double sr, double f, double q, double g)
{
    const double A = std::pow (10.0, g / 40.0), w = 2 * kPi * clampFreq (sr, f) / sr;
    const double cs = std::cos (w), alpha = std::sin (w) / (2 * std::max (q, 0.05));
    return normalised (1 + alpha * A, -2 * cs, 1 - alpha * A, 1 + alpha / A, -2 * cs, 1 - alpha / A);
}

inline BiquadCoeffs BiquadCoeffs::lowShelf (double sr, double f, double q, double g)
{
    const double A = std::pow (10.0, g / 40.0), w = 2 * kPi * clampFreq (sr, f) / sr;
    const double cs = std::cos (w), alpha = std::sin (w) / (2 * std::max (q, 0.05)), sA = 2 * std::sqrt (A) * alpha;
    return normalised (A * ((A + 1) - (A - 1) * cs + sA), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sA),
                       (A + 1) + (A - 1) * cs + sA, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sA);
}

inline BiquadCoeffs BiquadCoeffs::highShelf (double sr, double f, double q, double g)
{
    const double A = std::pow (10.0, g / 40.0), w = 2 * kPi * clampFreq (sr, f) / sr;
    const double cs = std::cos (w), alpha = std::sin (w) / (2 * std::max (q, 0.05)), sA = 2 * std::sqrt (A) * alpha;
    return normalised (A * ((A + 1) + (A - 1) * cs + sA), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sA),
                       (A + 1) - (A - 1) * cs + sA, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sA);
}

inline BiquadCoeffs BiquadCoeffs::lowPass (double sr, double f, double q)
{
    const double w = 2 * kPi * clampFreq (sr, f) / sr, cs = std::cos (w), alpha = std::sin (w) / (2 * q);
    return normalised ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha);
}

inline BiquadCoeffs BiquadCoeffs::highPass (double sr, double f, double q)
{
    const double w = 2 * kPi * clampFreq (sr, f) / sr, cs = std::cos (w), alpha = std::sin (w) / (2 * q);
    return normalised ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha);
}

inline BiquadCoeffs BiquadCoeffs::bandPass (double sr, double f, double q)
{
    const double w = 2 * kPi * clampFreq (sr, f) / sr, cs = std::cos (w), alpha = std::sin (w) / (2 * q);
    return normalised (alpha, 0, -alpha, 1 + alpha, -2 * cs, 1 - alpha);
}

inline double BiquadCoeffs::magnitude (double sr, double f) const noexcept
{
    const double w = 2 * kPi * f / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto num = b0 + b1 * z1 + b2 * z2;
    const auto den = 1.0 + a1 * z1 + a2 * z2;
    return std::abs (num / den);
}

// Transposed direct form II, double precision state, per channel.
struct Biquad
{
    BiquadCoeffs c;
    std::array<double, kMaxChannels> s1 {}, s2 {};

    void reset() noexcept { s1.fill (0); s2.fill (0); }
    void setCoeffs (const BiquadCoeffs& nc) noexcept { c = nc; }

    inline float process (float x, int ch) noexcept
    {
        const double in = x;
        const double y = c.b0 * in + s1[(size_t) ch];
        s1[(size_t) ch] = c.b1 * in - c.a1 * y + s2[(size_t) ch];
        s2[(size_t) ch] = c.b2 * in - c.a2 * y;
        return (float) y;
    }

    // Guard against denormals / blow-ups after extreme automation
    void sanitise() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            if (! std::isfinite (s1[(size_t) ch]) || ! std::isfinite (s2[(size_t) ch]))
                s1[(size_t) ch] = s2[(size_t) ch] = 0;
            if (std::abs (s1[(size_t) ch]) < 1e-25) s1[(size_t) ch] = 0;
            if (std::abs (s2[(size_t) ch]) < 1e-25) s2[(size_t) ch] = 0;
        }
    }
};

//==============================================================================
// Exponential smoother for control values (runs at control rate or sample rate)
struct Smoother
{
    float current = 0.f, target = 0.f, coeff = 0.f;
    bool initialised = false;

    void setTime (float ms, double rate) noexcept { coeff = timeCoeff (ms, rate); }
    void setTarget (float t) noexcept
    {
        target = t;
        if (! initialised) { current = t; initialised = true; }
    }
    void snap (float v) noexcept { current = target = v; initialised = true; }
    inline float next() noexcept { current = target + coeff * (current - target); return current; }
    bool isSettled() const noexcept { return std::abs (current - target) < 1e-5f; }
};

//==============================================================================
// Attack/release follower operating on a rectified or squared detector signal.
struct EnvelopeFollower
{
    float att = 0.f, rel = 0.f, env = 0.f;

    void setTimes (float attackMs, float releaseMs, double sr) noexcept
    {
        att = timeCoeff (attackMs, sr);
        rel = timeCoeff (releaseMs, sr);
    }
    void reset (float v = 0.f) noexcept { env = v; }
    inline float process (float x) noexcept
    {
        const float c = x > env ? att : rel;
        env = x + c * (env - x);
        return env;
    }
};

//==============================================================================
// Fixed-capacity delay line. Allocation happens only in prepare().
class DelayLine
{
public:
    void prepare (int maxDelaySamples, int numChannels)
    {
        size = 1;
        while (size < maxDelaySamples + 4) size <<= 1;
        mask = size - 1;
        buffer.assign ((size_t) (size * numChannels), 0.f);
        channels = numChannels;
        writePos = 0;
    }
    void reset() { std::fill (buffer.begin(), buffer.end(), 0.f); writePos = 0; }

    inline void push (int ch, float x) noexcept { buffer[(size_t) (ch * size + writePos)] = x; }
    inline void advance() noexcept { writePos = (writePos + 1) & mask; }

    // Integer delay read. Before advance(): read(0) is the sample just pushed.
    // After advance(): read(1) is the most recently pushed sample.
    inline float read (int ch, int delay) const noexcept
    {
        return buffer[(size_t) (ch * size + ((writePos - delay) & mask))];
    }
    // linear-interpolated fractional read
    inline float readFrac (int ch, float delay) const noexcept
    {
        const int d = (int) delay;
        const float f = delay - (float) d;
        const float a = read (ch, d), b = read (ch, d + 1);
        return a + f * (b - a);
    }
    int capacity() const noexcept { return size - 4; }

private:
    std::vector<float> buffer;
    int size = 0, mask = 0, writePos = 0, channels = 0;
};

//==============================================================================
// Linear-phase 2x halfband oversampler (63 taps, Kaiser beta 8 => ~80 dB stopband).
// Round-trip latency is exactly kLatency samples at the base rate, which keeps the
// dry/wet alignment used by A/B, Delta and Mix sample-accurate.
class Halfband2x
{
public:
    static constexpr int kTaps = 63;
    static constexpr int kCentre = 31;
    static constexpr int kEvenTaps = 32;        // non-zero even-index taps
    static constexpr int kLatency = 31;         // base-rate samples, up + down

    Halfband2x() { design(); }

    void reset()
    {
        for (auto& b : upHist) b.fill (0.f);
        for (auto& b : dnHist) b.fill (0.f);
        upPos.fill (0); dnPos.fill (0);
    }

    // x -> two samples at 2x rate
    inline void upsample (int ch, float x, float& y0, float& y1) noexcept
    {
        auto& h = upHist[(size_t) ch];
        int& p = upPos[(size_t) ch];
        p = (p - 1 + kEvenTaps) % kEvenTaps;
        h[(size_t) p] = x;
        h[(size_t) (p + kEvenTaps)] = x;
        const float* hp = h.data() + p;
        float acc = 0.f;
        for (int k = 0; k < kEvenTaps; ++k)
            acc += evenTaps[(size_t) k] * hp[k];
        y0 = 2.f * acc;
        y1 = 2.f * centreTap * hp[(kCentre - 1) / 2];   // x[m-15]
    }

    // two samples at 2x rate -> one base-rate sample
    inline float downsample (int ch, float u0, float u1) noexcept
    {
        // history holds u at 2x rate, newest first. We push u0 then u1 (u1 is the newest).
        auto& h = dnHist[(size_t) ch];
        int& p = dnPos[(size_t) ch];
        p = (p - 1 + kHist) % kHist; h[(size_t) p] = u0; h[(size_t) (p + kHist)] = u0;
        p = (p - 1 + kHist) % kHist; h[(size_t) p] = u1; h[(size_t) (p + kHist)] = u1;
        const float* hp = h.data() + p + 1; // hp[0] = u[2m], hp[1] = u[2m-1], ...
        // Decimate on the even phase: z[m] = sum_n h[n] u[2m-n]. Together with the
        // upsampler this gives an exact integer latency of kLatency base-rate samples.
        float acc = 0.f;
        for (int k = 0; k < kEvenTaps; ++k)
            acc += evenTaps[(size_t) k] * hp[2 * k];
        acc += centreTap * hp[kCentre];
        return acc;
    }

private:
    static constexpr int kHist = 64;
    std::array<float, kEvenTaps> evenTaps {};
    float centreTap = 0.5f;
    std::array<std::array<float, 2 * kEvenTaps>, kMaxChannels> upHist {};
    std::array<std::array<float, 2 * kHist>, kMaxChannels> dnHist {};
    std::array<int, kMaxChannels> upPos {}, dnPos {};

    static double besselI0 (double x)
    {
        double sum = 1, term = 1;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2 * k)) * (x / (2 * k));
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    void design()
    {
        const double beta = 8.0;
        std::array<double, kTaps> h {};
        double sum = 0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double d = n - kCentre;
            const double sinc = n == kCentre ? 0.5 : std::sin (kPi * 0.5 * d) / (kPi * d);
            const double r = d / kCentre;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / besselI0 (beta);
            h[(size_t) n] = sinc * w;
            sum += h[(size_t) n];
        }
        for (auto& v : h) v /= sum;   // unity DC gain
        for (int k = 0; k < kEvenTaps; ++k)
            evenTaps[(size_t) k] = (float) h[(size_t) (2 * k)];
        centreTap = (float) h[kCentre];
    }
};

} // namespace nova::dsp
