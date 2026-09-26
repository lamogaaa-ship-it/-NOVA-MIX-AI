#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SyntheticAudio.h"
#include "ai/Engineer.h"
#include "ai/IntentParser.h"
#include "analysis/Analyzer.h"

using namespace nova;
using namespace nova::ai;

namespace
{
std::shared_ptr<const analysis::AnalysisResult> analyse (const juce::AudioBuffer<float>& b, double sr, analysis::WorkMode m = analysis::WorkMode::Vocal)
{
    return analysis::AnalysisEngine::analyseBuffers (b, nullptr, sr, m, 1);
}
} // namespace

TEST_CASE ("Intent parser understands English engineering requests", "[ai][intent]")
{
    auto p = parseRequest ("Make this vocal more consistent and remove the harshness without making it dull.");
    CHECK (p.has ("level_consistency"));
    CHECK (p.has ("harshness"));
    CHECK_FALSE (p.has ("clarity"));
    CHECK (p.find ("harshness")->avoidDullness);
    CHECK (p.language == "en");

    CHECK (parseRequest ("The S sounds are hurting my ears").has ("sibilance"));
    CHECK (parseRequest ("It sounds muffled").has ("clarity"));
    CHECK (parseRequest ("Make the vocal darker without losing clarity").find ("brightness")->direction == -1);
    CHECK (parseRequest ("Bring the vocal closer").find ("space")->direction == -1);
    CHECK (parseRequest ("Make the chorus wider").find ("width")->direction == +1);
    auto st = parseRequest ("Give the vocal a strange rhythmic cutting effect on 1/16");
    REQUIRE (st.has ("rhythmic_gate"));
    CHECK (st.find ("rhythmic_gate")->pattern == 2);
    CHECK (st.find ("rhythmic_gate")->division == 3);
    auto m = parseRequest ("Master the song at -14 LUFS but don't crush the dynamics");
    REQUIRE (m.has ("loudness"));
    CHECK_THAT (m.find ("loudness")->targetLufs, Catch::Matchers::WithinAbs (-14.0, 0.01));
    CHECK (m.find ("loudness")->preserveDynamics);
    auto r = parseRequest ("Move my vocal toward this reference, mainly tone and space.");
    REQUIRE (r.has ("reference_match"));
    CHECK (r.find ("reference_match")->referenceDims == std::vector<std::string> { "tone", "space" });
    CHECK (parseRequest ("Give me an expensive modern pop vocal").generalMix);
    CHECK (parseRequest ("Give me an expensive modern pop vocal").styleHint == "modern_pop");
    CHECK (parseRequest ("Make it sound like a modern pop record").generalMix);
}

TEST_CASE ("Intent parser understands Egyptian Arabic, including follow-up corrections", "[ai][intent][arabic]")
{
    auto p = parseRequest ("لا، كده الفوكال بقى حاد شوية، رجّع الـhighs وخليه أدفى.");
    CHECK (p.language == "mixed");
    CHECK (p.refersToPrevious);
    CHECK (p.has ("harshness"));
    REQUIRE (p.has ("revert"));
    CHECK (p.find ("revert")->revertTarget == "highs");
    CHECK (p.has ("warmth"));
    CHECK (p.intensity < 0.5f);   // "شوية" = a bit

    CHECK (parseRequest ("الصوت بيعلى ويوطى").has ("level_consistency"));
    CHECK (parseRequest ("الصوت مكتوم ومش واضح").has ("clarity"));
    CHECK (parseRequest ("حرف السين بيوجع").has ("sibilance"));
    CHECK (parseRequest ("قرب الفوكال").find ("space")->direction == -1);
    // "مرجع" (reference) must not be read as "رجع" (revert)
    auto ref = parseRequest ("خلي الصوت زي المرجع");
    CHECK (ref.has ("reference_match"));
    CHECK_FALSE (ref.has ("revert"));
}

