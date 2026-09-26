#include "NovaChain.h"

namespace nova::dsp
{

void NovaChain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    rider.prepare (sr);
    eq.prepare (sr);
    toneMatch.prepare (sr);
    dynEq.prepare (sr);
    comp.prepare (sr);
    deess.prepare (sr);
    color.prepare (sr, maxBlockSize);
    gate.prepare (sr);
    space.prepare (sr, maxBlockSize);
    image.prepare (sr);
    limiter.prepare (sr);

    latency = ColorModule::latencySamples() + limiter.latencySamples();
    mixDry.prepare (latency + kControlBlock + 8, kMaxChannels);

    const double controlRate = sr / (double) kControlBlock;
    inGain.setTime (25.f, controlRate);
    outGain.setTime (25.f, controlRate);
    mix.setTime (25.f, controlRate);
    firstBlock = true;
    reset();
}

void NovaChain::reset()
{
    rider.reset(); eq.reset(); toneMatch.reset(); dynEq.reset(); comp.reset(); deess.reset();
    color.reset(); gate.reset(); space.reset(); image.reset(); limiter.reset();
    mixDry.reset();
    stats.reset();
    instant = {};
    firstBlock = true;
}

void NovaChain::process (float* const* ch, int numCh, int n, const ChainSettings& s, const ChainOrder& order, ProcessContext& ctx) noexcept
{
    numCh = std::clamp (numCh, 1, kMaxChannels);
    inGain.setTarget (dbToGain (s[P::InTrim]));
    outGain.setTarget (dbToGain (s[P::OutGain]));
    mix.setTarget (s[P::Mix] * 0.01f);
    if (firstBlock)
    {
        inGain.snap (inGain.target); outGain.snap (outGain.target); mix.snap (mix.target);
        firstBlock = false;
    }

    const double beatsPerSample = ctx.bpm / 60.0 / ctx.sampleRate;
    float* sub[kMaxChannels] = { nullptr, nullptr };

    for (int offset = 0; offset < n; offset += kControlBlock)
    {
        const int len = std::min (kControlBlock, n - offset);
        for (int c = 0; c < numCh; ++c) sub[c] = ch[c] + offset;

        ProcessContext subCtx = ctx;
        subCtx.ppqAtBlockStart = ctx.ppqAtBlockStart + offset * beatsPerSample;
        subCtx.freeRunSample = ctx.freeRunSample + offset;

        // input trim (ramped) + capture the aligned dry for the global mix
        const float g0 = inGain.current, g1 = inGain.next();
        const float gStep = (g1 - g0) / (float) len;
        for (int c = 0; c < numCh; ++c)
        {
            float g = g0;
            for (int i = 0; i < len; ++i) { g += gStep; sub[c][i] *= g; }
        }
        for (int i = 0; i < len; ++i)
        {
            for (int c = 0; c < numCh; ++c) mixDry.push (c, sub[c][i]);
            mixDry.advance();
        }

        for (auto slot : order)
        {
            switch (slot)
            {
                case ChainSlot::Level:     rider.process (sub, numCh, len, s, stats, instant); break;
                case ChainSlot::EQ:        eq.process (sub, numCh, len, s); break;
                case ChainSlot::ToneMatch: toneMatch.process (sub, numCh, len, s); break;
                case ChainSlot::DynEQ:     dynEq.process (sub, numCh, len, s, stats, instant); break;
                case ChainSlot::Comp:      comp.process (sub, numCh, len, s, stats, instant); break;
                case ChainSlot::DeEss:     deess.process (sub, numCh, len, s, stats, instant); break;
                case ChainSlot::Color:     color.process (sub, numCh, len, s); break;
                case ChainSlot::Motion:    gate.process (sub, numCh, len, s, subCtx); break;
                case ChainSlot::NumSlots:  break;
            }
        }
        space.process (sub, numCh, len, s, subCtx);
        image.process (sub, numCh, len, s);
        limiter.process (sub, numCh, len, s, stats, instant);

        // output gain + latency-aligned global dry/wet
        const float o0 = outGain.current, o1 = outGain.next();
        const float m0 = mix.current, m1 = mix.next();
        const float oStep = (o1 - o0) / (float) len, mStep = (m1 - m0) / (float) len;
        for (int c = 0; c < numCh; ++c)
        {
            float o = o0, m = m0;
            for (int i = 0; i < len; ++i)
            {
                o += oStep; m += mStep;
                const float wet = sub[c][i] * o;
                if (m < 0.9999f)
                {
                    const float dry = mixDry.read (c, latency + (len - i));   // input sample i-latency
                    sub[c][i] = dry + m * (wet - dry);
                }
                else
                    sub[c][i] = wet;
            }
        }
    }
    ctx.freeRunSample += n;
}

double NovaChain::staticResponseDb (const ChainSettings& s, double sampleRate, double f)
{
    double db = EqModule::responseDb (s, sampleRate, f) + ToneMatchModule::responseDb (s, sampleRate, f);
    if (s.on (P::ColOn) && std::abs (s[P::ColWarmth]) > 0.1f)
    {
        const double w = s[P::ColWarmth] * 0.01 * s[P::ColMix] * 0.01;
        db += 20.0 * std::log10 (BiquadCoeffs::lowShelf (sampleRate, 250.0, 0.7, 3.0 * w).magnitude (sampleRate, f)
                               * BiquadCoeffs::highShelf (sampleRate, 5500.0, 0.7, -3.5 * w).magnitude (sampleRate, f));
    }
    return db;
}

} // namespace nova::dsp
