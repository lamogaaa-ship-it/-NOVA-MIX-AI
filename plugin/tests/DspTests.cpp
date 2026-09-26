#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestUtils.h"
#include "dsp/NovaChain.h"

using namespace nova;
using namespace nova::dsp;
using Catch::Matchers::WithinAbs;

namespace
{
struct StereoBuf
{
    std::vector<float> l, r;
    explicit StereoBuf (std::vector<float> mono) : l (mono), r (std::move (mono)) {}
    float* ch[2] { nullptr, nullptr };
    float* const* ptrs() { ch[0] = l.data(); ch[1] = r.data(); return ch; }
};

// Run a chain over a whole buffer in host-sized blocks
void runChain (NovaChain& chain, StereoBuf& b, const ChainSettings& s, int block = 256, double sr = 48000.0)
{
    ProcessContext ctx;
    ctx.sampleRate = sr;
    const int n = (int) b.l.size();
    for (int off = 0; off < n; off += block)
    {
        const int len = std::min (block, n - off);
        float* p[2] = { b.l.data() + off, b.r.data() + off };
        chain.process (p, 2, len, s, defaultChainOrder(), ctx);
    }
}
} // namespace

TEST_CASE ("Biquad RBJ peak filter hits its gain at the centre frequency", "[dsp]")
{
    const double sr = 48000;
    for (double g : { -12.0, -3.0, 6.0, 12.0 })
    {
        auto c = BiquadCoeffs::peak (sr, 1000.0, 1.0, g);
        CHECK_THAT (20 * std::log10 (c.magnitude (sr, 1000.0)), WithinAbs (g, 0.01));
        CHECK_THAT (20 * std::log10 (c.magnitude (sr, 50.0)), WithinAbs (0.0, 0.3));
    }
    auto hp = BiquadCoeffs::highPass (sr, 100.0, 0.70710678);
    CHECK_THAT (20 * std::log10 (hp.magnitude (sr, 100.0)), WithinAbs (-3.01, 0.05));
    CHECK (20 * std::log10 (hp.magnitude (sr, 25.0)) < -23.0);
}

TEST_CASE ("Halfband oversampler round trip has an exact integer latency", "[dsp]")
{
    Halfband2x os;
    const double sr = 48000;
    auto x = test::sine (997.0, sr, 8192, 0.5f);
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        float a, b;
        os.upsample (0, x[i], a, b);
        y[i] = os.downsample (0, a, b);
    }
    double maxErr = 0;
    for (size_t i = 200; i < x.size(); ++i)
        maxErr = std::max (maxErr, (double) std::abs (y[i] - x[i - Halfband2x::kLatency]));
    CHECK (maxErr < 2.0e-3);   // ~ -54 dB relative to 0.5 amplitude, passband ripple only
}

TEST_CASE ("Neutral chain is a pure latency-compensated passthrough", "[dsp][chain]")
{
    NovaChain chain;
    chain.prepare (48000.0, 512);
    const int latency = chain.getLatencySamples();
    REQUIRE (latency == Halfband2x::kLatency + (int) std::lround (0.0015 * 48000.0));

    auto input = test::whiteNoise (48000, 0.3f);
    StereoBuf b (input);
    ChainSettings s;   // defaults: every processor disabled
    runChain (chain, b, s, 300);

    double maxErr = 0;
    for (size_t i = (size_t) latency; i < input.size(); ++i)
        maxErr = std::max (maxErr, (double) std::abs (b.l[i] - input[i - (size_t) latency]));
    CHECK (maxErr < 1.0e-6);
}

TEST_CASE ("Global mix at 0% returns the aligned dry signal even with processing enabled", "[dsp][chain]")
{
    NovaChain chain;
    chain.prepare (48000.0, 512);
    const int latency = chain.getLatencySamples();
    auto input = test::whiteNoise (24000, 0.3f, 99);
    StereoBuf b (input);
    ChainSettings s;
    s[P::CompOn] = 1; s[P::CompThresh] = -40; s[P::CompRatio] = 8;
    s[P::ColOn] = 1; s[P::ColDrive] = 18;
    s[P::Mix] = 0;
    runChain (chain, b, s);
    double maxErr = 0;
    for (size_t i = (size_t) latency + 64; i < input.size(); ++i)
        maxErr = std::max (maxErr, (double) std::abs (b.l[i] - input[i - (size_t) latency]));
    CHECK (maxErr < 1.0e-5);
}

