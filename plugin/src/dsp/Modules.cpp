#include "Modules.h"

namespace nova::dsp
{

//==============================================================================
// KWeighting
BiquadCoeffs KWeighting::shelfCoeffs (double fs)
{
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan (kPi * f0 / fs);
    const double Vh = std::pow (10.0, G / 20.0);
    const double Vb = std::pow (Vh, 0.4996667741545416);
    const double a0 = 1.0 + K / Q + K * K;
    BiquadCoeffs c;
    c.b0 = (Vh + Vb * K / Q + K * K) / a0;
    c.b1 = 2.0 * (K * K - Vh) / a0;
    c.b2 = (Vh - Vb * K / Q + K * K) / a0;
    c.a1 = 2.0 * (K * K - 1.0) / a0;
    c.a2 = (1.0 - K / Q + K * K) / a0;
    return c;
}

BiquadCoeffs KWeighting::highpassCoeffs (double fs)
{
    const double f0 = 38.13547087602444, Q = 0.5003270373238773;
    const double K = std::tan (kPi * f0 / fs);
    const double a0 = 1.0 + K / Q + K * K;
    BiquadCoeffs c;
    c.b0 = 1.0; c.b1 = -2.0; c.b2 = 1.0;
    c.a1 = 2.0 * (K * K - 1.0) / a0;
    c.a2 = (1.0 - K / Q + K * K) / a0;
    return c;
}

void KWeighting::prepare (double sr)
{
    shelf.setCoeffs (shelfCoeffs (sr));
    hp.setCoeffs (highpassCoeffs (sr));
    reset();
}

//==============================================================================
// LevelRider: macro (phrase level) gain riding on a K-weighted RMS detector.
void LevelRider::prepare (double s)
{
    sr = s;
    kw.prepare (sr);
    msCoeff = timeCoeff (60.f, sr);   // 60 ms mean-square integration
    reset();
}

void LevelRider::reset()
{
    kw.reset();
    msState = 0.f;
    gainDb = 0.f;
    currentLin = 1.f;
}

void LevelRider::process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept
{
    const bool on = s.on (P::LvlOn);
    if (! on && std::abs (gainDb) < 0.01f)
    {
        active = false;
        inst.riderGainDb = 0.f;
        // keep the detector warm so enabling it is smooth
        for (int i = 0; i < n; ++i)
        {
            float m = 0.f;
            for (int c = 0; c < numCh; ++c)
            {
                const float k = kw.process (ch[c][i], c);
                m = std::max (m, k * k);
            }
            msState = m + msCoeff * (msState - m);
        }
        return;
    }
    active = true;

    const float target = s[P::LvlTarget];
    const float range = s[P::LvlRange];
    const float gate = s[P::LvlGate];
    const float amount = s[P::LvlAmount] * 0.01f;
    const float speedCoeff = timeCoeff (s[P::LvlSpeed], sr);

    const float startLin = currentLin;
    float desired = gainDb;

    for (int i = 0; i < n; ++i)
    {
        float m = 0.f;
        for (int c = 0; c < numCh; ++c)
        {
            const float k = kw.process (ch[c][i], c);
            m = std::max (m, k * k);
        }
        msState = m + msCoeff * (msState - m);
        const float levelDb = 10.f * std::log10 (msState + 1e-12f);

        if (! on)
            desired = 0.f;
        else if (levelDb > gate)
            desired = std::clamp (target - levelDb, -range, range) * amount;
        // else: hold (do not chase breaths, bleed or noise floor)

        gainDb = desired + speedCoeff * (gainDb - desired);
    }

    // Apply as a linear ramp across the (small) control block
    currentLin = dbToGain (gainDb);
    const float step = (currentLin - startLin) / (float) std::max (1, n);
    for (int c = 0; c < numCh; ++c)
    {
        float g = startLin;
        for (int i = 0; i < n; ++i) { g += step; ch[c][i] *= g; }
    }

    inst.riderGainDb = gainDb;
    stats.riderAbsGain.add (std::abs (gainDb));
}

//==============================================================================
// EqModule
static constexpr float kHpOffFreq = 10.f;
static constexpr float kLpOffFreq = 30000.f;

void EqModule::prepare (double s)
{
    sr = s;
    // control rate smoothing: modules are called with <= 32 sample blocks
    const double controlRate = sr / 32.0;
    hpFreqLog.setTime (40.f, controlRate);
    lpFreqLog.setTime (40.f, controlRate);
    for (int b = 0; b < kNumEqBands; ++b)
    {
        fLog[(size_t) b].setTime (40.f, controlRate);
        gain[(size_t) b].setTime (40.f, controlRate);
        qLog[(size_t) b].setTime (40.f, controlRate);
    }
    first = true;
    reset();
}

void EqModule::reset()
{
    hp1.reset(); hp2.reset(); lp.reset();
    for (auto& b : bands) b.reset();
}

void EqModule::updateControl (const ChainSettings& s, int)
{
    const bool eqOn = s.on (P::EqOn);
    const float hpTarget = (eqOn && s.on (P::HpfOn)) ? s[P::HpfFreq] : kHpOffFreq;
    const float lpTarget = (eqOn && s.on (P::LpfOn)) ? s[P::LpfFreq] : kLpOffFreq;
    hpFreqLog.setTarget (std::log (hpTarget));
    lpFreqLog.setTarget (std::log (std::min (lpTarget, (float) (0.45 * sr))));

    for (int b = 0; b < kNumEqBands; ++b)
    {
        const bool bandOn = eqOn && s.on (eqBandParam (b, 0));
        fLog[(size_t) b].setTarget (std::log (s[eqBandParam (b, 2)]));
        gain[(size_t) b].setTarget (bandOn ? s[eqBandParam (b, 3)] : 0.f);
        qLog[(size_t) b].setTarget (std::log (s[eqBandParam (b, 4)]));
    }
    if (first)
    {
        hpFreqLog.snap (hpFreqLog.target); lpFreqLog.snap (lpFreqLog.target);
        for (int b = 0; b < kNumEqBands; ++b)
        {
            fLog[(size_t) b].snap (fLog[(size_t) b].target);
            gain[(size_t) b].snap (gain[(size_t) b].target);
            qLog[(size_t) b].snap (qLog[(size_t) b].target);
        }
        first = false;
    }

    const float hpF = std::exp (hpFreqLog.next());
    const int slope = s.choice (P::HpfSlope);
    if (differs (hpF, lastHp) || slope != lastSlope)
    {
        if (slope == 0)
            hp1.setCoeffs (BiquadCoeffs::highPass (sr, hpF, 0.70710678));
        else
        {
            hp1.setCoeffs (BiquadCoeffs::highPass (sr, hpF, 0.54119610));
            hp2.setCoeffs (BiquadCoeffs::highPass (sr, hpF, 1.30656296));
        }
        lastHp = hpF; lastSlope = slope;
    }
    const float lpF = std::exp (lpFreqLog.next());
    if (differs (lpF, lastLp))
    {
        lp.setCoeffs (BiquadCoeffs::lowPass (sr, std::min ((double) lpF, 0.45 * sr), 0.70710678));
        lastLp = lpF;
    }

    for (int b = 0; b < kNumEqBands; ++b)
    {
        const float f = std::exp (fLog[(size_t) b].next());
        const float g = gain[(size_t) b].next();
        const float q = std::exp (qLog[(size_t) b].next());
        const int type = s.choice (eqBandParam (b, 1));
        if (differs (f, lastF[(size_t) b]) || differs (g, lastG[(size_t) b]) || differs (q, lastQ[(size_t) b]) || type != lastType[(size_t) b])
        {
            BiquadCoeffs c;
            if (type == 1)      c = BiquadCoeffs::lowShelf (sr, f, q, g);
            else if (type == 2) c = BiquadCoeffs::highShelf (sr, f, q, g);
            else                c = BiquadCoeffs::peak (sr, f, q, g);
            bands[(size_t) b].setCoeffs (c);
            lastF[(size_t) b] = f; lastG[(size_t) b] = g; lastQ[(size_t) b] = q; lastType[(size_t) b] = type;
        }
    }
}

void EqModule::process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept
{
    updateControl (s, n);

    const bool hpActive = lastHp > kHpOffFreq * 1.05f;
    const bool hp2Active = hpActive && lastSlope == 1;
    const bool lpActive = lastLp < 0.44f * (float) sr && lastLp < kLpOffFreq * 0.95f;

    for (int c = 0; c < numCh; ++c)
    {
        float* d = ch[c];
        if (hpActive)  for (int i = 0; i < n; ++i) d[i] = hp1.process (d[i], c);
        if (hp2Active) for (int i = 0; i < n; ++i) d[i] = hp2.process (d[i], c);
        for (int b = 0; b < kNumEqBands; ++b)
            if (std::abs (lastG[(size_t) b]) > 0.001f)
                for (int i = 0; i < n; ++i) d[i] = bands[(size_t) b].process (d[i], c);
        if (lpActive)  for (int i = 0; i < n; ++i) d[i] = lp.process (d[i], c);
    }
    hp1.sanitise(); hp2.sanitise(); lp.sanitise();
    for (auto& b : bands) b.sanitise();
}

double EqModule::responseDb (const ChainSettings& s, double sr, double f)
{
    if (! s.on (P::EqOn)) return 0.0;
    double mag = 1.0;
    if (s.on (P::HpfOn))
    {
        if (s.choice (P::HpfSlope) == 0)
            mag *= BiquadCoeffs::highPass (sr, s[P::HpfFreq], 0.70710678).magnitude (sr, f);
        else
            mag *= BiquadCoeffs::highPass (sr, s[P::HpfFreq], 0.54119610).magnitude (sr, f)
                 * BiquadCoeffs::highPass (sr, s[P::HpfFreq], 1.30656296).magnitude (sr, f);
    }
    if (s.on (P::LpfOn))
        mag *= BiquadCoeffs::lowPass (sr, std::min ((double) s[P::LpfFreq], 0.45 * sr), 0.70710678).magnitude (sr, f);
    for (int b = 0; b < kNumEqBands; ++b)
    {
        if (! s.on (eqBandParam (b, 0))) continue;
        const int type = s.choice (eqBandParam (b, 1));
        const double bf = s[eqBandParam (b, 2)], g = s[eqBandParam (b, 3)], q = s[eqBandParam (b, 4)];
        BiquadCoeffs c = type == 1 ? BiquadCoeffs::lowShelf (sr, bf, q, g)
                       : type == 2 ? BiquadCoeffs::highShelf (sr, bf, q, g)
                                   : BiquadCoeffs::peak (sr, bf, q, g);
        mag *= c.magnitude (sr, f);
    }
    return 20.0 * std::log10 (std::max (mag, 1e-9));
}

//==============================================================================
// ToneMatchModule: fixed-frequency smooth "tone match" curve for reference matching.
BiquadCoeffs ToneMatchModule::bandCoeffs (double sr, int band, double g)
{
    const double f = kToneMatchFreqs[(size_t) band];
    if (band == 0)                       return BiquadCoeffs::lowShelf (sr, f * 1.6, 0.6, g);
    if (band == kNumToneMatchBands - 1)  return BiquadCoeffs::highShelf (sr, f * 0.75, 0.6, g);
    return BiquadCoeffs::peak (sr, f, 0.95, g);
}

void ToneMatchModule::prepare (double s)
{
    sr = s;
    for (auto& sm : g) sm.setTime (60.f, sr / 32.0);
    first = true;
    lastG.fill (1e9f);
    reset();
}

void ToneMatchModule::reset() { for (auto& b : bands) b.reset(); }

void ToneMatchModule::process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept
{
    const float scale = s.on (P::TmOn) ? s[P::TmAmount] * 0.01f : 0.f;
    bool any = false;
    for (int b = 0; b < kNumToneMatchBands; ++b)
    {
        auto& sm = g[(size_t) b];
        sm.setTarget (s[P::TmG1 + b] * scale);
        if (first) sm.snap (sm.target);
        const float gv = sm.next();
        if (differs (gv, lastG[(size_t) b]))
        {
            bands[(size_t) b].setCoeffs (bandCoeffs (sr, b, gv));
            lastG[(size_t) b] = gv;
        }
        any = any || std::abs (gv) > 0.001f;
    }
    first = false;
    if (! any) return;

    for (int c = 0; c < numCh; ++c)
        for (int b = 0; b < kNumToneMatchBands; ++b)
            if (std::abs (lastG[(size_t) b]) > 0.001f)
                for (int i = 0; i < n; ++i) ch[c][i] = bands[(size_t) b].process (ch[c][i], c);
    for (auto& b : bands) b.sanitise();
}

double ToneMatchModule::responseDbForGains (const std::array<float, kNumToneMatchBands>& gainsDb, double sr, double f)
{
    double mag = 1.0;
    for (int b = 0; b < kNumToneMatchBands; ++b)
        if (std::abs (gainsDb[(size_t) b]) > 1e-4f)
            mag *= bandCoeffs (sr, b, gainsDb[(size_t) b]).magnitude (sr, f);
    return 20.0 * std::log10 (std::max (mag, 1e-9));
}

double ToneMatchModule::responseDb (const ChainSettings& s, double sr, double f)
{
    if (! s.on (P::TmOn)) return 0.0;
    std::array<float, kNumToneMatchBands> gains {};
    for (int b = 0; b < kNumToneMatchBands; ++b)
        gains[(size_t) b] = s[P::TmG1 + b] * s[P::TmAmount] * 0.01f;
    return responseDbForGains (gains, sr, f);
}

//==============================================================================
// DynamicEq: parallel band-pass dynamic cut. y = x + (g - 1) * bp(x) is exactly x when idle.
void DynamicEq::prepare (double s)
{
    sr = s;
    for (auto& b : bands)
    {
        b.lastF = b.lastQ = b.lastA = b.lastR = -1;
        b.enable.setTime (20.f, sr / 32.0);
    }
    reset();
}

void DynamicEq::reset()
{
    for (auto& b : bands) { b.bp.reset(); b.env.reset(); b.cutDb = 0; }
}

void DynamicEq::process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept
{
    const bool moduleOn = s.on (P::DeqOn);
    for (int bi = 0; bi < kNumDynBands; ++bi)
    {
        auto& b = bands[(size_t) bi];
        const bool on = moduleOn && s.on (dynBandParam (bi, 0));
        b.enable.setTarget (on ? 1.f : 0.f);
        const float en = b.enable.next();
        if (en < 1e-4f && ! on)
        {
            b.cutDb = 0; inst.dynCutDb[(size_t) bi] = 0;
            continue;
        }

        const float f = s[dynBandParam (bi, 1)], q = s[dynBandParam (bi, 2)];
        const float thr = s[dynBandParam (bi, 3)], range = s[dynBandParam (bi, 4)];
        const float at = s[dynBandParam (bi, 5)], rl = s[dynBandParam (bi, 6)];
        if (differs (f, b.lastF) || differs (q, b.lastQ))
        {
            b.bp.setCoeffs (BiquadCoeffs::bandPass (sr, f, q));
            b.lastF = f; b.lastQ = q;
        }
        if (differs (at, b.lastA) || differs (rl, b.lastR))
        {
            b.env.setTimes (at, rl, sr);
            b.lastA = at; b.lastR = rl;
        }

        constexpr float ratio = 4.f, knee = 4.f;
        for (int i = 0; i < n; ++i)
        {
            float bpv[kMaxChannels] = { 0.f, 0.f };
            float det = 0.f;
            for (int c = 0; c < numCh; ++c)
            {
                bpv[c] = b.bp.process (ch[c][i], c);
                det = std::max (det, std::abs (bpv[c]));
            }
            const float e = b.env.process (det);
            const float levelDb = gainToDb (e);
            const float cut = std::min (range, Compressor::gainReductionDb (levelDb, thr, ratio, knee)) * en;
            b.cutDb = cut;
            const float gm1 = dbToGain (-cut) - 1.f;
            for (int c = 0; c < numCh; ++c)
                ch[c][i] += gm1 * bpv[c];
            stats.dynCut[(size_t) bi].add (cut);
        }
        b.bp.sanitise();
        inst.dynCutDb[(size_t) bi] = b.cutDb;
    }
}

//==============================================================================
// Compressor: feed-forward, stereo linked, soft knee, log-domain smooth branching detector.
float Compressor::gainReductionDb (float x, float T, float R, float W) noexcept
{
    const float over = x - T;
    if (2.f * over < -W) return 0.f;
    if (W > 0.f && 2.f * std::abs (over) <= W)
    {
        const float t = over + W * 0.5f;
        return -(1.f / R - 1.f) * t * t / (2.f * W);
    }
    return over - over / R;
}

void Compressor::prepare (double s)
{
    sr = s;
    rmsCoeff = timeCoeff (8.f, sr);
    makeup.setTime (30.f, sr / 32.0);
    mix.setTime (30.f, sr / 32.0);
    enable.setTime (20.f, sr / 32.0);
    reset();
}

void Compressor::reset() { rmsState = 0.f; grSmoothed = 0.f; }

void Compressor::process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept
{
    const bool on = s.on (P::CompOn);
    enable.setTarget (on ? 1.f : 0.f);
    makeup.setTarget (s[P::CompMakeup]);
    mix.setTarget (s[P::CompMix] * 0.01f);
    const float en = enable.next();
    const float mk = makeup.next();
    const float mx = mix.next() * en;
    if (en < 1e-4f && ! on) { grSmoothed = 0.f; inst.compGrDb = 0.f; return; }

    const float T = s[P::CompThresh], R = s[P::CompRatio], W = s[P::CompKnee];
    const float aC = timeCoeff (s[P::CompAttack], sr), rC = timeCoeff (s[P::CompRelease], sr);
    const bool rms = s.choice (P::CompDetector) == 1;

    for (int i = 0; i < n; ++i)
    {
        float det = 0.f;
        for (int c = 0; c < numCh; ++c)
            det = std::max (det, std::abs (ch[c][i]));
        float levelDb;
        if (rms)
        {
            rmsState = det * det + rmsCoeff * (rmsState - det * det);
            levelDb = 10.f * std::log10 (rmsState + 1e-12f) + 3.01f;  // sine-calibrated
        }
        else
            levelDb = gainToDb (det);

        const float gr = gainReductionDb (levelDb, T, R, W);
        const float c = gr > grSmoothed ? aC : rC;
        grSmoothed = gr + c * (grSmoothed - gr);

        const float wet = dbToGain (mk - grSmoothed);
        const float g = 1.f + mx * (wet - 1.f);
        for (int cc = 0; cc < numCh; ++cc)
            ch[cc][i] *= g;
        stats.compGr.add (grSmoothed * en);
    }
    inst.compGrDb = grSmoothed * en;
}

//==============================================================================
// DeEsser: band detector with fast attack; split-band (high band only) or wide-band reduction.
void DeEsser::prepare (double s)
{
    sr = s;
    env.setTimes (0.3f, 60.f, sr);
    enable.setTime (20.f, sr / 32.0);
    lastF = -1;
    reset();
}

void DeEsser::reset() { detect.reset(); band.reset(); env.reset(); grDb = 0; }

void DeEsser::process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept
{
    const bool on = s.on (P::DessOn);
    enable.setTarget (on ? 1.f : 0.f);
    const float en = enable.next();
    if (en < 1e-4f && ! on) { grDb = 0; inst.dessGrDb = 0; return; }

    const float f = s[P::DessFreq];
    if (differs (f, lastF))
    {
        detect.setCoeffs (BiquadCoeffs::bandPass (sr, f, 1.1));
        // Split mode reduces a wide band centred on the sibilance. A zero-phase-at-centre
        // band-pass keeps x + (g-1)*band(x) phase coherent (a high-pass split is ~60 deg off
        // near the crossover and would cancel only ~1 dB) and exactly transparent when idle.
        band.setCoeffs (BiquadCoeffs::bandPass (sr, f, 0.75));
        lastF = f;
    }
    const float thr = s[P::DessThresh], range = s[P::DessRange];
    const bool wide = s.choice (P::DessMode) == 1;

    for (int i = 0; i < n; ++i)
    {
        float det = 0.f;
        float hi[kMaxChannels] = { 0.f, 0.f };
        for (int c = 0; c < numCh; ++c)
        {
            det = std::max (det, std::abs (detect.process (ch[c][i], c)));
            hi[c] = band.process (ch[c][i], c);
        }
        const float e = env.process (det);
        const float gr = std::min (range, Compressor::gainReductionDb (gainToDb (e), thr, 5.f, 3.f)) * en;
        grDb = gr;
        const float g = dbToGain (-gr);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] = wide ? ch[c][i] * g : ch[c][i] + (g - 1.f) * hi[c];
        stats.dessGr.add (gr);
    }
    detect.sanitise(); band.sanitise();
    inst.dessGrDb = grDb;
}

