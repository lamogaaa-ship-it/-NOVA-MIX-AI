#include "IntentParser.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <set>

namespace nova::ai
{

bool ParsedRequest::has (const std::string& id) const { return find (id) != nullptr; }
const Intent* ParsedRequest::find (const std::string& id) const
{
    for (auto& i : intents) if (i.id == id) return &i;
    return nullptr;
}

std::string normaliseForMatching (const std::string& utf8)
{
    const juce::String in = juce::String::fromUTF8 (utf8.c_str()).toLowerCase();
    juce::String out;
    for (auto p = in.getCharPointer(); ! p.isEmpty(); ++p)
    {
        juce::juce_wchar c = *p;
        if ((c >= 0x064B && c <= 0x0652) || c == 0x0670 || c == 0x0640) continue;   // tashkeel, tatweel
        if (c == 0x0623 || c == 0x0625 || c == 0x0622 || c == 0x0671) c = 0x0627;   // alef forms
        else if (c == 0x0649) c = 0x064A;                                           // alef maqsura -> ya
        else if (c == 0x0629) c = 0x0647;                                           // ta marbuta -> ha
        else if (c == 0x060C || c == 0x061B || c == 0x061F) c = ' ';               // Arabic punctuation
        else if (c == 0x2019 || c == 0x2018) c = '\'';
        out += c;
    }
    return out.toStdString();
}

namespace
{
bool isAsciiOnly (const std::string& s)
{
    return std::all_of (s.begin(), s.end(), [] (unsigned char c) { return c < 128; });
}

struct Text
{
    std::string norm;
    std::string padded;              // " " + ascii-token-joined + " "
    std::set<std::string> tokens;

    explicit Text (const std::string& raw)
    {
        norm = normaliseForMatching (raw);
        std::string cur;
        std::vector<std::string> toks;
        for (unsigned char c : norm)
        {
            if (std::isalnum (c) || c == '/' || c == '\'' || c == '-' || c == '.' || c >= 128) cur += (char) c;
            else { if (! cur.empty()) toks.push_back (cur); cur.clear(); }
        }
        if (! cur.empty()) toks.push_back (cur);
        padded = " ";
        for (auto& t : toks)
        {
            std::string clean = t;
            while (! clean.empty() && (clean.back() == '.' || clean.back() == '\'' || clean.back() == '-')) clean.pop_back();
            tokens.insert (clean);
            padded += clean + " ";
        }
    }

    // pattern rules: ASCII phrase with spaces -> padded substring; ASCII word -> token (trailing '*' = prefix);
    // non-ASCII (Arabic) -> substring on the normalised text (handles attached articles/conjunctions)
    bool has (const std::string& pat) const
    {
        const std::string p = normaliseForMatching (pat);
        if (! isAsciiOnly (p)) return norm.find (p) != std::string::npos;
        if (p.find (' ') != std::string::npos) return padded.find (" " + p + " ") != std::string::npos || padded.find (" " + p) != std::string::npos;
        if (! p.empty() && p.back() == '*')
        {
            const auto stem = p.substr (0, p.size() - 1);
            for (auto& t : tokens) if (t.rfind (stem, 0) == 0) return true;
            return false;
        }
        return tokens.count (p) > 0;
    }

    bool any (std::initializer_list<const char*> pats, std::string* which = nullptr) const
    {
        for (auto* pt : pats)
            if (has (pt)) { if (which) *which = pt; return true; }
        return false;
    }
};

// "الـ S عالية" / "ال S بتصفر": the letter S written in Latin inside Arabic text
bool sLetterMentioned (const std::string& norm, std::string& which)
{
    std::string spaced = " " + norm + " ";
    for (auto& c : spaced) if (c == '.' || c == ',' || c == '!' || c == '?') c = ' ';
    for (auto* pat : { "الـ s ", "الـs ", "ال s ", "الs " })
        if (spaced.find (normaliseForMatching (pat)) != std::string::npos) { which = pat; return true; }
    return false;
}

void add (ParsedRequest& r, Intent i)
{
    for (auto& e : r.intents)
        if (e.id == i.id) { e.direction = i.direction; e.amount = std::max (e.amount, i.amount); return; }
    r.intents.push_back (std::move (i));
}
} // namespace

ParsedRequest parseRequest (const std::string& raw)
{
    ParsedRequest r;
    const Text t (raw);

    // language
    bool ar = false, lat = false;
    const auto rawText = juce::String::fromUTF8 (raw.c_str());   // must outlive the char pointer below
    for (auto p = rawText.getCharPointer(); ! p.isEmpty(); ++p)
    {
        if (*p >= 0x0600 && *p <= 0x06FF) ar = true;
        else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) lat = true;
    }
    r.language = ar && lat ? "mixed" : (ar ? "ar" : "en");
    r.isQuestion = t.norm.find ('?') != std::string::npos || raw.find ("\xd8\x9f") != std::string::npos   // '?' or Arabic '؟'
                   || t.any ({ "what did you", "why did", "explain", "what changed", "how does", "what do you think", "ليه", "عملت ايه", "اشرح",
                               "ايه رايك", "رايك", "تفتكر" });

