#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace vbd::gui
{

/**
    Everything the interface needs to draw one frame, as plain data.

    Magnitudes are stored linear, not in dB: the conversion is a log per point, and the
    GUI thread at 60 Hz is a far better place to pay for that than the audio thread at the
    frame rate.
*/
struct VisualSnapshot
{
    static constexpr int spectrumPoints = 256;
    static constexpr int maxBands = 256;

    // Log-spaced magnitude curves, normalised so 1.0 is roughly full scale.
    std::array<float, spectrumPoints> inputMag {};
    std::array<float, spectrumPoints> outputMag {};
    std::array<float, spectrumPoints> carrierMag {};

    // The fractal subdivision, as normalised log-frequency positions so the visualiser
    // does not need to know anything about FFT sizes.
    int numBands = 0;
    std::array<float, maxBands> bandLo {};
    std::array<float, maxBands> bandHi {};
    std::array<float, maxBands> bandEnergy {};
    std::array<unsigned char, maxBands> bandActive {};
    std::array<unsigned char, maxBands> bandLevel {};

    int   effectiveDepth = 0;
    float lowEnergy = 0.0f;      // drives hair sway in Phase E2
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    double bpm = 120.0;
    double ppqPosition = 0.0;
    bool playing = false;
};

/**
    Single-producer, single-consumer latest-value-wins handoff, built as a seqlock.

    The obvious design here is a triple buffer with an index the consumer claims. That
    turns out to be subtly racy: between the consumer reading the published index and
    announcing which slot it holds, a fast producer can cycle all the way around and begin
    writing the very slot the consumer is about to read. The window is small enough that it
    passes under a release build and fails under a debug one, which is the worst kind of
    bug to ship -- rare, timing-dependent visual tearing.

    A seqlock closes it properly. The writer bumps an odd sequence number before touching
    the data and an even one after; the reader copies the data and retries if the sequence
    moved or was odd when it started. The reader therefore needs its own copy rather than a
    reference into shared storage -- about 7 KB at 60 Hz, which is nothing.

    Retries are bounded: if the writer is mid-update for several attempts, read() gives up
    and reports failure, and the caller simply keeps the frame it already had. A visualiser
    would rather reuse last frame than spin on the message thread.
*/
class VisualBridge
{
public:
    /** Audio thread: begin filling the snapshot. Must be matched with publish(). */
    VisualSnapshot& beginWrite() noexcept
    {
        sequence.fetch_add (1, std::memory_order_release);   // now odd: write in progress
        return slot;
    }

    /** Audio thread: finish the update, making it visible. */
    void publish() noexcept
    {
        sequence.fetch_add (1, std::memory_order_release);   // now even: stable
    }

    /** GUI thread: copy the latest snapshot. Returns false if the writer was too busy,
        in which case `dest` is untouched and the caller should reuse its previous frame. */
    bool read (VisualSnapshot& dest) noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = sequence.load (std::memory_order_acquire);

            if ((before & 1) != 0)
                continue;                 // a write is in flight

            dest = slot;

            std::atomic_thread_fence (std::memory_order_acquire);

            if (sequence.load (std::memory_order_acquire) == before)
                return true;              // nothing moved while we copied
        }

        return false;
    }

private:
    VisualSnapshot slot {};
    std::atomic<unsigned> sequence { 0 };
};

} // namespace vbd::gui
