#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Identity.h"
#include "Params.h"
#include "dsp/ChordCarrier.h"
#include "dsp/ColorStage.h"
#include "dsp/SpectralChain.h"
#include "dsp/StereoDelay.h"
#include "gui/VisualBridge.h"
#include "dsp/StftEngine.h"
#include "dsp/Utils.h"

#include <atomic>

namespace vbd
{

/**
    Phase D: the full signal chain.

        input --+-> [StereoDelay, if placement = Pre]
                |        |
                |        v
                |   [modulator]  +  [carrier: sidechain / chord / self]
                |        |
                |        v
                |   STFT -> SpectralEngine -> FractalSlicer -> SpectralDelay -> iSTFT
                |        |
                |        v
                |   ColorStage (saturate, width, lo/hi cut)
                |        |
                |        v
                |   [StereoDelay, if placement = Post (default)]
                |        |
                +-> LatencyDelay (N samples) -- dry --+-> mix -> gain -> limiter -> out

    Both delay modules are off by default and skipped entirely when off.
*/
class VbdProcessor final : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    VbdProcessor();
    ~VbdProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return identity::productName; }

    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                        { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- VIIBEDEALER
    juce::AudioProcessorValueTreeState& state() noexcept      { return apvts; }

    /** True when the host has actually given us a sidechain bus with channels on it.
        Standalone never does, and in a DAW the user may leave it unpatched -- the carrier
        stage falls back to the internal chord oscillator in that case. */
    bool hasActiveSidechain() const noexcept;

    /** True when Sidechain is selected but nothing is feeding it, so the chord carrier is
        standing in. Drives the amber badge on the CARRIER panel. */
    bool isCarrierFallbackActive() const noexcept { return carrierFellBack.load (std::memory_order_relaxed); }

    /** The FFT size currently in use by the engine. */
    int getActiveFftSize() const noexcept { return activeFftSize.load (std::memory_order_relaxed); }

    /** Depth the fractal slicer actually reached, which may be below the requested value
        when the pattern's leaf count or the bin resolution will not allow it. */
    int getEffectiveFractalDepth() const noexcept { return effectiveDepth.load (std::memory_order_relaxed); }
    int getFractalBandCount() const noexcept { return fractalBands.load (std::memory_order_relaxed); }
    bool wasFractalDepthClamped() const noexcept { return depthClamped.load (std::memory_order_relaxed); }

    /** The spectral delay time actually achieved, which the 128-frame ring can cap below
        the requested value at small FFT sizes or high sample rates. */
    float getResolvedSpectralDelayMs() const noexcept { return resolvedSdlyMs.load (std::memory_order_relaxed); }

    int getRequestedFractalDepth() const noexcept { return requestedDepth.load (std::memory_order_relaxed); }
    bool isSpectralDelayOn() const noexcept { return spectralDelayOn.load (std::memory_order_relaxed); }
    bool isStereoDelayOn() const noexcept { return stereoDelayOn.load (std::memory_order_relaxed); }

    /** Delay feedback and time, for the character layer's ghost trails. */
    float getDelayFeedbackForDisplay() const noexcept { return displayDelayFb.load (std::memory_order_relaxed); }
    float getDelayTimeForDisplay() const noexcept { return displayDelayMs.load (std::memory_order_relaxed); }

    /** Display data, written by the audio thread and read by the editor. */
    gui::VisualBridge& visuals() noexcept { return visualBridge; }

    /** View state (scene, character visibility, ...). Message thread only. */
    juce::ValueTree uiState() const;
    void setUiProperty (const juce::Identifier& key, const juce::var& value);

private:
    static BusesProperties makeBuses();

    void timerCallback() override;
    void readParameters();
    void renderCarrier (const juce::AudioBuffer<float>& mainIn,
                        const juce::AudioBuffer<float>* sidechain,
                        int numSamples,
                        int numChannels);

    juce::AudioProcessorValueTreeState apvts;

    // Cached parameter pointers. Looking these up by string on the audio thread would be a
    // map lookup per block per parameter.
    struct ParamPtrs
    {
        std::atomic<float>* carrierSource = nullptr;
        std::atomic<float>* chordOsc = nullptr;
        std::atomic<float>* chordRoot = nullptr;
        std::atomic<float>* chordType = nullptr;
        std::atomic<float>* chordOctave = nullptr;
        std::atomic<float>* chordSpread = nullptr;
        std::atomic<float>* chordDetune = nullptr;
        std::atomic<float>* chordLevel = nullptr;
        std::atomic<float>* chordMidiOvr = nullptr;

        std::atomic<float>* fftSize = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* morph = nullptr;
        std::atomic<float>* formant = nullptr;
        std::atomic<float>* tilt = nullptr;
        std::atomic<float>* envRes = nullptr;
        std::atomic<float>* cepstral = nullptr;
        std::atomic<float>* phaseLock = nullptr;
        std::atomic<float>* sens = nullptr;
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* flip = nullptr;
        std::atomic<float>* freqShift = nullptr;

        std::atomic<float>* fracPattern = nullptr;
        std::atomic<float>* fracDepth = nullptr;
        std::atomic<float>* fracSeed = nullptr;
        std::atomic<float>* fracRatio = nullptr;
        std::atomic<float>* fracAsym = nullptr;
        std::atomic<float>* fracLoHz = nullptr;
        std::atomic<float>* fracHiHz = nullptr;
        std::atomic<float>* fracInvert = nullptr;
        std::atomic<float>* fracShatter = nullptr;
        std::atomic<float>* fracSync = nullptr;
        std::atomic<float>* fracDiv = nullptr;
        std::atomic<float>* fracRateHz = nullptr;
        std::atomic<float>* fracGate = nullptr;
        std::atomic<float>* fracGrit = nullptr;
        std::atomic<float>* fracSmooth = nullptr;
        std::atomic<float>* fracMix = nullptr;

        std::atomic<float>* sdlyOn = nullptr;
        std::atomic<float>* sdlyTimeMs = nullptr;
        std::atomic<float>* sdlySpread = nullptr;
        std::atomic<float>* sdlyFb = nullptr;
        std::atomic<float>* sdlyDamp = nullptr;
        std::atomic<float>* sdlyMix = nullptr;

        std::atomic<float>* dlyOn = nullptr;
        std::atomic<float>* dlyMode = nullptr;
        std::atomic<float>* dlyPlace = nullptr;
        std::atomic<float>* dlySync = nullptr;
        std::atomic<float>* dlyDivL = nullptr;
        std::atomic<float>* dlyDivR = nullptr;
        std::atomic<float>* dlyMsL = nullptr;
        std::atomic<float>* dlyMsR = nullptr;
        std::atomic<float>* dlyFb = nullptr;
        std::atomic<float>* dlyHp = nullptr;
        std::atomic<float>* dlyLp = nullptr;
        std::atomic<float>* dlySat = nullptr;
        std::atomic<float>* dlyModRate = nullptr;
        std::atomic<float>* dlyModDepth = nullptr;
        std::atomic<float>* dlyDiffuse = nullptr;
        std::atomic<float>* dlyDuck = nullptr;
        std::atomic<float>* dlyFreeze = nullptr;
        std::atomic<float>* dlyWidth = nullptr;
        std::atomic<float>* dlyMix = nullptr;

        std::atomic<float>* satType = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* colWidth = nullptr;
        std::atomic<float>* loCut = nullptr;
        std::atomic<float>* hiCut = nullptr;
        std::atomic<float>* limiter = nullptr;

        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outGain = nullptr;
    } p;

    dsp::StftEngine      stft;
    dsp::SpectralChain   chain;
    dsp::ChordCarrier    carrier;
    dsp::StereoDelay     stereoDelay;
    dsp::ColorStage      colorStage;

    juce::AudioBuffer<float> modBuffer;
    juce::AudioBuffer<float> carBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> dryBuffer;

    std::vector<dsp::LatencyDelay> dryDelays;

    juce::SmoothedValue<float> mixSmoothed;
    juce::SmoothedValue<float> gainSmoothed;
    juce::SmoothedValue<float> carrierLevelSmoothed;

    // Short fade applied after an FFT-size switch. The switch changes latency and flushes
    // the rings, so without this it clicks.
    int switchFadeSamples = 0;
    int switchFadeCounter = 0;

    std::atomic<int>  desiredLatency { 0 };
    std::atomic<int>  activeFftSize { 2048 };
    std::atomic<bool> carrierFellBack { false };
    std::atomic<int>  effectiveDepth { 0 };
    std::atomic<int>  fractalBands { 0 };
    std::atomic<bool> depthClamped { false };
    std::atomic<float> resolvedSdlyMs { 0.0f };
    std::atomic<int>  requestedDepth { 3 };
    std::atomic<bool> spectralDelayOn { false };
    std::atomic<bool> stereoDelayOn { false };
    std::atomic<float> displayDelayFb { 0.0f };
    std::atomic<float> displayDelayMs { 0.0f };

    gui::VisualBridge visualBridge;

    // Guards uiStateTree only. Never taken on the audio thread.
    mutable juce::CriticalSection uiLock;
    juce::ValueTree uiStateTree { identity::uiStateTag };

    double preparedSampleRate = 44100.0;
    int    preparedBlockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VbdProcessor)
};

} // namespace vbd
