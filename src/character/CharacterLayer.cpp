#include "CharacterLayer.h"

#include "../gui/Theme.h"

namespace vbd::character
{

namespace
{
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    // The character occupies the left edge of the centre column, the dragon coils around
    // the whole area. Both sit behind the visualiser, so neither can cover a control.
    constexpr float kCharacterWidthFraction = 0.34f;

    float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
}

CharacterLayer::CharacterLayer()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);

    // The @2x set is loaded when the display can show it; layer rects stay in 1x units
    // either way, so nothing downstream has to care which was used.
    const auto scale = juce::Desktop::getInstance().getDisplays()
                           .getPrimaryDisplay() != nullptr
                       ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                       : 1.0;

    assets.load (scale >= 1.5);

    for (auto& p : particles)
        p.life = 0.0f;
}

CharacterLayer::~CharacterLayer() = default;

juce::String CharacterLayer::getSceneName() const
{
    const auto& names = assets.sceneNames();

    if (names.isEmpty())
        return {};

    return names[juce::jlimit (0, names.size() - 1, sceneIndex)];
}

void CharacterLayer::setCharacterVisible (bool shouldShow)
{
    if (characterVisible == shouldShow)
        return;

    characterVisible = shouldShow;
    setInterceptsMouseClicks (characterVisible, false);
    repaint();
}

void CharacterLayer::setPerformanceMode (bool shouldDisable)
{
    if (performanceMode == shouldDisable)
        return;

    performanceMode = shouldDisable;
    repaint();
}

void CharacterLayer::setScene (int index)
{
    const auto count = juce::jmax (1, assets.sceneNames().size());
    const auto wrapped = ((index % count) + count) % count;

    if (sceneIndex == wrapped)
        return;

    sceneIndex = wrapped;
    repaint();
}

void CharacterLayer::setDelayState (bool anyOn, float feedback, float timeMs)
{
    delaysOn = anyOn;
    delayFeedback = feedback;
    delayTimeMs = timeMs;
}

juce::Rectangle<float> CharacterLayer::characterArea() const
{
    auto bounds = getLocalBounds().toFloat();
    const auto width = bounds.getWidth() * kCharacterWidthFraction;

    // Anchored bottom-left, sized to the panel height.
    return { bounds.getX(), bounds.getY(), width, bounds.getHeight() };
}

juce::Rectangle<float> CharacterLayer::dragonArea() const
{
    return getLocalBounds().toFloat().reduced (4.0f);
}

// ------------------------------------------------------------------------------- update

void CharacterLayer::update (const gui::VisualSnapshot& snap, double dtSeconds)
{
    if (! characterVisible || ! assets.isValid())
        return;

    if (performanceMode)
    {
        // Everything parked at rest: no springs, no phases, no particles. Still drawn,
        // just not animated.
        repaint();
        return;
    }

    const auto dt = juce::jlimit (0.001f, 0.1f, static_cast<float> (dtSeconds));

    // The prism position follows the spectral centroid, so the character's glow tracks
    // the colour of the sound rather than a free-running clock.
    float weighted = 0.0f, total = 0.0f;

    for (int i = 0; i < gui::VisualSnapshot::spectrumPoints; ++i)
    {
        const auto mag = snap.outputMag[static_cast<std::size_t> (i)];
        weighted += mag * static_cast<float> (i);
        total += mag;
    }

    const auto centroid = total > 1.0e-6f
        ? weighted / (total * static_cast<float> (gui::VisualSnapshot::spectrumPoints))
        : 0.3f;

    spectralColour = theme::prism (centroid);
    spectralGlow += 0.15f * (juce::jlimit (0.0f, 1.0f, snap.outputPeak * 2.0f) - spectralGlow);
    lowEnergy += 0.12f * (snap.lowEnergy - lowEnergy);

    updateCharacter (snap, dt);
    updateDragon (snap, dt);

    repaint();
}