//==============================================================================
// ColorModule: 2x oversampled saturation with auto gain, bias (tube), warmth tilt, and a
// latency-matched dry path so on/off and mix changes never click or comb-filter.
float ColorModule::shape (int type, float x, float d) noexcept
{
    switch (type)
    {
        case 1: // Tube: biased tanh -> even harmonics (DC removed downstream)
        {
            constexpr float bias = 0.22f;
            return (std::tanh (d * x + bias) - std::tanh (bias)) / d;
        }
        case 2: // Soft clip (cubic)
        {
            float v = d * x;
            v = std::clamp (v, -1.f, 1.f);
            return (1.5f * (v - v * v * v / 3.f)) / d;
        }
        default: // Tape: symmetric tanh
            return std::tanh (d * x) / d;
    }
}

void ColorModule::prepare (double s, int)
{
    sr = s;
    dryDelay.prepare (Halfband2x::kLatency + 8, kMaxChannels);
    dcBlock.setCoeffs (BiquadCoeffs::highPass (sr, 8.0, 0.70710678));
    drive.setTime (30.f, sr / 32.0);
    blend.setTime (25.f, sr / 32.0);
    warmth.setTime (40.f, sr / 32.0);
    lastWarm = 1e9f;
    reset();
}

void ColorModule::reset()
{
    os.reset(); dryDelay.reset(); dcBlock.reset(); warmLow.reset(); warmHigh.reset();
}

