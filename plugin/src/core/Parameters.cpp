#include "Parameters.h"

#include <algorithm>
#include <cmath>

namespace nova
{

const std::array<ParamSpec, P::Count> kParams = { {
    #define NOVA_X_SPEC(e, id, name, mod, kind, mn, mx, def, lg, unit, choices, step, desc) \
        ParamSpec { id, name, Module::mod, ParamKind::kind, mn, mx, def, lg, Unit::unit, choices, step, desc },
    NOVA_PARAM_LIST(NOVA_X_SPEC)
    #undef NOVA_X_SPEC
} };

std::optional<int> findParamIndex (std::string_view id)
{
    for (int i = 0; i < P::Count; ++i)
        if (id == kParams[(size_t) i].id)
            return i;
    return std::nullopt;
}

static constexpr const char* kModuleNames[] = {
    "global", "level", "eq", "tone_match", "dynamic_eq", "compressor", "deesser",
    "color", "space", "motion", "image", "limiter"
};

const char* moduleName (Module m)
{
    const auto i = (size_t) m;
    return i < std::size (kModuleNames) ? kModuleNames[i] : "unknown";
}

std::optional<Module> moduleFromName (std::string_view name)
{
    for (size_t i = 0; i < std::size (kModuleNames); ++i)
        if (name == kModuleNames[i])
            return (Module) i;

    // Friendly aliases the AI or UI might use
    if (name == "comp" || name == "compression") return Module::Comp;
    if (name == "de-esser" || name == "deess" || name == "de_esser") return Module::DeEss;
    if (name == "dyn_eq" || name == "dyneq") return Module::DynEQ;
    if (name == "reverb" || name == "delay") return Module::Space;
    if (name == "saturation") return Module::Color;
    if (name == "stereo" || name == "width") return Module::Image;
    if (name == "rider" || name == "level_rider") return Module::Level;
    if (name == "gate" || name == "stutter") return Module::Motion;
    return std::nullopt;
}

const char* unitSuffix (Unit u)
{
    switch (u)
    {
        case Unit::dB:      return "dB";
        case Unit::Hz:      return "Hz";
        case Unit::ms:      return "ms";
        case Unit::Percent: return "%";
        case Unit::Ratio:   return ":1";
        case Unit::Seconds: return "s";
        case Unit::Q:       return "Q";
        case Unit::None:    break;
    }
    return "";
}

int numChoices (const ParamSpec& spec)
{
    if (spec.kind != ParamKind::Choice) return 0;
    int n = 1;
    for (const char* c = spec.choices; *c != 0; ++c)
        if (*c == '|') ++n;
    return n;
}

std::string_view choiceName (const ParamSpec& spec, int index)
{
    std::string_view all (spec.choices);
    int current = 0;
    size_t start = 0;
    for (size_t i = 0; i <= all.size(); ++i)
    {
        if (i == all.size() || all[i] == '|')
        {
            if (current == index)
                return all.substr (start, i - start);
            ++current;
            start = i + 1;
        }
    }
    return {};
}

float clampToSpec (const ParamSpec& spec, float value)
{
    if (! std::isfinite (value))
        return spec.defaultValue;
    value = std::clamp (value, spec.minValue, spec.maxValue);
    if (spec.kind != ParamKind::Float)
        value = std::round (value);
    return value;
}

float normalise (const ParamSpec& spec, float value)
{
    value = clampToSpec (spec, value);
    if (spec.logScale && spec.minValue > 0.f)
        return std::log (value / spec.minValue) / std::log (spec.maxValue / spec.minValue);
    return (value - spec.minValue) / (spec.maxValue - spec.minValue);
}

float denormalise (const ParamSpec& spec, float n)
{
    n = std::clamp (n, 0.f, 1.f);
    if (spec.logScale && spec.minValue > 0.f)
        return clampToSpec (spec, spec.minValue * std::pow (spec.maxValue / spec.minValue, n));
    return clampToSpec (spec, spec.minValue + n * (spec.maxValue - spec.minValue));
}

ChainSettings::ChainSettings()
{
    for (int i = 0; i < P::Count; ++i)
        v[(size_t) i] = kParams[(size_t) i].defaultValue;
}

ChainOrder defaultChainOrder()
{
    return { ChainSlot::Level, ChainSlot::EQ, ChainSlot::ToneMatch, ChainSlot::DynEQ,
             ChainSlot::Comp, ChainSlot::DeEss, ChainSlot::Color, ChainSlot::Motion };
}

uint64_t packChainOrder (const ChainOrder& order)
{
    uint64_t packed = 0;
    for (int i = 0; i < kNumChainSlots; ++i)
        packed |= (uint64_t) order[(size_t) i] << (8 * i);
    return packed;
}

ChainOrder unpackChainOrder (uint64_t packed)
{
    ChainOrder order {};
    for (int i = 0; i < kNumChainSlots; ++i)
        order[(size_t) i] = (ChainSlot) ((packed >> (8 * i)) & 0xff);
    return isValidChainOrder (order) ? order : defaultChainOrder();
}

bool isValidChainOrder (const ChainOrder& order)
{
    std::array<bool, kNumChainSlots> seen {};
    for (auto s : order)
    {
        const auto i = (size_t) s;
        if (i >= (size_t) kNumChainSlots || seen[i])
            return false;
        seen[i] = true;
    }
    return true;
}

static constexpr const char* kSlotNames[] = { "level", "eq", "tone_match", "dynamic_eq", "compressor", "deesser", "color", "motion" };

const char* chainSlotName (ChainSlot s)
{
    const auto i = (size_t) s;
    return i < std::size (kSlotNames) ? kSlotNames[i] : "unknown";
}

std::optional<ChainSlot> chainSlotFromName (std::string_view name)
{
    for (size_t i = 0; i < std::size (kSlotNames); ++i)
        if (name == kSlotNames[i])
            return (ChainSlot) i;
    if (auto m = moduleFromName (name))
    {
        const Module mod = *m;
        if (mod == Module::Level)     return ChainSlot::Level;
        if (mod == Module::EQ)        return ChainSlot::EQ;
        if (mod == Module::ToneMatch) return ChainSlot::ToneMatch;
        if (mod == Module::DynEQ)     return ChainSlot::DynEQ;
        if (mod == Module::Comp)      return ChainSlot::Comp;
        if (mod == Module::DeEss)     return ChainSlot::DeEss;
        if (mod == Module::Color)     return ChainSlot::Color;
        if (mod == Module::Motion)    return ChainSlot::Motion;
    }
    return std::nullopt;
}

} // namespace nova
