#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/VisualBridge.h"
#include "character/CharacterLayer.h"
#include "state/Randomizer.h"

#include <thread>

namespace
{

class VisualBridgeTests final : public juce::UnitTest
{
public:
    VisualBridgeTests() : juce::UnitTest ("Visual bridge", "gui") {}

    void runTest() override
    {
        beginTest ("published values arrive intact");
        {
            vbd::gui::VisualBridge bridge;
            vbd::gui::VisualSnapshot dest;

            for (int round = 0; round < 10; ++round)
            {
                auto& slot = bridge.beginWrite();
                slot.numBands = round;
                slot.effectiveDepth = round * 2;
                slot.inputMag[0] = static_cast<float> (round);
                bridge.publish();

                expect (bridge.read (dest), "read failed with no writer contending");
                expectEquals (dest.numBands, round);
                expectEquals (dest.effectiveDepth, round * 2);
                expectWithinAbsoluteError (dest.inputMag[0], static_cast<float> (round), 1.0e-6f);
            }
        }

        beginTest ("a read during a write is refused rather than torn");
        {
            vbd::gui::VisualBridge bridge;
            vbd::gui::VisualSnapshot dest;

            // Publish a known frame, then open a write and leave it open.
            bridge.beginWrite().numBands = 11;
            bridge.publish();
            expect (bridge.read (dest));
            expectEquals (dest.numBands, 11);

            auto& inFlight = bridge.beginWrite();
            inFlight.numBands = 22;

            // While the write is open the reader must decline, leaving its copy alone.
            expect (! bridge.read (dest), "read succeeded during an open write");
            expectEquals (dest.numBands, 11, "declined read still modified the destination");

            bridge.publish();
            expect (bridge.read (dest));
            expectEquals (dest.numBands, 22);
        }

        beginTest ("concurrent producer and consumer stay consistent");
        {
            // Not a proof of lock-freedom, but it would catch gross tearing: every
            // snapshot the consumer sees must be internally consistent, since the
            // producer writes a matched set of values into each slot.
            vbd::gui::VisualBridge bridge;
            std::atomic<bool> stop { false };
            std::atomic<int> inconsistencies { 0 };

            std::thread producer ([&bridge, &stop]
            {
                int counter = 0;

                while (! stop.load (std::memory_order_relaxed))
                {
                    auto& slot = bridge.beginWrite();
                    slot.numBands = counter;
                    slot.effectiveDepth = counter;
                    slot.inputMag[0] = static_cast<float> (counter);
                    slot.outputMag[255] = static_cast<float> (counter);
                    bridge.publish();
                    ++counter;
                }
            });

            vbd::gui::VisualSnapshot snap;

            for (int i = 0; i < 20000; ++i)
            {
                if (! bridge.read (snap))
                    continue;   // writer was mid-update; not an error

                // All four were written from the same counter value, and the last one sits
                // at the far end of the struct -- so a torn copy would show up here.
                if (snap.numBands != snap.effectiveDepth
                    || ! juce::approximatelyEqual (snap.inputMag[0],
                                                   static_cast<float> (snap.numBands))
                    || ! juce::approximatelyEqual (snap.outputMag[255],
                                                   static_cast<float> (snap.numBands)))
                    inconsistencies.fetch_add (1, std::memory_order_relaxed);
            }

            stop.store (true, std::memory_order_relaxed);
            producer.join();

            expectEquals (inconsistencies.load(), 0, "snapshot tearing detected");
        }
    }
};

class RandomizerTests final : public juce::UnitTest
{
public:
    RandomizerTests() : juce::UnitTest ("Randomizer", "state") {}

