#include "MonitorStage.h"

namespace nova
{

void MonitorStage::prepare (double s, int latencySamples, int maxBlock)
{
    sr = s;
    latency = latencySamples;
    dryDelay.prepare (latency + maxBlock + 8, dsp::kMaxChannels);
    kDry.prepare (sr);
    kWet.prepare (sr);
    msCoeff = dsp::timeCoeff (1500.f, sr);
    aMix.setTime (12.f, sr);
    deltaMix.setTime (12.f, sr);
    bypassMix.setTime (12.f, sr);
    matchGain.setTime (150.f, sr);
    reset();
}

void MonitorStage::reset()
{
    dryDelay.reset();
    kDry.reset(); kWet.reset();
    msDry = msWet = 0.f;
    matchDb = 0.f;
    matchGain.snap (1.f);
}

void MonitorStage::process (float* const* wet, const float* const* dry, int numCh, int n, Flags f) noexcept
{
    aMix.setTarget (f.monitorA ? 1.f : 0.f);
    deltaMix.setTarget (f.delta ? 1.f : 0.f);
    bypassMix.setTarget (f.bypass ? 1.f : 0.f);

    constexpr float kSilenceMs = 1.0e-7f;   // ~ -70 dB K-weighted
    for (int i = 0; i < n; ++i)
    {
        float dk = 0.f, wk = 0.f;
        float alignedDry[dsp::kMaxChannels] = { 0.f, 0.f };
        for (int c = 0; c < numCh; ++c)
        {
            dryDelay.push (c, dry[c][i]);
            alignedDry[c] = dryDelay.read (c, latency);
            const float kd = kDry.process (alignedDry[c], c);
            const float kw = kWet.process (wet[c][i], c);
            dk += kd * kd; wk += kw * kw;
        }
        dryDelay.advance();

        const bool active = dk > kSilenceMs || wk > kSilenceMs;
        if (active)
        {
            msDry = dk + msCoeff * (msDry - dk);
            msWet = wk + msCoeff * (msWet - wk);
        }
        if (msDry > kSilenceMs && msWet > kSilenceMs)
            matchDb = std::clamp (10.f * std::log10 (msWet / msDry), -24.f, 24.f);

        matchGain.setTarget (f.loudnessMatch ? dsp::dbToGain (matchDb) : 1.f);
        const float gA = matchGain.next();
        const float am = aMix.next(), dm = deltaMix.next(), bm = bypassMix.next();

        for (int c = 0; c < numCh; ++c)
        {
            const float d = alignedDry[c];
            const float b = wet[c][i];
            const float base = b - dm * d * gA;                 // B, or B - A when delta
            const float ab = base + am * (d * gA - base);       // A/B crossfade
            wet[c][i] = ab + bm * (d - ab);                     // host bypass
        }
    }
}

} // namespace nova
