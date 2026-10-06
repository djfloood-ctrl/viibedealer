#include "ChordCarrier.h"
#include "Utils.h"

namespace vbd::dsp
{

namespace
{
    /** polyBLEP residual used to round off a waveform's discontinuity. */
    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t = t / dt - 1.0;
            return static_cast<float> (-t * t);
        }

        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt + 1.0;
            return static_cast<float> (t * t);
        }

        return 0.0f;
    }

    inline float midiToHz (float note) noexcept
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }
}

juce::Array<int> ChordCarrier::intervalsFor (ChordType type)
{
    switch (type)
    {
        case ChordType::maj:   return { 0, 4, 7 };
        case ChordType::min:   return { 0, 3, 7 };
        case ChordType::maj7:  return { 0, 4, 7, 11 };
        case ChordType::min9:  return { 0, 3, 7, 10, 14 };
        case ChordType::sus2:  return { 0, 2, 7 };
        case ChordType::add9:  return { 0, 4, 7, 14 };
        case ChordType::power: return { 0, 7, 12 };
        // A user-editable interval set lands with the preset system in Phase F; until then
        // Custom is a wide minor 11 voicing, which is useful in its own right.
        case ChordType::custom: return { 0, 3, 7, 10, 14, 17 };
    }

    return { 0, 3, 7 };
}

void ChordCarrier::prepare (double sr, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    sampleRate = sr;
    reset();
}

void ChordCarrier::reset()
{
    for (auto& v : voices)
    {
        v.phase = 0.0;
        v.active = false;
    }

    numActiveVoices = 0;
    numHeldNotes = 0;
    voicesDirty = true;
}

void ChordCarrier::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            const auto note = msg.getNoteNumber();

            bool alreadyHeld = false;

            for (int i = 0; i < numHeldNotes; ++i)
                if (heldNotes[static_cast<std::size_t> (i)] == note)
                    alreadyHeld = true;

            if (! alreadyHeld && numHeldNotes < maxNotes)
            {
                heldNotes[static_cast<std::size_t> (numHeldNotes++)] = note;
                voicesDirty = true;
            }
        }
        else if (msg.isNoteOff())
        {
            const auto note = msg.getNoteNumber();

            for (int i = 0; i < numHeldNotes; ++i)
            {
                if (heldNotes[static_cast<std::size_t> (i)] == note)
                {
                    for (int j = i; j < numHeldNotes - 1; ++j)
                        heldNotes[static_cast<std::size_t> (j)] =
                            heldNotes[static_cast<std::size_t> (j + 1)];

                    --numHeldNotes;
                    voicesDirty = true;
                    break;
                }
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            numHeldNotes = 0;
            voicesDirty = true;
        }
    }
}

void ChordCarrier::rebuildVoices()
{
    // Note set: MIDI when it is driving, otherwise Root + Chord Type.
    std::array<float, maxNotes> pitches {};
    int numPitches = 0;

    if (isMidiDriven())
    {
        for (int i = 0; i < numHeldNotes; ++i)
            pitches[static_cast<std::size_t> (numPitches++)] =
                static_cast<float> (heldNotes[static_cast<std::size_t> (i)]);
    }
    else
    {
        const auto intervals = intervalsFor (params.chord);

        // Root at C2 (MIDI 36) so the default sits in bass territory.
        const auto base = 36.0f + static_cast<float> (params.rootNote)
                        + 12.0f * static_cast<float> (params.octave);

        // Spread widens the voicing rather than just transposing it.
        const auto stretch = 1.0f + params.spread;

        for (int i = 0; i < intervals.size() && numPitches < maxNotes; ++i)
            pitches[static_cast<std::size_t> (numPitches++)] =
                base + static_cast<float> (intervals[i]) * stretch;
    }

    const auto unison = (params.osc == CarrierOsc::supersaw) ? maxUnison : 1;

    numActiveVoices = 0;

    for (int n = 0; n < numPitches; ++n)
    {
        // Pan successive chord notes outward with spread.
        const auto panPos = numPitches > 1
            ? (static_cast<float> (n) / static_cast<float> (numPitches - 1) - 0.5f) * 2.0f
            : 0.0f;

        for (int u = 0; u < unison; ++u)
        {
            if (numActiveVoices >= maxVoices)
                break;

            auto& v = voices[static_cast<std::size_t> (numActiveVoices)];

            // Unison detune fans out symmetrically around the note.
            const auto detuneSpan = unison > 1
                ? (static_cast<float> (u) / static_cast<float> (unison - 1) - 0.5f) * 2.0f
                : 0.0f;

            const auto cents = detuneSpan * params.detuneCents;
            const auto hz = midiToHz (pitches[static_cast<std::size_t> (n)] + cents / 100.0f);

            v.increment = static_cast<double> (hz) / sampleRate;

            // Unison voices also fan across the stereo field, which is what gives a
            // supersaw its width without any added processing.
            const auto pan = juce::jlimit (-1.0f, 1.0f,
                                           panPos * params.spread + detuneSpan * params.spread * 0.5f);

            // Constant-power pan.
            const auto angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            v.gainL = std::cos (angle);
            v.gainR = std::sin (angle);

            if (! v.active)
            {
                // Randomised start phase: aligning every voice at zero produces a loud
                // click and an unnaturally coherent attack.
                v.phase = rng.nextDouble();
                v.active = true;
            }

            ++numActiveVoices;
        }
    }

    for (int i = numActiveVoices; i < maxVoices; ++i)
        voices[static_cast<std::size_t> (i)].active = false;

    voiceNormalise = numActiveVoices > 0
        ? 1.0f / std::sqrt (static_cast<float> (numActiveVoices))
        : 0.0f;
}

