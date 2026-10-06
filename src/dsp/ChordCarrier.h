#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace vbd::dsp
{

enum class CarrierOsc { supersaw = 0, saw, square, noise };

enum class ChordType
{
    maj = 0, min, maj7, min9, sus2, add9, power, custom
};

/**
    Polyphonic oscillator stack used as the internal carrier.

    This is the heart of the color-bass trick: a held chord that the spectral engine
    imprints onto the incoming bass. Notes come either from the Root + Type controls or,
    when MIDI Override is on and keys are held, from the MIDI input.

    Anti-aliasing is polyBLEP on saw and square. Naive ramps would fold high harmonics
    back down into the bass register, where the whole point is a clean harmonic stack for
    the vocoder to work with.
*/
class ChordCarrier
{
public:
    static constexpr int maxNotes  = 8;
    static constexpr int maxUnison = 7;
    static constexpr int maxVoices = maxNotes * maxUnison;

    struct Params
    {
        CarrierOsc osc = CarrierOsc::supersaw;
        int   rootNote   = 0;    // 0..11, C..B
        ChordType chord  = ChordType::min;
        int   octave     = 0;    // -2..+2
        float spread     = 0.35f;
        float detuneCents = 12.0f;
        float level      = 1.0f; // linear
        bool  midiOverride = true;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParams (const Params& p) noexcept { params = p; }

    /** Consumes note on/off from the host. Safe to call every block; does not allocate. */
    void handleMidi (const juce::MidiBuffer& midi);

    /** Renders into the given stereo buffer, overwriting it. */
    void render (juce::AudioBuffer<float>& buffer, int numSamples);

    /** True when MIDI notes are currently driving the chord. */
    bool isMidiDriven() const noexcept { return params.midiOverride && numHeldNotes > 0; }

private:
    struct Voice
    {
        double phase = 0.0;
        double increment = 0.0;
        float gainL = 0.0f;
        float gainR = 0.0f;
        bool active = false;
    };

    void rebuildVoices();
    static juce::Array<int> intervalsFor (ChordType type);

    Params params;
    Params lastParams;
    bool voicesDirty = true;

    std::array<Voice, maxVoices> voices {};
    int numActiveVoices = 0;

    std::array<int, maxNotes> heldNotes {};
    int numHeldNotes = 0;

    double sampleRate = 44100.0;
    juce::Random rng { 20260106 };
    float voiceNormalise = 1.0f;
};

} // namespace vbd::dsp
