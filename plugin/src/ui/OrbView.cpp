#include "OrbView.h"

namespace nova::ui
{

using ai::AgentPhase;
static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

OrbView::OrbView()
{
    setInterceptsMouseClicks (false, false);
    juce::Random rng (7);
    particles.resize (320);
    for (auto& p : particles)
    {
        p.theta = rng.nextFloat() * kTwoPi;
        p.phi = std::acos (2.f * rng.nextFloat() - 1.f);
        p.r = 0.35f + 0.65f * std::sqrt (rng.nextFloat());
        p.speed = 0.4f + rng.nextFloat() * 1.2f;
        p.size = 0.8f + rng.nextFloat() * 1.8f;
        p.hue = rng.nextFloat();
    }
}

void OrbView::resized()
{
    auto b = getLocalBounds().toFloat();
    radius = std::min (b.getHeight() * 0.40f, b.getWidth() * 0.22f);
    orbArea = juce::Rectangle<float> (radius * 2, radius * 2).withCentre ({ b.getCentreX(), b.getY() + radius + b.getHeight() * 0.06f });
}

void OrbView::update (const Inputs& in, double dt)
{
    if (in.phase != shownPhase)
    {
        if (in.phase == AgentPhase::Complete) flash = 1.0;
        shownPhase = in.phase;
        phaseTime = 0;
    }
    state = in;
    t += dt;
    phaseTime += dt;
    flash = std::max (0.0, flash - dt / 1.4);
    levelSmooth += (float) std::min (1.0, dt * 10.0) * (in.level - levelSmooth);

    float targetSpeed = 0.18f, targetEnergy = 0.25f;
    switch (in.phase)
    {
        case AgentPhase::Listening:  targetSpeed = 0.35f; targetEnergy = 0.5f + levelSmooth * 0.5f; break;
        case AgentPhase::Analyzing:  targetSpeed = 1.1f;  targetEnergy = 0.85f; break;
        case AgentPhase::Thinking:   targetSpeed = 0.7f;  targetEnergy = 0.7f; break;
        case AgentPhase::Processing: targetSpeed = 0.9f;  targetEnergy = 0.9f; break;
        case AgentPhase::Matching:   targetSpeed = 0.8f;  targetEnergy = 0.85f; break;
        case AgentPhase::Complete:   targetSpeed = 0.4f;  targetEnergy = 0.6f; break;
        case AgentPhase::Error:      targetSpeed = 0.12f; targetEnergy = 0.2f; break;
        case AgentPhase::Idle:       break;
    }
    if (in.listening && in.phase == AgentPhase::Idle) { targetSpeed = 0.35f; targetEnergy = 0.5f + levelSmooth * 0.5f; }
    if (in.analyzing && in.phase == AgentPhase::Idle) { targetSpeed = 1.1f; targetEnergy = 0.85f; }
    speed += (float) std::min (1.0, dt * 2.0) * (targetSpeed - speed);
    energy += (float) std::min (1.0, dt * 2.5) * (targetEnergy - energy);
    spin += speed * (float) dt;
    repaint();
}

void OrbView::paint (juce::Graphics& g)
{
    const auto c = orbArea.getCentre();
    paintWings (g, c);
    paintOrb (g, c);
    paintLabels (g);
}

void OrbView::paintWings (juce::Graphics& g, juce::Point<float> c)
{
    // Flowing waveform "wings" on both sides of the orb. Amplitude follows the real input level;
    // while listening the flow runs into the orb, while processing it runs outward.
    const auto b = getLocalBounds().toFloat();
    const bool inward = state.listening || state.phase == AgentPhase::Listening;
    const float dir = inward ? 1.f : -1.f;
    const float amp = radius * (0.16f + 0.55f * levelSmooth + 0.12f * energy);
    for (int side = 0; side < 2; ++side)
    {
        const float x0 = side == 0 ? b.getX() : c.x + radius * 0.92f;
        const float x1 = side == 0 ? c.x - radius * 0.92f : b.getRight();
        for (int k = 0; k < 5; ++k)
        {
            juce::Path p;
            const int steps = 90;
            for (int i = 0; i <= steps; ++i)
            {
                const float u = (float) i / steps;
                const float x = x0 + u * (x1 - x0);
                const float towardOrb = side == 0 ? u : 1.f - u;               // 1 near the orb
                const float env = std::pow (std::sin (juce::MathConstants<float>::pi * juce::jlimit (0.f, 1.f, 0.15f + 0.85f * towardOrb)), 1.3f);
                const float ph = (float) t * (1.2f + 0.4f * k) * dir * (side == 0 ? 1.f : -1.f);
                const float y = c.y + std::sin (u * (5.5f + k * 1.7f) + ph + k) * amp * env * (0.45f + 0.18f * k)
                                + std::sin (u * 21.f - ph * 2.1f) * amp * 0.08f * env;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            const auto col = (k % 2 == 0 ? Colours::cyan : Colours::violet).withAlpha (0.22f + 0.22f * energy);
            juce::ColourGradient grad (col.withAlpha (0.f), side == 0 ? x0 : x1, c.y, col, side == 0 ? x1 : x0, c.y, false);
            g.setGradientFill (grad);
            g.strokePath (p, juce::PathStrokeType (k == 0 ? 2.2f : 1.2f));
        }
    }
}

void OrbView::paintOrb (juce::Graphics& g, juce::Point<float> c)
{
    const float breath = 1.f + 0.018f * std::sin ((float) t * 1.6f) + 0.03f * levelSmooth;
    const float R = radius * breath;

    // outer glow
    {
        juce::ColourGradient glow (Colours::violet.withAlpha (0.28f * (0.5f + energy)), c.x, c.y, juce::Colours::transparentBlack, c.x + R * 1.9f, c.y, true);
        glow.addColour (0.45, Colours::blue.withAlpha (0.16f * (0.4f + energy)));
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (R * 3.8f, R * 3.8f).withCentre (c));
    }
    // sphere body
    {
        juce::ColourGradient body (juce::Colour (0xff060a1c), c.x - R * 0.2f, c.y - R * 0.25f, juce::Colour (0xff1b2a78), c.x + R, c.y, true);
        body.addColour (0.62, juce::Colour (0xff0b1340));
        body.addColour (0.9, juce::Colour (0xff3a2a9e));
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (R * 2, R * 2).withCentre (c));
    }
    g.saveState();
    juce::Path clip; clip.addEllipse (juce::Rectangle<float> (R * 2, R * 2).withCentre (c));
    g.reduceClipRegion (clip);

