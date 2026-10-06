#include "Panels.h"
#include "Theme.h"

#include "../Params.h"

namespace vbd::gui
{

namespace
{
    constexpr int kTitleHeight = 20;
    constexpr int kKnobWidth   = 56;
    constexpr int kKnobHeight  = 54;
    constexpr int kRowGap      = 4;
    constexpr int kChoiceHeight = 21;
    constexpr int kToggleHeight = 19;
    constexpr int kPad = 8;
}

Panel::Panel (juce::AudioProcessorValueTreeState& state, const juce::String& panelTitle,
              juce::Colour panelAccent)
    : apvts (state), title (panelTitle), accent (panelAccent)
{
}

Panel::~Panel()
{
    // Attachments hold references to the controls, so drop them first.
    sliderAttachments.clear();
    comboAttachments.clear();
    buttonAttachments.clear();
}

void Panel::setDimmed (bool shouldDim)
{
    if (dimmed == shouldDim)
        return;

    dimmed = shouldDim;

    // Alpha on the whole panel, so every child dims together without touching their
    // enabled state -- the controls stay usable, which is what you want when the switch
    // that un-dims the panel is inside it.
    setAlpha (dimmed ? 0.45f : 1.0f);
    repaint();
}

void Panel::addKnob (const char* paramId, const juce::String& label)
{
    auto* param = apvts.getParameter (paramId);

    if (param == nullptr)
    {
        jassertfalse;   // a typo in a parameter id would otherwise fail silently
        return;
    }

    auto knob = std::make_unique<GlowKnob> (label, accent);
    addAndMakeVisible (*knob);

    sliderAttachments.push_back (
        std::make_unique<juce::SliderParameterAttachment> (*param, *knob, nullptr));

    entries.push_back ({ Kind::knob, std::move (knob), nullptr });
}

void Panel::addChoice (const char* paramId, const juce::String& label)
{
    auto* param = apvts.getParameter (paramId);

    if (param == nullptr)
    {
        jassertfalse;
        return;
    }

    auto box = std::make_unique<juce::ComboBox>();

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        box->addItemList (choice->choices, 1);

    box->getProperties().set (VbdLookAndFeel::accentProperty,
                              static_cast<int> (accent.getARGB()));
    addAndMakeVisible (*box);

    comboAttachments.push_back (
        std::make_unique<juce::ComboBoxParameterAttachment> (*param, *box, nullptr));

    auto caption = std::make_unique<juce::Label> (juce::String(), label);
    caption->setFont (juce::FontOptions (9.5f));
    caption->setColour (juce::Label::textColourId, theme::textDim);
    caption->setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (*caption);

    entries.push_back ({ Kind::choice, std::move (box), std::move (caption) });
}

void Panel::addToggle (const char* paramId, const juce::String& label)
{
    auto* param = apvts.getParameter (paramId);

    if (param == nullptr)
    {
        jassertfalse;
        return;
    }

    auto toggle = std::make_unique<GlowToggle> (label, accent);
    addAndMakeVisible (*toggle);

    buttonAttachments.push_back (
        std::make_unique<juce::ButtonParameterAttachment> (*param, *toggle, nullptr));

    entries.push_back ({ Kind::toggle, std::move (toggle), nullptr });
}

void Panel::addRowBreak()
{
    entries.push_back ({ Kind::rowBreak, nullptr, nullptr });
}

void Panel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (theme::panelEdge);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    // Title with an accent rule beneath it.
    auto titleArea = bounds.reduced (static_cast<float> (kPad), 0.0f)
                           .withHeight (static_cast<float> (kTitleHeight))
                           .translated (0.0f, 4.0f);

