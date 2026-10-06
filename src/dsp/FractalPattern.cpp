#include "FractalPattern.h"

#include <algorithm>
#include <cmath>

namespace vbd::dsp
{

namespace
{
    constexpr int kMinBinsPerBand = 2;

    /** Golden ratio conjugate, 1/phi. */
    constexpr double kInvPhi = 0.6180339887498949;

    int intPow (int base, int exp) noexcept
    {
        int result = 1;

        for (int i = 0; i < exp; ++i)
            result *= base;

        return result;
    }

    int popcount (unsigned v) noexcept
    {
        int n = 0;

        while (v != 0)
        {
            n += static_cast<int> (v & 1u);
            v >>= 1;
        }

        return n;
    }

    // ---- the pattern rules -------------------------------------------------------
    //
    // Each returns whether leaf `index` of `length` leaves is active. All are pure
    // functions of the index, which is what makes them unit-testable against hand-computed
    // golden values.

    /** Cantor: written in base 3, a leaf is removed if any digit is 1 -- i.e. it fell in
        the middle third at some level. This is the classic Cantor set membership test. */
    bool cantorActive (int index, int depth) noexcept
    {
        for (int i = 0; i < depth; ++i)
        {
            if (index % 3 == 1)
                return false;

            index /= 3;
        }

        return true;
    }

    /** Golden: the Fibonacci word, i.e. the characteristic Sturmian sequence of 1/phi.
        Self-similar with golden-ratio statistics rather than binary ones. */
    bool goldenActive (int index) noexcept
    {
        const auto a = static_cast<int> (std::floor (static_cast<double> (index + 1) * kInvPhi));
        const auto b = static_cast<int> (std::floor (static_cast<double> (index) * kInvPhi));

        return (a - b) == 1;
    }

    /** Thue-Morse: active where the population count of the index is even. */
    bool thueMorseActive (int index) noexcept
    {
        return (popcount (static_cast<unsigned> (index)) % 2) == 0;
    }

    /** Sierpinski: rule 90 (next = left XOR right) run from a single live cell for
        `depth` generations. The resulting row is a genuine Sierpinski cross-section,
        structurally distinct from the Fibonacci and Thue-Morse rules above. */
    bool sierpinskiActive (int index, int length, int depth) noexcept
    {
        std::array<bool, BandTable::maxBands> cur {};
        std::array<bool, BandTable::maxBands> next {};

        const auto n = std::min (length, BandTable::maxBands);

        cur[static_cast<std::size_t> (n / 2)] = true;

        for (int gen = 0; gen < depth; ++gen)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto left  = i > 0 ? cur[static_cast<std::size_t> (i - 1)] : false;
                const auto right = i < n - 1 ? cur[static_cast<std::size_t> (i + 1)] : false;
                next[static_cast<std::size_t> (i)] = left != right;
            }

            cur = next;
        }

        return index >= 0 && index < n && cur[static_cast<std::size_t> (index)];
    }

    /** L-System: a seeded ternary rewrite. The seed selects one production for each
        symbol, so a given seed always yields the same figure. */
    bool lSystemActive (int index, int depth, int seed) noexcept
    {
        static constexpr const char* rulesA[] = { "ABA", "AAB", "ABB" };
        static constexpr const char* rulesB[] = { "BBB", "BAB", "BBA" };

        const auto s = seed < 0 ? -seed : seed;
        const auto* ruleA = rulesA[static_cast<std::size_t> (s % 3)];
        const auto* ruleB = rulesB[static_cast<std::size_t> ((s / 3) % 3)];

        // Walk the index's base-3 digits from the most significant, following the
        // expansion without materialising the string.
        char symbol = 'A';

        for (int level = depth - 1; level >= 0; --level)
        {
            const auto digit = (index / intPow (3, level)) % 3;
            const auto* rule = (symbol == 'A') ? ruleA : ruleB;
            symbol = rule[static_cast<std::size_t> (digit)];
        }

        return symbol == 'A';
    }

    // ---- geometry ---------------------------------------------------------------

    struct Subdivider
    {
        const FractalPatternParams* params = nullptr;
        BandTable* table = nullptr;
        int arity = 2;
        int maxLevel = 1;
        double baseSplit = 0.5;

        /** Primary split fraction at a given level. `asym` alternates the split between
            levels, which is what turns a symmetric figure into a lopsided one. */
        double splitAt (int level) const noexcept
        {
            const auto asymAbs = static_cast<double> (std::abs (params->asym));

            // The sign of asym chooses which parity of level gets mirrored, so positive
            // and negative asymmetry lean the figure opposite ways rather than being
            // redundant.
            const auto flip = (params->asym >= 0.0f) ? (level % 2 == 1) : (level % 2 == 0);

            if (! flip || asymAbs <= 0.0)
                return baseSplit;

            const auto mirrored = 1.0 - baseSplit;

            return baseSplit + asymAbs * (mirrored - baseSplit);
        }

        void emit (int lo, int hi, float weight, int level) const noexcept
        {
            if (table->numBands >= BandTable::maxBands)
                return;

            auto& band = table->bands[static_cast<std::size_t> (table->numBands)];
            band.loBin  = lo;
            band.hiBin  = hi;
            band.weight = weight;
            band.level  = level;
            band.active = true;     // filled in by the caller's pattern rule
            ++table->numBands;
        }

        void recurse (int lo, int hi, int level, float weight, int nonPrimary) const noexcept
        {
            if (level >= maxLevel || hi - lo < arity * kMinBinsPerBand)
            {
                emit (lo, hi, weight, nonPrimary);
                return;
            }

            const auto span = static_cast<double> (hi - lo);

            if (arity == 3)
            {
                // Middle fraction from `ratio`, mapped so 0.5 lands on exactly one third.
                const auto r = static_cast<double> (params->ratio);
                const auto mid = r <= 0.5
                    ? 0.05 + (1.0 / 3.0 - 0.05) * (r * 2.0)
                    : 1.0 / 3.0 + (0.60 - 1.0 / 3.0) * ((r - 0.5) * 2.0);

                const auto outerTotal = 1.0 - mid;
                const auto leftShare = 0.5 + 0.4 * static_cast<double> (params->asym);

                auto w0 = static_cast<int> (std::llround (span * outerTotal * leftShare));
                auto w1 = static_cast<int> (std::llround (span * mid));

                w0 = std::clamp (w0, kMinBinsPerBand, hi - lo - 2 * kMinBinsPerBand);
                w1 = std::clamp (w1, kMinBinsPerBand, hi - lo - w0 - kMinBinsPerBand);

                const auto b0 = lo + w0;
                const auto b1 = b0 + w1;

                // Child 0 is primary; the others carry a falloff, giving a multiplicative
                // cascade -- the amplitude profile is itself fractal.
                recurse (lo, b0, level + 1, weight,          nonPrimary);
                recurse (b0, b1, level + 1, weight * 0.7f,   nonPrimary + 1);
                recurse (b1, hi, level + 1, weight * 0.85f,  nonPrimary + 1);
            }
            else
            {
                auto frac = splitAt (level);
                frac = std::clamp (frac, 0.12, 0.88);

                auto w0 = static_cast<int> (std::llround (span * frac));
                w0 = std::clamp (w0, kMinBinsPerBand, hi - lo - kMinBinsPerBand);

                const auto b0 = lo + w0;

                recurse (lo, b0, level + 1, weight,        nonPrimary);
                recurse (b0, hi, level + 1, weight * 0.7f, nonPrimary + 1);
            }
        }
    };
}

