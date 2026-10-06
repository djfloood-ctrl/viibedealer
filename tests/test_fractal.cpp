#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/FractalPattern.h"
#include "dsp/FractalSlicer.h"
#include "dsp/StftEngine.h"

#include <random>
#include <string>
#include <vector>

namespace
{

using namespace vbd::dsp;

/** Renders a pattern's activity as a string of A/. so golden values stay readable. */
std::string activityString (FractalPatternType type, int seed, int length)
{
    std::string s;
    s.reserve (static_cast<std::size_t> (length));

    for (int i = 0; i < length; ++i)
        s += patternStep (type, seed, length, i) ? 'A' : '.';

    return s;
}

class FractalPatternTests final : public juce::UnitTest
{
public:
    FractalPatternTests() : juce::UnitTest ("Fractal patterns", "dsp") {}

    void runTest() override
    {
        beginTest ("Cantor removes the middle third, recursively");
        {
            // Depth 1 over 3 leaves: keep, drop, keep.
            expectEquals (activityString (FractalPatternType::cantor, 0, 3), std::string ("A.A"));

            // Depth 2 over 9 leaves. The middle third of the whole range is gone, and each
            // surviving third has its own middle third removed:
            //   [A . A] [. . .] [A . A]
            expectEquals (activityString (FractalPatternType::cantor, 0, 9),
                          std::string ("A.A...A.A"));

            // Depth 3 over 27: the same figure at one more scale.
            expectEquals (activityString (FractalPatternType::cantor, 0, 27),
                          std::string ("A.A...A.A.........A.A...A.A"));
        }

        beginTest ("Thue-Morse follows the parity of the index's population count");
        {
            // 0,1,1,0,1,0,0,1 -> A..A.AA.
            expectEquals (activityString (FractalPatternType::thueMorse, 0, 8),
                          std::string ("A..A.AA."));

            // The sequence is its own complement when doubled, which is the defining
            // property of Thue-Morse.
            const auto s16 = activityString (FractalPatternType::thueMorse, 0, 16);
            expectEquals (s16.substr (0, 8), std::string ("A..A.AA."));
            expectEquals (s16.substr (8, 8), std::string (".AA.A..A"));
        }

        beginTest ("Golden follows the Fibonacci word");
        {
            // Characteristic Sturmian sequence of 1/phi: floor((i+1)/phi) - floor(i/phi),
            // with 1/phi = 0.6180339887. Hand-computed for i = 0..12:
            //   i : 0 1 2 3 4 5 6 7 8 9 10 11 12
            //   d : 0 1 0 1 1 0 1 0 1 1  0  1  1
            expectEquals (activityString (FractalPatternType::golden, 0, 13),
                          std::string (".A.AA.A.AA.AA"));

            // Density must approach 1/phi = 0.618.
            const auto s = activityString (FractalPatternType::golden, 0, 128);
            const auto actives = std::count (s.begin(), s.end(), 'A');
            const auto density = static_cast<double> (actives) / 128.0;

            expect (std::abs (density - 0.6180339887) < 0.02,
                    "golden density " + juce::String (density, 4) + " is not near 1/phi");
        }

        beginTest ("Sierpinski is symmetric, as a rule-90 figure must be");
        {
            constexpr int n = 32;
            const auto s = activityString (FractalPatternType::sierpinski, 0, n);

            // Rule 90 (next = left XOR right) is left-right symmetric, so the figure is
            // mirrored about the cell it was seeded from -- index n/2 -- NOT about the
            // midpoint of the array, which sits half a cell lower.
            constexpr int axis = n / 2;

            for (int d = 1; d < axis; ++d)
                expect (s[static_cast<std::size_t> (axis + d)] == s[static_cast<std::size_t> (axis - d)],
                        "sierpinski row is not symmetric about the seed at offset "
                        + juce::String (d));

            expect (std::count (s.begin(), s.end(), 'A') > 0, "sierpinski row is empty");
        }

        beginTest ("L-System depends on the seed, deterministically");
        {
            const auto a = activityString (FractalPatternType::lSystem, 1, 9);
            const auto b = activityString (FractalPatternType::lSystem, 1, 9);
            const auto c = activityString (FractalPatternType::lSystem, 2, 9);

            expectEquals (a, b, "same seed gave different results");
            expect (a != c, "different seeds gave identical figures");

            // Seed 1 selects A -> AAB, B -> BBB, so depth 2 expands
            //   A -> AAB -> (AAB)(AAB)(BBB) = AABAABBBB
            expectEquals (a, std::string ("AA.AA...."));
        }

        beginTest ("arity and depth ceilings");
        {
            expectEquals (arityFor (FractalPatternType::cantor), 3);
            expectEquals (arityFor (FractalPatternType::lSystem), 3);
            expectEquals (arityFor (FractalPatternType::golden), 2);
            expectEquals (arityFor (FractalPatternType::thueMorse), 2);
            expectEquals (arityFor (FractalPatternType::sierpinski), 2);

            // With plenty of bins, the binary patterns reach depth 7 (128 leaves) but the
            // ternary ones stop at 5, because 3^6 = 729 overruns the 256-band table.
            expectEquals (maxDepthFor (FractalPatternType::thueMorse, 100000), 7);
            expectEquals (maxDepthFor (FractalPatternType::cantor, 100000), 5);

            // Bin resolution is the other ceiling: 2 bins per band minimum.
            expect (maxDepthFor (FractalPatternType::thueMorse, 16) <= 3,
                    "depth not clamped by bin count");
            expect (maxDepthFor (FractalPatternType::thueMorse, 8) <= 2);
        }

        beginTest ("band tables tile the range exactly, with no gaps or overlaps");
        {
            const FractalPatternType types[] = {
                FractalPatternType::cantor, FractalPatternType::golden,
                FractalPatternType::thueMorse, FractalPatternType::sierpinski,
                FractalPatternType::lSystem
            };

            for (auto type : types)
            {
                for (int depth = 1; depth <= 7; ++depth)
                {
                    for (float ratio : { 0.0f, 0.5f, 1.0f })
                    {
                        for (float asym : { -1.0f, 0.0f, 1.0f })
                        {
                            FractalPatternParams pp;
                            pp.type = type;
                            pp.depth = depth;
                            pp.ratio = ratio;
                            pp.asym = asym;
                            pp.loBin = 3;
                            pp.hiBin = 1025;

                            BandTable table;
                            buildBandTable (pp, table);

                            expect (table.numBands > 0, "empty band table");
                            expect (table.numBands <= BandTable::maxBands,
                                    "band table overflowed");

                            // Contiguity: each band must start where the previous ended.
                            expectEquals (table.bands[0].loBin, pp.loBin, "first band misaligned");

                            for (int i = 1; i < table.numBands; ++i)
                                expectEquals (table.bands[static_cast<std::size_t> (i)].loBin,
                                              table.bands[static_cast<std::size_t> (i - 1)].hiBin,
                                              "gap or overlap between bands");

                            expectEquals (table.bands[static_cast<std::size_t> (table.numBands - 1)].hiBin,
                                          pp.hiBin, "last band misaligned");

                            // Every band must be wide enough to mean something.
                            for (int i = 0; i < table.numBands; ++i)
                            {
                                const auto& b = table.bands[static_cast<std::size_t> (i)];
                                expect (b.hiBin - b.loBin >= 1,
                                        "zero-width band at index " + juce::String (i));
                                expect (b.weight > 0.0f && b.weight <= 1.0f,
                                        "weight out of range: " + juce::String (b.weight));
                            }
                        }
                    }
                }
            }
        }

        beginTest ("depth clamping is reported, not silently applied");
        {
            FractalPatternParams pp;
            pp.type = FractalPatternType::cantor;
            pp.depth = 7;
            pp.loBin = 1;
            pp.hiBin = 1025;

            BandTable table;
            buildBandTable (pp, table);

            expectEquals (table.requestedDepth, 7);
            expect (table.effectiveDepth < 7, "cantor depth 7 should have been clamped");
            expect (table.depthWasClamped, "clamp happened but was not reported");

            // And when nothing needs clamping, the flag must stay clear.
            pp.type = FractalPatternType::thueMorse;
            pp.depth = 4;
            buildBandTable (pp, table);

            expectEquals (table.effectiveDepth, 4);
            expect (! table.depthWasClamped, "clamp wrongly reported");
        }

        beginTest ("a range too narrow to subdivide degrades to one passthrough band");
        {
            FractalPatternParams pp;
            pp.type = FractalPatternType::cantor;
            pp.depth = 5;
            pp.loBin = 10;
            pp.hiBin = 13;   // 3 bins: cannot make 3 bands of 2 bins

            BandTable table;
            buildBandTable (pp, table);

            expectEquals (table.numBands, 1);
            expect (table.bands[0].active, "passthrough band must be active");
            expectEquals (table.bands[0].loBin, 10);
            expectEquals (table.bands[0].hiBin, 13);
        }

        beginTest ("inverted and degenerate ranges do not crash or produce empty tables");
        {
            for (auto [lo, hi] : { std::pair { 100, 100 }, std::pair { 500, 20 },
                                   std::pair { 0, 1 }, std::pair { -50, 10 } })
            {
                FractalPatternParams pp;
                pp.type = FractalPatternType::golden;
                pp.depth = 4;
                pp.loBin = lo;
                pp.hiBin = hi;

                BandTable table;
                buildBandTable (pp, table);

                expect (table.numBands >= 1,
                        "empty table for range " + juce::String (lo) + ".." + juce::String (hi));
            }
        }
    }
};

class FractalSlicerTests final : public juce::UnitTest
{
public:
    FractalSlicerTests() : juce::UnitTest ("Fractal slicer", "dsp") {}