void ColorModule::process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept
{
    const bool on = s.on (P::ColOn);
    blend.setTarget (on ? s[P::ColMix] * 0.01f : 0.f);
    drive.setTarget (s[P::ColDrive]);
    warmth.setTarget (s[P::ColWarmth] * 0.01f);
    const float bl = blend.next();
    const float dDb = drive.next();
    const float w = warmth.next();
    const float d = dbToGain (dDb);
    const int type = s.choice (P::ColType);

    if (differs (w, lastWarm))
    {
        warmLow.setCoeffs (BiquadCoeffs::lowShelf (sr, 250.0, 0.7, 3.0 * w));
        warmHigh.setCoeffs (BiquadCoeffs::highShelf (sr, 5500.0, 0.7, -3.5 * w));
        lastWarm = w;
    }

    const bool wetNeeded = bl > 1e-4f;
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < numCh; ++c)
        {
            const float x = ch[c][i];
            dryDelay.push (c, x);
            const float dry = dryDelay.read (c, Halfband2x::kLatency);
            float y0, y1;
            os.upsample (c, x, y0, y1);   // always run to keep filter state continuous
            if (wetNeeded)
            {
                y0 = shape (type, y0, d);
                y1 = shape (type, y1, d);
            }
            float wet = os.downsample (c, y0, y1);
            if (wetNeeded)
            {
                wet = dcBlock.process (wet, c);
                if (std::abs (w) > 0.001f)
                    wet = warmHigh.process (warmLow.process (wet, c), c);
                ch[c][i] = dry + bl * (wet - dry);
            }
            else
                ch[c][i] = dry;
        }
        dryDelay.advance();
    }
    dcBlock.sanitise(); warmLow.sanitise(); warmHigh.sanitise();
}

