#include "Semantic.h"
#include "Analyzer.h"

#include <algorithm>
#include <cmath>

namespace nova::analysis
{

const char* workModeName (WorkMode m)
{
    switch (m)
    {
        case WorkMode::Vocal:  return "vocal";
        case WorkMode::Mix:    return "mix";
        case WorkMode::Master: return "master";
    }
    return "vocal";
}

const Observation* SemanticProfile::find (const std::string& id) const
{
    for (auto& p : problems) if (p.id == id) return &p;
    return nullptr;
}

static float clamp01 (float v) { return std::clamp (v, 0.f, 1.f); }

static std::string fmt (float v, int decimals = 1)
{
    return juce::String (v, decimals).toStdString();
}

static std::string hz (float v)
{
    return v >= 1000.f ? juce::String (v / 1000.f, 1).toStdString() + " kHz" : juce::String (juce::roundToInt (v)).toStdString() + " Hz";
}

SemanticProfile describe (const AudioFeatures& f, WorkMode mode)
{
    SemanticProfile s;
    s.source = f.source;
    s.sourceConfidence = f.sourceConfidence;
    s.enoughAudio = f.valid && f.activeSec >= 3.0;
    if (! f.valid)
        return s;

    const bool vocal = mode == WorkMode::Vocal;
    const float phraseMedian = [&]
    {
        std::vector<float> v;
        for (auto& p : f.phrases) v.push_back (p.loudnessDb);
        return v.empty() ? f.integratedLufs : median (v);
    }();
    const float amountOfAudio = clamp01 ((float) f.activeSec / 10.f);   // less audio -> less confidence

    auto add = [&] (std::string id, std::string title, float sev, float conf, float freq, std::string evidence)
    {
        if (sev <= 0.f || conf <= 0.f) return;
        s.problems.push_back ({ std::move (id), std::move (title), clamp01 (sev), clamp01 (conf * (0.5f + 0.5f * amountOfAudio)), freq, std::move (evidence) });
    };

    //--------------------------------------------------------------------------
    // Level consistency
    if (f.phrases.size() >= 3)
    {
        const float sd = f.phraseLevelStdDb;
        if (sd > 2.5f)
            add ("inconsistent_level", "Phrase-to-phrase level varies a lot", (sd - 2.f) / 5.f,
                 f.phrases.size() >= 5 ? 0.85f : 0.6f, 0,
                 "phrase loudness std " + fmt (sd) + " dB over " + std::to_string (f.phrases.size()) + " phrases; macro range " + fmt (f.dynamicRangeDb) + " dB");
    }
    else if (f.shortTermStdDb > 3.5f)
        add ("inconsistent_level", "Short-term loudness moves around", (f.shortTermStdDb - 3.f) / 5.f, 0.5f, 0,
             "short-term loudness std " + fmt (f.shortTermStdDb) + " dB");

    if (f.microDynamicsDb > 14.f && vocal)
        add ("word_level_spikes", "Individual words/syllables jump out", (f.microDynamicsDb - 12.f) / 8.f, 0.55f, 0,
             "median 10-90% level swing inside phrases " + fmt (f.microDynamicsDb) + " dB");

    //--------------------------------------------------------------------------
    // Sibilance (event based, relative to this voice's own vowel level)
    if (! f.sibilantEvents.empty())
    {
        const float rate = (float) f.sibilantEvents.size() / (float) std::max (1.0, f.activeSec);
        if (f.sibilanceSeverityDb > -6.f)
            add ("sibilance", "Sibilants are loud relative to the voice", (f.sibilanceSeverityDb + 6.f) / 9.f,
                 f.sibilantEvents.size() >= 5 ? 0.8f : 0.55f, f.sibilanceCenterHz,
                 std::to_string (f.sibilantEvents.size()) + " sibilant events (" + fmt (rate) + "/s), loudest 10% reach "
                     + fmt (f.sibilanceSeverityDb) + " dB vs vowel level, centred near " + hz (f.sibilanceCenterHz));
    }

    //--------------------------------------------------------------------------
    // Harshness (event based: loud moments whose 2-5 kHz share jumps above this source's norm)
    if (f.harshEvents.size() >= 2 && f.harshSeverityDb > 4.5f)
    {
        std::vector<int> phrasesWith;
        for (size_t i = 0; i < f.phrases.size(); ++i) if (f.phrases[i].harshEvents > 0) phrasesWith.push_back ((int) i);
        add ("harshness", "Loud moments turn harsh in the upper mids", (f.harshSeverityDb - 4.f) / 6.f,
             f.harshEvents.size() >= 4 ? 0.8f : 0.6f, f.harshCenterHz,
             std::to_string (f.harshEvents.size()) + " harsh events in " + std::to_string (phrasesWith.size()) + " of "
                 + std::to_string (f.phrases.size()) + " phrases, up to +" + fmt (f.harshSeverityDb) + " dB above normal 2-5 kHz share, centred near "
                 + hz (f.harshCenterHz));
    }
    if (f.upperMidDb > -7.f)
        add ("upper_mid_heavy", "Overall balance leans hard into the upper mids", (f.upperMidDb + 8.f) / 5.f, 0.45f, 3000.f,
             "2-5 kHz holds " + fmt (f.upperMidDb) + " dB of total energy");

    //--------------------------------------------------------------------------
    // Tonal balance (prior based -> moderate confidence)
    const float mudIndex = f.lowMidDb - f.presenceDb;
    if (vocal && mudIndex > 11.f)
        add ("low_mid_congestion", "Low-mids are crowding the voice", (mudIndex - 10.f) / 8.f, 0.5f, 350.f,
             "250-500 Hz is " + fmt (mudIndex) + " dB above 1.5-4 kHz presence");
    for (auto& r : f.resonances)
    {
        if (r.freqHz >= 180.f && r.freqHz <= 650.f && r.prominenceDb > 4.f)
            add ("boxiness", "Boxy resonance", (r.prominenceDb - 3.f) / 6.f, 0.55f, r.freqHz,
                 "narrow peak at " + hz (r.freqHz) + ", " + fmt (r.prominenceDb) + " dB above the smoothed spectrum");
        else if (vocal && r.freqHz > 650.f && r.freqHz <= 1600.f && r.prominenceDb > 4.5f)
            add ("nasality", "Nasal/honky resonance", (r.prominenceDb - 3.5f) / 6.f, 0.45f, r.freqHz,
                 "narrow peak at " + hz (r.freqHz) + ", " + fmt (r.prominenceDb) + " dB prominent");
        else if (r.freqHz > 1600.f && r.freqHz <= 6000.f && r.prominenceDb > 5.f)
            add ("resonance", "Ringing upper-mid resonance", (r.prominenceDb - 4.f) / 6.f, 0.5f, r.freqHz,
                 "narrow peak at " + hz (r.freqHz) + ", " + fmt (r.prominenceDb) + " dB prominent");
    }
    if (vocal && f.presenceDb < -17.f)
        add ("lack_of_presence", "Voice lacks presence / intelligibility", (-15.f - f.presenceDb) / 8.f, 0.5f, f.presencePeakHz > 0 ? f.presencePeakHz : 3000.f,
             "1.5-4 kHz holds only " + fmt (f.presenceDb) + " dB of total energy");
    if (f.sampleRate >= 40000 && f.airDb < -38.f && f.centroidHz < 2500.f)
        add ("lack_of_air", "Top end (air) is missing", (-36.f - f.airDb) / 10.f, 0.5f, 12000.f,
             "10-20 kHz holds " + fmt (f.airDb) + " dB of total energy, centroid " + hz (f.centroidHz));
    if (f.centroidHz > 3200.f && f.tiltDbPerOct > -1.f)
        add ("excessive_brightness", "Overall very bright", (f.centroidHz - 3000.f) / 2000.f, 0.45f, 6000.f,
             "spectral centroid " + hz (f.centroidHz) + ", tilt " + fmt (f.tiltDbPerOct) + " dB/oct");
    if (vocal && f.subDb > -30.f)
        add ("rumble", "Sub-bass rumble under the voice", (f.subDb + 32.f) / 12.f, 0.7f, 50.f,
             "below 60 Hz holds " + fmt (f.subDb) + " dB of total energy");
    if (vocal && f.lowDb > -4.f && f.f0MedianHz > 90.f)
        add ("proximity_boom", "Boomy low end (proximity effect)", (f.lowDb + 5.f) / 6.f, 0.5f, 150.f,
             "60-250 Hz holds " + fmt (f.lowDb) + " dB of total energy with f0 around " + hz (f.f0MedianHz));
    if (vocal && (std::pow (10.f, f.lowDb / 10.f) + std::pow (10.f, f.lowMidDb / 10.f)) < 0.06f)
        add ("thinness", "Voice sounds thin (little body)", 0.5f, 0.4f, 200.f,
             "60-500 Hz holds only " + fmt (10.f * std::log10 (std::pow (10.f, f.lowDb / 10.f) + std::pow (10.f, f.lowMidDb / 10.f))) + " dB of total energy");

    //--------------------------------------------------------------------------
    // Recording problems
    if (! f.plosiveEvents.empty() && vocal)
        add ("plosives", "Plosive pops (P/B bursts)", std::min (1.f, (float) f.plosiveEvents.size() / 4.f), 0.6f, 80.f,
             std::to_string (f.plosiveEvents.size()) + " low-frequency bursts detected");
    const float snr = phraseMedian - f.noiseFloorDb;
    if (f.silenceRatio > 0.05f && snr < 42.f && f.noiseFloorDb > -90.f)
        add ("noise", "Audible noise floor between phrases", (42.f - snr) / 20.f, f.silenceRatio > 0.15f ? 0.65f : 0.4f, 0,
             "noise floor " + fmt (f.noiseFloorDb) + " dB, only " + fmt (snr) + " dB below phrase level");
    if (f.clippedSamples > 0)
        add ("clipping", "Clipped samples", std::min (1.f, f.clippedSamples / 50.f), 0.95f, 0,
             std::to_string (f.clippedSamples) + " samples at full scale in runs of 3+");
    if (std::abs (f.dcOffset) > 0.005f)
        add ("dc_offset", "DC offset", std::min (1.f, std::abs (f.dcOffset) / 0.05f), 0.95f, 0, "mean sample value " + fmt (f.dcOffset, 4));
    if (f.clickEvents.size() > 3)
        add ("clicks", "Clicks / mouth noise", std::min (1.f, f.clickEvents.size() / 20.f), 0.4f, 0,
             std::to_string (f.clickEvents.size()) + " isolated transient spikes");
    if (vocal && f.spaceConfidence > 0.3f && f.tailToDirectDb > -24.f)
        add ("roomy_recording", "Room/reverb already baked into the recording", (f.tailToDirectDb + 30.f) / 15.f, 0.7f * f.spaceConfidence, 0,
             "level 80 ms after phrase ends is " + fmt (f.tailToDirectDb) + " dB re phrase level, decay ~" + fmt (f.decayTimeSec, 2) + " s");
    if (f.crestDb < 9.f && f.microDynamicsDb < 5.f && f.phrases.size() > 2)
        add ("over_compressed", "Already heavily compressed / limited", (10.f - f.crestDb) / 5.f, 0.5f, 0,
             "crest factor " + fmt (f.crestDb) + " dB, in-phrase swing " + fmt (f.microDynamicsDb) + " dB");

    //--------------------------------------------------------------------------
    // Stereo
    if (f.isStereo && ! f.isEffectivelyMono)
    {
        if (f.monoLossDb < -3.f || f.correlationLow < -0.1f)
            add ("mono_compatibility", "Mono compatibility risk (phase)", clamp01 ((-f.monoLossDb - 2.f) / 4.f + (f.correlationLow < 0 ? 0.3f : 0.f)), 0.8f, 0,
                 "mono sum loses " + fmt (-f.monoLossDb) + " dB, worst-case correlation " + fmt (f.correlationLow, 2));
        if (std::abs (f.balanceDb) > 1.5f)
            add ("stereo_imbalance", "Left/right imbalance", (std::abs (f.balanceDb) - 1.f) / 4.f, 0.75f, 0,
                 "L/R energy differs by " + fmt (f.balanceDb) + " dB");
        if (std::abs (f.interChannelDelayMs) > 0.05f)
            add ("channel_delay", "Channels are offset in time", std::abs (f.interChannelDelayMs), 0.6f, 0,
                 "inter-channel delay " + fmt (f.interChannelDelayMs, 2) + " ms");
    }

    //--------------------------------------------------------------------------
    // Mix / master specifics
    if (mode != WorkMode::Vocal)
    {
        if (f.truePeakDb > -1.f)
            add ("true_peak_over", "True peak above -1 dBTP", (f.truePeakDb + 1.5f) / 2.f, 0.9f, 0, "true peak " + fmt (f.truePeakDb) + " dBTP");
        if (f.plr < 7.f && f.integratedLufs > -30.f)
            add ("over_limited", "Peak-to-loudness ratio is low (over-limited)", (8.f - f.plr) / 4.f, 0.65f, 0,
                 "PLR " + fmt (f.plr) + " dB, crest " + fmt (f.crestDb) + " dB");
        if (f.subDb > -8.f)
            add ("excess_sub", "Sub-bass dominates", (f.subDb + 10.f) / 6.f, 0.45f, 45.f, "below 60 Hz holds " + fmt (f.subDb) + " dB");
        else if (f.subDb < -28.f && mode == WorkMode::Master)
            add ("lacks_sub", "Little sub-bass weight", (-26.f - f.subDb) / 10.f, 0.35f, 50.f, "below 60 Hz holds only " + fmt (f.subDb) + " dB");
        if (f.isStereo && f.sideToMidDb < -24.f && ! f.isEffectivelyMono)
            add ("narrow_image", "Stereo image is narrow", (-22.f - f.sideToMidDb) / 12.f, 0.45f, 0, "side is " + fmt (-f.sideToMidDb) + " dB below mid");
        if (f.sections.size() >= 2)
        {
            float lo = 1e9f, hiv = -1e9f;
            for (auto& sec : f.sections) { lo = std::min (lo, sec.loudnessDb); hiv = std::max (hiv, sec.loudnessDb); }
            if (hiv - lo > 9.f)
                add ("section_imbalance", "Large loudness jumps between sections", (hiv - lo - 8.f) / 6.f, 0.5f, 0,
                     "section loudness spans " + fmt (hiv - lo) + " LU");
        }
    }

    std::sort (s.problems.begin(), s.problems.end(), [] (const Observation& a, const Observation& b)
               { return a.severity * a.confidence > b.severity * b.confidence; });

    //--------------------------------------------------------------------------
    // Character tags
    auto tag = [&] (const char* t, float conf, std::string ev) { if (conf > 0.15f) s.character.push_back ({ t, clamp01 (conf), std::move (ev) }); };
    if (f.centroidHz > 2600.f) tag ("bright", (f.centroidHz - 2200.f) / 2000.f, "centroid " + hz (f.centroidHz));
    if (f.centroidHz < 1300.f) tag ("dark", (1600.f - f.centroidHz) / 800.f, "centroid " + hz (f.centroidHz));
    if (f.lowMidDb > -5.f && f.presenceDb < -12.f) tag ("warm", 0.5f, "strong 250-500 Hz, soft presence");
    if (f.crestDb > 16.f || f.phraseLevelStdDb > 4.f) tag ("dynamic", 0.4f + (f.crestDb - 14.f) / 10.f, "crest " + fmt (f.crestDb) + " dB, phrase std " + fmt (f.phraseLevelStdDb) + " dB");
    if (f.crestDb < 11.f && f.phraseLevelStdDb < 2.f) tag ("controlled", 0.6f, "crest " + fmt (f.crestDb) + " dB");
    if (f.spaceConfidence > 0.3f)
    {
        if (f.tailToDirectDb < -35.f) tag ("dry", f.spaceConfidence, "tail " + fmt (f.tailToDirectDb) + " dB at 80 ms");
        else if (f.tailToDirectDb > -24.f) tag ("roomy", f.spaceConfidence, "tail " + fmt (f.tailToDirectDb) + " dB at 80 ms");
        if (f.tailToDirectDb < -35.f && f.presenceDb > -15.f) tag ("intimate", 0.5f * f.spaceConfidence, "dry and present");
        if (f.tailToDirectDb > -22.f && f.presenceDb < -15.f) tag ("distant", 0.5f * f.spaceConfidence, "roomy with soft presence");
    }
    if (f.hnrDb > 0 && f.hnrDb < 8.f && f.voicedRatio > 0.3f) tag ("breathy", (10.f - f.hnrDb) / 8.f, "HNR " + fmt (f.hnrDb) + " dB");
    if (f.voicedRatio > 0.3f && f.pitchJitterCents > 0 && f.pitchJitterCents < 15.f) tag ("steady_pitch", 0.5f, "median frame-to-frame pitch change " + fmt (f.pitchJitterCents) + " cents");
    if (s.find ("sibilance") != nullptr) tag ("sibilant", s.find ("sibilance")->confidence, s.find ("sibilance")->evidence);
    if (s.find ("harshness") != nullptr) tag ("harsh_on_loud_notes", s.find ("harshness")->confidence, s.find ("harshness")->evidence);

    //--------------------------------------------------------------------------
    // Treatment recommendations derived from problems (the engineer still verifies them)
    auto rec = [&] (const char* r) { if (std::find (s.recommendations.begin(), s.recommendations.end(), r) == s.recommendations.end()) s.recommendations.push_back (r); };
    for (auto& p : s.problems)
    {
        if (p.confidence < 0.35f) continue;
        if (p.id == "inconsistent_level") { rec ("macro_leveling"); rec ("gentle_compression"); }
        else if (p.id == "word_level_spikes") rec ("fast_peak_control");
        else if (p.id == "sibilance") rec ("selective_deessing");
        else if (p.id == "harshness" || p.id == "resonance") rec ("dynamic_harshness_control");
        else if (p.id == "upper_mid_heavy" || p.id == "excessive_brightness") rec ("broad_upper_mid_reduction");
        else if (p.id == "low_mid_congestion" || p.id == "boxiness") rec ("low_mid_cleanup");
        else if (p.id == "nasality") rec ("nasal_notch");
        else if (p.id == "lack_of_presence") rec ("presence_recovery");
        else if (p.id == "lack_of_air") rec ("air_shelf");
        else if (p.id == "rumble" || p.id == "plosives" || p.id == "dc_offset") rec ("high_pass");
        else if (p.id == "proximity_boom") rec ("low_shelf_cut");
        else if (p.id == "true_peak_over" || p.id == "over_limited") rec ("true_peak_safe_limiting");
        else if (p.id == "mono_compatibility") rec ("mono_bass");
    }

    s.dynamicsSummary = "integrated " + fmt (f.integratedLufs) + " LUFS, LRA " + fmt (f.lra) + " LU, crest " + fmt (f.crestDb)
                        + " dB, phrase std " + fmt (f.phraseLevelStdDb) + " dB, macro range " + fmt (f.dynamicRangeDb) + " dB";
    s.spaceSummary = f.spaceConfidence > 0.f
                         ? "estimated decay ~" + fmt (f.decayTimeSec, 2) + " s, tail " + fmt (f.tailToDirectDb) + " dB at 80 ms (confidence " + fmt (f.spaceConfidence, 2) + ")"
                         : "space could not be estimated (no clear phrase endings)";
    return s;
}

//==============================================================================
static double r1 (double v) { return std::round (v * 10.0) / 10.0; }
static double r2 (double v) { return std::round (v * 100.0) / 100.0; }

juce::var featuresToJson (const AudioFeatures& f, bool includeEvents)
{
    auto* o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("valid", f.valid);
    o->setProperty ("duration_s", r1 (f.durationSec));
    o->setProperty ("active_s", r1 (f.activeSec));
    o->setProperty ("sample_rate", f.sampleRate);

    auto* lv = new juce::DynamicObject();
    lv->setProperty ("peak_dbfs", r1 (f.peakDb));
    lv->setProperty ("true_peak_dbtp", r1 (f.truePeakDb));
    lv->setProperty ("rms_dbfs", r1 (f.rmsDb));
    lv->setProperty ("crest_db", r1 (f.crestDb));
    lv->setProperty ("integrated_lufs", r1 (f.integratedLufs));
    lv->setProperty ("short_term_max_lufs", r1 (f.shortTermMaxLufs));
    lv->setProperty ("momentary_max_lufs", r1 (f.momentaryMaxLufs));
    lv->setProperty ("lra_lu", r1 (f.lra));
    lv->setProperty ("plr_db", r1 (f.plr));
    lv->setProperty ("noise_floor_db", r1 (f.noiseFloorDb));
    lv->setProperty ("macro_range_db", r1 (f.dynamicRangeDb));
    lv->setProperty ("phrase_level_std_db", r1 (f.phraseLevelStdDb));
    lv->setProperty ("short_term_std_db", r1 (f.shortTermStdDb));
    lv->setProperty ("in_phrase_swing_db", r1 (f.microDynamicsDb));
    lv->setProperty ("clipped_samples", f.clippedSamples);
    lv->setProperty ("dc_offset", r2 (f.dcOffset * 100.0) / 100.0);
    o->setProperty ("level", juce::var (lv));

    auto* sp = new juce::DynamicObject();
    sp->setProperty ("centroid_hz", juce::roundToInt (f.centroidHz));
    sp->setProperty ("rolloff85_hz", juce::roundToInt (f.rolloffHz));
    sp->setProperty ("tilt_db_per_oct", r2 (f.tiltDbPerOct));
    sp->setProperty ("flatness", r2 (f.flatness));
    auto* bands = new juce::DynamicObject();
    bands->setProperty ("sub_20_60", r1 (f.subDb));
    bands->setProperty ("low_60_250", r1 (f.lowDb));
    bands->setProperty ("lowmid_250_500", r1 (f.lowMidDb));
    bands->setProperty ("mid_500_2k", r1 (f.midDb));
    bands->setProperty ("presence_1k5_4k", r1 (f.presenceDb));
    bands->setProperty ("uppermid_2k_5k", r1 (f.upperMidDb));
    bands->setProperty ("sibilance_5k_10k", r1 (f.sibilanceBandDb));
    bands->setProperty ("air_10k_20k", r1 (f.airDb));
    sp->setProperty ("band_share_db_re_total", juce::var (bands));
    juce::Array<juce::var> toct;
    for (auto v : f.thirdOctaveDb) toct.add (r1 (v));
    sp->setProperty ("third_octave_db_re_total", toct);
    juce::Array<juce::var> res;
    for (auto& r : f.resonances)
    {
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("hz", juce::roundToInt (r.freqHz));
        ro->setProperty ("prominence_db", r1 (r.prominenceDb));
        res.add (juce::var (ro));
    }
    sp->setProperty ("resonances", res);
    o->setProperty ("spectrum", juce::var (sp));

    auto* tr = new juce::DynamicObject();
    tr->setProperty ("onsets_per_s", r2 (f.onsetRate));
    tr->setProperty ("attack_db_per_ms", r2 (f.attackSharpness));
    tr->setProperty ("transient_crest_db", r1 (f.transientCrestDb));
    o->setProperty ("transients", juce::var (tr));

    auto* vo = new juce::DynamicObject();
    vo->setProperty ("voiced_ratio", r2 (f.voicedRatio));
    vo->setProperty ("f0_median_hz", juce::roundToInt (f.f0MedianHz));
    vo->setProperty ("f0_range_hz", juce::Array<juce::var> { juce::roundToInt (f.f0MinHz), juce::roundToInt (f.f0MaxHz) });
    vo->setProperty ("pitch_jitter_cents", r1 (f.pitchJitterCents));
    vo->setProperty ("hnr_db", r1 (f.hnrDb));
    vo->setProperty ("formants_hz", juce::Array<juce::var> { juce::roundToInt (f.f1Hz), juce::roundToInt (f.f2Hz), juce::roundToInt (f.f3Hz) });
    vo->setProperty ("formant_confidence", r2 (f.formantConfidence));
    vo->setProperty ("presence_peak_hz", juce::roundToInt (f.presencePeakHz));
    o->setProperty ("voice", juce::var (vo));

    auto* ev = new juce::DynamicObject();
    auto eventList = [&] (const std::vector<TimedEvent>& events, int maxN)
    {
        std::vector<TimedEvent> sorted = events;
        std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.severityDb > b.severityDb; });
        juce::Array<juce::var> arr;
        for (int i = 0; i < std::min (maxN, (int) sorted.size()); ++i)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("t", r2 (sorted[(size_t) i].start));
            e->setProperty ("dur_ms", juce::roundToInt ((sorted[(size_t) i].end - sorted[(size_t) i].start) * 1000));
            if (sorted[(size_t) i].freqHz > 0) e->setProperty ("hz", juce::roundToInt (sorted[(size_t) i].freqHz));
            e->setProperty ("severity_db", r1 (sorted[(size_t) i].severityDb));
            arr.add (juce::var (e));
        }
        return arr;
    };
    ev->setProperty ("sibilant_count", (int) f.sibilantEvents.size());
    ev->setProperty ("sibilance_center_hz", juce::roundToInt (f.sibilanceCenterHz));
    ev->setProperty ("sibilance_p90_db_re_vowels", r1 (f.sibilanceSeverityDb));
    ev->setProperty ("harsh_count", (int) f.harshEvents.size());
    ev->setProperty ("harsh_center_hz", juce::roundToInt (f.harshCenterHz));
    ev->setProperty ("harsh_p90_excess_db", r1 (f.harshSeverityDb));
    ev->setProperty ("plosive_count", (int) f.plosiveEvents.size());
    ev->setProperty ("breath_count", (int) f.breathEvents.size());
    ev->setProperty ("click_count", (int) f.clickEvents.size());
    if (includeEvents)
    {
        ev->setProperty ("worst_sibilants", eventList (f.sibilantEvents, 5));
        ev->setProperty ("worst_harsh", eventList (f.harshEvents, 5));
    }
    o->setProperty ("events", juce::var (ev));

    auto* st = new juce::DynamicObject();
    st->setProperty ("is_stereo", f.isStereo);
    st->setProperty ("effectively_mono", f.isEffectivelyMono);
    st->setProperty ("correlation", r2 (f.correlation));
    st->setProperty ("correlation_p5", r2 (f.correlationLow));
    st->setProperty ("side_to_mid_db", r1 (f.sideToMidDb));
    st->setProperty ("balance_db", r1 (f.balanceDb));
    st->setProperty ("mono_sum_change_db", r1 (f.monoLossDb));
    st->setProperty ("inter_channel_delay_ms", r2 (f.interChannelDelayMs));
    o->setProperty ("stereo", juce::var (st));

    auto* spc = new juce::DynamicObject();
    spc->setProperty ("decay_s", r2 (f.decayTimeSec));
    spc->setProperty ("tail_80ms_db_re_phrase", r1 (f.tailToDirectDb));
    spc->setProperty ("confidence", r2 (f.spaceConfidence));
    o->setProperty ("space", juce::var (spc));

    auto* stc = new juce::DynamicObject();
    stc->setProperty ("phrase_count", (int) f.phrases.size());
    if (includeEvents)
    {
        juce::Array<juce::var> ph;
        for (size_t i = 0; i < std::min<size_t> (f.phrases.size(), 24); ++i)
        {
            auto& p = f.phrases[i];
            auto* po = new juce::DynamicObject();
            po->setProperty ("t", r1 (p.start));
            po->setProperty ("dur_s", r1 (p.end - p.start));
            po->setProperty ("lufs", r1 (p.loudnessDb));
            if (p.harshEvents > 0) po->setProperty ("harsh", p.harshEvents);
            if (p.sibilantEvents > 0) po->setProperty ("sib", p.sibilantEvents);
            ph.add (juce::var (po));
        }
        stc->setProperty ("phrases", ph);
        juce::Array<juce::var> secs;
        for (auto& sec : f.sections)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("t", r1 (sec.start));
            so->setProperty ("end", r1 (sec.end));
            so->setProperty ("lufs", r1 (sec.loudnessDb));
            so->setProperty ("label", juce::String (sec.label));
            secs.add (juce::var (so));
        }
        stc->setProperty ("sections", secs);
    }
    o->setProperty ("structure", juce::var (stc));

    o->setProperty ("source_guess", juce::String (sourceTypeName (f.source)));
    o->setProperty ("source_confidence", r2 (f.sourceConfidence));
    return root;
}