void CharacterLayer::updateCharacter (const gui::VisualSnapshot& snap, float dt)
{
    // Idle breathing: a slow sine on the torso and head.
    breathePhase += dt * 0.28f;

    if (breathePhase > 1.0f)
        breathePhase -= 1.0f;

    breathe = std::sin (breathePhase * kTwoPi);

    // Random blinking. Humans blink irregularly, so the interval is re-rolled each time
    // rather than fixed -- a metronomic blink is immediately uncanny.
    nextBlinkIn -= dt;

    if (nextBlinkIn <= 0.0)
    {
        nextBlinkIn = 1.6 + rng.nextDouble() * 4.2;
        blink = 1.0f;
    }

    blink = juce::jmax (0.0f, blink - dt * 7.0f);

    // Hair and outfit sway: springs chasing a target set by low-end energy, so the
    // character moves with the bass rather than on a timer.
    const auto swayTarget = lowEnergy * 1.6f + std::sin (breathePhase * kTwoPi * 0.5f) * 0.18f;
    swaySlow.update (swayTarget, 26.0f, 7.0f, dt);
    swayFast.update (swayTarget * 1.4f, 70.0f, 9.0f, dt);

    // Head bob locked to the host tempo, from the playhead.
    const auto beatsPerSecond = static_cast<float> (snap.bpm) / 60.0f;
    headBobPhase += dt * (snap.playing ? beatsPerSecond : 0.6f);

    if (headBobPhase > 1.0f)
        headBobPhase -= 1.0f;

    headBob = std::sin (headBobPhase * kTwoPi) * (snap.playing ? 1.0f : 0.35f);

    // Eyes follow the pointer.
    const auto area = characterArea();
    const auto eyeTargetX = pointerInside
        ? juce::jlimit (-1.0f, 1.0f, (pointer.x - area.getCentreX()) / juce::jmax (1.0f, area.getWidth()))
        : 0.0f;
    const auto eyeTargetY = pointerInside
        ? juce::jlimit (-1.0f, 1.0f, (pointer.y - area.getY() - area.getHeight() * 0.2f)
                                       / juce::jmax (1.0f, area.getHeight() * 0.5f))
        : 0.0f;

    eyeX.update (eyeTargetX, 90.0f, 13.0f, dt);
    eyeY.update (eyeTargetY, 90.0f, 13.0f, dt);

    react = juce::jmax (0.0f, react - dt * 1.6f);
}

void CharacterLayer::updateDragon (const gui::VisualSnapshot& snap, float dt)
{
    const auto beatsPerSecond = static_cast<float> (snap.bpm) / 60.0f;

    splinePhase += dt * 0.08f;

    if (splinePhase > 1.0f)
        splinePhase -= 1.0f;

    // Wings flap on the beat.
    wingPhase += dt * (snap.playing ? beatsPerSecond * 0.5f : 0.35f);

    if (wingPhase > 1.0f)
        wingPhase -= 1.0f;

    wingFlap = std::sin (wingPhase * kTwoPi);

    // Head turns towards the pointer.
    const auto area = dragonArea();
    const auto lookTarget = pointerInside
        ? juce::jlimit (-1.0f, 1.0f,
                        (pointer.y - area.getCentreY()) / juce::jmax (1.0f, area.getHeight() * 0.5f))
        : 0.0f;

    headLook.update (lookTarget, 60.0f, 11.0f, dt);

    // Secondary motion: each segment lags the one ahead of it.
    for (std::size_t i = 0; i < segmentLag.size(); ++i)
    {
        const auto lead = i == 0 ? headLook.value : segmentLag[i - 1].value;
        segmentLag[i].update (lead, 48.0f - static_cast<float> (i) * 1.6f, 9.0f, dt);
    }

    roar = juce::jmax (0.0f, roar - dt * 1.3f);

    // Fire on input transients. The snapshot's peak rising sharply is the cue.
    const auto transient = snap.inputPeak - lastTransient;
    lastTransient = lerp (lastTransient, snap.inputPeak, 0.25f);

    if ((transient > 0.07f || roar > 0.5f) && ! performanceMode)
        spawnFire (snap, roar > 0.5f ? 5 : 3);

    // Particle integration.
    for (auto& p : particles)
    {
        if (p.life <= 0.0f)
            continue;

        p.life -= dt * 1.1f;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 26.0f * dt;          // slight lift, fire rises then falls
        p.vx *= 0.985f;
    }
}