//==============================================================================
// RhythmGate: tempo-synced gate following the host transport (or an internal clock).
double RhythmGate::stepLengthBeats (int division)
{
    switch (division)
    {
        case 0: return 1.0;          // 1/4
        case 1: return 0.5;          // 1/8
        case 2: return 1.0 / 3.0;    // 1/8 triplet
        case 3: return 0.25;         // 1/16
        case 4: return 1.0 / 6.0;    // 1/16 triplet
        case 5: return 0.125;        // 1/32
        default: return 0.25;
    }
}

bool RhythmGate::stepOpen (int pattern, int step, double phase)
{
    static constexpr bool kBroken[16] = { 1,0,1,1, 0,1,0,0, 1,0,1,1, 0,1,1,0 };
    static constexpr bool kStutter[8] = { 1,1,0, 1,1,0, 1,0 };
    switch (pattern)
    {
        case 0: return phase < 0.5;                                // straight chop
        case 1: return (step & 1) == 1;                            // offbeat
        case 2: return kStutter[step % 8] && phase < 0.8;          // 3-3-2
        case 3: return kBroken[step % 16] && phase < 0.85;         // broken
        case 4: return (step % 4) < 2;                             // half-time
        default: return true;
    }
}

void RhythmGate::prepare (double s)
{
    sr = s;
    enable.setTime (20.f, sr / 32.0);
    reset();
}

