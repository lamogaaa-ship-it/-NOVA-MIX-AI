#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SyntheticAudio.h"
#include "analysis/Analyzer.h"
#include "analysis/Semantic.h"

using namespace nova;
using namespace nova::analysis;
using Catch::Matchers::WithinAbs;

TEST_CASE ("BS.1770 loudness is calibrated (EBU Tech 3341 case 1 style)", "[analysis][loudness]")
{
    // 1 kHz sine at -23 dBFS in both channels must read -23.0 LUFS
    const double sr = 48000;
    const float amp = dsp::dbToGain (-23.f);
    auto s = test::sine (1000.0, sr, (int) (sr * 20), amp);
    auto buf = test::toBuffer (s, s);
    const auto l = Analyzer::measureLoudness (buf.getArrayOfReadPointers(), 2, buf.getNumSamples(), sr);
    CHECK_THAT (l.integrated, WithinAbs (-23.0, 0.1));
    CHECK_THAT (l.momentaryMax, WithinAbs (-23.0, 0.1));
    CHECK_THAT (l.shortTermMax, WithinAbs (-23.0, 0.1));
    CHECK (l.lra < 0.2f);

    // the same at 44.1 kHz (filter coefficients are derived for any rate)
    const double sr2 = 44100;
    auto s2 = test::sine (1000.0, sr2, (int) (sr2 * 10), amp);
    auto buf2 = test::toBuffer (s2, s2);
    CHECK_THAT (Analyzer::measureLoudness (buf2.getArrayOfReadPointers(), 2, buf2.getNumSamples(), sr2).integrated, WithinAbs (-23.0, 0.1));
}

TEST_CASE ("Loudness range reflects level changes (EBU Tech 3342 style)", "[analysis][loudness]")
{
    // 20 s at -20 dBFS followed by 20 s at -30 dBFS -> LRA ~ 10 LU
    const double sr = 48000;
    std::vector<float> s;
    auto a = test::sine (1000.0, sr, (int) (sr * 20), dsp::dbToGain (-20.f));
    auto b = test::sine (1000.0, sr, (int) (sr * 20), dsp::dbToGain (-30.f));
    s.insert (s.end(), a.begin(), a.end());
    s.insert (s.end(), b.begin(), b.end());
    auto buf = test::toBuffer (s, s);
    const auto l = Analyzer::measureLoudness (buf.getArrayOfReadPointers(), 2, buf.getNumSamples(), sr);
    CHECK_THAT (l.lra, WithinAbs (10.0, 1.0));
}

TEST_CASE ("True peak detects inter-sample peaks", "[analysis]")
{
    // fs/4 sine at 45 degrees phase: samples sit at 0.707 of the real peak
    const double sr = 48000;
    auto s = test::sine (sr / 4.0, sr, 4800, 0.5f, test::kPi / 4.0);
    auto buf = test::toBuffer (s, s);
    const float samplePeak = dsp::gainToDb ((float) test::peak (s.data(), (int) s.size()));
    const float tp = Analyzer::truePeakDb (buf.getArrayOfReadPointers(), 2, buf.getNumSamples());
    CHECK_THAT (samplePeak, WithinAbs (-9.03, 0.1));
    CHECK_THAT (tp, WithinAbs (-6.02, 0.5));
}

TEST_CASE ("YIN tracks the fundamental of a harmonic voice", "[analysis][pitch]")
{
    test::VoiceSpec spec;
    spec.phrases = 3;
    spec.f0 = 196.f;
    spec.phraseLevelsDb = { -18 };
    spec.harshPhrase = { false };
    spec.sibilants = false;
    auto voice = test::makeVoice (spec);
    const auto track = Analyzer::trackPitch (voice.getReadPointer (0), voice.getNumSamples(), spec.sampleRate);
    std::vector<float> f0;
    for (float v : track.f0) if (v > 0) f0.push_back (v);
    REQUIRE (f0.size() > 50);
    // notes are f0, 1.12 f0, 1.24 f0 -> the median lies within that range
    const float med = median (f0);
    CHECK (med > 190.f);
    CHECK (med < 250.f);
    // first phrase is exactly the base note
    std::vector<float> first;
    for (size_t i = 0; i < track.f0.size(); ++i)
        if (track.f0[i] > 0 && i / track.frameRate > 0.6 && i / track.frameRate < 1.8) first.push_back (track.f0[i]);
    REQUIRE (! first.empty());
    CHECK_THAT (median (first), WithinAbs (196.0, 3.0));
}