    // global intensity
    float amount = 0.5f;
    if (t.any ({ "a bit", "a little", "slightly", "slight", "subtle", "subtly", "touch", "tiny", "gently", "شويه", "حبه", "بسيط", "خفيف", "سنه" })) amount = 0.3f;
    if (t.any ({ "very", "really", "a lot", "much more", "way more", "extremely", "heavily", "hard", "اوي", "جدا", "خالص", "جامد", "قوي" })) amount = 0.8f;
    r.intensity = amount;

    const bool preserveDyn = t.any ({ "don't crush", "dont crush", "not crush", "without crushing", "preserve dynamic*", "keep the dynamic*", "keep dynamic*",
                                      "not squash*", "without squash*", "natural", "not over-compress*", "without sounding over", "over-compressed",
                                      "keep the punch", "preserve punch", "preserve the punch", "without over", "من غير ما تبوظ", "من غير ضغط", "متضغطش", "طبيعي" });
    static const std::initializer_list<const char*> kAvoidDull { "without making it dull", "not dull", "without losing clarity", "without dulling",
        "keep it clear", "keep the clarity", "without losing the clarity", "من غير ما يبقي مكتوم", "من غير ما يكون مكتوم", "بدون ما يبقي مكتوم",
        "من غير ما يبقي مطفي", "مايبقاش مكتوم", "ميبقاش مكتوم", "من غير ما يقفل", "يفضل واضح" };
    const bool avoidDull = t.any (kAvoidDull);
    // The protection clause itself ("... without it getting muffled") is not a clarity request:
    // look for clarity complaints in the text with those clauses removed.
    std::string withoutClauses = t.norm;
    for (auto* ph : kAvoidDull)
    {
        const auto pn = normaliseForMatching (ph);
        for (size_t pos; (pos = withoutClauses.find (pn)) != std::string::npos;) withoutClauses.replace (pos, pn.size(), " ");
    }
    const Text tc (withoutClauses);
    r.refersToPrevious = t.any ({ "no,", "no ", "now it", "now its", "now it's", "too much", "went too", "overdid", "back off", "لا ", "لا,", "بقي", "كده", "زياده" })
                         || t.norm.rfind ("no", 0) == 0 || t.norm.rfind ("لا", 0) == 0;

    auto mk = [&] (const char* id, int dir, const std::string& ev)
    {
        Intent i; i.id = id; i.direction = dir; i.amount = amount; i.preserveDynamics = preserveDyn; i.avoidDullness = avoidDull; i.evidence = ev;
        return i;
    };
    std::string w;

    // ---- level / dynamics
    if (t.any ({ "consistent*", "even out", "evened", "evenly", "inconsistent", "level it", "leveling", "levelling", "volume keeps", "high and low",
                 "up and down", "too loud in some", "quiet in others", "some words", "jumps out", "jumping", "all over the place", "ride the",
                 "بيعلي", "بيوطي", "يعلي ويوطي", "عالي وواطي", "مش ثابت", "ثبت", "ثابت", "متساوي", "فرق في الصوت", "بيعلي ويوطي" }, &w))
        add (r, mk ("level_consistency", +1, w));

    // ---- harshness (complaints about pain / piercing)
    if (t.any ({ "harsh*", "hurt*", "piercing", "shrill", "painful", "screech*", "ear fatigue", "fatiguing", "grating", "stabbing", "abrasive",
                 "حاد", "حده", "بيوجع", "يوجع", "مزعج", "بيخرم", "صريخ" }, &w))
        add (r, mk ("harshness", +1, w));