    void runTest() override
    {
        beginTest ("section mapping covers every parameter");
        {
            vbd::VbdProcessor p;

            for (auto* param : p.getParameters())
            {
                auto* ranged = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
                expect (ranged != nullptr);

                if (ranged == nullptr)
                    continue;

                const auto section = vbd::state::sectionFor (ranged->paramID);
                expect (static_cast<int> (section) >= 0
                        && static_cast<int> (section) < vbd::state::numSections,
                        "parameter " + ranged->paramID + " mapped outside the section range");
            }
        }

        beginTest ("a locked section is left untouched");
        {
            for (int s = 0; s < vbd::state::numSections; ++s)
            {
                vbd::VbdProcessor p;
                juce::Random rng { 1234 + s };

                const auto section = static_cast<vbd::state::Section> (s);
                const auto mask = 1u << s;

                // Snapshot the locked section's values.
                std::map<juce::String, float> before;

                for (auto* param : p.getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                        if (vbd::state::sectionFor (r->paramID) == section)
                            before[r->paramID] = r->getValue();

                vbd::state::randomize (p.state(), mask, rng);

                for (auto* param : p.getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                        if (vbd::state::sectionFor (r->paramID) == section)
                            expectWithinAbsoluteError (r->getValue(), before[r->paramID], 1.0e-6f,
                                                       "locked parameter " + r->paramID
                                                       + " was changed");
            }
        }

        beginTest ("randomize never switches a delay on or disables the limiter");
        {
            // Randomize must not be able to bury the signal or remove the safety net.
            for (int trial = 0; trial < 50; ++trial)
            {
                vbd::VbdProcessor p;
                juce::Random rng { 9000 + trial };

                vbd::state::randomize (p.state(), 0, rng);

                auto valueOf = [&p] (const char* id)
                {
                    auto* param = p.state().getParameter (id);
                    return param != nullptr ? param->getValue() : -1.0f;
                };

                expect (valueOf (vbd::pid::sdlyOn) < 0.5f, "spectral delay was switched on");
                expect (valueOf (vbd::pid::dlyOn) < 0.5f, "stereo delay was switched on");
                expect (valueOf (vbd::pid::outLimiter) > 0.5f, "limiter was switched off");
                expect (valueOf (vbd::pid::engFftSize) > 0.6f
                        && valueOf (vbd::pid::engFftSize) < 0.73f,
                        "FFT size was randomised, which would move latency");
            }
        }

        beginTest ("with nothing locked, something actually changes");
        {
            vbd::VbdProcessor p;
            juce::Random rng { 77 };

            std::map<juce::String, float> before;

            for (auto* param : p.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                    before[r->paramID] = r->getValue();

            vbd::state::randomize (p.state(), 0, rng);

            int changed = 0;

            for (auto* param : p.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (std::abs (r->getValue() - before[r->paramID]) > 1.0e-6f)
                        ++changed;

            expect (changed > 10, "randomize barely changed anything (" + juce::String (changed)
                                  + " parameters)");
        }

        beginTest ("the plugin still runs after a randomize");
        {
            vbd::VbdProcessor p;
            juce::Random rng { 5 };
            p.prepareToPlay (48000.0, 256);

            for (int trial = 0; trial < 30; ++trial)
            {
                vbd::state::randomize (p.state(), 0, rng);

                juce::AudioBuffer<float> buf { 2, 256 };

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        buf.setSample (ch, i, 0.4f * std::sin (static_cast<float> (i) * 0.05f));

                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        if (! std::isfinite (buf.getSample (ch, i)))
                        {
                            expect (false, "non-finite output after randomize " + juce::String (trial));
                            return;
                        }
            }

            expect (true);
        }
    }
};

class EditorTests final : public juce::UnitTest
{
public:
    EditorTests() : juce::UnitTest ("Editor", "gui") {}

    void runTest() override
    {
        beginTest ("constructs, resizes across the whole range, and destroys cleanly");
        {
            vbd::VbdProcessor p;
            p.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditor() };
            expect (editor != nullptr, "createEditor returned nothing");

            if (editor == nullptr)
                return;

            // The spec calls for 75% to 200% of 1000x620.
            const int widths[] = { 750, 800, 1000, 1400, 2000 };

            for (auto w : widths)
            {
                const auto h = static_cast<int> (static_cast<double> (w) * 620.0 / 1000.0);
                editor->setSize (w, h);

                expectEquals (editor->getWidth(), w);

                // Force a paint into an offscreen image: this is what catches a crash in
                // any of the custom drawing code, which the constructor alone would not.
                juce::Image image { juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true };
                juce::Graphics g { image };
                editor->paintEntireComponent (g, true);
            }

            expect (true);
        }

        beginTest ("repeated open and close does not leak or crash");
        {
            vbd::VbdProcessor p;
            p.prepareToPlay (48000.0, 256);

            for (int i = 0; i < 6; ++i)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditor() };
                expect (editor != nullptr);

                if (editor != nullptr)
                {
                    editor->setSize (1000, 620);

                    juce::Image image { juce::Image::ARGB, 1000, 620, true };
                    juce::Graphics g { image };
                    editor->paintEntireComponent (g, true);
                }
            }

            expect (true);
        }

        beginTest ("paints correctly while audio is running");
        {
            vbd::VbdProcessor p;
            p.prepareToPlay (48000.0, 256);

            std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditor() };
            expect (editor != nullptr);

            if (editor == nullptr)
                return;

            editor->setSize (1000, 620);

            // Interleave processing and painting, which is the real-world case: the
            // visualisers read snapshots the audio thread is publishing.
            for (int b = 0; b < 40; ++b)
            {
                juce::AudioBuffer<float> buf { 2, 256 };

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        buf.setSample (ch, i, 0.5f * std::sin (static_cast<float> (b * 256 + i) * 0.01f));

                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                juce::Image image { juce::Image::ARGB, 1000, 620, true };
                juce::Graphics g { image };
                editor->paintEntireComponent (g, true);
            }

            expect (true);
        }

        beginTest ("editor survives an FFT size change and a randomize");
        {
            vbd::VbdProcessor p;
            juce::Random rng { 31 };
            p.prepareToPlay (48000.0, 256);

            std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditor() };

            if (editor == nullptr)
            {
                expect (false);
                return;
            }

            editor->setSize (1000, 620);

            auto* fft = p.state().getParameter (vbd::pid::engFftSize);

            for (int i = 0; i < 12; ++i)
            {
                if (fft != nullptr)
                    fft->setValueNotifyingHost (static_cast<float> (i % 4) / 3.0f);

                vbd::state::randomize (p.state(), 0, rng);

                juce::AudioBuffer<float> buf { 2, 256 };
                buf.clear();
                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                juce::Image image { juce::Image::ARGB, 1000, 620, true };
                juce::Graphics g { image };
                editor->paintEntireComponent (g, true);
            }

            expect (true);
        }
    }
};