int arityFor (FractalPatternType type) noexcept
{
    switch (type)
    {
        case FractalPatternType::cantor:
        case FractalPatternType::lSystem:
            return 3;

        case FractalPatternType::golden:
        case FractalPatternType::thueMorse:
        case FractalPatternType::sierpinski:
            return 2;
    }

    return 2;
}

int maxDepthFor (FractalPatternType type, int binsInRange) noexcept
{
    const auto arity = arityFor (type);

    int best = 1;

    for (int depth = 1; depth <= 7; ++depth)
    {
        const auto leaves = intPow (arity, depth);

        if (leaves > BandTable::maxBands)
            break;

        if (leaves * kMinBinsPerBand > binsInRange)
            break;

        best = depth;
    }

    return best;
}

void buildBandTable (const FractalPatternParams& params, BandTable& table) noexcept
{
    table.numBands = 0;
    table.requestedDepth = params.depth;

    const auto lo = std::max (0, params.loBin);
    const auto hi = std::max (lo + 1, params.hiBin);
    const auto binsInRange = hi - lo;

    const auto arity = arityFor (params.type);
    const auto allowed = maxDepthFor (params.type, binsInRange);
    const auto depth = std::clamp (params.depth, 1, allowed);

    table.effectiveDepth = depth;
    table.depthWasClamped = depth < params.depth;

    if (binsInRange < arity * kMinBinsPerBand)
    {
        // Range too narrow to subdivide at all: one passthrough band.
        table.bands[0] = FractalBand { lo, hi, 1.0f, 0, true };
        table.numBands = 1;
        table.effectiveDepth = 0;
        table.depthWasClamped = params.depth > 0;
        return;
    }

    Subdivider sub;
    sub.params = &params;
    sub.table = &table;
    sub.arity = arity;
    sub.maxLevel = depth;
    sub.baseSplit = (params.type == FractalPatternType::golden)
                      ? kInvPhi
                      : 0.5 + 0.35 * (static_cast<double> (params.ratio) - 0.5);

    sub.recurse (lo, hi, 0, 1.0f, 0);

    // Apply the pattern's activity rule by leaf index.
    for (int i = 0; i < table.numBands; ++i)
        table.bands[static_cast<std::size_t> (i)].active =
            patternStep (params.type, params.seed, table.numBands, i);
}

bool patternStep (FractalPatternType type, int seed, int sequenceLength, int index) noexcept
{
    if (sequenceLength <= 0)
        return true;

    // Wrap rather than clamp, so the same rule can be sampled along time for the rhythm
    // gate without running off the end of the sequence.
    index = ((index % sequenceLength) + sequenceLength) % sequenceLength;

    const auto arity = arityFor (type);

    // Recover the depth that produced this many leaves.
    int depth = 0;

    for (int d = 0; d <= 8; ++d)
    {
        if (intPow (arity, d) >= sequenceLength)
        {
            depth = d;
            break;
        }

        depth = d;
    }

    depth = std::max (1, depth);

    switch (type)
    {
        case FractalPatternType::cantor:     return cantorActive (index, depth);
        case FractalPatternType::golden:     return goldenActive (index);
        case FractalPatternType::thueMorse:  return thueMorseActive (index);
        case FractalPatternType::sierpinski: return sierpinskiActive (index, sequenceLength, depth);
        case FractalPatternType::lSystem:    return lSystemActive (index, depth, seed);
    }

    return true;
}

} // namespace vbd::dsp