TEST_CASE ("Analyzer finds phrases, level spread, harshness and sibilance in a synthetic vocal", "[analysis][events]")
{
    test::VoiceSpec spec;   // 6 phrases, levels spread 14 dB, harsh phrases 3 and 5, sibilants
    auto voice = test::makeVoice (spec);
    AnalysisOptions opt;
    opt.hint = SourceType::LeadVocal;
    const auto f = Analyzer::analyze (voice.getArrayOfReadPointers(), 2, voice.getNumSamples(), spec.sampleRate, opt);

    REQUIRE (f.valid);
    CHECK (f.phrases.size() == 6);
    CHECK (f.phraseLevelStdDb > 3.0f);   // nominal spread is 5.8 dB; formants change actual loudness per note
    CHECK (f.isEffectivelyMono);
    CHECK (f.voicedRatio > 0.5f);
    CHECK (f.source == SourceType::LeadVocal);

    // harsh events must come from the harsh phrases, near the harsh frequency
    INFO ("harsh: events " << f.harshEvents.size() << " p90 excess " << f.harshSeverityDb << " dB at " << f.harshCenterHz << " Hz");
    for (auto& e : f.harshEvents) UNSCOPED_INFO ("  event t=" << e.start << " sev=" << e.severityDb << " f=" << e.freqHz);
    REQUIRE (f.harshEvents.size() >= 2);
    int inHarshPhrases = 0;
    for (auto& e : f.harshEvents)
        for (size_t p = 0; p < f.phrases.size(); ++p)
            if (e.start >= f.phrases[p].start && e.start < f.phrases[p].end && (p == 2 || p == 4)) ++inHarshPhrases;
    CHECK (inHarshPhrases >= (int) f.harshEvents.size() - 1);
    CHECK (std::abs (std::log2 (f.harshCenterHz / spec.harshFreq)) < 0.35);

    // sibilants near 6.8 kHz, roughly one per phrase
    CHECK (f.sibilantEvents.size() >= 4);
    CHECK (f.sibilantEvents.size() <= 8);
    CHECK (std::abs (std::log2 (f.sibilanceCenterHz / spec.sibilantFreq)) < 0.3);

    const auto sem = describe (f, WorkMode::Vocal);
    REQUIRE (sem.find ("inconsistent_level") != nullptr);
    REQUIRE (sem.find ("harshness") != nullptr);
    CHECK (std::abs (std::log2 (sem.find ("harshness")->freqHz / spec.harshFreq)) < 0.35);
    // JSON contract round-trips
    const auto js = juce::JSON::toString (semanticToJson (sem));
    CHECK (js.contains ("harshness"));
    const auto fj = juce::JSON::toString (featuresToJson (f));
    CHECK (juce::JSON::parse (fj).getProperty ("level", {}).getProperty ("integrated_lufs", {}).isDouble());
}

TEST_CASE ("A clean, even vocal does not get invented problems", "[analysis][events]")
{
    test::VoiceSpec spec;
    spec.phraseLevelsDb = { -18, -18.5f, -17.5f, -18, -18.2f, -17.8f };
    spec.harshPhrase = { false };
    spec.sibilantLevelDb = -34.f;   // soft 's'
    auto voice = test::makeVoice (spec);
    const auto f = Analyzer::analyze (voice.getArrayOfReadPointers(), 2, voice.getNumSamples(), spec.sampleRate, {});
    const auto sem = describe (f, WorkMode::Vocal);
    INFO ("clean: harsh events " << f.harshEvents.size() << " p90 excess " << f.harshSeverityDb << " dB at " << f.harshCenterHz << " Hz");
    CHECK (sem.find ("inconsistent_level") == nullptr);
    CHECK (sem.find ("harshness") == nullptr);
    CHECK (sem.find ("sibilance") == nullptr);
    CHECK (sem.find ("clipping") == nullptr);
}

