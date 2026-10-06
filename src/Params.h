#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter identifiers and the APVTS layout.
//
// IDs are stable strings and must never change once shipped -- host automation and saved
// sessions key off them. Display names and ranges can change; IDs cannot.

namespace vbd::pid
{
    // ---- CARRIER
    inline constexpr auto carrierSource   = "carrier.source";
    inline constexpr auto chordOsc        = "chord.osc";
    inline constexpr auto chordRoot       = "chord.root";
    inline constexpr auto chordType       = "chord.type";
    inline constexpr auto chordOctave     = "chord.octave";
    inline constexpr auto chordSpread     = "chord.spread";
    inline constexpr auto chordDetune     = "chord.detune";
    inline constexpr auto chordLevel      = "chord.level";
    inline constexpr auto chordMidiOvr    = "chord.midiOverride";

    // ---- ENGINE
    inline constexpr auto engFftSize      = "eng.fftSize";
    inline constexpr auto engMode         = "eng.mode";
    inline constexpr auto engMorph        = "eng.morph";
    inline constexpr auto engFormant      = "eng.formant";
    inline constexpr auto engTilt         = "eng.tilt";
    inline constexpr auto engEnvRes       = "eng.envRes";
    inline constexpr auto engEnvCepstral  = "eng.envCepstral";
    inline constexpr auto engPhaseLock    = "eng.phaseLock";
    inline constexpr auto engSens         = "eng.sens";
    inline constexpr auto engFreeze       = "eng.freeze";
    inline constexpr auto engFlip         = "eng.flip";
    inline constexpr auto engFreqShift    = "eng.freqShift";

    // ---- FRACTAL
    inline constexpr auto fracPattern     = "frac.pattern";
    inline constexpr auto fracDepth       = "frac.depth";
    inline constexpr auto fracSeed        = "frac.seed";
    inline constexpr auto fracRatio       = "frac.ratio";
    inline constexpr auto fracAsym        = "frac.asym";
    inline constexpr auto fracLoHz        = "frac.loHz";
    inline constexpr auto fracHiHz        = "frac.hiHz";
    inline constexpr auto fracInvert      = "frac.invert";
    inline constexpr auto fracShatter     = "frac.shatter";
    inline constexpr auto fracSync        = "frac.sync";
    inline constexpr auto fracDiv         = "frac.div";
    inline constexpr auto fracRateHz      = "frac.rateHz";
    inline constexpr auto fracGate        = "frac.gate";
    inline constexpr auto fracGrit        = "frac.grit";
    inline constexpr auto fracSmooth      = "frac.smooth";
    inline constexpr auto fracMix         = "frac.mix";

    // ---- SPECTRAL DELAY
    inline constexpr auto sdlyOn          = "sdly.on";
    inline constexpr auto sdlyTimeMs      = "sdly.timeMs";
    inline constexpr auto sdlySpread      = "sdly.spread";
    inline constexpr auto sdlyFb          = "sdly.fb";
    inline constexpr auto sdlyDamp        = "sdly.damp";
    inline constexpr auto sdlyMix         = "sdly.mix";

    // ---- STEREO DELAY
    inline constexpr auto dlyOn           = "dly.on";
    inline constexpr auto dlyMode         = "dly.mode";
    inline constexpr auto dlyPlace        = "dly.place";
    inline constexpr auto dlySync         = "dly.sync";
    inline constexpr auto dlyDivL         = "dly.divL";
    inline constexpr auto dlyDivR         = "dly.divR";
    inline constexpr auto dlyMsL          = "dly.msL";
    inline constexpr auto dlyMsR          = "dly.msR";
    inline constexpr auto dlyFb           = "dly.fb";
    inline constexpr auto dlyHp           = "dly.hp";
    inline constexpr auto dlyLp           = "dly.lp";
    inline constexpr auto dlySat          = "dly.sat";
    inline constexpr auto dlyModRate      = "dly.modRate";
    inline constexpr auto dlyModDepth     = "dly.modDepth";
    inline constexpr auto dlyDiffuse      = "dly.diffuse";
    inline constexpr auto dlyDuck         = "dly.duck";
    inline constexpr auto dlyFreeze       = "dly.freeze";
    inline constexpr auto dlyWidth        = "dly.width";
    inline constexpr auto dlyMix          = "dly.mix";

    // ---- OUTPUT
    inline constexpr auto colSatType      = "col.satType";
    inline constexpr auto colDrive        = "col.drive";
    inline constexpr auto colWidth        = "col.width";
    inline constexpr auto colLoCut        = "col.loCut";
    inline constexpr auto colHiCut        = "col.hiCut";
    inline constexpr auto outMix          = "out.mix";
    inline constexpr auto outGain         = "out.gain";
    inline constexpr auto outLimiter      = "out.limiter";
}

namespace vbd
{
    // Choice lists are shared with the GUI, so they live next to the layout that uses them.
    juce::StringArray carrierSourceChoices();
    juce::StringArray oscChoices();
    juce::StringArray noteChoices();
    juce::StringArray chordTypeChoices();
    juce::StringArray fftSizeChoices();
    juce::StringArray engineModeChoices();
    juce::StringArray fractalPatternChoices();
    juce::StringArray syncDivChoices();
    juce::StringArray delayModeChoices();
    juce::StringArray placementChoices();
    juce::StringArray satTypeChoices();

    /** FFT size in samples for an fftSize choice index. */
    int fftSizeForIndex (int index);

    /** Multiplier on a quarter note for a syncDiv choice index (0.25 = 1/16, 4.0 = 1/1). */
    double syncDivToQuarterNotes (int index);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
