#pragma once

#include <array>
#include <cstdint>

namespace vbd::dsp
{

enum class FractalPatternType
{
    cantor = 0,
    golden,
    thueMorse,
    sierpinski,
    lSystem
};

/** One leaf of the recursive subdivision: a contiguous run of FFT bins. */
struct FractalBand
{
    int   loBin  = 0;   // inclusive
    int   hiBin  = 0;   // exclusive
    float weight = 1.0f;
    int   level  = 0;   // how many non-primary branches were taken to reach this leaf
    bool  active = true;
};

/**
    The result of building a pattern: a frequency-ordered set of bands covering the
    selected range, each flagged active or not by the pattern's rule.

    Fixed capacity and no owning pointers, so building one on the audio thread allocates
    nothing.
*/
struct BandTable
{
    static constexpr int maxBands = 256;

    std::array<FractalBand, maxBands> bands {};
    int numBands        = 0;
    int effectiveDepth  = 0;
    int requestedDepth  = 0;
    bool depthWasClamped = false;
};

struct FractalPatternParams
{
    FractalPatternType type = FractalPatternType::cantor;
    int   depth = 3;        // 1..7 as requested by the user
    int   seed  = 1;
    float ratio = 0.5f;     // 0..1, primary split fraction
    float asym  = 0.0f;     // -1..1, alternates the split by level
    int   loBin = 1;
    int   hiBin = 512;      // exclusive
};

/** Children per subdivision step: 3 for the ternary patterns, 2 for the binary ones. */
int arityFor (FractalPatternType type) noexcept;

/**
    Largest usable depth for a pattern given the bins available.

    Two independent ceilings, and the lower one wins:

    - **Bin resolution.** A band narrower than 2 bins cannot be gated meaningfully; it
      aliases into noise. This is the limit PLAN section 1.4 describes.
    - **Band count.** Leaf count is `arity^depth`, so the ternary patterns (Cantor,
      L-System) grow as 3^d -- 2187 leaves at depth 7, far past the table's capacity.
      The binary patterns reach depth 7 comfortably at 128.

    So the same Depth setting reaches a different effective depth depending on the
    pattern, which is why the GUI has to show the resolved value rather than the request.
*/
int maxDepthFor (FractalPatternType type, int binsInRange) noexcept;

/** Builds the band table. Pure, deterministic, and allocation-free. */
void buildBandTable (const FractalPatternParams& params, BandTable& table) noexcept;

/**
    The pattern's self-similar 0/1 sequence, used both for which bands are active and --
    sampled along time instead of frequency -- for the rhythm gate, so the stutter is the
    same fractal as the spectral split.
*/
bool patternStep (FractalPatternType type, int seed, int sequenceLength, int index) noexcept;

} // namespace vbd::dsp