    // ---- sibilance
    if (t.any ({ "sibilan*", "s sounds", "s's", "esses", "the s ", "ss sounds", "hissy", "hiss*", "de-ess*", "deess*", "sss*", "'s'",
                 "السين", "حرف س", "الصفير", "صفير", "تسسس", "حروف الـ s", "حروف ال s", "حرف الـ s", "الشين" }, &w)
        || sLetterMentioned (t.norm, w))
        add (r, mk ("sibilance", +1, w));

    // ---- clarity / muffled
    if (tc.any ({ "muffled", "muddy and", "unclear", "not clear", "clearer", "clarity", "clear", "buried", "cloudy", "dull", "intelligib*", "can't understand",
                  "مكتوم", "مش واضح", "اوضح", "وضوح", "مخنوق", "مدفون", "واضح" }, &w))
        add (r, mk ("clarity", +1, w));

    // ---- brightness / darkness (order matters: "too bright" is a complaint)
    if (t.any ({ "too bright", "less bright", "darker", "dark", "less highs", "less treble", "tame the highs", "too much top", "اغمق", "غامق", "الهاي عالي", "قلل الهاي" }, &w))
        add (r, mk ("brightness", -1, w));
    else if (t.any ({ "brighter", "bright", "more highs", "more treble", "crisp*", "too dark", "sparkl*", "shine", "اسطع", "لامع", "لمعه", "لمعان" }, &w))
        add (r, mk ("brightness", +1, w));

    if (t.any ({ "more air", "airy", "air", "breathy top", "open up the top", "top end", "هوا", "هواء" }, &w) && ! t.has ("too much air"))
        add (r, mk ("air", +1, w));
    if (t.any ({ "too much air", "less air" }, &w)) add (r, mk ("air", -1, w));

    // ---- warmth
    if (t.any ({ "warm*", "analog*", "analogue", "vintage", "smooth*", "rich", "cozy", "ادفي", "دافي", "دافئ", "دفا", "دفء" }, &w) && ! t.any ({ "too warm", "less warm" }))
        add (r, mk ("warmth", +1, w));
    if (t.any ({ "too warm", "less warm" }, &w)) add (r, mk ("warmth", -1, w));
    if (t.any ({ "analog*", "analogue", "tape", "tube", "saturat*", "distort*", "grit*", "dirty", "harmonic*", "colour", "color",
                 "انالوج", "تيب", "تيوب", "تشبع", "ساتشوريشن", "تشويه" }, &w))
    {
        Intent i = mk ("saturation", +1, w);
        if (t.any ({ "tube", "تيوب" })) i.pattern = 1;
        else if (t.any ({ "distort*", "grit*", "dirty", "aggressive", "تشويه" })) i.pattern = 2;
        else i.pattern = 0;
        if (t.any ({ "analog feel", "analog*", "warm*", "ادفي", "انالوج" }) && amount > 0.4f) i.amount = 0.4f;
        add (r, i);
    }

    // ---- low end / low mids
    if (t.any ({ "mud*", "woolly", "wooly", "congest*", "boomy", "boom", "too thick", "طين", "معكر", "موحل", "زحمه" }, &w))
        add (r, mk ("mud", +1, w));
    if (t.any ({ "boxy", "honky", "nasal", "ringing", "resonan*", "خنفه", "مخنف", "انفي" }, &w))
        add (r, mk ("resonance", +1, w));
    if (t.any ({ "rumble", "low end noise", "high pass", "high-pass", "highpass", "hpf", "clean the lows", "هدير" }, &w))
        add (r, mk ("rumble", +1, w));
    if (t.any ({ "plosive*", "pops", "popping", "p sounds", "b sounds", "p's", "فرقعه", "طرقعه", "حرف الب" }, &w))
        add (r, mk ("plosives", +1, w));
    if (t.any ({ "thin", "more body", "thicker", "fuller", "fatter", "weight", "رفيع", "جسم", "تقيل", "سميك" }, &w) && ! t.any ({ "thin it", "less body", "too thick" }))
        add (r, mk ("body", +1, w));
    if (t.any ({ "less body", "thin it", "too heavy" }, &w)) add (r, mk ("body", -1, w));

