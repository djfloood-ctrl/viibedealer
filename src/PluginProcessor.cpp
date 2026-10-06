#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <optional>

namespace vbd
{

juce::AudioProcessor::BusesProperties VbdProcessor::makeBuses()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        // Declared but disabled by default: the plugin must be fully functional with
        // nothing patched into it, and Standalone has no sidechain at all.
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true);
}

namespace
{
    const juce::Identifier kUiScene            { "scene" };
    const juce::Identifier kUiCharacterVisible { "characterVisible" };
    const juce::Identifier kUiPerfMode         { "perfMode" };
    const juce::Identifier kUiTailMacro        { "tailMacroTarget" };
    const juce::Identifier kUiSizeScale        { "sizeScale" };
    const juce::Identifier kUiLockMask         { "lockMask" };
    const juce::Identifier kStateVersion       { "stateVersion" };

    template <typename Enum>
    Enum enumFromParam (const std::atomic<float>* raw, Enum fallback) noexcept
    {
        if (raw == nullptr)
            return fallback;

        return static_cast<Enum> (static_cast<int> (raw->load (std::memory_order_relaxed)));
    }

    float paramValue (const std::atomic<float>* raw, float fallback = 0.0f) noexcept
    {
        return raw != nullptr ? raw->load (std::memory_order_relaxed) : fallback;
    }
}

VbdProcessor::VbdProcessor()
    : juce::AudioProcessor (makeBuses()),
      apvts (*this, nullptr, identity::apvtsTag, createParameterLayout())
{
    uiStateTree.setProperty (kUiScene, 0, nullptr);
    uiStateTree.setProperty (kUiCharacterVisible, true, nullptr);
    uiStateTree.setProperty (kUiPerfMode, false, nullptr);
    uiStateTree.setProperty (kUiTailMacro, 0, nullptr);
    uiStateTree.setProperty (kUiSizeScale, 1.0, nullptr);
    uiStateTree.setProperty (kUiLockMask, 0, nullptr);

    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    p.carrierSource = raw (pid::carrierSource);
    p.chordOsc      = raw (pid::chordOsc);
    p.chordRoot     = raw (pid::chordRoot);
    p.chordType     = raw (pid::chordType);
    p.chordOctave   = raw (pid::chordOctave);
    p.chordSpread   = raw (pid::chordSpread);
    p.chordDetune   = raw (pid::chordDetune);
    p.chordLevel    = raw (pid::chordLevel);
    p.chordMidiOvr  = raw (pid::chordMidiOvr);

    p.fftSize   = raw (pid::engFftSize);
    p.mode      = raw (pid::engMode);
    p.morph     = raw (pid::engMorph);
    p.formant   = raw (pid::engFormant);
    p.tilt      = raw (pid::engTilt);
    p.envRes    = raw (pid::engEnvRes);
    p.cepstral  = raw (pid::engEnvCepstral);
    p.phaseLock = raw (pid::engPhaseLock);
    p.sens      = raw (pid::engSens);
    p.freeze    = raw (pid::engFreeze);
    p.flip      = raw (pid::engFlip);
    p.freqShift = raw (pid::engFreqShift);

    p.fracPattern = raw (pid::fracPattern);
    p.fracDepth   = raw (pid::fracDepth);
    p.fracSeed    = raw (pid::fracSeed);
    p.fracRatio   = raw (pid::fracRatio);
    p.fracAsym    = raw (pid::fracAsym);
    p.fracLoHz    = raw (pid::fracLoHz);
    p.fracHiHz    = raw (pid::fracHiHz);
    p.fracInvert  = raw (pid::fracInvert);
    p.fracShatter = raw (pid::fracShatter);
    p.fracSync    = raw (pid::fracSync);
    p.fracDiv     = raw (pid::fracDiv);
    p.fracRateHz  = raw (pid::fracRateHz);
    p.fracGate    = raw (pid::fracGate);
    p.fracGrit    = raw (pid::fracGrit);
    p.fracSmooth  = raw (pid::fracSmooth);
    p.fracMix     = raw (pid::fracMix);

    p.sdlyOn     = raw (pid::sdlyOn);
    p.sdlyTimeMs = raw (pid::sdlyTimeMs);
    p.sdlySpread = raw (pid::sdlySpread);
    p.sdlyFb     = raw (pid::sdlyFb);
    p.sdlyDamp   = raw (pid::sdlyDamp);
    p.sdlyMix    = raw (pid::sdlyMix);

    p.dlyOn        = raw (pid::dlyOn);
    p.dlyMode      = raw (pid::dlyMode);
    p.dlyPlace     = raw (pid::dlyPlace);
    p.dlySync      = raw (pid::dlySync);
    p.dlyDivL      = raw (pid::dlyDivL);
    p.dlyDivR      = raw (pid::dlyDivR);
    p.dlyMsL       = raw (pid::dlyMsL);
    p.dlyMsR       = raw (pid::dlyMsR);
    p.dlyFb        = raw (pid::dlyFb);
    p.dlyHp        = raw (pid::dlyHp);
    p.dlyLp        = raw (pid::dlyLp);
    p.dlySat       = raw (pid::dlySat);
    p.dlyModRate   = raw (pid::dlyModRate);
    p.dlyModDepth  = raw (pid::dlyModDepth);
    p.dlyDiffuse   = raw (pid::dlyDiffuse);
    p.dlyDuck      = raw (pid::dlyDuck);
    p.dlyFreeze    = raw (pid::dlyFreeze);
    p.dlyWidth     = raw (pid::dlyWidth);
    p.dlyMix       = raw (pid::dlyMix);

    p.satType  = raw (pid::colSatType);
    p.drive    = raw (pid::colDrive);
    p.colWidth = raw (pid::colWidth);
    p.loCut    = raw (pid::colLoCut);
    p.hiCut    = raw (pid::colHiCut);
    p.limiter  = raw (pid::outLimiter);

    p.mix     = raw (pid::outMix);
    p.outGain = raw (pid::outGain);

    // Report the default FFT size's latency straight away. Some hosts query latency after
    // instantiation but before prepareToPlay and cache the answer, so reporting 0 there
    // and correcting it later would leave the plugin permanently misaligned in them.
    const auto initialFft = fftSizeForIndex (static_cast<int> (paramValue (p.fftSize, 2.0f)));
    activeFftSize.store (initialFft, std::memory_order_relaxed);
    desiredLatency.store (initialFft, std::memory_order_relaxed);
    setLatencySamples (initialFft);

    // The host is told about latency *changes* from the message thread, never from
    // processBlock -- see timerCallback.
    startTimer (40);
}

