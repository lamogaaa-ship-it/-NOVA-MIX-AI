#pragma once

// Suggestion chips shown in the assistant panel, in English and Egyptian Arabic. Clicking a chip
// sends its text as a request, so every phrasing must be understood by the intent parser
// (EngineerTests verifies each one against `intent`).

#include <string>
#include <vector>

namespace nova::ai
{

struct SuggestionText
{
    const char* key;
    const char* en;
    const char* ar;
    const char* intent;     // expected parse: an intent id, or "master" / "mix" for request flags
};

inline const std::vector<SuggestionText>& suggestionTexts()
{
    static const std::vector<SuggestionText> t {
        { "harshness", "Tame the harsh notes without making it dull", "هدّي الحدة من غير ما يبقى مكتوم", "harshness" },
        { "sibilance", "The S sounds are too sharp", "حروف الـ S عالية", "sibilance" },
        { "level", "Make the level more consistent", "خلّي مستوى الصوت ثابت", "level_consistency" },
        { "mud", "Clean up the muddy low-mids", "نضّف الزحمة في الـ low-mids", "mud" },
        { "presence", "Bring the vocal forward", "قدّم الفوكال لقدّام", "presence" },
        { "air", "Add some air on top", "زوّد هوا فوق", "air" },
        { "rumble", "Remove the low rumble", "شيل الهدير اللي تحت", "rumble" },
        { "plosives", "Fix the P pops", "صلّح فرقعة حرف الـ P", "plosives" },
        { "closer", "Bring the vocal closer", "قرّب الفوكال", "space" },
        { "loud_punch", "Master it loud but keep the punch", "ماستر عالي تجاري من غير ما تبوظ الـ punch", "loudness" },
        { "streaming", "Master this for streaming (-14 LUFS)", "اعمل ماستر للستريمنج (-14 LUFS)", "master" },
        { "commercial", "Make it commercially loud but keep the punch", "خليه تجاري عالي من غير ما تبوظ الـ punch", "loudness" },
        { "brighter", "Make the vocal brighter and clearer", "خلّي الفوكال ألمع وأوضح", "brightness" },
        { "warmth", "More warmth and analog feel", "دفء أكتر وإحساس أنالوج", "warmth" },
        { "match_vocal", "Match this reference vocal", "خلّي الفوكال زي الريفرنس", "reference_match" },
        { "match_master", "Match this reference master", "خلّي الماستر زي الريفرنس", "reference_match" },
        { "pop", "Make it sound like a modern pop record", "اعمل مكس بوب احترافي", "mix" },
        { "master_dyn", "Master the song but don't crush the dynamics", "اعمل ماستر للأغنية من غير ضغط زيادة", "master" },
    };
    return t;
}

inline const SuggestionText* suggestionFor (const std::string& key)
{
    for (auto& s : suggestionTexts())
        if (key == s.key) return &s;
    return nullptr;
}

} // namespace nova::ai