    void runTest() override
    {
        constexpr int fftSize = 2048;
        constexpr int numBins = fftSize / 2 + 1;
        constexpr double sr = 48000.0;

        // A frame of uniform magnitude, so any gain change is obvious.
        auto makeFlatFrame = [] ()
        {
            std::vector<float> f (static_cast<std::size_t> (2 * numBins), 0.0f);

            for (int k = 0; k < numBins; ++k)
            {
                f[static_cast<std::size_t> (2 * k)]     = 1.0f;
                f[static_cast<std::size_t> (2 * k + 1)] = 0.0f;
            }

            return f;
        };

        auto frameEnergy = [] (const std::vector<float>& f)
        {
            double e = 0.0;

            for (int k = 0; k < numBins; ++k)
            {
                const auto re = static_cast<double> (f[static_cast<std::size_t> (2 * k)]);
                const auto im = static_cast<double> (f[static_cast<std::size_t> (2 * k + 1)]);
                e += re * re + im * im;
            }

            return e;
        };

        beginTest ("Shatter at 0 is bit-exact transparent");
        {
            FractalSlicer slicer;
            slicer.prepare (2, StftEngine::maxFftSize, sr);
            slicer.setFftSize (fftSize, sr);

            FractalSlicer::Params fp;
            fp.shatter = 0.0f;
            fp.gate = 0.0f;
            fp.invert = false;
            slicer.setParams (fp);

            const auto reference = makeFlatFrame();

            // Several frames, so any slow smoothing drift would show up.
            for (int frame = 0; frame < 40; ++frame)
            {
                auto f = reference;
                slicer.beginFrame (512, numBins);
                slicer.processFrame (f.data(), numBins, 0);

                for (std::size_t i = 0; i < f.size(); ++i)
                    if (! juce::exactlyEqual (f[i], reference[i]))
                    {
                        expect (false, "slicer altered the frame at Shatter 0, index "
                                       + juce::String (static_cast<int> (i)));
                        return;
                    }
            }

            expect (true);
        }

        beginTest ("Shatter removes energy, and more of it as it rises");
        {
            double previous = -1.0;

            for (float shatter : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                FractalSlicer slicer;
                slicer.prepare (2, StftEngine::maxFftSize, sr);
                slicer.setFftSize (fftSize, sr);

                FractalSlicer::Params fp;
                fp.type = FractalPatternType::cantor;
                fp.depth = 3;
                fp.shatter = shatter;
                fp.smoothMs = 0.1f;   // settle fast so the measurement is steady-state
                fp.loHz = 100.0f;
                fp.hiHz = 10000.0f;
                slicer.setParams (fp);

                std::vector<float> f;

                for (int frame = 0; frame < 60; ++frame)
                {
                    f = makeFlatFrame();
                    slicer.beginFrame (512, numBins);
                    slicer.processFrame (f.data(), numBins, 0);
                }

                const auto energy = frameEnergy (f);

                expect (energy < frameEnergy (makeFlatFrame()),
                        "Shatter " + juce::String (shatter) + " removed no energy");

                if (previous >= 0.0)
                    expect (energy <= previous * 1.05,
                            "Shatter " + juce::String (shatter)
                            + " did not remove at least as much energy as the step below");

                previous = energy;
            }
        }

        beginTest ("Invert complements which bands survive");
        {
            auto run = [&] (bool invert)
            {
                FractalSlicer slicer;
                slicer.prepare (2, StftEngine::maxFftSize, sr);
                slicer.setFftSize (fftSize, sr);

                FractalSlicer::Params fp;
                fp.type = FractalPatternType::cantor;
                fp.depth = 2;
                fp.shatter = 1.0f;
                fp.grit = 1.0f;       // hard edges, so the comparison is unambiguous
                fp.smoothMs = 0.1f;
                fp.invert = invert;
                // Hold the animation effectively still. Otherwise the pattern rotates
                // every frame and the gain map is a moving target, which this test is not
                // about -- the tempo-sync test below covers animation.
                fp.sync = false;
                fp.rateHz = 0.01f;
                slicer.setParams (fp);

                std::vector<float> f;

                for (int frame = 0; frame < 60; ++frame)
                {
                    f = makeFlatFrame();
                    slicer.beginFrame (512, numBins);
                    slicer.processFrame (f.data(), numBins, 0);
                }

                return f;
            };

            const auto normal = run (false);
            const auto inverted = run (true);

            // Only the bins inside the slicer's frequency range mean anything here:
            // outside it the slicer is transparent by design, so both states pass signal
            // and counting those would swamp the comparison.
            const auto binHz = static_cast<float> (sr) / static_cast<float> (fftSize);
            const auto loBin = juce::jlimit (1, numBins - 1,
                                             static_cast<int> (std::floor (60.0f / binHz)));
            const auto hiBin = juce::jlimit (loBin + 1, numBins,
                                             static_cast<int> (std::ceil (12000.0f / binHz)));

            // A band is "open" if it passes anything at all. The threshold has to be near
            // zero, not 0.5: an active band's gain is its cascade weight, which can be
            // well below 0.5 after two non-primary branches.
            int complementary = 0, both = 0;

            for (int k = loBin; k < hiBin; ++k)
            {
                const auto a = std::abs (normal[static_cast<std::size_t> (2 * k)]) > 0.01f;
                const auto b = std::abs (inverted[static_cast<std::size_t> (2 * k)]) > 0.01f;

                if (a != b) ++complementary;
                if (a && b) ++both;
            }

            expect (complementary > both,
                    "invert did not complement the pattern (" + juce::String (complementary)
                    + " complementary vs " + juce::String (both) + " shared)");
        }

        beginTest ("every pattern, depth and parameter rail stays finite and bounded");
        {
            const FractalPatternType types[] = {
                FractalPatternType::cantor, FractalPatternType::golden,
                FractalPatternType::thueMorse, FractalPatternType::sierpinski,
                FractalPatternType::lSystem
            };

            std::mt19937 rng { 31337 };
            std::uniform_real_distribution<float> dist { -1.0f, 1.0f };

            for (auto type : types)
            {
                for (int depth = 1; depth <= 7; ++depth)
                {
                    for (float rail : { 0.0f, 1.0f })
                    {
                        for (auto size : { 512, 4096 })
                        {
                            FractalSlicer slicer;
                            slicer.prepare (2, StftEngine::maxFftSize, sr);
                            slicer.setFftSize (size, sr);

                            const auto bins = size / 2 + 1;

                            FractalSlicer::Params fp;
                            fp.type = type;
                            fp.depth = depth;
                            fp.seed = depth * 7;
                            fp.ratio = rail;
                            fp.asym = rail * 2.0f - 1.0f;
                            fp.loHz = rail > 0.5f ? 20.0f : 5000.0f;
                            fp.hiHz = rail > 0.5f ? 20000.0f : 6000.0f;
                            fp.invert = rail > 0.5f;
                            fp.shatter = rail;
                            fp.gate = rail;
                            fp.grit = rail;
                            fp.smoothMs = rail > 0.5f ? 50.0f : 0.1f;
                            fp.mix = rail;
                            fp.sync = rail > 0.5f;
                            fp.divIndex = depth;
                            fp.rateHz = rail * 50.0f;
                            slicer.setParams (fp);
                            slicer.setTransport (rail > 0.5f ? 200.0 : 60.0, true);

                            for (int frame = 0; frame < 24; ++frame)
                            {
                                std::vector<float> f (static_cast<std::size_t> (2 * bins));

                                for (auto& v : f)
                                    v = dist (rng);

                                slicer.beginFrame (size / 4, bins);
                                slicer.processFrame (f.data(), bins, 0);
                                slicer.processFrame (f.data(), bins, 1);

                                for (auto v : f)
                                {
                                    if (! std::isfinite (v))
                                    {
                                        expect (false, "non-finite output: pattern "
                                                + juce::String (static_cast<int> (type))
                                                + " depth " + juce::String (depth)
                                                + " size " + juce::String (size));
                                        return;
                                    }

                                    // The slicer only attenuates and shifts; it must never
                                    // amplify beyond the input's range.
                                    if (std::abs (v) > 1.5f)
                                    {
                                        expect (false, "slicer amplified past input range: "
                                                       + juce::String (v));
                                        return;
                                    }
                                }
                            }

                            expect (slicer.getEffectiveDepth() <= depth,
                                    "effective depth exceeded the request");
                            expect (slicer.getNumBands() > 0, "no bands built");
                        }
                    }
                }
            }
        }

        beginTest ("gain changes are slew-limited, so gate transitions cannot click");
        {
            FractalSlicer slicer;
            slicer.prepare (2, StftEngine::maxFftSize, sr);
            slicer.setFftSize (fftSize, sr);

            FractalSlicer::Params fp;
            fp.type = FractalPatternType::cantor;
            fp.depth = 2;
            fp.shatter = 1.0f;
            fp.smoothMs = 25.0f;   // deliberately slow
            fp.grit = 0.0f;
            slicer.setParams (fp);

            // First processed frame must not jump straight to full attenuation.
            auto f = makeFlatFrame();
            slicer.beginFrame (512, numBins);
            slicer.processFrame (f.data(), numBins, 0);

            const auto first = frameEnergy (f);
            const auto flat = frameEnergy (makeFlatFrame());

            expect (first > flat * 0.5,
                    "first frame attenuated too abruptly for a 25 ms slew (energy ratio "
                    + juce::String (first / flat, 3) + ")");

            // After plenty of frames it should have arrived.
            for (int i = 0; i < 200; ++i)
            {
                f = makeFlatFrame();
                slicer.beginFrame (512, numBins);
                slicer.processFrame (f.data(), numBins, 0);
            }

            expect (frameEnergy (f) < first,
                    "slew never reached the target attenuation");
        }

        beginTest ("tempo sync and free rate both advance the pattern");
        {
            for (bool sync : { true, false })
            {
                FractalSlicer slicer;
                slicer.prepare (2, StftEngine::maxFftSize, sr);
                slicer.setFftSize (fftSize, sr);

                FractalSlicer::Params fp;
                fp.type = FractalPatternType::thueMorse;
                fp.depth = 4;
                fp.shatter = 1.0f;
                fp.grit = 1.0f;
                fp.smoothMs = 0.1f;
                fp.sync = sync;
                fp.divIndex = 11;      // 1/16
                fp.rateHz = 8.0f;
                slicer.setParams (fp);
                slicer.setTransport (128.0, true);

                // Collect gain patterns over time; they must not all be identical.
                std::vector<double> energies;

                for (int frame = 0; frame < 120; ++frame)
                {
                    auto f = makeFlatFrame();
                    slicer.beginFrame (512, numBins);
                    slicer.processFrame (f.data(), numBins, 0);

                    if (frame > 20)
                        energies.push_back (frameEnergy (f));
                }

                const auto lo = *std::min_element (energies.begin(), energies.end());
                const auto hi = *std::max_element (energies.begin(), energies.end());

                expect (hi - lo > 1.0e-6,
                        juce::String (sync ? "synced" : "free")
                        + " animation never changed the pattern");
            }
        }

        beginTest ("mix at 0 is transparent even with Shatter at maximum");
        {
            FractalSlicer slicer;
            slicer.prepare (2, StftEngine::maxFftSize, sr);
            slicer.setFftSize (fftSize, sr);

            FractalSlicer::Params fp;
            fp.shatter = 1.0f;
            fp.gate = 1.0f;
            fp.mix = 0.0f;
            slicer.setParams (fp);

            const auto reference = makeFlatFrame();

            for (int frame = 0; frame < 20; ++frame)
            {
                auto f = reference;
                slicer.beginFrame (512, numBins);
                slicer.processFrame (f.data(), numBins, 0);

                for (std::size_t i = 0; i < f.size(); ++i)
                    if (! juce::exactlyEqual (f[i], reference[i]))
                    {
                        expect (false, "mix 0 was not transparent");
                        return;
                    }
            }

            expect (true);
        }
    }
};

static FractalPatternTests fractalPatternTests;
static FractalSlicerTests fractalSlicerTests;

} // namespace