VbdProcessor::~VbdProcessor()
{
    stopTimer();
}

// ----------------------------------------------------------------------------- lifecycle

void VbdProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize  = samplesPerBlock;

    const auto numChannels = juce::jmax (2, getTotalNumInputChannels());

    stft.prepare (numChannels, samplesPerBlock);
    stft.setFftSize (fftSizeForIndex (static_cast<int> (paramValue (p.fftSize, 2.0f))));

    chain.prepare (numChannels, dsp::StftEngine::maxFftSize, sampleRate);
    chain.setFftSize (stft.getFftSize(), stft.currentFft(), stft.magnitudeScale(), sampleRate);
    chain.setHopSize (stft.getHopSize());
    chain.setVisualBridge (&visualBridge);

    carrier.prepare (sampleRate, samplesPerBlock);
    stereoDelay.prepare (numChannels, sampleRate, samplesPerBlock);
    colorStage.prepare (numChannels, sampleRate, samplesPerBlock);

    modBuffer.setSize (numChannels, samplesPerBlock, false, false, true);
    carBuffer.setSize (numChannels, samplesPerBlock, false, false, true);
    wetBuffer.setSize (numChannels, samplesPerBlock, false, false, true);
    dryBuffer.setSize (numChannels, samplesPerBlock, false, false, true);

    modBuffer.clear();
    carBuffer.clear();
    wetBuffer.clear();
    dryBuffer.clear();

    dryDelays.resize (static_cast<std::size_t> (numChannels));

    for (auto& d : dryDelays)
    {
        d.prepare (dsp::StftEngine::maxFftSize, samplesPerBlock);
        d.setDelay (stft.getLatencySamples());
    }

    // 5 ms is long enough to hide the discontinuity of an FFT switch and short enough not
    // to be heard as a dip.
    switchFadeSamples = juce::jmax (1, static_cast<int> (sampleRate * 0.005));
    switchFadeCounter = 0;

    const auto smoothingSeconds = 0.02;
    mixSmoothed.reset (sampleRate, smoothingSeconds);
    gainSmoothed.reset (sampleRate, smoothingSeconds);
    carrierLevelSmoothed.reset (sampleRate, smoothingSeconds);

    mixSmoothed.setCurrentAndTargetValue (paramValue (p.mix, 100.0f) * 0.01f);
    gainSmoothed.setCurrentAndTargetValue (dsp::dbToGain (paramValue (p.outGain)));
    carrierLevelSmoothed.setCurrentAndTargetValue (dsp::dbToGain (paramValue (p.chordLevel)));

    activeFftSize.store (stft.getFftSize(), std::memory_order_relaxed);
    desiredLatency.store (stft.getLatencySamples(), std::memory_order_relaxed);

    // Safe here: prepareToPlay is not the audio thread, and hosts query latency after it.
    setLatencySamples (stft.getLatencySamples());
}

