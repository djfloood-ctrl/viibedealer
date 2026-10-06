#include "Randomizer.h"

#include "../Params.h"

namespace vbd::state
{

const char* sectionName (Section s) noexcept
{
    switch (s)
    {
        case Section::carrier: return "CARRIER";
        case Section::engine:  return "ENGINE";
        case Section::fractal: return "FRACTAL";
        case Section::delays:  return "DELAYS";
        case Section::output:  return "OUTPUT";
        case Section::count:   break;
    }

    return "";
}

const char* sectionShortName (Section s) noexcept
{
    switch (s)
    {
        case Section::carrier: return "CAR";
        case Section::engine:  return "ENG";
        case Section::fractal: return "FRC";
        case Section::delays:  return "DLY";
        case Section::output:  return "OUT";
        case Section::count:   break;
    }

    return "";
}

Section sectionFor (const juce::String& paramId) noexcept
{
    if (paramId.startsWith ("carrier.") || paramId.startsWith ("chord."))
        return Section::carrier;

    if (paramId.startsWith ("eng."))
        return Section::engine;

    if (paramId.startsWith ("frac."))
        return Section::fractal;

    if (paramId.startsWith ("sdly.") || paramId.startsWith ("dly."))
        return Section::delays;

    return Section::output;
}

namespace
{
    /** Parameters Randomize must never touch.

        Structural or destructive settings: FFT size changes latency and would make the
        host re-sync; the module switches would turn delays on unasked; Mix at a random
        value can bury the signal entirely; Limiter off invites a nasty surprise. */
    bool isExcluded (const juce::String& id) noexcept
    {
        static const char* excluded[] = {
            pid::engFftSize,
            pid::sdlyOn, pid::dlyOn, pid::dlyFreeze, pid::engFreeze,
            pid::dlyPlace, pid::dlySync, pid::fracSync,
            pid::outMix, pid::outGain, pid::outLimiter,
            pid::chordMidiOvr, pid::chordLevel
        };

        for (const auto* e : excluded)
            if (id == e)
                return true;

        return false;
    }

    /** A plausible window around the current value rather than the full range. */
    float rollValue (const juce::RangedAudioParameter& param, juce::Random& rng)
    {
        const auto current = param.getValue();

        // Discrete parameters: pick any option, since a "nearby" choice is meaningless.
        if (param.getNumSteps() > 0 && param.getNumSteps() < 32)
            return rng.nextFloat();

        // Continuous: wander up to 45% of the range from where it is, so a randomise
        // feels like a variation on the current patch rather than a reset.
        const auto spread = 0.45f;
        const auto delta = (rng.nextFloat() * 2.0f - 1.0f) * spread;

        return juce::jlimit (0.0f, 1.0f, current + delta);
    }
}

void randomize (juce::AudioProcessorValueTreeState& apvts,
                juce::uint32 lockMask,
                juce::Random& rng)
{
    for (auto* param : apvts.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);

        if (ranged == nullptr)
            continue;

        const auto& id = ranged->paramID;

        if (isExcluded (id))
            continue;

        const auto section = sectionFor (id);
        const auto bit = 1u << static_cast<int> (section);

        if ((lockMask & bit) != 0)
            continue;

        ranged->setValueNotifyingHost (rollValue (*ranged, rng));
    }
}

} // namespace vbd::state