    g.setColour (accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (title, titleArea, juce::Justification::centredLeft, false);

    g.setColour (accent.withAlpha (0.3f));
    g.fillRect (bounds.getX() + static_cast<float> (kPad),
                titleArea.getBottom() + 1.0f,
                bounds.getWidth() - static_cast<float> (kPad * 2),
                1.0f);
}

void Panel::resized()
{
    auto area = getLocalBounds().reduced (kPad, kPad);
    area.removeFromTop (kTitleHeight + 4);

    auto row = juce::Rectangle<int>();
    auto startRow = [&] (int height)
    {
        row = area.removeFromTop (height);
        area.removeFromTop (kRowGap);
    };

    // Knobs pack across the row until they no longer fit, which keeps the layout
    // responsive to panel width without a hand-written table per panel.
    int rowRemaining = 0;

    for (auto& entry : entries)
    {
        switch (entry.kind)
        {
            case Kind::rowBreak:
                rowRemaining = 0;
                break;

            case Kind::knob:
            {
                if (rowRemaining < kKnobWidth)
                {
                    startRow (kKnobHeight);
                    rowRemaining = row.getWidth();
                }

                entry.control->setBounds (row.removeFromLeft (kKnobWidth).reduced (1, 0));
                rowRemaining = row.getWidth();
                break;
            }

            case Kind::choice:
            {
                startRow (kChoiceHeight);
                rowRemaining = 0;

                auto cell = row;
                const auto captionWidth = juce::jmin (76, cell.getWidth() / 2);
                entry.caption->setBounds (cell.removeFromLeft (captionWidth));
                entry.control->setBounds (cell);
                break;
            }

            case Kind::toggle:
            {
                if (rowRemaining < 64)
                {
                    startRow (kToggleHeight);
                    rowRemaining = row.getWidth();
                }

                const auto w = juce::jmin (juce::jmax (58, rowRemaining / 2), rowRemaining);
                entry.control->setBounds (row.removeFromLeft (w).reduced (1, 0));
                rowRemaining = row.getWidth();
                break;
            }
        }
    }
}

// ============================================================================== Carrier

CarrierPanel::CarrierPanel (juce::AudioProcessorValueTreeState& state)
    : Panel (state, "CARRIER", theme::neon)
{
    addChoice (pid::carrierSource, "Source");
    addChoice (pid::chordOsc, "Osc");
    addChoice (pid::chordType, "Chord");
    addChoice (pid::chordRoot, "Root");
    addRowBreak();
    addKnob (pid::chordOctave, "Oct");
    addKnob (pid::chordSpread, "Spread");
    addKnob (pid::chordDetune, "Detune");
    addKnob (pid::chordLevel, "Level");
    addRowBreak();
    addToggle (pid::chordMidiOvr, "MIDI");
}

void CarrierPanel::setFallbackActive (bool active)
{
    if (fallbackActive == active)
        return;

    fallbackActive = active;
    repaint();
}

void CarrierPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);

    if (! fallbackActive)
        return;

    // Amber badge: the sidechain is selected but nothing is feeding it, so the chord
    // carrier is standing in. Without this the plugin would just look broken.
    auto badge = getLocalBounds().toFloat().reduced (8.0f, 0.0f)
                     .withHeight (16.0f).translated (0.0f, 6.0f)
                     .removeFromRight (104.0f);

    g.setColour (theme::amber.withAlpha (0.18f));
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (theme::amber);
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    g.drawText ("NO SIDECHAIN", badge, juce::Justification::centred, false);
}

// =============================================================================== Engine

EnginePanel::EnginePanel (juce::AudioProcessorValueTreeState& state)
    : Panel (state, "ENGINE", theme::violet)
{
    addChoice (pid::engMode, "Mode");
    addChoice (pid::engFftSize, "FFT");
    addRowBreak();
    addKnob (pid::engMorph, "Morph");
    addKnob (pid::engFormant, "Formant");
    addKnob (pid::engTilt, "Tilt");
    addKnob (pid::engEnvRes, "Env Res");
    addRowBreak();
    addKnob (pid::engPhaseLock, "Ph Lock");
    addKnob (pid::engSens, "Sens");
    addKnob (pid::engFreqShift, "Freq Sh");
    addRowBreak();
    addToggle (pid::engFreeze, "FREEZE");
    addToggle (pid::engFlip, "FLIP");
    addRowBreak();
    addToggle (pid::engEnvCepstral, "CEPSTRAL");
}

// ============================================================================== Fractal

FractalPanel::FractalPanel (juce::AudioProcessorValueTreeState& state)
    : Panel (state, "FRACTAL", theme::magenta)
{
    addChoice (pid::fracPattern, "Pattern");
    addRowBreak();
    addKnob (pid::fracDepth, "Depth");
    addKnob (pid::fracShatter, "Shatter");
    addKnob (pid::fracRatio, "Ratio");
    addKnob (pid::fracAsym, "Asym");
    addKnob (pid::fracLoHz, "Low");
    addRowBreak();
    addKnob (pid::fracHiHz, "High");
    addKnob (pid::fracGate, "Gate");
    addKnob (pid::fracGrit, "Grit");
    addKnob (pid::fracMix, "Mix");
    addRowBreak();
    addChoice (pid::fracDiv, "Sync Rate");
    addRowBreak();
    addToggle (pid::fracSync, "SYNC");
    addToggle (pid::fracInvert, "INVERT");
}