TEST_CASE ("Space estimate separates a dry voice from a reverberant one", "[analysis][space]")
{
    test::VoiceSpec dry;
    dry.sibilants = false;
    dry.phraseLevelsDb = { -18 };
    dry.harshPhrase = { false };
    auto dv = test::makeVoice (dry);
    test::VoiceSpec wetSpec = dry;
    wetSpec.roomDecaySec = 1.2f;
    auto wv = test::makeVoice (wetSpec);

    const auto fd = Analyzer::analyze (dv.getArrayOfReadPointers(), 2, dv.getNumSamples(), dry.sampleRate, {});
    const auto fw = Analyzer::analyze (wv.getArrayOfReadPointers(), 2, wv.getNumSamples(), dry.sampleRate, {});
    INFO ("dry decay " << fd.decayTimeSec << " tail " << fd.tailToDirectDb << " | wet decay " << fw.decayTimeSec << " tail " << fw.tailToDirectDb);
    REQUIRE (fd.spaceConfidence > 0);
    REQUIRE (fw.spaceConfidence > 0);
    CHECK (fw.tailToDirectDb > fd.tailToDirectDb + 10.f);
    CHECK (fw.decayTimeSec > fd.decayTimeSec);
}

TEST_CASE ("Stereo analysis: mono, inverted and decorrelated material", "[analysis][stereo]")
{
    const double sr = 48000;
    auto n1 = test::whiteNoise ((int) sr * 3, 0.3f, 1);
    auto n2 = test::whiteNoise ((int) sr * 3, 0.3f, 2);
    std::vector<float> inv (n1.size());
    for (size_t i = 0; i < n1.size(); ++i) inv[i] = -n1[i];

    auto mono = test::toBuffer (n1, n1);
    auto fm = Analyzer::analyze (mono.getArrayOfReadPointers(), 2, mono.getNumSamples(), sr, {});
    CHECK (fm.isEffectivelyMono);
    CHECK_THAT (fm.correlation, WithinAbs (1.0, 1e-3));

    auto anti = test::toBuffer (n1, inv);
    auto fa = Analyzer::analyze (anti.getArrayOfReadPointers(), 2, anti.getNumSamples(), sr, {});
    CHECK_THAT (fa.correlation, WithinAbs (-1.0, 1e-3));
    CHECK (fa.monoLossDb < -40.f);
    CHECK (describe (fa, WorkMode::Mix).find ("mono_compatibility") != nullptr);

    auto dec = test::toBuffer (n1, n2);
    auto fdc = Analyzer::analyze (dec.getArrayOfReadPointers(), 2, dec.getNumSamples(), sr, {});
    CHECK_THAT (fdc.correlation, WithinAbs (0.0, 0.05));
    CHECK_THAT (fdc.monoLossDb, WithinAbs (-3.0, 0.3));
}

TEST_CASE ("Third-octave spectrum and tilt behave for white and pink-ish noise", "[analysis][spectrum]")
{
    const double sr = 48000;
    auto w = test::whiteNoise ((int) sr * 4, 0.3f, 3);
    auto wb = test::toBuffer (w, w);
    const auto fw = Analyzer::analyze (wb.getArrayOfReadPointers(), 2, wb.getNumSamples(), sr, {});
    // white noise: band power grows ~3 dB per octave
    CHECK_THAT (fw.tiltDbPerOct, WithinAbs (3.0, 0.6));
    CHECK (fw.airDb > fw.subDb + 15.f);
}