    // ---- presence / forwardness
    if (t.any ({ "forward", "upfront", "up front", "in front", "presence", "present", "cut through", "قدام", "بارز", "حضور" }, &w))
        add (r, mk ("presence", +1, w));

    // ---- space
    if (t.any ({ "closer", "close up", "intimate", "drier", "too wet", "too much reverb", "less reverb", "in your face", "bring the vocal", "roomy", "too roomy",
                 "قرب", "قريب", "اقرب", "حميمي", "ناشف", "في وشك" }, &w))
        add (r, mk ("space", -1, w));
    else if (t.any ({ "reverb*", "space", "spacious", "ambience", "ambient", "room", "wet", "distant", "further", "bigger", "atmospher*", "hall", "too dry",
                      "ريفرب", "صدي", "مساحه", "بعيد", "ابعد" }, &w))
        add (r, mk ("space", +1, w));

    // ---- width
    if (t.any ({ "narrow*", "too wide", "less wide", "ضيق" }, &w)) add (r, mk ("width", -1, w));
    else if (t.any ({ "wider", "wide", "width", "stereo", "spread", "عريض", "اعرض", "واسع", "ستيريو" }, &w)) add (r, mk ("width", +1, w));

    // ---- delay
    if (t.any ({ "delay*", "echo*", "throw*", "slapback", "ديلاي", "اكو" }, &w))
    {
        Intent i = mk ("delay", +1, w);
        if (t.any ({ "1/4", "quarter" })) i.division = 1;
        else if (t.any ({ "dotted" })) i.division = t.any ({ "1/4" }) ? 2 : 5;
        else if (t.any ({ "1/8", "eighth" })) i.division = 4;
        add (r, i);
    }

    // ---- rhythmic effects
    if (t.any ({ "stutter*", "chop*", "gate", "gated", "gating", "rhythmic", "cutting effect", "trance gate", "glitch*", "تقطيع", "ستاتر", "يقطع", "متقطع" }, &w))
    {
        Intent i = mk ("rhythmic_gate", +1, w);
        if (t.any ({ "1/32", "thirty-second" })) i.division = 5;
        else if (t.any ({ "1/16 triplet", "sixteenth triplet" })) i.division = 4;
        else if (t.any ({ "1/16", "sixteenth*", "16th*" })) i.division = 3;
        else if (t.any ({ "triplet*", "تريبلت" })) i.division = 2;
        else if (t.any ({ "1/8", "eighth*", "8th*" })) i.division = 1;
        else if (t.any ({ "1/4", "quarter*" })) i.division = 0;
        if (t.any ({ "strange", "weird", "interesting", "unusual", "غريب", "مختلف" })) i.pattern = 2;
        else if (t.any ({ "glitch*", "broken", "random" })) i.pattern = 3;
        else if (t.any ({ "offbeat", "off-beat" })) i.pattern = 1;
        else if (t.any ({ "half*" })) i.pattern = 4;
        add (r, i);
    }

    // ---- punch
    if (t.any ({ "punch*", "hit harder", "hits harder", "harder", "impact", "slam*", "بانش", "ضربه", "يخبط", "اقوي" }, &w))
        add (r, mk ("punch", +1, w));

    // ---- mastering / loudness
    if (t.any ({ "master*", "ماستر", "ماسترينج" })) r.masterRequest = true;
    if (t.any ({ "loud*", "louder", "commercial*", "competitive", "lufs", "streaming", "spotify", "loudness", "تجاري", "لاودنس" }, &w)
        && (r.masterRequest || t.any ({ "lufs", "commercial*", "competitive", "streaming", "spotify", "master*", "loudness", "تجاري" })))
    {
        Intent i = mk ("loudness", +1, w);
        // explicit number like "-14 lufs" / "-9lufs"
        const juce::String s = juce::String::fromUTF8 (t.norm.c_str());
        const int idx = s.indexOfIgnoreCase ("lufs");
        if (idx > 0)
        {
            const auto before = s.substring (std::max (0, idx - 7), idx).trim();
            const auto num = before.fromLastOccurrenceOf (" ", false, false).retainCharacters ("-0123456789.");
            if (num.isNotEmpty()) i.targetLufs = -std::abs (num.getFloatValue());
        }
        if (i.targetLufs >= -1.f)
        {
            if (t.any ({ "streaming", "spotify", "apple music", "youtube" })) i.targetLufs = -14.f;
            else if (t.any ({ "commercial*", "loud", "competitive", "تجاري" })) i.targetLufs = preserveDyn ? -10.f : -9.f;
        }
        add (r, i);
    }
    else if (r.masterRequest)
    {
        Intent i = mk ("loudness", +1, "master");
        i.targetLufs = preserveDyn ? -11.f : -10.f;
        add (r, i);
    }