void VbdProcessor::releaseResources()
{
    stft.reset();
    chain.reset();
    carrier.reset();
    stereoDelay.reset();
    colorStage.reset();

    for (auto& d : dryDelays)
        d.reset();
}

double VbdProcessor::getTailLengthSeconds() const
{
    // Reported from the *current* settings, not the worst case the controls allow.
    //
    // Hosts append the tail to every offline render, and the theoretical maximum here is
    // about 35 s -- which would mean half a minute of silence after each bounce. So this
    // estimates the time for the active feedback to decay by 60 dB, and caps it: Freeze
    // is unbounded by design, and no sensible render waits for that.
    constexpr double maxReportedTail = 12.0;

    const auto frameTail = static_cast<double> (getActiveFftSize()) / preparedSampleRate;

    auto decayFor = [] (double timeSeconds, double feedback)
    {
        if (feedback <= 0.001)
            return timeSeconds;

        // Repeats to fall 60 dB, i.e. feedback^n = 0.001.
        const auto repeats = std::log (0.001) / std::log (juce::jlimit (0.001, 0.999, feedback));
        return timeSeconds * repeats;
    };

    double delayTail = 0.0;

    if (paramValue (p.dlyOn) > 0.5f)
    {
        const auto freeze = paramValue (p.dlyFreeze) > 0.5f;

        if (freeze)
        {
            delayTail = maxReportedTail;
        }
        else
        {
            const auto longestMs = juce::jmax (paramValue (p.dlyMsL, 375.0f),
                                               paramValue (p.dlyMsR, 500.0f));
            delayTail = decayFor (static_cast<double> (longestMs) * 0.001,
                                  static_cast<double> (paramValue (p.dlyFb, 45.0f)) * 0.01);
        }
    }

    double spectralTail = 0.0;

    if (paramValue (p.sdlyOn) > 0.5f)
    {
        const auto resolved = static_cast<double> (resolvedSdlyMs.load (std::memory_order_relaxed));
        const auto timeSeconds = resolved > 0.0 ? resolved * 0.001
                                                : static_cast<double> (paramValue (p.sdlyTimeMs, 250.0f)) * 0.001;

        spectralTail = decayFor (timeSeconds,
                                 static_cast<double> (paramValue (p.sdlyFb, 40.0f)) * 0.01);
    }

    return juce::jmin (maxReportedTail,
                       frameTail + juce::jmax (delayTail, spectralTail));
}

void VbdProcessor::timerCallback()
{
    const auto wanted = desiredLatency.load (std::memory_order_relaxed);

    if (wanted != getLatencySamples())
        setLatencySamples (wanted);
}

// ----------------------------------------------------------------------------- buses