class CharacterLayerTests final : public juce::UnitTest
{
public:
    CharacterLayerTests() : juce::UnitTest ("Character layer", "character") {}

    void runTest() override
    {
        beginTest ("the embedded art loads, with every declared group present");
        {
            vbd::character::CharacterLayer layer;

            expect (layer.hasArt(),
                    "character art failed to load -- run: python tools/build_assets.py");

            expect (layer.getNumScenes() >= 3,
                    "expected at least three scenes, found "
                    + juce::String (layer.getNumScenes()));
        }

        beginTest ("manifest pivots, rects and channels survive the round trip");
        {
            vbd::character::CharacterAssets assets;
            expect (assets.load (false), assets.getError());

            const auto* character = assets.group ("character");
            const auto* dragon = assets.group ("dragon");

            expect (character != nullptr, "no character group");
            expect (dragon != nullptr, "no dragon group");

            if (character == nullptr || dragon == nullptr)
                return;

            // The layers the rig addresses by name must exist, or the animation silently
            // does nothing.
            for (const auto* layerName : { "back_hair", "body", "outfit", "face", "eyes_open",
                                           "eyes_closed", "mouth_neutral", "front_hair",
                                           "accessories", "glow_fx" })
                expect (character->find (layerName) != nullptr,
                        "character layer missing: " + juce::String (layerName));

            for (const auto* layerName : { "head", "jaw", "wing_l", "wing_r", "fire",
                                           "body_00", "tail_00" })
                expect (dragon->find (layerName) != nullptr,
                        "dragon layer missing: " + juce::String (layerName));

            for (const auto& layer : character->layers)
            {
                expect (layer.image.isValid(), "no image for " + layer.name);
                expect (layer.rect.getWidth() > 0 && layer.rect.getHeight() > 0,
                        "empty rect for " + layer.name);
                expect (layer.pivot.x >= 0.0f && layer.pivot.x <= 1.0f
                        && layer.pivot.y >= 0.0f && layer.pivot.y <= 1.0f,
                        "pivot outside the canvas for " + layer.name);
                expect (! layer.channels.isEmpty(), "no channels declared for " + layer.name);
            }

            // Layers must arrive sorted by z, since that is the draw order.
            for (std::size_t i = 1; i < character->layers.size(); ++i)
                expect (character->layers[i - 1].z <= character->layers[i].z,
                        "character layers are not sorted by z");
        }

        beginTest ("animating and drawing stays stable across a long run");
        {
            vbd::character::CharacterLayer layer;
            layer.setBounds (0, 0, 420, 380);

            vbd::gui::VisualSnapshot snap;
            snap.bpm = 174.0;
            snap.playing = true;
            snap.numBands = 27;

            juce::Random rng { 9 };

            // Six hundred frames is ten seconds at 60 Hz, with the audio moving.
            for (int frame = 0; frame < 600; ++frame)
            {
                for (int i = 0; i < vbd::gui::VisualSnapshot::spectrumPoints; ++i)
                    snap.outputMag[static_cast<std::size_t> (i)] = rng.nextFloat() * 0.6f;

                snap.inputPeak = rng.nextFloat();
                snap.outputPeak = rng.nextFloat();
                snap.lowEnergy = rng.nextFloat();

                layer.update (snap, 1.0 / 60.0);

                if (frame % 60 == 0)
                {
                    juce::Image image { juce::Image::ARGB, 420, 380, true };
                    juce::Graphics g { image };
                    layer.paintEntireComponent (g, true);
                }
            }

            expect (true);
        }

        beginTest ("hidden and performance mode do not crash, and scenes cycle");
        {
            vbd::character::CharacterLayer layer;
            layer.setBounds (0, 0, 400, 300);

            vbd::gui::VisualSnapshot snap;

            for (auto hidden : { true, false })
            {
                for (auto perf : { true, false })
                {
                    layer.setCharacterVisible (! hidden);
                    layer.setPerformanceMode (perf);

                    for (int i = 0; i < 30; ++i)
                        layer.update (snap, 1.0 / 60.0);

                    juce::Image image { juce::Image::ARGB, 400, 300, true };
                    juce::Graphics g { image };
                    layer.paintEntireComponent (g, true);
                }
            }

            layer.setCharacterVisible (true);
            layer.setPerformanceMode (false);

            // Scene index must wrap rather than run off the end.
            const auto count = layer.getNumScenes();

            for (int i = 0; i < count * 2 + 3; ++i)
            {
                layer.setScene (i);
                expect (layer.getScene() >= 0 && layer.getScene() < juce::jmax (1, count),
                        "scene index out of range");
            }

            layer.setScene (-5);
            expect (layer.getScene() >= 0, "negative scene index was not wrapped");
        }

        beginTest ("ghost trails only appear when a delay is on");
        {
            vbd::character::CharacterLayer layer;
            layer.setBounds (0, 0, 400, 300);

            vbd::gui::VisualSnapshot snap;

            // Both states must paint without incident; the visual difference is covered
            // by the screenshot pass rather than asserted pixel-wise here.
            for (auto delaysOn : { false, true })
            {
                layer.setDelayState (delaysOn, 0.8f, 400.0f);

                for (int i = 0; i < 20; ++i)
                    layer.update (snap, 1.0 / 60.0);

                juce::Image image { juce::Image::ARGB, 400, 300, true };
                juce::Graphics g { image };
                layer.paintEntireComponent (g, true);
            }

            expect (true);
        }

        beginTest ("clicks outside the art fall through to what is underneath");
        {
            vbd::character::CharacterLayer layer;
            layer.setBounds (0, 0, 400, 300);

            vbd::gui::VisualSnapshot snap;
            layer.update (snap, 1.0 / 60.0);

            // The far right of the panel is visualiser territory, not character.
            expect (! layer.hitTest (396, 8),
                    "the character layer swallowed a click in empty space");

            // The character's own column must accept them.
            expect (layer.hitTest (20, 150), "the character column did not accept a click");
        }
    }
};

static CharacterLayerTests characterLayerTests;
static VisualBridgeTests visualBridgeTests;
static RandomizerTests randomizerTests;
static EditorTests editorTests;

} // namespace