void ChordCarrier::render (juce::AudioBuffer<float>& buffer, int numSamples)
{
    buffer.clear (0, numSamples);

    const auto paramsChanged =
        params.osc != lastParams.osc
        || params.rootNote != lastParams.rootNote
        || params.chord != lastParams.chord
        || params.octave != lastParams.octave
        || ! juce::approximatelyEqual (params.spread, lastParams.spread)
        || ! juce::approximatelyEqual (params.detuneCents, lastParams.detuneCents)
        || params.midiOverride != lastParams.midiOverride;

    if (voicesDirty || paramsChanged)
    {
        rebuildVoices();
        lastParams = params;
        voicesDirty = false;
    }

    if (numActiveVoices == 0 || params.level <= 0.0f)
        return;

    const auto numChannels = buffer.getNumChannels();
    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const auto gain = params.level * voiceNormalise;

    if (params.osc == CarrierOsc::noise)
    {
        // Noise needs no oscillator bank -- it is the one carrier with a flat spectrum,
        // which makes it the right choice for pure formant/vocoder textures.
        for (int i = 0; i < numSamples; ++i)
        {
            const auto l = (rng.nextFloat() * 2.0f - 1.0f) * params.level;
            const auto r = (rng.nextFloat() * 2.0f - 1.0f) * params.level;

            left[i] = l;

            if (right != nullptr)
                right[i] = r;
        }

        return;
    }

    for (int vi = 0; vi < numActiveVoices; ++vi)
    {
        auto& v = voices[static_cast<std::size_t> (vi)];

        if (! v.active || v.increment <= 0.0 || v.increment >= 0.5)
            continue;

        auto phase = v.phase;
        const auto inc = v.increment;
        const auto gl = v.gainL * gain;
        const auto gr = v.gainR * gain;

        for (int i = 0; i < numSamples; ++i)
        {
            float sample = 0.0f;

            if (params.osc == CarrierOsc::square)
            {
                sample = phase < 0.5 ? 1.0f : -1.0f;
                sample += polyBlep (phase, inc);

                auto shifted = phase + 0.5;

                if (shifted >= 1.0)
                    shifted -= 1.0;

                sample -= polyBlep (shifted, inc);
            }
            else
            {
                // saw and supersaw share the same single-saw core.
                sample = static_cast<float> (2.0 * phase - 1.0);
                sample -= polyBlep (phase, inc);
            }

            left[i] += sample * gl;

            if (right != nullptr)
                right[i] += sample * gr;

            phase += inc;

            if (phase >= 1.0)
                phase -= 1.0;
        }

        v.phase = phase;
    }

    for (int ch = 0; ch < numChannels; ++ch)
        scrubBlock (buffer.getWritePointer (ch), numSamples);
}

} // namespace vbd::dsp