TEST_CASE ("Compressor static curve and gain reduction", "[dsp]")
{
    CHECK_THAT (Compressor::gainReductionDb (-30.f, -20.f, 4.f, 0.f), WithinAbs (0.0, 1e-6));
    CHECK_THAT (Compressor::gainReductionDb (-10.f, -20.f, 4.f, 0.f), WithinAbs (7.5, 1e-4));   // 10 dB over, 4:1
    // knee is continuous
    CHECK_THAT (Compressor::gainReductionDb (-23.f, -20.f, 4.f, 6.f), WithinAbs (0.0, 1e-4));
    CHECK (Compressor::gainReductionDb (-20.f, -20.f, 4.f, 6.f) > 0.f);

    NovaChain chain;
    chain.prepare (48000.0, 512);
    auto input = test::sine (440.0, 48000.0, 48000, 0.5f);   // -6 dBFS peak
    StereoBuf b (input);
    ChainSettings s;
    s[P::CompOn] = 1; s[P::CompThresh] = -24; s[P::CompRatio] = 4; s[P::CompKnee] = 0;
    s[P::CompAttack] = 1; s[P::CompRelease] = 50; s[P::CompDetector] = 1;
    runChain (chain, b, s);
    const double inDb = test::rmsDb (input.data() + 24000, 24000);
    const double outDb = test::rmsDb (b.l.data() + 24000, 24000);
    // RMS detector calibrated to sine peak: level ~ -6 dB, 18 dB over at 4:1 => 13.5 dB GR
    CHECK_THAT (inDb - outDb, WithinAbs (13.5, 1.0));
}

TEST_CASE ("De-esser reduces sibilant bursts and leaves low content alone", "[dsp]")
{
    const double sr = 48000;
    NovaChain chain;
    chain.prepare (sr, 512);
    const int latency = chain.getLatencySamples();

    // vowel-like 220 Hz tone + periodic 7 kHz noise bursts
    const int n = 48000;
    auto tone = test::sine (220.0, sr, n, 0.3f);
    auto noise = test::whiteNoise (n, 1.f, 7);
    Biquad bp; bp.setCoeffs (BiquadCoeffs::bandPass (sr, 7000.0, 1.5));
    std::vector<float> sib ((size_t) n, 0.f);
    for (int i = 0; i < n; ++i)
    {
        const bool burst = (i / 4800) % 2 == 1;
        sib[(size_t) i] = burst ? 0.6f * bp.process (noise[(size_t) i], 0) : 0.f;
    }
    std::vector<float> input ((size_t) n);
    for (int i = 0; i < n; ++i) input[(size_t) i] = tone[(size_t) i] + sib[(size_t) i];

    StereoBuf b (input);
    ChainSettings s;
    s[P::DessOn] = 1; s[P::DessFreq] = 7000; s[P::DessThresh] = -40; s[P::DessRange] = 12;
    runChain (chain, b, s);

    // measure the 7 kHz band during bursts and the 220 Hz tone between bursts
    Biquad m1, m2; m1.setCoeffs (BiquadCoeffs::bandPass (sr, 7000.0, 2.0)); m2.setCoeffs (BiquadCoeffs::bandPass (sr, 7000.0, 2.0));
    double inHi = 0, outHi = 0; int cnt = 0;
    double inLo = 0, outLo = 0;
    for (int i = latency; i < n; ++i)
    {
        const float xi = m1.process (input[(size_t) (i - latency)], 0);
        const float yo = m2.process (b.l[(size_t) i], 0);
        const int src = i - latency;
        if ((src / 4800) % 2 == 1 && (src % 4800) > 480) { inHi += xi * xi; outHi += yo * yo; ++cnt; }
        if ((src / 4800) % 2 == 0 && (src % 4800) > 2400) { inLo += input[(size_t) src] * input[(size_t) src]; outLo += b.l[(size_t) i] * b.l[(size_t) i]; }
    }
    const double hiReductionDb = 10 * std::log10 (inHi / outHi);
    const double loChangeDb = 10 * std::log10 (outLo / inLo);
    INFO ("sibilant band reduction " << hiReductionDb << " dB, tone change " << loChangeDb << " dB");
    CHECK (cnt > 0);
    CHECK (hiReductionDb > 4.0);
    CHECK (std::abs (loChangeDb) < 0.3);
}

TEST_CASE ("Dynamic EQ only acts on loud band events", "[dsp]")
{
    const double sr = 48000;
    NovaChain chain;
    chain.prepare (sr, 512);
    const int latency = chain.getLatencySamples();
    const int n = 48000;
    // 3 kHz tone: quiet in first half, loud in second
    std::vector<float> input ((size_t) n);
    for (int i = 0; i < n; ++i)
        input[(size_t) i] = (i < n / 2 ? 0.02f : 0.4f) * (float) std::sin (2 * test::kPi * 3000.0 * i / sr);
    StereoBuf b (input);
    ChainSettings s;
    s[P::DeqOn] = 1; s[P::DeqB1On] = 1; s[P::DeqB1Freq] = 3000; s[P::DeqB1Q] = 2;
    s[P::DeqB1Thresh] = -24; s[P::DeqB1Range] = 9;
    runChain (chain, b, s);
    const double quietIn = test::rmsDb (input.data() + 12000, 10000);
    const double quietOut = test::rmsDb (b.l.data() + 12000 + latency, 10000);
    const double loudIn = test::rmsDb (input.data() + 36000, 10000);
    const double loudOut = test::rmsDb (b.l.data() + 36000 + latency, 10000);
    CHECK_THAT (quietOut - quietIn, WithinAbs (0.0, 0.2));
    CHECK (loudIn - loudOut > 5.0);
    CHECK (loudIn - loudOut < 9.5);
}