void RhythmGate::reset() { gain = 1.f; }

void RhythmGate::process (float* const* ch, int numCh, int n, const ChainSettings& s, const ProcessContext& ctx) noexcept
{
    const bool on = s.on (P::GateOn);
    enable.setTarget (on ? 1.f : 0.f);
    const float en = enable.next();
    if (en < 1e-4f && ! on) { gain = 1.f; return; }

    coeff = timeCoeff (s[P::GateSmooth], sr);
    const double stepBeats = stepLengthBeats (s.choice (P::GateDiv));
    const int pattern = s.choice (P::GatePattern);
    const float depth = s[P::GateDepth] * 0.01f;
    const double beatsPerSample = ctx.bpm / 60.0 / sr;
    const double startBeat = ctx.hasPpq ? ctx.ppqAtBlockStart : (double) ctx.freeRunSample * beatsPerSample;

    for (int i = 0; i < n; ++i)
    {
        const double beat = startBeat + i * beatsPerSample;
        const double stepPos = beat / stepBeats;
        const auto stepIndex = (int) std::floor (stepPos);
        const double phase = stepPos - std::floor (stepPos);
        const bool open = stepOpen (pattern, ((stepIndex % 64) + 64) % 64, phase);
        const float target = open ? 1.f : 1.f - depth;
        gain = target + coeff * (gain - target);
        const float g = 1.f + en * (gain - 1.f);
        for (int c = 0; c < numCh; ++c)
            ch[c][i] *= g;
    }
}