TEST_CASE ("Milestone: 'more consistent + remove harshness without making it dull' closes the loop", "[ai][closedloop]")
{
    test::VoiceSpec spec;   // 6 phrases with 14 dB spread, harsh phrases, sibilants
    auto voice = test::makeVoice (spec);
    EngineerContext ctx;
    ctx.audio = analyse (voice, spec.sampleRate);
    ctx.mode = analysis::WorkMode::Vocal;
    SessionMemory mem;
    ctx.memory = &mem;

    auto out = OfflineEngineer::handle ("Make this vocal more consistent and remove the harshness without making it dull.", ctx);
    INFO (out.reply);
    INFO (out.replyEngineer);
    REQUIRE (out.ok);
    REQUIRE (out.changed);
    REQUIRE (out.hasMetrics);
    CHECK (std::find (out.treatments.begin(), out.treatments.end(), "level_consistency") != out.treatments.end());
    CHECK (std::find (out.treatments.begin(), out.treatments.end(), "harshness") != out.treatments.end());

    // closed-loop results measured on the rendered audio
    CHECK (out.after.phraseStdDb < out.before.phraseStdDb * 0.7f);
    CHECK (out.before.harshOnEvents - out.after.harshOnEvents > 2.5f);
    CHECK (out.after.presence - out.before.presence > -1.0f);           // not dull
    CHECK (std::abs (out.after.integratedLufs - out.before.integratedLufs) < 1.0f);   // no "louder is better"
    CHECK (out.after.truePeakDb < 0.f);
    // explanation is grounded in measured numbers
    CHECK (out.reply.find ("dB") != std::string::npos);
    CHECK (out.replyEngineer.find ("Verified on the rendered result") != std::string::npos);
    // the dynamic EQ targets the measured harsh frequency, not a preset
    const float fh = out.settings[P::DeqB1Freq];
    CHECK (std::abs (std::log2 (fh / ctx.audio->inputFeatures.harshCenterHz)) < 0.2);
}

TEST_CASE ("Follow-up 'no, now it's harsh, bring back the highs and make it warmer' uses session memory", "[ai][closedloop][arabic]")
{
    test::VoiceSpec spec;
    spec.harshPhrase = { false };
    auto voice = test::makeVoice (spec);
    EngineerContext ctx;
    ctx.audio = analyse (voice, spec.sampleRate);
    SessionMemory mem;
    ctx.memory = &mem;

    // step 1: the user asks for more brightness/air
    auto first = OfflineEngineer::handle ("Make the vocal brighter and add air", ctx);
    REQUIRE (first.changed);
    ActionRecord rec;
    rec.request = "Make the vocal brighter and add air";
    rec.treatments = first.treatments;
    rec.changes = diffSettings (ctx.current, first.settings);
    rec.simple = first.reply;
    mem.add (rec);
    const float shelfAfterFirst = first.settings[eqBandParam (4, 3)];
    REQUIRE (shelfAfterFirst > 1.f);

    // step 2: Egyptian Arabic correction
    ctx.current = first.settings;
    auto second = OfflineEngineer::handle ("لا، كده الفوكال بقى حاد شوية، رجّع الـhighs وخليه أدفى.", ctx);
    INFO (second.reply);
    REQUIRE (second.changed);
    CHECK (std::find (second.treatments.begin(), second.treatments.end(), "revert_highs") != second.treatments.end());
    CHECK (std::find (second.treatments.begin(), second.treatments.end(), "warmth") != second.treatments.end());
    CHECK (std::find (second.treatments.begin(), second.treatments.end(), "harshness") == second.treatments.end());
    CHECK (second.settings[eqBandParam (4, 3)] < shelfAfterFirst);          // highs put back
    CHECK (second.settings.on (P::ColOn));                                   // warmer (tape colour)

    // replies follow the user's language: the English request got English, the Egyptian one Arabic
    auto hasArabic = [] (const std::string& t) { const auto s = juce::String::fromUTF8 (t.c_str()); for (auto c : s) if (c >= 0x0600 && c <= 0x06FF) return true; return false; };
    CHECK_FALSE (hasArabic (first.reply));
    CHECK (hasArabic (second.reply));
    CHECK (second.reply.find ("I ") == std::string::npos);
}