TEST_CASE ("Level rider reduces phrase-to-phrase level variation", "[dsp]")
{
    const double sr = 48000;
    NovaChain chain;
    chain.prepare (sr, 512);
    const int n = 48000 * 6;
    // alternating 1-second "phrases" at -30 dBFS and -12 dBFS RMS (pink-ish noise)
    auto noise = test::whiteNoise (n, 1.f, 5);
    Biquad lp; lp.setCoeffs (BiquadCoeffs::lowPass (sr, 3000.0, 0.7));
    std::vector<float> input ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float lvl = ((i / 48000) % 2 == 0) ? 0.05f : 0.4f;
        input[(size_t) i] = lvl * lp.process (noise[(size_t) i], 0);
    }
    StereoBuf b (input);
    ChainSettings s;
    s[P::LvlOn] = 1; s[P::LvlTarget] = -20; s[P::LvlRange] = 12; s[P::LvlSpeed] = 80;
    runChain (chain, b, s);
    const int lat = chain.getLatencySamples();
    auto phraseDb = [&] (const std::vector<float>& v, int phrase, int offset)
    { return test::rmsDb (v.data() + phrase * 48000 + 24000 + offset, 20000); };
    const double inSpread = std::abs (phraseDb (input, 3, 0) - phraseDb (input, 2, 0));
    const double outSpread = std::abs (phraseDb (b.l, 3, lat) - phraseDb (b.l, 2, lat));
    INFO ("input spread " << inSpread << " dB, output spread " << outSpread << " dB");
    CHECK (inSpread > 15.0);
    CHECK (outSpread < 0.5 * inSpread);
}

TEST_CASE ("Limiter holds the ceiling and raises the body of a high-crest signal", "[dsp]")
{
    const double sr = 48000;
    NovaChain chain;
    chain.prepare (sr, 512);
    // quiet sustained body (0.1) with sparse transient spikes (0.9): crest factor ~ 19 dB
    auto input = test::sine (180.0, sr, 48000, 0.1f);
    for (int i = 1000; i < 48000; i += 4800)
        for (int k = 0; k < 24; ++k) input[(size_t) (i + k)] = (k % 2 == 0 ? 0.9f : -0.9f);
    StereoBuf b (input);
    ChainSettings s;
    s[P::LimOn] = 1; s[P::LimGain] = 12; s[P::LimCeiling] = -1; s[P::LimRelease] = 10;
    runChain (chain, b, s);
    const double ceil = std::pow (10.0, -1.0 / 20.0);
    const int lat = chain.getLatencySamples();
    CHECK (test::peak (b.l.data() + 2000, 46000) <= ceil + 1e-6);
    // sustained body between transients is lifted by ~ the drive amount
    const double bodyIn = test::rmsDb (input.data() + 4000, 1500);
    const double bodyOut = test::rmsDb (b.l.data() + 4000 + lat, 1500);
    INFO ("body gain " << bodyOut - bodyIn << " dB");
    CHECK (bodyOut - bodyIn > 9.0);
}

TEST_CASE ("Space adds a decaying tail after the dry signal stops", "[dsp]")
{
    const double sr = 48000;
    NovaChain chain;
    chain.prepare (sr, 512);
    std::vector<float> input ((size_t) sr * 3, 0.f);
    auto burst = test::whiteNoise (4800, 0.5f, 11);
    std::copy (burst.begin(), burst.end(), input.begin());
    StereoBuf b (input);
    ChainSettings s;
    s[P::SpcOn] = 1; s[P::SpcRevMix] = 60; s[P::SpcDecay] = 1.5f;
    runChain (chain, b, s);
    const double tail1 = test::rmsDb (b.l.data() + 12000, 4800);    // 0.25 s
    const double tail2 = test::rmsDb (b.l.data() + 48000, 4800);    // 1.0 s
    INFO ("tail at 0.25 s " << tail1 << " dB, at 1.0 s " << tail2);
    CHECK (tail1 > -60.0);
    CHECK (tail2 < tail1 - 10.0);   // decays
    for (float v : b.l) REQUIRE (std::isfinite (v));
}

TEST_CASE ("Rhythm gate chops in time with the tempo", "[dsp]")
{
    const double sr = 48000;
    NovaChain chain;
    chain.prepare (sr, 512);
    auto input = test::sine (220.0, sr, 48000, 0.5f);
    StereoBuf b (input);
    ChainSettings s;
    s[P::GateOn] = 1; s[P::GateDiv] = 1; s[P::GatePattern] = 0; s[P::GateDepth] = 100; s[P::GateSmooth] = 1;
    runChain (chain, b, s);   // 120 bpm, 1/8 = 250 ms steps, open for first half
    const int lat = chain.getLatencySamples();
    const double open = test::rmsDb (b.l.data() + 24000 + lat + 1000, 3000);      // step 4, first half
    const double closed = test::rmsDb (b.l.data() + 24000 + 6000 + lat + 1000, 3000); // second half
    CHECK (open > -10.0);
    CHECK (closed < open - 40.0);
}