void CharacterLayer::spawnFire (const gui::VisualSnapshot& snap, int count)
{
    const auto area = dragonArea();
    const auto mouth = splinePoint (0.0f, splinePhase);

    for (int i = 0; i < count; ++i)
    {
        auto& p = particles[static_cast<std::size_t> (nextParticle)];
        nextParticle = (nextParticle + 1) % static_cast<int> (particles.size());

        p.x = mouth.x + (rng.nextFloat() - 0.5f) * 6.0f;
        p.y = mouth.y + (rng.nextFloat() - 0.5f) * 6.0f;
        p.vx = -40.0f - rng.nextFloat() * 70.0f;
        p.vy = (rng.nextFloat() - 0.5f) * 60.0f;
        p.life = 0.7f + rng.nextFloat() * 0.5f;
        p.size = 2.5f + rng.nextFloat() * 5.0f;

        // Fire colour follows the spectrum, as specified -- bright content gives a
        // cooler flame, bass-heavy content a warmer one.
        p.colour = spectralColour.interpolatedWith (theme::amber, 0.45f + rng.nextFloat() * 0.3f);
    }

    juce::ignoreUnused (snap, area);
}

// -------------------------------------------------------------------------- spline path

juce::Point<float> CharacterLayer::splinePoint (float t, float phase) const
{
    // A flattened coil around the visualiser area: the dragon reads as wrapping the
    // display rather than sitting beside it.
    const auto area = dragonArea();
    const auto angle = (t * 1.35f + phase) * kTwoPi;

    const auto rx = area.getWidth() * 0.42f;
    const auto ry = area.getHeight() * 0.40f;

    // A second harmonic gives the serpentine waver, so it is not a plain ellipse.
    const auto wobble = std::sin (angle * 2.0f + phase * kTwoPi) * 0.12f;

    return { area.getCentreX() + std::cos (angle) * rx * (1.0f + wobble),
             area.getCentreY() + std::sin (angle) * ry * (1.0f - wobble) };
}

// ------------------------------------------------------------------------------ drawing

juce::AffineTransform CharacterLayer::layerTransform (const LayerInfo& layer,
                                                      const LayerGroup& group,
                                                      juce::Rectangle<float> dest,
                                                      juce::AffineTransform local) const
{
    // Canvas pixels to destination rectangle.
    const auto sx = dest.getWidth() / static_cast<float> (juce::jmax (1, group.canvas.x));
    const auto sy = dest.getHeight() / static_cast<float> (juce::jmax (1, group.canvas.y));

    const auto pivotX = layer.pivot.x * static_cast<float> (group.canvas.x);
    const auto pivotY = layer.pivot.y * static_cast<float> (group.canvas.y);

    // Place the cropped image at its recorded offset, apply the layer's own animation
    // about its declared pivot, then map the whole canvas into the destination.
    return juce::AffineTransform::translation (static_cast<float> (layer.rect.getX()) - pivotX,
                                               static_cast<float> (layer.rect.getY()) - pivotY)
             .followedBy (local)
             .followedBy (juce::AffineTransform::translation (pivotX, pivotY))
             .followedBy (juce::AffineTransform::scale (sx, sy))
             .followedBy (juce::AffineTransform::translation (dest.getX(), dest.getY()));
}