    // nebula haze
    for (int i = 0; i < 3; ++i)
    {
        const float a = spin * (0.6f + i * 0.35f) + i * 2.1f;
        const auto hc = c + juce::Point<float> (std::cos (a), std::sin (a * 1.3f)) * R * 0.35f;
        const auto col = (i == 0 ? Colours::cyan : (i == 1 ? Colours::purple : Colours::blue)).withAlpha (0.22f + 0.14f * energy);
        juce::ColourGradient haze (col, hc.x, hc.y, juce::Colours::transparentBlack, hc.x + R * 0.9f, hc.y, true);
        g.setGradientFill (haze);
        g.fillEllipse (juce::Rectangle<float> (R * 1.8f, R * 1.8f).withCentre (hc));
    }

    // filament rings: great circles at different tilts, projected
    const int rings = 7;
    for (int i = 0; i < rings; ++i)
    {
        const float tilt = 0.25f + i * 0.21f + 0.15f * std::sin (spin * 0.7f + i);
        const float rot = spin * (i % 2 == 0 ? 0.9f : -0.7f) + i * 0.9f;
        juce::Path ring;
        ring.addEllipse (-R * 0.97f, -R * 0.97f * std::abs (std::cos (tilt)), R * 1.94f, R * 1.94f * std::abs (std::cos (tilt)));
        ring.applyTransform (juce::AffineTransform::rotation (rot).translated (c.x, c.y));
        const auto col = (i % 3 == 0 ? Colours::cyan : (i % 3 == 1 ? Colours::violet : Colours::magenta)).withAlpha (0.20f + 0.22f * energy);
        g.setColour (col);
        g.strokePath (ring, juce::PathStrokeType (0.9f + 0.5f * energy));
    }