    // ---- reference
    if (t.any ({ "reference", "match", "like this", "toward*", "towards", "sound like", "ريفرنس", "المرجع", "زي الاغنيه", "زي ده", "زي دي" }, &w))
    {
        Intent i = mk ("reference_match", +1, w);
        if (t.any ({ "tone", "tonal", "eq", "timbre", "الطبقه", "التون" })) i.referenceDims.push_back ("tone");
        if (t.any ({ "dynamic*", "compression", "punch", "الديناميك" })) i.referenceDims.push_back ("dynamics");
        if (t.any ({ "space", "reverb*", "ambience", "room", "ريفرب", "مساحه" })) i.referenceDims.push_back ("space");
        if (t.any ({ "width", "stereo", "wide", "ستيريو" })) i.referenceDims.push_back ("width");
        if (t.any ({ "color", "colour", "saturation", "character" })) i.referenceDims.push_back ("color");
        if (t.any ({ "loudness", "loud", "level" })) i.referenceDims.push_back ("loudness");
        if (i.referenceDims.empty() || t.any ({ "everything", "full match", "full", "كل حاجه" })) i.referenceDims = { "tone", "dynamics", "space", "width" };
        add (r, i);
    }

    // ---- revert / undo
    // Arabic "رجع" also occurs inside "مرجع" (reference), so only its imperative forms count
    if (t.any ({ "undo", "go back", "revert", "restore", "bring back", "put back", "reset", "الغي", "تراجع", "ارجع", "رجع ال", "رجعه", "رجعها", "رجع زي", "رجع الصوت" }, &w))
    {
        Intent i = mk ("revert", +1, w);
        if (t.any ({ "highs", "high end", "top", "treble", "brightness", "الهاي", "هايز", "الحده" }) || t.norm.find ("highs") != std::string::npos) i.revertTarget = "highs";
        else if (t.any ({ "lows", "low end", "bass", "body", "اللو", "البيز" })) i.revertTarget = "lows";
        else if (t.any ({ "reverb", "space", "ريفرب" })) i.revertTarget = "reverb";
        else if (t.any ({ "compression", "dynamics", "الكومبريسور" })) i.revertTarget = "compression";
        else if (t.any ({ "width", "stereo" })) i.revertTarget = "width";
        else i.revertTarget = "last";
        if (t.any ({ "undo", "الغي", "تراجع" }) && i.revertTarget == "last") i.id = "undo";
        add (r, i);
    }

    // ---- general "mix it" requests / style
    if (t.any ({ "mix this", "mix the", "mix my", "mix it", "mix vocal", "make it sound pro*", "professional", "radio ready", "polish*", "fix it", "fix this",
                 "sound good", "sound better", "expensive", "record", "مكس", "اعمل مكس", "ظبط", "اظبط", "احترافي", "برو", "حسن" }))
        r.generalMix = true;
    if (t.any ({ "modern pop", "pop" })) r.styleHint = "modern_pop";
    else if (t.any ({ "rap", "hip hop", "hip-hop", "trap", "راب" })) r.styleHint = "rap";
    else if (t.any ({ "r&b", "rnb", "soul" })) r.styleHint = "rnb";
    else if (t.any ({ "rock" })) r.styleHint = "rock";
    else if (t.any ({ "acoustic", "folk", "ballad" })) r.styleHint = "acoustic";
    else if (t.any ({ "shaabi", "mahraganat", "شعبي", "مهرجان" })) r.styleHint = "mahraganat";

    if (r.intents.empty() && ! r.isQuestion && t.any ({ "listen", "analy*", "what do you hear", "اسمع", "حلل" }))
    {
        Intent i; i.id = "listen"; i.evidence = "listen";
        r.intents.push_back (i);
    }
    return r;
}

} // namespace nova::ai