void CharacterLayer::drawLayer (juce::Graphics& g, const LayerInfo& layer,
                                const LayerGroup& group, juce::Rectangle<float> dest,
                                juce::AffineTransform local, float alpha,
                                juce::Colour tint, float tintAmount) const
{
    if (! layer.image.isValid() || alpha <= 0.004f)
        return;

    auto transform = layerTransform (layer, group, dest, local);

    // The @2x art is twice the size its rect describes, so it draws at half scale.
    if (assets.getImageScale() == 2)
        transform = juce::AffineTransform::scale (0.5f).followedBy (transform);

    g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha));
    g.drawImageTransformed (layer.image, transform, false);

    g.setOpacity (1.0f);

    // Spectral colour wash over the layer's own footprint.
    //
    // The obvious way to recolour a layer is to clip to its alpha and fill -- but
    // reduceClipRegion with an image rasterises a mask every call, which is far too
    // expensive sixty times a second across forty layers. A soft radial over the layer's
    // bounds reads the same at this size and costs one ellipse.
    if (tintAmount > 0.01f)
    {
        const auto centre = layer.rect.toFloat().getCentre().transformedBy (
            layerTransform (layer, group, dest, local));
        const auto radius = juce::jmax (layer.rect.getWidth(), layer.rect.getHeight())
                          * 0.5f * (dest.getWidth() / static_cast<float> (juce::jmax (1, group.canvas.x)))
                          * 1.6f;

        juce::ColourGradient glow (tint.withAlpha (juce::jlimit (0.0f, 0.6f, tintAmount * alpha)),
                                   centre,
                                   tint.withAlpha (0.0f),
                                   centre.translated (radius, 0.0f), true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }
}

void CharacterLayer::drawScene (juce::Graphics& g) const
{
    const auto* scene = assets.scene (getSceneName());

    if (scene == nullptr)
        return;

    const auto dest = getLocalBounds().toFloat();

    for (const auto& layer : scene->layers)
    {
        // Parallax: deeper layers move less as the pointer travels.
        const auto offsetX = pointerInside
            ? (pointer.x / juce::jmax (1.0f, dest.getWidth()) - 0.5f) * layer.parallax * 26.0f
            : 0.0f;
        const auto offsetY = pointerInside
            ? (pointer.y / juce::jmax (1.0f, dest.getHeight()) - 0.5f) * layer.parallax * 14.0f
            : 0.0f;

        drawLayer (g, layer, *scene, dest,
                   juce::AffineTransform::translation (-offsetX, -offsetY),
                   0.42f + layer.parallax * 0.22f, spectralColour, 0.0f);
    }
}

void CharacterLayer::drawCharacter (juce::Graphics& g) const
{
    const auto* group = assets.group ("character");

    if (group == nullptr)
        return;

    const auto dest = characterArea();

    const auto breatheScale = 1.0f + breathe * 0.012f;
    const auto swaySlowAngle = swaySlow.value * 0.05f;
    const auto swayFastAngle = swayFast.value * 0.07f;
    const auto bobY = headBob * 2.4f;

    for (const auto& layer : group->layers)
    {
        // Mutually exclusive variants: pick by expression and blink state.
        if (layer.name.startsWith ("eyes_"))
        {
            const auto wantClosed = blink > 0.35f;
            const auto wantHalf = ! wantClosed && (expression == 2);
            const auto wanted = wantClosed ? "eyes_closed" : (wantHalf ? "eyes_half" : "eyes_open");

            if (layer.name != wanted)
                continue;
        }

        if (layer.name.startsWith ("mouth_"))
        {
            const char* wanted = expression == 1 ? "mouth_smile"
                               : (expression == 3 ? "mouth_open" : "mouth_neutral");

            if (layer.name != wanted)
                continue;
        }

        juce::AffineTransform local;
        auto alpha = 1.0f;
        auto tintAmount = 0.0f;

        if (layer.hasChannel ("breathe"))
            local = local.followedBy (juce::AffineTransform::scale (1.0f, breatheScale));

        if (layer.hasChannel ("sway_slow"))
            local = local.followedBy (juce::AffineTransform::rotation (swaySlowAngle));

        if (layer.hasChannel ("sway_fast"))
            local = local.followedBy (juce::AffineTransform::rotation (swayFastAngle));

        if (layer.hasChannel ("head_bob"))
            local = local.followedBy (juce::AffineTransform::translation (0.0f, bobY));

        if (layer.hasChannel ("eye_track"))
            local = local.followedBy (juce::AffineTransform::translation (eyeX.value * 3.2f,
                                                                          eyeY.value * 2.0f));

        if (layer.hasChannel ("blink") && layer.name == "eyes_open")
            local = local.followedBy (juce::AffineTransform::scale (1.0f,
                                                                    juce::jmax (0.08f, 1.0f - blink)));

        if (layer.hasChannel ("spectral_glow"))
            tintAmount = 0.25f + spectralGlow * 0.55f;

        if (layer.hasChannel ("low_energy"))
            local = local.followedBy (juce::AffineTransform::rotation (lowEnergy * 0.035f));

        if (layer.name == "glow_fx")
            alpha = 0.18f + spectralGlow * 0.5f + react * 0.3f;

        drawLayer (g, layer, *group, dest, local, alpha, spectralColour, tintAmount);
    }
}