    // particles on/in the sphere, depth shaded
    const float cr = std::cos (spin), sr = std::sin (spin);
    const float tiltX = 0.35f;
    for (auto& p : particles)
    {
        const float th = p.theta + spin * p.speed * 0.6f;
        float x = std::sin (p.phi) * std::cos (th) * p.r;
        float y = std::cos (p.phi) * p.r;
        float z = std::sin (p.phi) * std::sin (th) * p.r;
        // listening: particles drift outward-in along the horizontal axis (waves flowing in)
        if (state.listening || state.phase == AgentPhase::Listening)
            x *= 1.f + 0.12f * std::sin ((float) t * 3.f + p.hue * 6.f);
        const float yz = y * std::cos (tiltX) - z * std::sin (tiltX);
        z = y * std::sin (tiltX) + z * std::cos (tiltX);
        y = yz;
        const float xr = x * cr - z * sr * 0.2f;
        const float depth = (z + 1.f) * 0.5f;
        const auto pt = c + juce::Point<float> (xr, y) * R * 0.95f;
        const float s = p.size * (0.7f + 0.9f * depth) * (0.9f + 0.4f * energy);
        const auto col = (p.hue < 0.45f ? Colours::cyan : (p.hue < 0.8f ? Colours::violet : Colours::magenta))
                             .withAlpha (juce::jlimit (0.f, 1.f, (0.30f + 0.7f * depth) * (0.6f + 0.4f * energy)));
        g.setColour (col);
        g.fillEllipse (pt.x - s * 0.5f, pt.y - s * 0.5f, s, s);
    }

    // thinking: orbiting nodes on the rings
    if (state.phase == AgentPhase::Thinking)
    {
        for (int i = 0; i < 3; ++i)
        {
            const float a = spin * 2.2f + i * kTwoPi / 3.f;
            const auto pt = c + juce::Point<float> (std::cos (a) * R * 0.78f, std::sin (a) * R * 0.30f);
            g.setColour (Colours::cyan.withAlpha (0.25f));
            g.fillEllipse (pt.x - 7, pt.y - 7, 14, 14);
            g.setColour (juce::Colours::white);
            g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5, 5);
        }
    }
    // analyzing: scanning sweep
    if (state.phase == AgentPhase::Analyzing || state.analyzing)
    {
        const float sweep = std::fmod ((float) phaseTime * 1.6f, 2.f) - 1.f;
        juce::ColourGradient sw (Colours::cyan.withAlpha (0.f), c.x, c.y + sweep * R - R * 0.25f, Colours::cyan.withAlpha (0.22f), c.x, c.y + sweep * R, false);
        sw.addColour (0.99, Colours::cyan.withAlpha (0.0f));
        g.setGradientFill (sw);
        g.fillRect (juce::Rectangle<float> (c.x - R, c.y + sweep * R - R * 0.25f, R * 2, R * 0.25f));
    }
    g.restoreState();

    // rim
    {
        const auto rim = juce::Rectangle<float> (R * 2, R * 2).withCentre (c);
        g.setGradientFill (accentGradient (rim.getTopLeft(), rim.getBottomRight(), 0.55f + 0.35f * energy));
        g.drawEllipse (rim, 1.6f);
        g.setColour (Colours::cyan.withAlpha (0.10f + 0.12f * energy));
        g.drawEllipse (rim.expanded (4.f), 5.f);
    }

    // matching: two signatures converge from left (current) and right (reference)
    if (state.phase == AgentPhase::Matching)
    {
        const float k = (float) std::min (1.0, phaseTime / 2.5);
        for (int side = 0; side < 2; ++side)
        {
            const float off = (1.f - k) * R * 0.9f * (side == 0 ? -1.f : 1.f);
            const auto rc = c + juce::Point<float> (off, 0);
            const auto col = side == 0 ? Colours::cyan : Colours::purple;
            g.setColour (col.withAlpha (0.55f));
            g.drawEllipse (juce::Rectangle<float> (R * 1.1f, R * 1.1f).withCentre (rc), 1.5f);
        }
    }
    // processing: energy streaming down toward the chain
    if (state.phase == AgentPhase::Processing)
    {
        const float h = (float) getHeight();
        for (int i = 0; i < 6; ++i)
        {
            const float ph = std::fmod ((float) phaseTime * 1.2f + i / 6.f, 1.f);
            const float x = c.x + (i - 2.5f) * R * 0.25f;
            const float y0 = c.y + R * 0.9f + ph * (h - c.y - R * 0.9f);
            juce::ColourGradient beam (Colours::cyan.withAlpha (0.f), x, y0 - 40, Colours::cyan.withAlpha (0.55f * (1.f - ph)), x, y0, false);
            g.setGradientFill (beam);
            g.fillRoundedRectangle (x - 1.2f, y0 - 40, 2.4f, 40, 1.2f);
        }
    }

    // core + "AI"
    {
        const float cr2 = R * 0.34f;
        const auto core = juce::Rectangle<float> (cr2 * 2, cr2 * 2).withCentre (c);
        juce::ColourGradient cg (juce::Colour (0xff070b1f), c.x, c.y, juce::Colour (0xff111a48), c.x + cr2, c.y, true);
        g.setGradientFill (cg);
        g.fillEllipse (core);
        g.setColour (Colours::cyan.withAlpha (0.25f + 0.2f * energy));
        g.drawEllipse (core, 1.2f);
        g.setFont (Fonts::brand (R * 0.34f));
        const juce::String ai ("AI");
        g.setColour (Colours::cyan.withAlpha (0.25f));
        for (auto d : { -2.f, 2.f }) g.drawText (ai, core.translated (d, 0), juce::Justification::centred, false);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x, c.y - cr2, Colours::cyan, c.x, c.y + cr2, false));
        g.drawText (ai, core, juce::Justification::centred, false);
    }

    // completion flash
    if (flash > 0.0)
    {
        const float k = 1.f - (float) flash;
        const float rr = R * (1.0f + 0.6f * k);
        g.setColour (Colours::cyan.withAlpha ((float) flash * 0.7f));
        g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre (c), 2.5f * (float) flash + 0.5f);
    }
    if (state.phase == AgentPhase::Error)
    {
        g.setColour (Colours::bad.withAlpha (0.25f));
        g.drawEllipse (juce::Rectangle<float> (R * 2.08f, R * 2.08f).withCentre (c), 2.f);
    }
}

