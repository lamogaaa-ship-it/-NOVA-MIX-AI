#include "Icons.h"

namespace nova::ui
{

namespace
{
void line (juce::Path& p, float x1, float y1, float x2, float y2) { p.startNewSubPath (x1, y1); p.lineTo (x2, y2); }
void arc (juce::Path& p, float cx, float cy, float r, float a0, float a1) { p.addCentredArc (cx, cy, r, r, 0.f, a0, a1, true); }
constexpr float pi = juce::MathConstants<float>::pi;
} // namespace

juce::Path iconPath (Icon icon)
{
    juce::Path p;
    switch (icon)
    {
        case Icon::Mic:
            p.addRoundedRectangle (9.f, 3.f, 6.f, 11.f, 3.f);
            arc (p, 12, 11, 6, pi * 0.5f, pi * 1.5f);
            line (p, 12, 17, 12, 21); line (p, 8.5f, 21, 15.5f, 21);
            break;
        case Icon::Pulse:
            p.startNewSubPath (2, 12); p.lineTo (7, 12); p.lineTo (9.5f, 6); p.lineTo (13.5f, 18); p.lineTo (16, 12); p.lineTo (22, 12);
            break;
        case Icon::Bars:
            line (p, 5, 18, 5, 10); line (p, 9.5f, 18, 9.5f, 5); line (p, 14, 18, 14, 8); line (p, 18.5f, 18, 18.5f, 12);
            break;
        case Icon::Gear:
        {
            p.addEllipse (9, 9, 6, 6);
            for (int i = 0; i < 8; ++i)
            {
                const float a = i * pi / 4.f;
                line (p, 12 + 6.2f * std::cos (a), 12 + 6.2f * std::sin (a), 12 + 8.8f * std::cos (a), 12 + 8.8f * std::sin (a));
            }
            p.addEllipse (5.8f, 5.8f, 12.4f, 12.4f);
            break;
        }
        case Icon::Dots:
            p.addEllipse (4.5f, 11, 2, 2); p.addEllipse (11, 11, 2, 2); p.addEllipse (17.5f, 11, 2, 2);
            break;
        case Icon::Send:
            p.startNewSubPath (4, 12); p.lineTo (20, 4); p.lineTo (14, 20); p.lineTo (11.5f, 13.5f); p.closeSubPath();
            line (p, 11.5f, 13.5f, 20, 4);
            break;
        case Icon::Close:
            line (p, 6, 6, 18, 18); line (p, 18, 6, 6, 18);
            break;
        case Icon::Upload:
            line (p, 6, 12, 6, 16); line (p, 9, 9, 9, 17); line (p, 12, 6, 12, 18); line (p, 15, 9, 15, 17); line (p, 18, 12, 18, 16);
            break;
        case Icon::Level:
            arc (p, 12, 13, 7.5f, -pi * 0.75f, pi * 0.75f);
            line (p, 12, 13, 16.5f, 8.5f);
            break;
        case Icon::Eq:
            line (p, 6, 4, 6, 20); line (p, 12, 4, 12, 20); line (p, 18, 4, 18, 20);
            p.addEllipse (4, 13, 4, 4); p.addEllipse (10, 7, 4, 4); p.addEllipse (16, 11, 4, 4);
            break;
        case Icon::Comp:
            line (p, 4, 19, 11, 12); p.lineTo (20, 8);
            line (p, 4, 5, 20, 5);
            p.addEllipse (10, 10.5f, 3, 3);
            break;
        case Icon::DeEss:
            p.startNewSubPath (17, 6.5f);
            p.cubicTo (14, 3.5f, 7.5f, 4.5f, 8.5f, 8.5f);
            p.cubicTo (9.5f, 12, 16.5f, 11.5f, 16, 16);
            p.cubicTo (15.5f, 20, 9, 20.5f, 6.5f, 17.5f);
            break;
        case Icon::Color:
            p.startNewSubPath (12, 3.5f);
            p.cubicTo (12, 3.5f, 5.5f, 10.5f, 5.5f, 14.5f);
            p.cubicTo (5.5f, 18.5f, 8.5f, 20.5f, 12, 20.5f);
            p.cubicTo (15.5f, 20.5f, 18.5f, 18.5f, 18.5f, 14.5f);
            p.cubicTo (18.5f, 10.5f, 12, 3.5f, 12, 3.5f);
            p.closeSubPath();
            break;
        case Icon::Space:
            arc (p, 6, 12, 3.5f, 0.2f * pi, 0.8f * pi);
            arc (p, 6, 12, 8.f, 0.28f * pi, 0.72f * pi);
            arc (p, 6, 12, 12.5f, 0.33f * pi, 0.67f * pi);
            p.addEllipse (4.5f, 10.5f, 3, 3);
            break;
        case Icon::Power:
            arc (p, 12, 13, 7.5f, pi * 0.22f, pi * 1.78f);
            line (p, 12, 3.5f, 12, 11);
            break;
        case Icon::Undo:
            arc (p, 12.5f, 13, 7, -pi * 0.95f, pi * 0.5f);
            p.startNewSubPath (3.5f, 6.5f); p.lineTo (5.5f, 12); p.lineTo (10.5f, 9.5f);
            break;
        case Icon::Redo:
            arc (p, 11.5f, 13, 7, -pi * 0.5f, pi * 0.95f);
            p.startNewSubPath (20.5f, 6.5f); p.lineTo (18.5f, 12); p.lineTo (13.5f, 9.5f);
            break;
        case Icon::ThumbUp:
            p.addRoundedRectangle (3.5f, 10.5f, 4, 9, 1);
            p.startNewSubPath (7.5f, 11); p.lineTo (11.5f, 4); p.cubicTo (13.5f, 4, 14, 5.5f, 13.5f, 7.5f); p.lineTo (13, 10);
            p.lineTo (18.5f, 10); p.cubicTo (20, 10, 20.8f, 11.3f, 20.3f, 12.6f); p.lineTo (18.3f, 18.3f); p.cubicTo (18, 19.2f, 17.3f, 19.5f, 16.5f, 19.5f);
            p.lineTo (7.5f, 19.5f);
            break;
        case Icon::ThumbDown:
        {
            p = iconPath (Icon::ThumbUp);
            p.applyTransform (juce::AffineTransform::scale (1, -1, 12, 12));
            break;
        }
        case Icon::ChevronDown:
            p.startNewSubPath (7, 10); p.lineTo (12, 15); p.lineTo (17, 10);
            break;
        case Icon::ChevronRight:
            p.startNewSubPath (10, 7); p.lineTo (15, 12); p.lineTo (10, 17);
            break;
        case Icon::Help:
            p.addEllipse (3.5f, 3.5f, 17, 17);
            p.startNewSubPath (9.5f, 9.5f); p.cubicTo (9.5f, 6.5f, 14.5f, 6.5f, 14.5f, 9.5f); p.cubicTo (14.5f, 11.5f, 12, 11.5f, 12, 14);
            p.addEllipse (11.3f, 16.3f, 1.4f, 1.4f);
            break;
        case Icon::Sparkle:
            p.startNewSubPath (12, 3); p.cubicTo (12.8f, 8.5f, 15.5f, 11.2f, 21, 12); p.cubicTo (15.5f, 12.8f, 12.8f, 15.5f, 12, 21);
            p.cubicTo (11.2f, 15.5f, 8.5f, 12.8f, 3, 12); p.cubicTo (8.5f, 11.2f, 11.2f, 8.5f, 12, 3); p.closeSubPath();
            break;
        case Icon::Wave:
            line (p, 3, 12, 3, 12); line (p, 6, 9, 6, 15); line (p, 9, 6, 9, 18); line (p, 12, 3.5f, 12, 20.5f);
            line (p, 15, 6, 15, 18); line (p, 18, 9, 18, 15); line (p, 21, 11, 21, 13);
            break;
        case Icon::Stop:
            p.addRoundedRectangle (6.5f, 6.5f, 11, 11, 2);
            break;
        case Icon::Delta:
            p.startNewSubPath (12, 4.5f); p.lineTo (20.5f, 19.5f); p.lineTo (3.5f, 19.5f); p.closeSubPath();
            break;
        case Icon::Folder:
            p.startNewSubPath (3.5f, 7); p.lineTo (3.5f, 18.5f); p.lineTo (20.5f, 18.5f); p.lineTo (20.5f, 9); p.lineTo (11.5f, 9); p.lineTo (9.5f, 6.5f); p.lineTo (4, 6.5f);
            break;
        case Icon::Check:
            p.startNewSubPath (5, 12.5f); p.lineTo (10, 17.5f); p.lineTo (19, 7);
            break;
        case Icon::Plus:
            line (p, 12, 5, 12, 19); line (p, 5, 12, 19, 12);
            break;
        case Icon::Motion:
            line (p, 3, 12, 6, 12); p.addRoundedRectangle (6, 7, 3, 10, 1); line (p, 11, 12, 13, 12);
            p.addRoundedRectangle (13, 7, 3, 10, 1); line (p, 18, 12, 21, 12);
            break;
        case Icon::Limiter:
            line (p, 3, 6, 21, 6);
            p.startNewSubPath (3, 18); p.lineTo (8, 11); p.lineTo (11, 14); p.lineTo (15, 6.5f); p.lineTo (21, 6.5f);
            break;
        case Icon::Image:
            p.addEllipse (3.5f, 7, 10, 10); p.addEllipse (10.5f, 7, 10, 10);
            break;
    }
    return p;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeScale)
{
    auto p = iconPath (icon);
    const float s = std::min (area.getWidth(), area.getHeight()) / 24.f;
    p.applyTransform (juce::AffineTransform::scale (s).translated (area.getCentreX() - 12.f * s, area.getCentreY() - 12.f * s));
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.6f * s * strokeScale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace nova::ui