void CharacterLayer::drawDragon (juce::Graphics& g, float trailAlpha, float phaseOffset) const
{
    const auto* group = assets.group ("dragon");

    if (group == nullptr)
        return;

    const auto area = dragonArea();

    // Each body/tail segment rides the spline at its own offset.
    const auto segmentSize = juce::jmin (area.getWidth(), area.getHeight()) * 0.30f;

    for (const auto& layer : group->layers)
    {
        float t = -1.0f;

        if (layer.name.startsWith ("body_"))
            t = static_cast<float> (layer.name.getTrailingIntValue()) * 0.055f;
        else if (layer.name.startsWith ("tail_"))
            t = 0.46f + static_cast<float> (layer.name.getTrailingIntValue()) * 0.055f;
        else if (layer.name == "head" || layer.name == "jaw" || layer.name == "eye")
            t = -0.055f;
        else if (layer.name == "fire")
            t = -0.12f;
        else if (layer.name.startsWith ("wing"))
            t = 0.11f;

        if (t < -0.5f)
            continue;

        const auto point = splinePoint (t, splinePhase + phaseOffset);
        const auto ahead = splinePoint (t - 0.03f, splinePhase + phaseOffset);
        const auto heading = std::atan2 (ahead.y - point.y, ahead.x - point.x);

        const auto index = juce::jlimit (0, static_cast<int> (segmentLag.size()) - 1,
                                          static_cast<int> (t * 18.0f));
        const auto lag = segmentLag[static_cast<std::size_t> (index)].value;

        auto local = juce::AffineTransform::rotation (heading + lag * 0.22f);

        if (layer.hasChannel ("wing_flap"))
        {
            const auto flap = 0.65f + 0.35f * wingFlap * (layer.name == "wing_r" ? -1.0f : 1.0f);
            local = juce::AffineTransform::scale (1.0f, flap).followedBy (local);
        }

        if (layer.hasChannel ("look_at"))
            local = juce::AffineTransform::rotation (headLook.value * 0.3f).followedBy (local);

        if (layer.hasChannel ("roar") && roar > 0.0f)
            local = juce::AffineTransform::scale (1.0f + roar * 0.22f).followedBy (local);

        if (layer.hasChannel ("tail_drag"))
            local = juce::AffineTransform::rotation (tailDrag * 0.5f).followedBy (local);

        // Place the segment: the canvas maps to a box centred on the spline point.
        const auto dest = juce::Rectangle<float> (segmentSize * 2.0f, segmentSize)
                              .withCentre (point);

        auto alpha = trailAlpha;

        if (layer.name == "fire")
            alpha *= juce::jlimit (0.0f, 1.0f, roar * 0.8f + spectralGlow * 0.35f);

        const auto tintAmount = layer.hasChannel ("spectral_glow")
                                  ? 0.3f + spectralGlow * 0.5f : 0.0f;

        drawLayer (g, layer, *group, dest, local, alpha, spectralColour, tintAmount);
    }
}

void CharacterLayer::drawParticles (juce::Graphics& g) const
{
    for (const auto& p : particles)
    {
        if (p.life <= 0.0f)
            continue;

        const auto alpha = juce::jlimit (0.0f, 1.0f, p.life * 0.8f);
        const auto size = p.size * (0.4f + p.life * 0.9f);

        g.setColour (p.colour.withAlpha (alpha * 0.55f));
        g.fillEllipse (p.x - size, p.y - size, size * 2.0f, size * 2.0f);

        g.setColour (p.colour.withAlpha (alpha));
        g.fillEllipse (p.x - size * 0.45f, p.y - size * 0.45f, size * 0.9f, size * 0.9f);
    }
}