//==============================================================================
void ImageModule::prepare (double s)
{
    sr = s;
    width.setTime (40.f, sr / 32.0);
    monoBlend.setTime (40.f, sr / 32.0);
    lastF = -1;
    reset();
}

void ImageModule::reset() { sideHp.reset(); }

void ImageModule::process (float* const* ch, int numCh, int n, const ChainSettings& s) noexcept
{
    if (numCh < 2) return;
    width.setTarget (s[P::ImgWidth] * 0.01f);
    monoBlend.setTarget (s.on (P::ImgMonoBassOn) ? 1.f : 0.f);
    const float w = width.next();
    const float mb = monoBlend.next();
    if (std::abs (w - 1.f) < 1e-4f && mb < 1e-4f) return;

    const float f = s[P::ImgMonoBass];
    if (differs (f, lastF))
    {
        sideHp.setCoeffs (BiquadCoeffs::highPass (sr, f, 0.70710678));
        lastF = f;
    }
    for (int i = 0; i < n; ++i)
    {
        const float L = ch[0][i], R = ch[1][i];
        const float M = 0.5f * (L + R);
        float S = 0.5f * (L - R);
        if (mb > 1e-4f)
        {
            const float hp = sideHp.process (S, 0);
            S = S + mb * (hp - S);
        }
        S *= w;
        ch[0][i] = M + S;
        ch[1][i] = M - S;
    }
    sideHp.sanitise();
}