TEST_CASE ("Arabic requests get Arabic explanations with the measured numbers", "[ai][arabic]")
{
    test::VoiceSpec spec;
    auto voice = test::makeVoice (spec);
    EngineerContext ctx;
    ctx.audio = analyse (voice, spec.sampleRate);
    auto out = OfflineEngineer::handle ("الصوت حاد شوية والـ S عالية، خليه أنعم من غير ما يبقى مكتوم", ctx);
    INFO (out.reply);
    REQUIRE (out.changed);
    const auto reply = juce::String::fromUTF8 (out.reply.c_str());
    CHECK (reply.containsChar ((juce::juce_wchar) 0x0627));            // Arabic letters
    CHECK (reply.contains ("dB"));                                     // numbers and units kept
    CHECK_FALSE (reply.contains ("I tamed"));
    // questions about the audio are answered in Arabic too
    auto q = OfflineEngineer::handle ("ايه رأيك في الصوت؟", ctx);
    CHECK (juce::String::fromUTF8 (q.reply.c_str()).contains (juce::String::fromUTF8 ("سمعت")));
    // English stays English
    auto en = OfflineEngineer::handle ("The S sounds are too sharp", ctx);
    CHECK (juce::String::fromUTF8 (en.reply.c_str()).containsChar ((juce::juce_wchar) 0x0627) == false);
}

TEST_CASE ("Offline engineer refuses to invent problems and asks for audio when it has none", "[ai]")
{
    EngineerContext ctx;
    auto out = OfflineEngineer::handle ("Mix this vocal", ctx);
    CHECK_FALSE (out.ok);
    CHECK_FALSE (out.changed);
    CHECK (out.reply.find ("haven't heard") != std::string::npos);
}

TEST_CASE ("Reference match moves tone and space toward a reference", "[ai][reference]")
{
    test::VoiceSpec dry;
    dry.harshPhrase = { false };
    dry.phraseLevelsDb = { -18 };
    dry.sibilants = false;
    auto mine = test::makeVoice (dry);

    // reference: same kind of voice, but brighter and in a room
    test::VoiceSpec refSpec = dry;
    refSpec.f0 = 220.f;
    refSpec.roomDecaySec = 1.4f;
    auto refAudio = test::makeVoice (refSpec);
    {
        dsp::Biquad hs; hs.setCoeffs (dsp::BiquadCoeffs::highShelf (48000.0, 3000.0, 0.7, 7.0));
        for (int c = 0; c < 2; ++c) { hs.reset(); for (int i = 0; i < refAudio.getNumSamples(); ++i) refAudio.setSample (c, i, hs.process (refAudio.getSample (c, i), 0)); }
    }
    auto refProfile = std::make_shared<reference::ReferenceProfile>();
    refProfile->name = "synthetic_reference.wav";
    refProfile->sampleRate = 48000;
    refProfile->features = analysis::Analyzer::analyze (refAudio.getArrayOfReadPointers(), 2, refAudio.getNumSamples(), 48000.0, {});
    REQUIRE (refProfile->features.valid);

    auto analysed = analyse (mine, 48000.0);
    TreatmentSession session (analysed->input, 48000.0, analysed->inputFeatures, analysis::WorkMode::Vocal, ChainSettings(), defaultChainOrder(), {});
    reference::MatchDimensions dims = reference::MatchDimensions::fromNames ({ "tone", "space" });
    auto result = reference::matchReference (session, *refProfile, dims, 1.0f);
    INFO (result.simple << " | " << result.engineer);
    REQUIRE (result.applied);
    REQUIRE (result.reports.size() >= 2);
    const auto& tone = result.reports[0];
    CHECK (tone.dimension == "tone");
    CHECK (tone.distanceAfter < tone.distanceBefore * 0.5f);
    const auto& space = result.reports[1];
    CHECK (space.dimension == "space");
    CHECK (space.after > space.current + 4.f);   // wetter, toward the room-y reference
    CHECK (result.settings.on (P::TmOn));

    // the comparison speaks in perceptual terms
    const auto cmp = reference::compareToReference (analysed->inputFeatures, *refProfile);
    const auto js = juce::JSON::toString (cmp);
    CHECK (js.contains ("brighter"));
    CHECK (js.contains ("wetter"));
}
