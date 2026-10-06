#include "Params.h"

namespace vbd
{

namespace
{
    using APF   = juce::AudioParameterFloat;
    using APC   = juce::AudioParameterChoice;
    using APB   = juce::AudioParameterBool;
    using API   = juce::AudioParameterInt;
    using Group = juce::AudioProcessorParameterGroup;
    using Attr  = juce::AudioParameterFloatAttributes;

    // versionHint 1 for every parameter: AUv3 requires it, and keeping it uniform means
    // later additions can bump only their own hint without disturbing existing automation.
    constexpr int kHint = 1;

    juce::ParameterID pidOf (const char* id) { return juce::ParameterID { id, kHint }; }

    juce::NormalisableRange<float> linRange (float lo, float hi, float step = 0.01f)
    {
        return juce::NormalisableRange<float> { lo, hi, step };
    }

    /** Frequency range skewed so the given centre sits mid-travel -- i.e. a log-feeling dial. */
    juce::NormalisableRange<float> freqRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r { lo, hi, 0.01f };
        r.setSkewForCentre (centre);
        return r;
    }

    std::unique_ptr<APF> pct (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<APF> (pidOf (id), name, linRange (0.0f, 100.0f, 0.1f), def,
                                      Attr().withLabel ("%"));
    }

    std::unique_ptr<APF> flt (const char* id, const juce::String& name,
                              juce::NormalisableRange<float> range, float def,
                              const juce::String& unit)
    {
        return std::make_unique<APF> (pidOf (id), name, range, def, Attr().withLabel (unit));
    }
}

// ----------------------------------------------------------------------------- choices

juce::StringArray carrierSourceChoices()   { return { "Sidechain", "Chord", "Self" }; }
juce::StringArray oscChoices()             { return { "Supersaw", "Saw", "Square", "Noise" }; }

juce::StringArray noteChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray chordTypeChoices()
{
    return { "Maj", "Min", "Maj7", "Min9", "Sus2", "Add9", "Power", "Custom" };
}

juce::StringArray fftSizeChoices()         { return { "512", "1024", "2048", "4096" }; }
juce::StringArray engineModeChoices()      { return { "Vocode", "Mag Morph", "Phase Morph", "Cross" }; }

juce::StringArray fractalPatternChoices()
{
    return { "Cantor", "Golden", "Thue-Morse", "Sierpinski", "L-System" };
}

juce::StringArray syncDivChoices()
{
    return { "1/1", "1/2D", "1/2", "1/2T", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T",
             "1/16D", "1/16", "1/16T", "1/32D", "1/32", "1/32T", "1/64" };
}

juce::StringArray delayModeChoices()       { return { "Stereo", "Ping-Pong", "Dual" }; }
juce::StringArray placementChoices()       { return { "Pre", "Post" }; }
juce::StringArray satTypeChoices()         { return { "Soft Clip", "Tube", "Wavefold" }; }

int fftSizeForIndex (int index)
{
    switch (index)
    {
        case 0:  return 512;
        case 1:  return 1024;
        case 2:  return 2048;
        case 3:  return 4096;
        default: return 2048;
    }
}

double syncDivToQuarterNotes (int index)
{
    // Expressed in quarter notes. Dotted = x1.5, triplet = x2/3.
    switch (index)
    {
        case 0:  return 4.0;              // 1/1
        case 1:  return 2.0 * 1.5;        // 1/2 dotted
        case 2:  return 2.0;              // 1/2
        case 3:  return 2.0 * 2.0 / 3.0;  // 1/2 triplet
        case 4:  return 1.0 * 1.5;        // 1/4 dotted
        case 5:  return 1.0;              // 1/4
        case 6:  return 1.0 * 2.0 / 3.0;  // 1/4 triplet
        case 7:  return 0.5 * 1.5;        // 1/8 dotted
        case 8:  return 0.5;              // 1/8
        case 9:  return 0.5 * 2.0 / 3.0;  // 1/8 triplet
        case 10: return 0.25 * 1.5;       // 1/16 dotted
        case 11: return 0.25;             // 1/16
        case 12: return 0.25 * 2.0 / 3.0; // 1/16 triplet
        case 13: return 0.125 * 1.5;      // 1/32 dotted
        case 14: return 0.125;            // 1/32
        case 15: return 0.125 * 2.0 / 3.0;// 1/32 triplet
        case 16: return 0.0625;           // 1/64
        default: return 0.25;
    }
}

