#include "SpaceModule.h"

namespace nova::dsp
{

void SpaceModule::prepare (double s, int)
{
    sr = s;
    preDelay.prepare ((int) (0.26 * sr) + 8, 1);
    static constexpr float kDiffMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
    for (int i = 0; i < 4; ++i)
    {
        diffLen[(size_t) i] = std::max (1, (int) (kDiffMs[i] * 0.001 * sr));
        diffusers[(size_t) i].prepare (diffLen[(size_t) i] + 4, 1);
    }
    baseLenMs = { 29.7f, 37.1f, 41.1f, 43.7f, 53.1f, 59.3f, 67.9f, 73.3f };
    for (int i = 0; i < kLines; ++i)
        lines[(size_t) i].prepare ((int) (baseLenMs[(size_t) i] * 0.001 * sr * 1.7) + 64, 1);

    echo.prepare ((int) (2.6 * sr) + 8, kMaxChannels);
    echoTime.setTime (60.f, sr);
    duckEnv.setTimes (8.f, 250.f, sr);
    revGain.setTime (30.f, sr / 32.0);
    dlyGain.setTime (30.f, sr / 32.0);
    active.setTime (40.f, sr / 32.0);
    lastDecay = lastSize = lastDamp = lastLowCut = -1;
    reset();
}

void SpaceModule::reset()
{
    preDelay.reset();
    for (auto& d : diffusers) d.reset();
    for (auto& l : lines) l.reset();
    dampState.fill (0.f);
    lowCut.reset();
    echo.reset();
    echoLp.fill (0.f); echoHpState.fill (0.f);
    duckEnv.reset();
    lfoPhase = 0.0;
}

double SpaceModule::divisionBeats (int division)
{
    static constexpr double kBeats[] = { 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
    return division >= 0 && division < 9 ? kBeats[division] : 1.0;
}

double SpaceModule::delayTimeMs (const ChainSettings& s, double bpm)
{
    if (s.on (P::SpcDlySync))
        return std::clamp (divisionBeats (s.choice (P::SpcDlyDiv)) * 60000.0 / std::max (20.0, bpm), 1.0, 2500.0);
    return s[P::SpcDlyTime];
}

void SpaceModule::updateReverb (const ChainSettings& s)
{
    const float decay = s[P::SpcDecay], size = s[P::SpcSize], damp = s[P::SpcDamping], lc = s[P::SpcLowCut];
    if (differs (decay, lastDecay) || differs (size, lastSize))
    {
        const float scale = 0.6f + size * 0.01f;   // 0.6 .. 1.6
        for (int i = 0; i < kLines; ++i)
        {
            lineLen[(size_t) i] = (float) (baseLenMs[(size_t) i] * scale * 0.001 * sr);
            // per-line gain for the requested RT60
            lineGain[(size_t) i] = (float) std::pow (10.0, -3.0 * lineLen[(size_t) i] / (std::max (0.1f, decay) * sr));
        }
        lastDecay = decay; lastSize = size;
    }
    if (differs (damp, lastDamp))
    {
        const double cutoff = 16000.0 * std::pow (1500.0 / 16000.0, damp * 0.01);
        dampCoeff = (float) std::exp (-2.0 * kPi * std::min (cutoff, 0.45 * sr) / sr);
        lastDamp = damp;
    }
    if (differs (lc, lastLowCut))
    {
        lowCut.setCoeffs (BiquadCoeffs::highPass (sr, lc, 0.70710678));
        lastLowCut = lc;
    }
}

void SpaceModule::process (float* const* ch, int numCh, int n, const ChainSettings& s, const ProcessContext& ctx) noexcept
{
    const bool on = s.on (P::SpcOn);
    active.setTarget (on ? 1.f : 0.f);
    const float act = active.next();
    if (! on && act < 1e-4f)
    {
        wasActive = false;
        return;
    }
    if (! wasActive)
    {
        reset();                       // start from silence: no stale tails
        wasActive = true;
    }

    updateReverb (s);
    const float rm = s[P::SpcRevMix] * 0.01f, dm = s[P::SpcDlyMix] * 0.01f;
    revGain.setTarget (rm * rm * 0.9f + rm * 0.35f);
    dlyGain.setTarget (dm * 0.8f);
    const float rg = revGain.next() * act;
    const float dg = dlyGain.next() * act;

    const int preSamples = (int) (s[P::SpcPreDelay] * 0.001f * (float) sr);
    const float widthRev = s[P::SpcRevWidth] * 0.01f;
    const float duck = s[P::SpcDuck] * 0.01f;
    const float fb = s[P::SpcDlyFeedback] * 0.01f;
    const bool pingPong = s.on (P::SpcDlyPingPong) && numCh > 1;
    echoTime.setTarget ((float) (delayTimeMs (s, ctx.bpm) * 0.001 * sr));
    echoLpCoeff = (float) std::exp (-2.0 * kPi * std::min ((double) s[P::SpcDlyTone], 0.45 * sr) / sr);
    const float hpCoeff = (float) std::exp (-2.0 * kPi * 150.0 / sr);
    const double lfoInc = 2.0 * kPi * 0.63 / sr;
    const float modDepth = (float) (0.00035 * sr);
    constexpr float kDiffG = 0.62f;
    constexpr float kOutNorm = 0.35f;

    for (int i = 0; i < n; ++i)
    {
        const float inL = ch[0][i];
        const float inR = numCh > 1 ? ch[1][i] : inL;
        const float mono = 0.5f * (inL + inR);

        // ducking follows the dry signal
        const float e = duckEnv.process (std::abs (mono));
        const float duckDb = std::clamp ((gainToDb (e) + 42.f) / 30.f, 0.f, 1.f) * duck * 14.f;
        const float duckG = dbToGain (-duckDb);

        float outL = 0.f, outR = 0.f;

        if (rg > 1e-5f)
        {
            // pre-delay + low cut + input diffusion
            preDelay.push (0, mono);
            float x = preDelay.read (0, std::min (preSamples, preDelay.capacity() - 1));
            preDelay.advance();
            x = lowCut.process (x, 0);
            for (int d = 0; d < 4; ++d)
            {
                auto& dl = diffusers[(size_t) d];
                const float delayed = dl.read (0, diffLen[(size_t) d] - 1);
                const float v = x + kDiffG * delayed;
                dl.push (0, v);
                dl.advance();
                x = delayed - kDiffG * v;
            }

            // FDN with Householder feedback
            std::array<float, kLines> taps {};
            lfoPhase += lfoInc;
            if (lfoPhase > 2.0 * kPi) lfoPhase -= 2.0 * kPi;
            const float mod0 = modDepth * (float) std::sin (lfoPhase);
            const float mod1 = modDepth * (float) std::cos (lfoPhase * 0.77);
            float sum = 0.f;
            for (int l = 0; l < kLines; ++l)
            {
                float len = lineLen[(size_t) l];
                if (l == 1) len += mod0;
                if (l == 6) len += mod1;
                float v = lines[(size_t) l].readFrac (0, std::max (1.f, len - 1.f));
                // damping low-pass in the loop
                dampState[(size_t) l] = v + dampCoeff * (dampState[(size_t) l] - v);
                v = dampState[(size_t) l] * lineGain[(size_t) l];
                taps[(size_t) l] = v;
                sum += v;
            }
            const float h = sum * (2.f / (float) kLines);
            for (int l = 0; l < kLines; ++l)
            {
                const float fbv = taps[(size_t) l] - h + x * ((l & 1) ? -0.5f : 0.5f);
                lines[(size_t) l].push (0, std::isfinite (fbv) ? fbv : 0.f);
                lines[(size_t) l].advance();
            }
            const float wl = (taps[0] - taps[2] + taps[4] - taps[6] + taps[1] * 0.5f) * kOutNorm;
            const float wr = (taps[1] - taps[3] + taps[5] - taps[7] + taps[2] * 0.5f) * kOutNorm;
            const float wm = 0.5f * (wl + wr), ws = 0.5f * (wl - wr) * widthRev;
            outL += (wm + ws) * rg * duckG;
            outR += (wm - ws) * rg * duckG;
        }

        if (dg > 1e-5f)
        {
            const float t = std::max (1.f, echoTime.next());
            const float dl = echo.readFrac (0, t);
            const float dr = numCh > 1 ? echo.readFrac (1, t) : dl;
            // feedback tone: low-pass + gentle high-pass
            auto shapeFb = [&] (int c, float v)
            {
                echoLp[(size_t) c] = v + echoLpCoeff * (echoLp[(size_t) c] - v);
                const float lpv = echoLp[(size_t) c];
                echoHpState[(size_t) c] = lpv + hpCoeff * (echoHpState[(size_t) c] - lpv);
                return lpv - echoHpState[(size_t) c];
            };
            const float fl = shapeFb (0, dl), fr = shapeFb (1, dr);
            if (pingPong)
            {
                echo.push (0, mono + fr * fb);
                echo.push (1, fl * fb);
            }
            else
            {
                echo.push (0, inL + fl * fb);
                if (numCh > 1) echo.push (1, inR + fr * fb);
            }
            echo.advance();
            outL += dl * dg * duckG;
            outR += dr * dg * duckG;
        }

        if (numCh > 1) { ch[0][i] += outL; ch[1][i] += outR; }
        else ch[0][i] += 0.5f * (outL + outR);
    }
    for (int l = 0; l < kLines; ++l)
        if (std::abs (dampState[(size_t) l]) < 1e-20f) dampState[(size_t) l] = 0.f;
    lowCut.sanitise();
}

} // namespace nova::dsp