bool VbdProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    // The sidechain may be absent, mono or stereo. Anything else we decline rather than
    // silently mis-interpret.
    if (layouts.inputBuses.size() > 1)
    {
        const auto side = layouts.getChannelSet (true, 1);

        if (! side.isDisabled()
            && side != juce::AudioChannelSet::mono()
            && side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

bool VbdProcessor::hasActiveSidechain() const noexcept
{
    if (getBusCount (true) < 2)
        return false;

    if (auto* bus = getBus (true, 1))
        return bus->isEnabled() && bus->getNumberOfChannels() > 0;

    return false;
}

// ----------------------------------------------------------------------------- parameters

void VbdProcessor::readParameters()
{
    dsp::SpectralEngine::Params ep;
    ep.mode         = enumFromParam (p.mode, dsp::EngineMode::vocode);
    ep.morph        = paramValue (p.morph, 100.0f) * 0.01f;
    ep.formantShift = paramValue (p.formant);
    ep.tilt         = paramValue (p.tilt);
    ep.envRes       = paramValue (p.envRes, 50.0f) * 0.01f;
    ep.cepstral     = paramValue (p.cepstral) > 0.5f;
    ep.phaseLock    = paramValue (p.phaseLock, 50.0f) * 0.01f;
    ep.gateDb       = paramValue (p.sens, -60.0f);
    ep.freeze       = paramValue (p.freeze) > 0.5f;
    ep.flip         = paramValue (p.flip) > 0.5f;
    ep.freqShiftHz  = paramValue (p.freqShift);
    chain.spectralEngine().setParams (ep);

    dsp::FractalSlicer::Params fp;
    fp.type     = enumFromParam (p.fracPattern, dsp::FractalPatternType::cantor);
    fp.depth    = static_cast<int> (paramValue (p.fracDepth, 3.0f));
    fp.seed     = static_cast<int> (paramValue (p.fracSeed, 1.0f));
    fp.ratio    = paramValue (p.fracRatio, 50.0f) * 0.01f;
    fp.asym     = paramValue (p.fracAsym) * 0.01f;
    fp.loHz     = paramValue (p.fracLoHz, 60.0f);
    fp.hiHz     = paramValue (p.fracHiHz, 12000.0f);
    fp.invert   = paramValue (p.fracInvert) > 0.5f;
    fp.shatter  = paramValue (p.fracShatter) * 0.01f;
    fp.sync     = paramValue (p.fracSync, 1.0f) > 0.5f;
    fp.divIndex = static_cast<int> (paramValue (p.fracDiv, 8.0f));
    fp.rateHz   = paramValue (p.fracRateHz, 2.0f);
    fp.gate     = paramValue (p.fracGate) * 0.01f;
    fp.grit     = paramValue (p.fracGrit) * 0.01f;
    fp.smoothMs = paramValue (p.fracSmooth, 6.0f);
    fp.mix      = paramValue (p.fracMix, 100.0f) * 0.01f;

    // loHz above hiHz would invert the range; treat it as a narrow band at the crossover
    // rather than producing an empty table.
    if (fp.loHz > fp.hiHz)
        std::swap (fp.loHz, fp.hiHz);

    chain.fractalSlicer().setParams (fp);

    dsp::ChordCarrier::Params cp;
    cp.osc          = enumFromParam (p.chordOsc, dsp::CarrierOsc::supersaw);
    cp.rootNote     = static_cast<int> (paramValue (p.chordRoot));
    cp.chord        = enumFromParam (p.chordType, dsp::ChordType::min);
    cp.octave       = static_cast<int> (paramValue (p.chordOctave));
    cp.spread       = paramValue (p.chordSpread, 35.0f) * 0.01f;
    cp.detuneCents  = paramValue (p.chordDetune, 12.0f);
    cp.level        = 1.0f; // applied via the smoothed value below
    cp.midiOverride = paramValue (p.chordMidiOvr, 1.0f) > 0.5f;
    carrier.setParams (cp);

    dsp::SpectralDelay::Params sdp;
    sdp.on       = paramValue (p.sdlyOn) > 0.5f;
    sdp.timeMs   = paramValue (p.sdlyTimeMs, 250.0f);
    sdp.spread   = paramValue (p.sdlySpread, 50.0f) * 0.01f;
    sdp.feedback = paramValue (p.sdlyFb, 40.0f) * 0.01f;
    sdp.damping  = paramValue (p.sdlyDamp, 30.0f) * 0.01f;
    sdp.mix      = paramValue (p.sdlyMix, 50.0f) * 0.01f;
    chain.spectralDelay().setParams (sdp);

    dsp::StereoDelay::Params dp;
    dp.on         = paramValue (p.dlyOn) > 0.5f;
    dp.mode       = enumFromParam (p.dlyMode, dsp::DelayMode::pingPong);
    dp.placement  = enumFromParam (p.dlyPlace, dsp::DelayPlacement::post);
    dp.sync       = paramValue (p.dlySync, 1.0f) > 0.5f;
    dp.divL       = static_cast<int> (paramValue (p.dlyDivL, 8.0f));
    dp.divR       = static_cast<int> (paramValue (p.dlyDivR, 10.0f));
    dp.msL        = paramValue (p.dlyMsL, 375.0f);
    dp.msR        = paramValue (p.dlyMsR, 500.0f);
    dp.feedback   = paramValue (p.dlyFb, 45.0f) * 0.01f;
    dp.hpHz       = paramValue (p.dlyHp, 120.0f);
    dp.lpHz       = paramValue (p.dlyLp, 8000.0f);
    dp.saturation = paramValue (p.dlySat, 20.0f) * 0.01f;
    dp.modRateHz  = paramValue (p.dlyModRate, 0.4f);
    dp.modDepth   = paramValue (p.dlyModDepth, 15.0f) * 0.01f;
    dp.diffusion  = paramValue (p.dlyDiffuse, 25.0f) * 0.01f;
    dp.ducking    = paramValue (p.dlyDuck) * 0.01f;
    dp.freeze     = paramValue (p.dlyFreeze) > 0.5f;
    dp.width      = paramValue (p.dlyWidth, 100.0f) * 0.01f;
    dp.mix        = paramValue (p.dlyMix, 35.0f) * 0.01f;
    stereoDelay.setParams (dp);

    dsp::ColorStage::Params colp;
    colp.satType = enumFromParam (p.satType, dsp::SatType::softClip);
    colp.driveDb = paramValue (p.drive);
    colp.width   = paramValue (p.colWidth, 100.0f) * 0.01f;
    colp.loCutHz = paramValue (p.loCut, 20.0f);
    colp.hiCutHz = paramValue (p.hiCut, 20000.0f);
    colp.limiter = paramValue (p.limiter, 1.0f) > 0.5f;
    colorStage.setParams (colp);

    mixSmoothed.setTargetValue (paramValue (p.mix, 100.0f) * 0.01f);
    gainSmoothed.setTargetValue (dsp::dbToGain (paramValue (p.outGain)));
    carrierLevelSmoothed.setTargetValue (dsp::dbToGain (paramValue (p.chordLevel)));
}

void VbdProcessor::renderCarrier (const juce::AudioBuffer<float>& mainIn,
                                  const juce::AudioBuffer<float>* sidechain,
                                  int numSamples,
                                  int numChannels)
{
    const auto source = static_cast<int> (paramValue (p.carrierSource, 1.0f));

    const auto sidechainUsable = sidechain != nullptr
                              && sidechain->getNumChannels() > 0
                              && hasActiveSidechain();

    auto useChord = (source == 1);
    auto fellBack = false;

    if (source == 0) // Sidechain
    {
        if (sidechainUsable)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto srcCh = juce::jmin (ch, sidechain->getNumChannels() - 1);
                carBuffer.copyFrom (ch, 0, *sidechain, srcCh, 0, numSamples);
            }
        }
        else
        {
            // Nothing patched in. Silence through a vocoder is silence, which reads as a
            // broken plugin -- fall back to the chord carrier and say so in the GUI.
            useChord = true;
            fellBack = true;
        }
    }
    else if (source == 2) // Self
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto srcCh = juce::jmin (ch, mainIn.getNumChannels() - 1);
            carBuffer.copyFrom (ch, 0, mainIn, srcCh, 0, numSamples);
        }
    }

    carrierFellBack.store (fellBack, std::memory_order_relaxed);

    if (useChord)
    {
        carrier.render (carBuffer, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = carBuffer.getWritePointer (ch);
            auto level = carrierLevelSmoothed;

            for (int i = 0; i < numSamples; ++i)
                data[i] *= level.getNextValue();
        }

        carrierLevelSmoothed.skip (numSamples);
    }
}

