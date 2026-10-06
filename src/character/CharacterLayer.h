#pragma once

#include "CharacterAssets.h"

#include "../gui/VisualBridge.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace vbd::character
{

/** Critically-damped-ish spring, used for hair, outfit and dragon secondary motion. */
struct Spring
{
    float value = 0.0f;
    float velocity = 0.0f;

    void update (float target, float stiffness, float damping, float dt) noexcept
    {
        const auto accel = (target - value) * stiffness - velocity * damping;
        velocity += accel * dt;
        value += velocity * dt;

        if (! std::isfinite (value) || ! std::isfinite (velocity))
        {
            value = target;
            velocity = 0.0f;
        }
    }
};

/**
    The illustrated layer: an original character and a dragon, rigged in code and driven
    by the audio snapshot.

    Sits BEHIND the fractal visualiser and never above a control, so it cannot get in the
    way of using the plugin. It reads only the lock-free snapshot the editor already
    acquires -- it never touches the processor or the audio thread.

    Performance Mode freezes every channel at rest and skips the per-frame maths; hiding
    the layer skips drawing entirely.
*/
class CharacterLayer final : public juce::Component
{
public:
    CharacterLayer();
    ~CharacterLayer() override;

    /** Advances the rig. `dt` in seconds, `snap` the latest audio snapshot. */
    void update (const gui::VisualSnapshot& snap, double dt);

    void paint (juce::Graphics&) override;

    void setCharacterVisible (bool);
    bool isCharacterVisible() const noexcept { return characterVisible; }

    void setPerformanceMode (bool);
    bool isPerformanceMode() const noexcept { return performanceMode; }

    void setScene (int index);
    int getScene() const noexcept { return sceneIndex; }
    int getNumScenes() const noexcept { return assets.sceneNames().size(); }
    juce::String getSceneName() const;

    /** True when the art loaded. False means the layer draws nothing, quietly. */
    bool hasArt() const noexcept { return assets.isValid(); }

    /** Delay state drives the ghost trails: none at all when both delays are off. */
    void setDelayState (bool anyDelayOn, float feedback, float timeMs);

    // Interactions, wired by the editor.
    std::function<void()> onRandomizeRequested;     // double-click the character
    std::function<void()> onSpectralDelayToggled;   // click the dragon
    std::function<void (float)> onTailMacro;        // drag the tail along its arc

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Hit test only where the art actually is, so clicks elsewhere fall through. */
    bool hitTest (int x, int y) override;

private:
    struct Particle
    {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float life = 0.0f, size = 0.0f;
        juce::Colour colour;
    };

    void updateCharacter (const gui::VisualSnapshot&, float dt);
    void updateDragon (const gui::VisualSnapshot&, float dt);
    void spawnFire (const gui::VisualSnapshot&, int count);

    void drawScene (juce::Graphics&) const;
    void drawCharacter (juce::Graphics&) const;
    void drawDragon (juce::Graphics&, float trailAlpha, float phaseOffset) const;
    void drawParticles (juce::Graphics&) const;

    juce::AffineTransform layerTransform (const LayerInfo&, const LayerGroup&,
                                          juce::Rectangle<float> dest,
                                          juce::AffineTransform local) const;

    void drawLayer (juce::Graphics&, const LayerInfo&, const LayerGroup&,
                    juce::Rectangle<float> dest, juce::AffineTransform local,
                    float alpha, juce::Colour tint, float tintAmount) const;

    juce::Point<float> splinePoint (float t, float phase) const;
    juce::Rectangle<float> characterArea() const;
    juce::Rectangle<float> dragonArea() const;

    CharacterAssets assets;

    // ---- character channels
    float breathePhase = 0.0f;
    float breathe = 0.0f;
    Spring swaySlow, swayFast;
    float blink = 0.0f;
    double nextBlinkIn = 2.0;
    int expression = 0;
    float react = 0.0f;
    Spring eyeX, eyeY;
    float headBobPhase = 0.0f;
    float headBob = 0.0f;
    float lowEnergy = 0.0f;
    juce::Colour spectralColour { 0xff35f0ff };
    float spectralGlow = 0.0f;

    // ---- dragon channels
    float splinePhase = 0.0f;
    float wingPhase = 0.0f;
    float wingFlap = 0.0f;
    Spring headLook;
    float roar = 0.0f;
    float tailDrag = 0.0f;
    bool draggingTail = false;
    std::array<Spring, 16> segmentLag {};

    std::array<Particle, 96> particles {};
    int nextParticle = 0;
    float lastTransient = 0.0f;

    // ---- delay-driven trails
    bool delaysOn = false;
    float delayFeedback = 0.0f;
    float delayTimeMs = 0.0f;

    // ---- state
    bool characterVisible = true;
    bool performanceMode = false;
    int sceneIndex = 0;
    juce::Point<float> pointer { -1.0f, -1.0f };
    bool pointerInside = false;
    juce::Random rng { 20260106 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CharacterLayer)
};

} // namespace vbd::character
