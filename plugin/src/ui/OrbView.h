#pragma once

#include "Theme.h"
#include "../ai/Engineer.h"

namespace nova::ui
{

// The NOVA orb. Its behaviour is driven by the engine's REAL phase (never a looping fake):
//   IDLE subtle breathing · LISTENING waves flow into the orb · ANALYZING particles/rings speed up
//   THINKING rotating structures · PROCESSING energy flows toward the chain · MATCHING two
//   signatures converge · COMPLETE short confirmation flash.
class OrbView : public juce::Component
{
public:
    OrbView();

    struct Inputs
    {
        ai::AgentPhase phase = ai::AgentPhase::Idle;
        bool listening = false, analyzing = false;
        float level = 0.f;           // input level 0..1 (from the real meters)
        float listenProgress = -1.f;
        bool referenceLoaded = false;
    };

    void update (const Inputs& in, double dt);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Particle { float theta, phi, r, speed, size, hue; };
    std::vector<Particle> particles;
    Inputs state;
    ai::AgentPhase shownPhase = ai::AgentPhase::Idle;
    double t = 0, phaseTime = 0, flash = 0;
    float energy = 0.f, spin = 0.f, speed = 0.2f, levelSmooth = 0.f;
    juce::Rectangle<float> orbArea;
    float radius = 100.f;

    void paintWings (juce::Graphics& g, juce::Point<float> c);
    void paintOrb (juce::Graphics& g, juce::Point<float> c);
    void paintLabels (juce::Graphics& g);
};

} // namespace nova::ui