void FractalPanel::setResolvedDepth (int requested, int effective, int bands)
{
    if (requestedDepth == requested && effectiveDepth == effective && bandCount == bands)
        return;

    requestedDepth = requested;
    effectiveDepth = effective;
    bandCount = bands;
    repaint();
}

void FractalPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);

    // The resolved depth belongs on screen: a pattern's leaf count or the bin resolution
    // can put the requested depth out of reach, and a Depth control that silently ignores
    // you is worse than one that explains itself.
    auto badge = getLocalBounds().toFloat().reduced (8.0f, 0.0f)
                     .withHeight (16.0f).translated (0.0f, 6.0f)
                     .removeFromRight (108.0f);

    const auto clamped = effectiveDepth > 0 && effectiveDepth < requestedDepth;

    g.setColour (clamped ? theme::amber : theme::textDim);
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));

    const auto text = clamped
        ? "D" + juce::String (requestedDepth) + juce::String (" -> ")
              + juce::String (effectiveDepth) + "  " + juce::String (bandCount) + "B"
        : juce::String (bandCount) + " BANDS";

    g.drawText (text, badge, juce::Justification::centredRight, false);
}

// =============================================================================== Delays

DelaysPanel::DelaysPanel (juce::AudioProcessorValueTreeState& state)
    : Panel (state, "DELAYS", theme::lime)
{
    addToggle (pid::sdlyOn, "SPECTRAL");
    addRowBreak();
    addKnob (pid::sdlyTimeMs, "Time");
    addKnob (pid::sdlySpread, "Spread");
    addKnob (pid::sdlyFb, "Fdbk");
    addKnob (pid::sdlyDamp, "Damp");
    addKnob (pid::sdlyMix, "Mix");
    addRowBreak();
    addToggle (pid::dlyOn, "STEREO");
    addToggle (pid::dlyFreeze, "HOLD");
    addRowBreak();
    addChoice (pid::dlyMode, "Mode");
    addRowBreak();
    addKnob (pid::dlyFb, "Fdbk");
    addKnob (pid::dlyMix, "Mix");
    addKnob (pid::dlyDiffuse, "Diffuse");
    addKnob (pid::dlyDuck, "Duck");
    addKnob (pid::dlyWidth, "Width");
    addRowBreak();
    addKnob (pid::dlyHp, "Loop HP");
    addKnob (pid::dlyLp, "Loop LP");
    addKnob (pid::dlySat, "Drive");
    addKnob (pid::dlyModDepth, "Wow");
    addKnob (pid::dlyModRate, "Rate");
    addRowBreak();
    addToggle (pid::dlySync, "SYNC");
    addToggle (pid::dlyPlace, "POST");
}

void DelaysPanel::setResolvedSpectralTime (float ms)
{
    if (juce::approximatelyEqual (resolvedMs, ms))
        return;

    resolvedMs = ms;
    repaint();
}

void DelaysPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);

    if (resolvedMs <= 0.0f)
        return;

    // The achieved spectral delay time, which the 128-frame ring can cap below the
    // request at small FFT sizes or high sample rates.
    auto badge = getLocalBounds().toFloat().reduced (8.0f, 0.0f)
                     .withHeight (16.0f).translated (0.0f, 6.0f)
                     .removeFromRight (90.0f);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    g.drawText (juce::String (resolvedMs, 0) + " MS", badge,
                juce::Justification::centredRight, false);
}

// =============================================================================== Output

OutputPanel::OutputPanel (juce::AudioProcessorValueTreeState& state)
    : Panel (state, "OUTPUT", theme::amber)
{
    // Laid out as a wide strip beneath the analyser rather than a column panel: the side
    // columns cannot hold five panels at a readable control size, and the output stage is
    // the one section that reads naturally as a horizontal chain.
    addChoice (pid::colSatType, "Saturation");
    addRowBreak();
    addKnob (pid::colDrive, "Drive");
    addKnob (pid::colWidth, "Width");
    addKnob (pid::colLoCut, "Lo Cut");
    addKnob (pid::colHiCut, "Hi Cut");
    addKnob (pid::outMix, "Mix");
    addKnob (pid::outGain, "Gain");
    addRowBreak();
    addToggle (pid::outLimiter, "LIMITER");
}

} // namespace vbd::gui