juce::var semanticToJson (const SemanticProfile& s)
{
    auto* o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("source_type", juce::String (sourceTypeName (s.source)));
    o->setProperty ("source_confidence", r2 (s.sourceConfidence));
    o->setProperty ("enough_audio", s.enoughAudio);
    juce::Array<juce::var> ch;
    for (auto& c : s.character)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("tag", juce::String (c.tag));
        co->setProperty ("confidence", r2 (c.confidence));
        co->setProperty ("evidence", juce::String (c.evidence));
        ch.add (juce::var (co));
    }
    o->setProperty ("character", ch);
    juce::Array<juce::var> pr;
    for (auto& p : s.problems)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("id", juce::String (p.id));
        po->setProperty ("title", juce::String (p.title));
        po->setProperty ("severity", r2 (p.severity));
        po->setProperty ("confidence", r2 (p.confidence));
        if (p.freqHz > 0) po->setProperty ("hz", juce::roundToInt (p.freqHz));
        po->setProperty ("evidence", juce::String (p.evidence));
        pr.add (juce::var (po));
    }
    o->setProperty ("problems", pr);
    juce::Array<juce::var> rec;
    for (auto& r : s.recommendations) rec.add (juce::String (r));
    o->setProperty ("recommendations", rec);
    o->setProperty ("dynamics", juce::String (s.dynamicsSummary));
    o->setProperty ("space", juce::String (s.spaceSummary));
    return root;
}

} // namespace nova::analysis
