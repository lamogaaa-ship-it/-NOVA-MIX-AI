#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nova::ui
{

// One consistent icon family: 24x24 grid, round caps/joins, drawn as strokes (1.6 px at 24).
enum class Icon
{
    Mic, Pulse, Bars, Gear, Dots, Send, Close, Upload, Level, Eq, Comp, DeEss, Color, Space, Power, Undo, Redo,
    ThumbUp, ThumbDown, ChevronDown, ChevronRight, Help, Sparkle, Wave, Stop, Delta, Folder, Check, Plus, Motion, Limiter, Image
};

juce::Path iconPath (Icon icon);                                    // in 24x24 units
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeScale = 1.f);

} // namespace nova::ui