void OrbView::paintLabels (juce::Graphics& g)
{
    // Status labels around the orb light up only for the phase the engine is really in.
    const auto c = orbArea.getCentre();
    struct L { const char* text; bool left; float dy; bool active; };
    const bool listening = state.listening || state.phase == AgentPhase::Listening;
    const bool analyzing = state.analyzing || state.phase == AgentPhase::Analyzing;
    const L labels[] = {
        { "ANALYZING", true, -0.52f, analyzing },
        { "UNDERSTANDING", true, -0.24f, state.phase == AgentPhase::Thinking },
        { "ADAPTING", true, 0.04f, state.phase == AgentPhase::Complete },
        { "LISTENING", false, -0.52f, listening },
        { "MATCHING", false, -0.24f, state.phase == AgentPhase::Matching },
        { "CRAFTING", false, 0.04f, state.phase == AgentPhase::Processing },
    };
    g.setFont (Fonts::title (10.5f, 0.25f));
    for (auto& l : labels)
    {
        const float y = c.y + l.dy * radius * 1.3f;
        const float dotX = l.left ? c.x - radius * 1.22f : c.x + radius * 1.22f;
        const float pulse = l.active ? 0.6f + 0.4f * std::sin ((float) t * 6.f) : 0.f;
        g.setColour (l.active ? Colours::cyan.withAlpha (0.25f + 0.25f * pulse) : juce::Colours::transparentBlack);
        g.fillEllipse (dotX - 6, y - 6, 12, 12);
        g.setColour (l.active ? Colours::cyan : Colours::cyanSoft.withAlpha (0.45f));
        g.fillEllipse (dotX - 3, y - 3, 6, 6);
        g.setColour (l.active ? Colours::text : Colours::textMute);
        const float w = 130.f;
        if (l.left) g.drawText (l.text, juce::Rectangle<float> (dotX - 14 - w, y - 8, w, 16), juce::Justification::centredRight, false);
        else g.drawText (l.text, juce::Rectangle<float> (dotX + 14, y - 8, w, 16), juce::Justification::centredLeft, false);
    }
}

} // namespace nova::ui