//==============================================================================
// Limiter: lookahead peak limiter (sliding minimum + box-filter attack + exponential release).
void Limiter::prepare (double s)
{
    sr = s;
    lookahead = std::max (8, (int) std::lround (0.0015 * sr));
    audioDelay.prepare (lookahead + 4, kMaxChannels);
    dqCap = lookahead + 4;
    dqVal.assign ((size_t) dqCap, 1.f);
    dqIdx.assign ((size_t) dqCap, 0);
    boxBuf.assign ((size_t) lookahead, 1.f);
    drive.setTime (30.f, sr / 32.0);
    ceiling.setTime (30.f, sr / 32.0);
    enable.setTime (20.f, sr / 32.0);
    reset();
}

void Limiter::reset()
{
    audioDelay.reset();
    dqHead = dqTail = 0;
    sampleIndex = 0;
    std::fill (boxBuf.begin(), boxBuf.end(), 1.f);
    boxSum = (double) lookahead;
    boxPos = 0;
    relGain = 1.f;
}

void Limiter::process (float* const* ch, int numCh, int n, const ChainSettings& s, ChainStats& stats, ChainInstant& inst) noexcept
{
    const bool on = s.on (P::LimOn);
    enable.setTarget (on ? 1.f : 0.f);
    drive.setTarget (on ? s[P::LimGain] : 0.f);
    ceiling.setTarget (s[P::LimCeiling]);
    const float en = enable.next();
    const float dr = dbToGain (drive.next());
    const float ceil = dbToGain (ceiling.next());
    const float relC = timeCoeff (s[P::LimRelease], sr);

    float lastGr = 0.f;
    for (int i = 0; i < n; ++i)
    {
        float peak = 0.f;
        for (int c = 0; c < numCh; ++c)
        {
            const float x = ch[c][i] * dr;
            audioDelay.push (c, x);
            peak = std::max (peak, std::abs (x));
        }
        audioDelay.advance();

        const float req = peak > ceil ? ceil / peak : 1.f;

        // sliding window minimum over the last (lookahead + 1) samples
        while (dqTail != dqHead)
        {
            const int last = (dqTail - 1 + dqCap) % dqCap;
            if (dqVal[(size_t) last] >= req) dqTail = last; else break;
        }
        dqVal[(size_t) dqTail] = req; dqIdx[(size_t) dqTail] = sampleIndex;
        dqTail = (dqTail + 1) % dqCap;
        while (dqIdx[(size_t) dqHead] < sampleIndex - lookahead)
            dqHead = (dqHead + 1) % dqCap;
        const float held = dqVal[(size_t) dqHead];
        ++sampleIndex;

        // instant attack to the held value, exponential release
        relGain = held < relGain ? held : held + relC * (relGain - held);

        // box filter (length = lookahead) smooths the attack so it lands on the peak
        boxSum += (double) relGain - (double) boxBuf[(size_t) boxPos];
        boxBuf[(size_t) boxPos] = relGain;
        boxPos = (boxPos + 1) % lookahead;
        if ((sampleIndex & 0x3fff) == 0)
        {   // periodically re-sync the running sum to avoid floating-point drift
            double sum = 0; for (float v : boxBuf) sum += v; boxSum = sum;
        }
        float g = (float) (boxSum / (double) lookahead);
        g = std::min (g, 1.f);

        for (int c = 0; c < numCh; ++c)
        {
            const float delayed = audioDelay.read (c, lookahead + 1);
            float limited = delayed * g;
            limited = std::clamp (limited, -ceil, ceil);   // final safety
            const float bypassed = audioDelay.read (c, lookahead + 1) / dr;
            ch[c][i] = bypassed + en * (limited - bypassed);
        }
        lastGr = -gainToDb (g) * en;
        stats.limGr.add (lastGr);
    }
    inst.limGrDb = lastGr;
}

} // namespace nova::dsp