// ----------------------------------------------------------------------------- layout

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- CARRIER -----------------------------------------------------------------
    auto carrier = std::make_unique<Group> ("carrier", "Carrier", "|");

    carrier->addChild (
        std::make_unique<APC> (pidOf (pid::carrierSource), "Carrier Source",
                               carrierSourceChoices(), 1),
        std::make_unique<APC> (pidOf (pid::chordOsc), "Chord Osc", oscChoices(), 0),
        std::make_unique<APC> (pidOf (pid::chordRoot), "Chord Root", noteChoices(), 0),
        std::make_unique<APC> (pidOf (pid::chordType), "Chord Type", chordTypeChoices(), 1),
        std::make_unique<API>  (pidOf (pid::chordOctave), "Chord Octave", -2, 2, 0),
        pct (pid::chordSpread, "Chord Spread", 35.0f),
        flt (pid::chordDetune, "Chord Detune", linRange (0.0f, 50.0f, 0.1f), 12.0f, "cents"),
        flt (pid::chordLevel,  "Chord Level",  linRange (-60.0f, 12.0f, 0.1f), 0.0f, "dB"),
        std::make_unique<APB> (pidOf (pid::chordMidiOvr), "MIDI Override", true));

    layout.add (std::move (carrier));

    // ---- ENGINE ------------------------------------------------------------------
    auto engine = std::make_unique<Group> ("engine", "Engine", "|");

    engine->addChild (
        std::make_unique<APC> (pidOf (pid::engFftSize), "FFT Size", fftSizeChoices(), 2),
        std::make_unique<APC> (pidOf (pid::engMode), "Engine Mode", engineModeChoices(), 0),
        pct (pid::engMorph, "Morph", 100.0f),
        flt (pid::engFormant, "Formant Shift", linRange (-24.0f, 24.0f, 0.01f), 0.0f, "st"),
        flt (pid::engTilt,    "Spectral Tilt", linRange (-6.0f, 6.0f, 0.01f), 0.0f, "dB/oct"),
        pct (pid::engEnvRes, "Envelope Resolution", 50.0f),
        std::make_unique<APB> (pidOf (pid::engEnvCepstral), "Cepstral Envelope", false),
        pct (pid::engPhaseLock, "Phase Lock", 50.0f),
        flt (pid::engSens, "Sensitivity", linRange (-80.0f, 0.0f, 0.1f), -60.0f, "dB"),
        std::make_unique<APB> (pidOf (pid::engFreeze), "Freeze", false),
        std::make_unique<APB> (pidOf (pid::engFlip), "Flip", false),
        flt (pid::engFreqShift, "Freq Shift", linRange (-500.0f, 500.0f, 0.1f), 0.0f, "Hz"));

    layout.add (std::move (engine));

    // ---- FRACTAL -----------------------------------------------------------------
    auto fractal = std::make_unique<Group> ("fractal", "Fractal", "|");

    fractal->addChild (
        std::make_unique<APC> (pidOf (pid::fracPattern), "Fractal Pattern",
                               fractalPatternChoices(), 0),
        std::make_unique<API> (pidOf (pid::fracDepth), "Fractal Depth", 1, 7, 3),
        std::make_unique<API> (pidOf (pid::fracSeed), "Fractal Seed", 0, 9999, 1),
        pct (pid::fracRatio, "Split Ratio", 50.0f),
        flt (pid::fracAsym, "Split Asymmetry", linRange (-100.0f, 100.0f, 0.1f), 0.0f, "%"),
        flt (pid::fracLoHz, "Fractal Low Bound",  freqRange (20.0f, 20000.0f, 200.0f), 60.0f, "Hz"),
        flt (pid::fracHiHz, "Fractal High Bound", freqRange (20.0f, 20000.0f, 2000.0f), 12000.0f, "Hz"),
        std::make_unique<APB> (pidOf (pid::fracInvert), "Fractal Invert", false),
        pct (pid::fracShatter, "Shatter", 0.0f),
        std::make_unique<APB> (pidOf (pid::fracSync), "Fractal Sync", true),
        std::make_unique<APC> (pidOf (pid::fracDiv), "Fractal Rate (Sync)", syncDivChoices(), 8),
        flt (pid::fracRateHz, "Fractal Rate (Free)", freqRange (0.01f, 50.0f, 2.0f), 2.0f, "Hz"),
        pct (pid::fracGate, "Rhythm Gate", 0.0f),
        pct (pid::fracGrit, "Grit", 0.0f),
        flt (pid::fracSmooth, "Edge Smoothing", linRange (0.1f, 50.0f, 0.1f), 6.0f, "ms"),
        pct (pid::fracMix, "Fractal Mix", 100.0f));

    layout.add (std::move (fractal));

    // ---- SPECTRAL DELAY ----------------------------------------------------------
    auto sdly = std::make_unique<Group> ("spectralDelay", "Spectral Delay", "|");

    sdly->addChild (
        std::make_unique<APB> (pidOf (pid::sdlyOn), "Spectral Delay On", false),
        flt (pid::sdlyTimeMs, "Spectral Delay Time", freqRange (1.0f, 3000.0f, 250.0f), 250.0f, "ms"),
        pct (pid::sdlySpread, "Spectral Delay Spread", 50.0f),
        pct (pid::sdlyFb,     "Spectral Delay Feedback", 40.0f),
        pct (pid::sdlyDamp,   "Spectral Delay Damping", 30.0f),
        pct (pid::sdlyMix,    "Spectral Delay Mix", 50.0f));

    layout.add (std::move (sdly));

    // ---- STEREO DELAY ------------------------------------------------------------
    auto dly = std::make_unique<Group> ("stereoDelay", "Stereo Delay", "|");

    dly->addChild (
        std::make_unique<APB> (pidOf (pid::dlyOn), "Stereo Delay On", false),
        std::make_unique<APC> (pidOf (pid::dlyMode), "Delay Mode", delayModeChoices(), 1),
        std::make_unique<APC> (pidOf (pid::dlyPlace), "Delay Placement", placementChoices(), 1),
        std::make_unique<APB> (pidOf (pid::dlySync), "Delay Sync", true),
        std::make_unique<APC> (pidOf (pid::dlyDivL), "Delay Div L", syncDivChoices(), 8),
        std::make_unique<APC> (pidOf (pid::dlyDivR), "Delay Div R", syncDivChoices(), 10),
        flt (pid::dlyMsL, "Delay Time L", freqRange (1.0f, 4000.0f, 400.0f), 375.0f, "ms"),
        flt (pid::dlyMsR, "Delay Time R", freqRange (1.0f, 4000.0f, 400.0f), 500.0f, "ms"),
        pct (pid::dlyFb, "Delay Feedback", 45.0f),
        flt (pid::dlyHp, "Delay Loop HP", freqRange (20.0f, 2000.0f, 200.0f), 120.0f, "Hz"),
        flt (pid::dlyLp, "Delay Loop LP", freqRange (200.0f, 20000.0f, 4000.0f), 8000.0f, "Hz"),
        pct (pid::dlySat, "Delay Saturation", 20.0f),
        flt (pid::dlyModRate, "Delay Mod Rate", freqRange (0.01f, 10.0f, 0.5f), 0.4f, "Hz"),
        pct (pid::dlyModDepth, "Delay Mod Depth", 15.0f),
        pct (pid::dlyDiffuse,  "Delay Diffusion", 25.0f),
        pct (pid::dlyDuck,     "Delay Ducking", 0.0f),
        std::make_unique<APB> (pidOf (pid::dlyFreeze), "Delay Freeze", false),
        pct (pid::dlyWidth, "Delay Width", 100.0f),
        pct (pid::dlyMix,   "Delay Mix", 35.0f));

    layout.add (std::move (dly));

    // ---- OUTPUT ------------------------------------------------------------------
    auto out = std::make_unique<Group> ("output", "Output", "|");

    out->addChild (
        std::make_unique<APC> (pidOf (pid::colSatType), "Saturation Type", satTypeChoices(), 0),
        flt (pid::colDrive, "Drive", linRange (0.0f, 36.0f, 0.1f), 0.0f, "dB"),
        flt (pid::colWidth, "Stereo Width", linRange (0.0f, 200.0f, 0.1f), 100.0f, "%"),
        flt (pid::colLoCut, "Low Cut",  freqRange (20.0f, 1000.0f, 100.0f), 20.0f, "Hz"),
        flt (pid::colHiCut, "High Cut", freqRange (1000.0f, 20000.0f, 8000.0f), 20000.0f, "Hz"),
        pct (pid::outMix, "Dry/Wet Mix", 100.0f),
        flt (pid::outGain, "Output Gain", linRange (-24.0f, 12.0f, 0.1f), 0.0f, "dB"),
        std::make_unique<APB> (pidOf (pid::outLimiter), "Safety Limiter", true));

    layout.add (std::move (out));

    return layout;
}

} // namespace vbd