void CharacterLayer::paint (juce::Graphics& g)
{
    if (! characterVisible || ! assets.isValid())
        return;

    drawScene (g);

    // Ghost trails, driven by the delays. Count follows feedback, fade follows time --
    // and with both delays off there are none at all, as specified.
    if (delaysOn && ! performanceMode)
    {
        const auto trails = juce::jlimit (1, 5, static_cast<int> (delayFeedback * 6.0f));
        const auto spacing = juce::jlimit (0.004f, 0.05f, delayTimeMs * 0.00004f);

        for (int i = trails; i >= 1; --i)
        {
            const auto fade = 0.26f * std::pow (juce::jlimit (0.1f, 0.95f, delayFeedback),
                                                 static_cast<float> (i));
            drawDragon (g, fade, -spacing * static_cast<float> (i));
        }
    }

    drawDragon (g, 0.95f, 0.0f);
    drawParticles (g);
    drawCharacter (g);
}

// -------------------------------------------------------------------------- interaction

bool CharacterLayer::hitTest (int x, int y)
{
    if (! characterVisible || ! assets.isValid())
        return false;

    const auto point = juce::Point<float> (static_cast<float> (x), static_cast<float> (y));

    // Only the character's own column and the dragon's head and tail are clickable; the
    // rest of the area belongs to the visualiser above.
    if (characterArea().reduced (0.0f, characterArea().getHeight() * 0.1f).contains (point))
        return true;

    const auto head = splinePoint (-0.055f, splinePhase);
    const auto tail = splinePoint (0.62f, splinePhase);
    const auto radius = juce::jmin (getWidth(), getHeight()) * 0.09f;

    return point.getDistanceFrom (head) < radius || point.getDistanceFrom (tail) < radius;
}

void CharacterLayer::mouseMove (const juce::MouseEvent& e)
{
    pointer = e.position;
    pointerInside = true;
}

void CharacterLayer::mouseExit (const juce::MouseEvent&)
{
    pointerInside = false;
}

void CharacterLayer::mouseDown (const juce::MouseEvent& e)
{
    pointer = e.position;
    pointerInside = true;

    const auto tail = splinePoint (0.62f, splinePhase);
    const auto radius = juce::jmin (getWidth(), getHeight()) * 0.09f;

    if (e.position.getDistanceFrom (tail) < radius)
    {
        draggingTail = true;
        return;
    }

    const auto head = splinePoint (-0.055f, splinePhase);

    if (e.position.getDistanceFrom (head) < radius)
    {
        roar = 1.0f;
        spawnFire ({}, 14);

        if (onSpectralDelayToggled != nullptr)
            onSpectralDelayToggled();

        return;
    }

    if (characterArea().contains (e.position))
    {
        // Cycle expressions.
        expression = (expression + 1) % 4;
        react = 0.5f;
        repaint();
    }
}

void CharacterLayer::mouseDrag (const juce::MouseEvent& e)
{
    pointer = e.position;

    if (! draggingTail)
        return;

    // The tail sweeps an arc; its angle about the centre drives the macro.
    const auto centre = dragonArea().getCentre();
    const auto angle = std::atan2 (e.position.y - centre.y, e.position.x - centre.x);

    tailDrag = juce::jlimit (-1.0f, 1.0f, angle / juce::MathConstants<float>::pi);

    if (onTailMacro != nullptr)
        onTailMacro (juce::jlimit (0.0f, 1.0f, (tailDrag + 1.0f) * 0.5f));

    repaint();
}

void CharacterLayer::mouseUp (const juce::MouseEvent&)
{
    draggingTail = false;
}

void CharacterLayer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! characterArea().contains (e.position))
        return;

    react = 1.0f;
    expression = 3;

    if (onRandomizeRequested != nullptr)
        onRandomizeRequested();
}

} // namespace vbd::character