// ----------------------------------------------------------------------------- audio

void VbdProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto totalIn  = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples <= 0)
        return;

    auto mainIn   = getBusBuffer (buffer, true, 0);
    auto mainOut  = getBusBuffer (buffer, false, 0);
    const auto numChannels = juce::jmin (mainOut.getNumChannels(), modBuffer.getNumChannels());

    if (numChannels <= 0)
        return;

    readParameters();
    carrier.handleMidi (midi);

    // FFT size changes flush the STFT rings and move latency, so fade the output across
    // the switch and tell the host from the message thread.
    const auto wantedFft = fftSizeForIndex (static_cast<int> (paramValue (p.fftSize, 2.0f)));

    if (wantedFft != stft.getFftSize())
    {
        stft.setFftSize (wantedFft);
        chain.setFftSize (stft.getFftSize(), stft.currentFft(), stft.magnitudeScale(),
                          preparedSampleRate);
        chain.setHopSize (stft.getHopSize());

        for (auto& d : dryDelays)
            d.setDelay (stft.getLatencySamples());

        activeFftSize.store (stft.getFftSize(), std::memory_order_relaxed);
        desiredLatency.store (stft.getLatencySamples(), std::memory_order_relaxed);
        switchFadeCounter = switchFadeSamples;
    }

    // ---- modulator and dry tap
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto srcCh = juce::jmin (ch, mainIn.getNumChannels() - 1);
        modBuffer.copyFrom (ch, 0, mainIn, srcCh, 0, numSamples);
    }

    // ---- stereo delay, Pre placement
    //
    // Pre means the delay feeds the engine, so its repeats get vocoded and sliced rather
    // than sitting after them. It runs on the modulator copy, which is also the carrier
    // when Carrier Source is Self.
    const auto delayIsPre = static_cast<int> (paramValue (p.dlyPlace, 1.0f)) == 0;

    if (delayIsPre)
    {
        auto preView = juce::AudioBuffer<float> (modBuffer.getArrayOfWritePointers(),
                                                 numChannels, numSamples);
        stereoDelay.process (preView, numSamples);
    }

    // ---- carrier
    carBuffer.clear (0, numSamples);

    std::optional<juce::AudioBuffer<float>> sidechainBuffer;

    if (getBusCount (true) > 1 && hasActiveSidechain())
        sidechainBuffer = getBusBuffer (buffer, true, 1);

    renderCarrier (mainIn, sidechainBuffer.has_value() ? &sidechainBuffer.value() : nullptr,
                   numSamples, numChannels);

    // ---- dry path, aligned to the engine's latency
    for (int ch = 0; ch < numChannels; ++ch)
        dryDelays[static_cast<std::size_t> (ch)].process (modBuffer.getReadPointer (ch),
                                                          dryBuffer.getWritePointer (ch),
                                                          numSamples);

    // ---- transport, for the fractal animation's tempo sync
    {
        auto bpm = 120.0;
        auto ppq = 0.0;
        auto playing = false;

        if (auto* ph = getPlayHead())
        {
            if (const auto pos = ph->getPosition())
            {
                if (const auto hostBpm = pos->getBpm())
                    bpm = *hostBpm;

                if (const auto hostPpq = pos->getPpqPosition())
                    ppq = *hostPpq;

                playing = pos->getIsPlaying();
            }
        }

        chain.fractalSlicer().setTransport (bpm, playing);
        chain.setTransportInfo (bpm, ppq, playing);
        stereoDelay.setTransport (bpm);
    }

    // ---- spectral engine
    {
        std::array<const float*, 32> modPtrs {};
        std::array<const float*, 32> carPtrs {};
        std::array<float*, 32> wetPtrs {};

        const auto chans = juce::jmin (numChannels, 32);

        for (int ch = 0; ch < chans; ++ch)
        {
            modPtrs[static_cast<std::size_t> (ch)] = modBuffer.getReadPointer (ch);
            carPtrs[static_cast<std::size_t> (ch)] = carBuffer.getReadPointer (ch);
            wetPtrs[static_cast<std::size_t> (ch)] = wetBuffer.getWritePointer (ch);
        }

        stft.process (modPtrs.data(), carPtrs.data(), wetPtrs.data(), chans, numSamples, chain);
    }

    // ---- colour stage, then the stereo delay if it sits Post (the default)
    {
        auto wetView = juce::AudioBuffer<float> (wetBuffer.getArrayOfWritePointers(),
                                                 numChannels, numSamples);
        colorStage.process (wetView, numSamples);

        if (! delayIsPre)
            stereoDelay.process (wetView, numSamples);
    }

    // ---- mix, switch fade, output gain
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* dry = dryBuffer.getReadPointer (ch);
        const auto* wet = wetBuffer.getReadPointer (ch);
        auto* out = mainOut.getWritePointer (ch);

        auto mix  = mixSmoothed;
        auto gain = gainSmoothed;
        auto fade = switchFadeCounter;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto m = mix.getNextValue();

            auto wetSample = dsp::scrub (wet[i]);

            if (fade > 0)
            {
                // Ramp the wet path back in after a flush.
                const auto t = 1.0f - static_cast<float> (fade) / static_cast<float> (switchFadeSamples);
                wetSample *= t;
                --fade;
            }

            out[i] = dsp::scrub ((dry[i] * (1.0f - m) + wetSample * m) * gain.getNextValue());
        }
    }

    {
        const auto& slicer = chain.fractalSlicer();
        effectiveDepth.store (slicer.getEffectiveDepth(), std::memory_order_relaxed);
        fractalBands.store (slicer.getNumBands(), std::memory_order_relaxed);
        depthClamped.store (slicer.wasDepthClamped(), std::memory_order_relaxed);
        resolvedSdlyMs.store (chain.spectralDelay().getResolvedTimeMs(), std::memory_order_relaxed);
        requestedDepth.store (static_cast<int> (paramValue (p.fracDepth, 3.0f)), std::memory_order_relaxed);
        spectralDelayOn.store (paramValue (p.sdlyOn) > 0.5f, std::memory_order_relaxed);
        stereoDelayOn.store (paramValue (p.dlyOn) > 0.5f, std::memory_order_relaxed);

        const auto usingStereo = paramValue (p.dlyOn) > 0.5f;
        displayDelayFb.store (usingStereo ? paramValue (p.dlyFb, 45.0f) * 0.01f
                                          : paramValue (p.sdlyFb, 40.0f) * 0.01f,
                              std::memory_order_relaxed);
        displayDelayMs.store (usingStereo ? juce::jmax (paramValue (p.dlyMsL, 375.0f),
                                                        paramValue (p.dlyMsR, 500.0f))
                                          : chain.spectralDelay().getResolvedTimeMs(),
                              std::memory_order_relaxed);
    }

    mixSmoothed.skip (numSamples);
    gainSmoothed.skip (numSamples);
    switchFadeCounter = juce::jmax (0, switchFadeCounter - numSamples);

    // Any channels past the main output (e.g. a sidechain's slots) must not leak out.
    for (int ch = numChannels; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

// ----------------------------------------------------------------------------- editor

juce::AudioProcessorEditor* VbdProcessor::createEditor()
{
    return new VbdEditor (*this);
}

// ----------------------------------------------------------------------------- state

juce::ValueTree VbdProcessor::uiState() const
{
    const juce::ScopedLock sl (uiLock);
    return uiStateTree.createCopy();
}

void VbdProcessor::setUiProperty (const juce::Identifier& key, const juce::var& value)
{
    const juce::ScopedLock sl (uiLock);
    uiStateTree.setProperty (key, value, nullptr);
}

void VbdProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root { identity::stateTag };
    root.setProperty (kStateVersion, identity::stateVersion, nullptr);

    root.appendChild (apvts.copyState(), nullptr);

    {
        const juce::ScopedLock sl (uiLock);
        root.appendChild (uiStateTree.createCopy(), nullptr);
    }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void VbdProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);

    if (! root.isValid())
        return;

    // Tolerate state written by a plain APVTS dump (no wrapper), which is what an early
    // build could have saved, as well as our versioned wrapper.
    if (root.hasType (identity::apvtsTag))
    {
        apvts.replaceState (root);
        return;
    }

    if (! root.hasType (identity::stateTag))
        return;

    const int version = root.getProperty (kStateVersion, 1);

    // Version 1 is current. Future migrations branch here; unknown *newer* versions are
    // loaded best-effort rather than discarded, so a session saved by a later build still
    // recalls whatever this build understands.
    juce::ignoreUnused (version);

    if (auto params = root.getChildWithName (identity::apvtsTag); params.isValid())
        apvts.replaceState (params);

    if (auto ui = root.getChildWithName (identity::uiStateTag); ui.isValid())
    {
        const juce::ScopedLock sl (uiLock);

        for (int i = 0; i < ui.getNumProperties(); ++i)
        {
            const auto key = ui.getPropertyName (i);
            uiStateTree.setProperty (key, ui.getProperty (key), nullptr);
        }
    }
}

} // namespace vbd

// ----------------------------------------------------------------------------- factory

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vbd::VbdProcessor();
}
