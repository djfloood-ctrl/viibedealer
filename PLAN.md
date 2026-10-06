# VIIBEDEALER — Implementation Plan

Spectral vocoder / fractal spectral slicer for color bass.
C++20 · JUCE 8.0.15 · CMake · VST3 + Standalone (+ AU on macOS)

Status: **awaiting approval. No implementation code written yet.**

---

## 0. Environment report

Verified on this machine, 2026-10-06.

| Item | Found | Verdict |
|---|---|---|
| OS | Windows 11 Home, build 10.0.26200.9457, x64 | OK |
| Visual Studio | 2022 Community 17.14.36 | OK |
| MSVC toolset | 14.44.35207 (`cl.exe` Hostx64/x64) | OK |
| Windows SDK | 10.0.26100.0 | OK |
| CMake | 4.4.3 | OK (JUCE 8 needs >= 3.22) |
| Git | 2.54.0.windows.1, network reach to JUCE repo confirmed | OK — FetchContent viable |
| C++20 toolchain | configure + build + run of a real `std::span`/`concepts` binary | **passed** |
| CPU / RAM | Ryzen AI 9 365, 10C/20T, 31.1 GB | OK |
| GPU | Radeon 880M (integrated) | drives renderer choice, see 2.2 |
| Disk free (C:) | 64.7 GB | OK, not roomy. JUCE + two build configs ~3-5 GB |
| DAW hosts | REAPER x64, FL Studio (Image-Line) | OK for live testing |
| VST3 install dir | `C:\Program Files\Common Files\VST3` exists | OK |
| Ninja | **1.13.2, installed this session** via winget | OK — primary generator |
| Blender | **5.2.0 LTS**, `C:\Program Files\Blender Foundation\Blender 5.2` | OK — drives the art pipeline, see 1.11 |
| Blender RGBA sprite output | smoke-tested: EEVEE + `film_transparent` -> PNG color type 6 | **passed** |
| Blender Cycles | **not registered**, even forcing `addon_utils.enable('cycles')` | irrelevant here, see 1.11 note |
| Pillow | 12.3.0 | OK — trim / pivot derivation / `@2x` pairing |
| ffmpeg | 8.1.1 | OK (not needed by the plugin; present for art previews) |
| pluginval | **absent** | downloaded in Phase A per workflow step 3 |
| Xcode / macOS | n/a | **AU is write-only here** — see 0.1 |
| Linux toolchain | n/a | CMake paths written but unexercised |

Nothing required is missing.

### 0.1 Honest limits of this machine

- **AU cannot be built or tested here.** `juce_add_plugin` will list `AU` only when
  `APPLE`, so the Mac build is correct-by-construction but unverified. It stays
  unverified until you build on a Mac. I will not claim AU works.
- **Linux is in the same position.** The dependency package list goes in README.md
  as instructions, not as something I validated.
- Everything else — VST3, Standalone, pluginval, unit tests, REAPER/FL load tests —
  is fully testable here and will actually be run, not assumed.

### 0.2 Layout decision

`C:\FLOOD SANDBOX` is already your `mgfx` Blender pipeline, and **`build/` there holds
its rendered frames and `master.mp4`**. The plugin therefore lives in its own
subfolder with its own git repo and its own build tree name:

- project root: `C:\FLOOD SANDBOX\viibedealer\`
- build trees:  `viibedealer\build-ninja\` (primary, fast iteration) and
  `viibedealer\build-vs\` (Visual Studio generator, for IDE debugging). **Never `build/`.**
- `git init` scoped to `viibedealer\` only. `mgfx` is untouched.

Ninja is now installed, so Ninja Multi-Config is the default generator and the VS generator
stays available for stepping through DSP in the debugger.

### 0.3 Identity constants

| Thing | Value |
|---|---|
| Product name | `VIIBEDEALER` |
| CMake target | `viibedealer` |
| Company | `FLOOD` |
| Plugin manufacturer code | `Flod` |
| Plugin code | `Vbdl` |
| Bundle id | `com.flood.viibedealer` |
| C++ class prefix | `Vbd` |
| Namespace | `vbd` |

---

## 1. Technical review of the spec

You asked me to flag anything ambiguous or unwise rather than guess. Eleven items.
**(A)** = I proceed this way unless you object. **(Q)** = I want your call.

### 1.1 (A) "Phase Morph: modulator magnitudes with blended phases"

Linearly blending two phase values is wrong — phase is circular, so blending 0.1pi and
1.9pi lands on pi, the opposite of both inputs. That produces cancellation and warble,
not a morph.

**Proposal:** blend the *phase gradient* (instantaneous frequency, i.e. the frame-to-frame
phase increment) and integrate it — the phase-vocoder-correct approach. Blending on the
unit circle (`atan2` of summed unit vectors) is also implemented, and I expose the choice
as a **Phase Lock** sub-control instead of two separate modes. As specced, the mode would
simply sound broken.

### 1.2 (A) Overlap-add normalization, stated concretely

Hann analysis + Hann synthesis (WOLA) at hop = N/4. The sum of squared periodic Hann at
75% overlap is exactly **1.5**, so synthesis gain is **2/3**. I use a window *pair* rather
than analysis-only (which needs 0.5 and assumes an unmodified spectrum) because every mode
here modifies magnitudes heavily, and WOLA degrades far more gracefully. A unit test
asserts passthrough unity gain within 0.01 dB at every FFT size.

### 1.3 (A) Latency is one full FFT frame, and the dry path must match exactly

Reported latency = **N samples** (the FFT size), re-reported via `setLatencySamples` on FFT
change. The exact figure is confirmed by an impulse test in Phase B, not taken on faith.
The dry path runs through a plain integer delay of the same length, so mix is phase-coherent
and `mix = 0` is a true null.

Caveat worth knowing up front: changing FFT size mid-playback changes latency, and **some
hosts only re-query at a block boundary while others ignore it until the plugin is
reloaded**. That is inherent to selectable FFT size, not something I can fix. Mitigation: a
short crossfade through the switch so it does not click, plus a note in README.

### 1.4 (A) Fractal depth must be clamped to bin resolution

Depth 7 asks for up to 128 subdivisions. At FFT 512 there are only 257 bins, and a narrow
Lo/Hi range makes it far worse — bands would be sub-bin and the pattern would collapse into
aliased noise.

**Proposal:** keep the 1–7 parameter range, but compute an *effective depth* of
`min(depth, floor(log2(binsInRange / 2)))` — at least 2 bins per band — and display the
clamped value in the GUI ("Depth 7 -> 5") so the limit is visible rather than silently
swallowed.

### 1.5 (A) Spectral Delay: parameter in ms, storage in frames, hard frame cap

Per-bin delay at FFT 4096 with 2 s of range at 192 kHz needs ~375 frames x 2049 bins x 2 ch
x 2 floats = **~12.6 MB**, preallocated for the worst case, per instance. Too fat for
"several instances".

**Proposal:** cap the ring at **128 frames** (~4.2 MB preallocated), expose the parameter in
**ms**, and convert with `frames = clamp(round(ms * sr / hop), 1, 128)`. The consequence to
be aware of: at a given ms setting the *achievable* maximum shrinks as FFT size drops or
sample rate rises — 128 frames is 2.97 s at 4096/44.1k but 0.17 s at 512/192k. The GUI
readout shows the actual resolved time, so the ceiling is legible instead of mysterious.

### 1.6 (A) "Zero CPU when off" — what I can actually deliver

Literally zero is impossible; one branch is not free. What I will deliver: when a module is
off it is skipped entirely — no buffer traversal, no filter state updates, no per-sample
work — so its cost is a single well-predicted branch per block. Measured, not asserted, with
an A/B CPU figure in README.

Toggling also needs a **short crossfade and a state flush**, or the stale feedback tail
clicks on re-enable. Both are in the design.

### 1.7 (A) Carrier "Frequency shift" is a frequency shift, not a pitch shift

Bin-shifting is a true, inharmonic frequency shift: it breaks harmonic ratios and makes
chords go sour. That is a legitimate effect but it is *not* what people usually want on a
chord carrier, and the chord's pitch is already set by Root + Octave.

**Proposal:** keep it, label it **"Freq Shift (Hz)"**, document the inharmonic behaviour,
default 0, bipolar +/-500 Hz. I will not silently substitute a pitch shifter for it.

### 1.8 (A) Fractal band bin-shifting will sound metallic, and that is the point

Shifting a band's bins inside an STFT frame breaks phase continuity across frames, giving
metallic smear and pre-echo. For color bass that is desirable, so it stays — but it is gated
behind the upper range of the **Shatter** macro and clamped so it cannot run away. Flagged
so the character of the sound is not a surprise later.

### 1.9 (A) Sidechain carrier in Standalone, and with nothing patched

Standalone has no sidechain bus at all, and in a DAW the user can simply leave it
unconnected. Silence-as-carrier through a vocoder gives silence, which reads as "the plugin
is broken".

**Proposal:** detect an absent or silent sidechain and **auto-fall-back to the Chord
Carrier**, with a visible amber badge on the CARRIER panel saying so.
`isBusesLayoutSupported` accepts the sidechain bus disabled, and the plugin is fully
functional in that state.

### 1.10 (A) Cepstral envelope is the expensive option, so it is not the default

True cepstral liftering costs an extra forward + inverse FFT per frame per channel, roughly
doubling engine cost.

**Proposal:** Envelope Resolution stays one continuous control with two backends — cheap
log-domain / Mel-band smoothing (default) and cepstral (opt-in toggle) — so the CPU budget
for "several instances at 2048" is met by default and the expensive path is a deliberate
choice.

### 1.11 (RESOLVED) Placeholder art comes from Blender

**Decided: Blender-rendered placeholders.** Verified working on this machine before being
promised — EEVEE with `film_transparent` produces true RGBA PNGs (color type 6, alpha
intact), headless, in a subprocess.

This is a clear upgrade on flat Pillow silhouettes: real cel shading via a two-step
emission/diffuse ramp, a genuine glowing rim light from a back-facing area lamp, soft
gradient falloff, and consistent perspective across every layer because they all render
from one orthographic camera in one scene. The placeholders will actually read as cel-shaded
character art in the neon palette rather than as flat blocks.

**Architecture — rendering and running are separate concerns.** Blender is a *build-time
asset tool only*. It is never a plugin dependency, is not required to compile, and the
committed PNGs are what the plugin embeds. Anyone cloning the repo builds fine without
Blender installed.

```
tools/
  build_assets.py            orchestrator, runs OUTSIDE Blender
  blender/
    vbd_compat.py            vendored Blender 5.2 API shims (see note below)
    vbd_rig.py               builds the character + dragon as separated layer objects
    vbd_look.py              cel ramp, rim light, neon palette, per-scene backdrops
    render_layers.py         runs INSIDE Blender: one layer -> one RGBA PNG
  postprocess.py             Pillow: alpha trim, pivot derivation, @2x pairing, manifest
```

Pipeline, one command (`python tools/build_assets.py`):

1. One Blender subprocess per layer group, mirroring `mgfx`'s own structure so one failed
   layer cannot take down the set and finished PNGs survive an interrupt.
2. Each layer renders in isolation with every sibling layer hidden, from a single shared
   orthographic camera — so all layers share one canvas and one pixel grid, which is
   exactly what the rig's pivot math needs.
3. Rendered at `@2x` first, then Pillow derives the 1x by downscale (sharper than rendering
   twice, and guarantees the two are perfectly registered).
4. Pillow computes each layer's **tight alpha bounding box and pivot** and writes those
   measured values into `assets/manifest.json`, so pivots are derived from the art rather
   than hand-guessed.
5. Structured results come back on `VBD_INFO <json>` sentinel lines, and a `Traceback`
   on stdout is treated as failure — **because Blender exits 0 even when an embedded script
   raises**, a trap your `render.py` already documents.

**Convention reuse, without a cross-repo dependency.** Your `mgfx/compat.py` has the
Blender 5.2 API migrations already probe-verified (EEVEE registered as `BLENDER_EEVEE`,
`scene.compositing_node_group` replacing `Scene.node_tree`, MENU sockets set by display
name). I **vendor** a trimmed copy into `tools/blender/vbd_compat.py` with an attribution
comment rather than importing across `..\mgfx`, because the plugin repo must stay
self-contained and cloneable. `mgfx` is not modified or imported.

**Two notes.** Cycles is not registered in this Blender install — I could not enable it even
via `addon_utils.enable('cycles')`, which means `mgfx`'s own `--engine cycles` flag would
also fail here. It does not matter for this work: EEVEE is the right engine for flat cel
sprites, and path tracing would actively fight the look. Separately, the goal is unchanged —
these are *placeholders*. Replacing them with a real illustrator's PSD export must stay a
pure file swap with **zero code change**, so nothing in the rig may depend on how the art
was produced. `ASSETS.md` is written for a human illustrator, not for the Blender script.

---

## 2. Architecture

### 2.1 Signal flow

```
                  +------------ MIDI in -----------+
                  v                                |
  main in --> [modulator tap]                       |
     |                                              v
     |        carrier select --> Sidechain in --+
     |                       --> ChordCarrier ---+   (poly osc stack)
     |                       --> Self (= main) --+
     |                                           v
     |                                +----------------------+
     +- dry tap -> LatencyDelay --+   | STFTEngine (N, N/4)  |
     |             (N samples)    |   |  Hann WOLA, gain 2/3 |
     |                            |   +----------+-----------+
     |                            |              v
     |                            |       SpectralEngine
     |                            |    (vocode / magmorph /
     |                            |     phasemorph / cross,
     |                            |     formant, tilt, freeze,
     |                            |     flip, freq shift)
     |                            |              v
     |                            |       FractalSlicer
     |                            |    (pattern -> band table,
     |                            |     time rotate, rhythm gate,
     |                            |     shatter, grit)
     |                            |              v
     |                            |       SpectralDelay   [optional, OFF default]
     |                            |              v
     |                            |         iFFT + OLA
     |                            v              v
  [StereoDelay, pre placement] --> ColorStage (sat, width, locut, hicut)
                                               v
                                  [StereoDelay, post placement, default]
                                               v
                                     dry/wet mix <-- LatencyDelay
                                               v
                                    out gain -> safety limiter -> out
```

### 2.2 Renderer

One `RenderBackend` abstraction, chosen per platform:

- **Windows: Direct2D** — JUCE 8's Windows renderer is Direct2D-backed and GPU-accelerated.
  Attaching a `juce::OpenGLContext` *overrides* it and the two are mutually exclusive; on
  the Radeon 880M, Direct2D is both faster and free of OpenGL driver quirks.
- **macOS: CoreGraphics/Metal** (JUCE default).
- **Linux: `juce::OpenGLContext`**, where it is the real win.

### 2.3 Repo layout

```
viibedealer/
  CMakeLists.txt              JUCE 8.0.15 via FetchContent (pinned tag)
  PLAN.md  README.md  ASSETS.md  .gitignore  .clang-format
  cmake/
    Warnings.cmake            /W4 /WX ; -Wall -Wextra -Wpedantic -Werror
    JUCEFetch.cmake
  src/
    Identity.h                the table in 0.3, single source of truth
    Params.h / Params.cpp     ParamIDs, createParameterLayout(), ranges + units
    PluginProcessor.h/.cpp    VbdProcessor
    PluginEditor.h/.cpp       VbdEditor
    dsp/
      Utils.h                 denormal + NaN scrub, smoothers, dB, tempo divisions
      STFTEngine.h/.cpp       windowing, hop scheduling, OLA, latency math
      SpectralFrame.h         non-owning POD view over mag/phase spans
      SpectralEnvelope.h/.cpp Mel/log smoothing + opt-in cepstral backend
      SpectralEngine.h/.cpp   four modes + formant/tilt/freeze/flip/shift
      ChordCarrier.h/.cpp     poly osc stack, chord tables, MIDI override
      FractalPattern.h/.cpp   PURE: pattern -> BandTable. no RT alloc, unit-tested
      FractalSlicer.h/.cpp    applies BandTable, animation, gate, grit
      SpectralDelay.h/.cpp    128-frame bin-domain ring, per-band time/fb/damp
      StereoDelay.h/.cpp      modes, loop HP/LP, sat, wow, diffusion, duck, freeze
      ColorStage.h/.cpp       softclip/tube/wavefold, width, filters
      LatencyDelay.h/.cpp     integer dry-path aligner
      SafetyLimiter.h/.cpp
    gui/
      Theme.h                 palette, prism gradient, metrics
      VbdLookAndFeel.h/.cpp
      widgets/ GlowKnob GlowToggle VbdCombo ValueReadout SyncDivSelector
      FractalVisualizer.h/.cpp
      SpectrumAnalyzer.h/.cpp
      panels/ CarrierPanel EnginePanel FractalPanel DelaysPanel OutputPanel
      TopBar.h/.cpp           preset browser, A/B, randomize + per-section locks
      VisualBridge.h/.cpp     GUI-side consumer of the lock-free snapshot
    character/
      AssetManifest.h/.cpp    parses assets/manifest.json
      SpringBone.h            critically-damped spring, per-axis
      RigNode.h               z-order, pivot, channel bindings
      CharacterRig.h/.cpp     breathing, blink, hair/outfit sway, eye tracking
      DragonRig.h/.cpp        Catmull-Rom spline body, segment follow, wings
      ParticleFire.h/.cpp     fixed-capacity pool, spectrum-coloured
      GhostTrails.h/.cpp      delay-driven, disabled when delays off
      SceneBackdrop.h/.cpp    3 scenes, parallax layers
      CharacterLayer.h/.cpp   owns the above, one Component, hit-testing
    state/
      PresetManager.h/.cpp    factory table + user XML, versioned state
      Randomizer.h/.cpp       per-section locks
  assets/
    manifest.json
    character/*.png  character/@2x/*.png
    dragon/*.png     dragon/@2x/*.png
    scenes/{neon_city,shrine_dusk,cosmic_void}/*.png
  tools/
    build_assets.py           Blender art orchestrator (build-time only, see 1.11)
    postprocess.py            Pillow trim / pivot derivation / @2x / manifest emission
    blender/                  vbd_compat.py vbd_rig.py vbd_look.py render_layers.py
    get_pluginval.ps1
  tests/
    CMakeLists.txt            console target viibedealer_tests (juce::UnitTest)
    test_fractal_pattern.cpp  test_stft.cpp  test_latency.cpp
    test_robustness.cpp       test_feedback.cpp  test_params.cpp
```

### 2.4 Audio -> GUI data path

No FIFO of audio samples. Two mechanisms, both lock-free, both written only by the audio
thread and read only by the message thread:

1. **`VisualSnapshot` triple buffer.** A POD struct: 256 decimated log-spaced input bins,
   256 output bins, 256 carrier bins, 128 band energies, low-band energy, BPM, PPQ,
   effective depth, resolved delay times. The audio thread fills the spare slot and
   publishes it with one `std::atomic<int>` release-store; the GUI acquire-loads the index.
   Latest-value-wins, which is exactly what a visualizer wants, and it can neither tear nor
   block.
2. **`AbstractFifo` of discrete events** (capacity 64) for things that must not be dropped:
   input transients (fire-breath triggers), MIDI chord changes, pattern-rotation ticks.

GUI -> audio is only ever APVTS parameter writes. The character layer never touches DSP.

### 2.5 Real-time safety, enforced rather than merely intended

- All buffers sized in `prepareToPlay` for **max FFT 4096, max block, 192 kHz**. Nothing
  resizes in `processBlock`, including on FFT-size change — the largest allocation is always
  held and only a smaller active window is used.
- `juce::ScopedNoDenormals` at the top of `processBlock`.
- No `juce::String`, no `std::vector` growth, no `new`, no `malloc`, no locks, no file I/O,
  no `DBG`/logging on the audio thread. Phase F adds a debug-only RT assert hook that trips
  on any allocation inside `processBlock`, so regressions are caught mechanically rather
  than by eyeballing diffs.
- Every feedback loop: `tanh` soft-saturate, DC blocker, `std::isfinite` scrub to 0, hard
  clamp to +/-4.0. The feedback parameter tops out at 0.98; Freeze is input-mute plus 1.0
  recirculation through the saturator, never a gain above 1.
- Parameter reads via cached `std::atomic<float>*` fed into `SmoothedValue` — linear for
  gains, multiplicative for frequencies. Every continuous parameter smoothed.

### 2.6 Parameters (APVTS)

~62 automatable parameters, grouped, all with units and clear names.

**CARRIER** `carrier.source` (Sidechain/Chord/Self) · `chord.osc`
(Supersaw/Saw/Square/Noise) · `chord.root` (note) · `chord.type`
(Maj/Min/Maj7/Min9/Sus2/Add9/Power/Custom) · `chord.octave` (-2..+2) · `chord.spread` (%)
· `chord.detune` (cents) · `chord.level` (dB) · `chord.midiOverride` (bool)

**ENGINE** `eng.fftSize` (512/1024/2048/4096) · `eng.mode`
(Vocode/MagMorph/PhaseMorph/Cross) · `eng.morph` (%) · `eng.formant` (semitones, +/-24)
· `eng.tilt` (dB/oct, +/-6) · `eng.envRes` (%) · `eng.envCepstral` (bool)
· `eng.phaseLock` (%) · `eng.sens` (dB) · `eng.freeze` (bool) · `eng.flip` (bool)
· `eng.freqShift` (Hz, +/-500)

**FRACTAL** `frac.pattern` (Cantor/Golden/ThueMorse/Sierpinski/LSystem) · `frac.depth`
(1..7) · `frac.seed` (int) · `frac.ratio` (%) · `frac.asym` (%) · `frac.loHz` (Hz, log)
· `frac.hiHz` (Hz, log) · `frac.invert` (bool) · `frac.shatter` (%) · `frac.sync` (bool)
· `frac.div` (1/1..1/64, dotted + triplet) · `frac.rateHz` (Hz) · `frac.gate` (%)
· `frac.grit` (%) · `frac.smooth` (ms) · `frac.mix` (%)

**SPECTRAL DELAY** `sdly.on` (bool, **off**) · `sdly.timeMs` · `sdly.spread` (%)
· `sdly.fb` (%) · `sdly.damp` (%) · `sdly.mix` (%)

**STEREO DELAY** `dly.on` (bool, **off**) · `dly.mode` (Stereo/PingPong/Dual)
· `dly.place` (Pre/**Post**) · `dly.sync` (bool) · `dly.divL` · `dly.divR` · `dly.msL`
· `dly.msR` · `dly.fb` (%) · `dly.hp` (Hz) · `dly.lp` (Hz) · `dly.sat` (%)
· `dly.modRate` (Hz) · `dly.modDepth` (%) · `dly.diffuse` (%) · `dly.duck` (%)
· `dly.freeze` (bool) · `dly.width` (%) · `dly.mix` (%)

**OUTPUT** `col.satType` (SoftClip/Tube/Wavefold) · `col.drive` (dB) · `col.width` (%)
· `col.loCut` (Hz) · `col.hiCut` (Hz) · `out.mix` (%) · `out.gain` (dB)
· `out.limiter` (bool, on)

**Non-automatable, stored in the state tree** (view state, not parameters): `ui.scene`,
`ui.characterVisible`, `ui.perfMode`, `ui.tailMacroTarget`, `ui.sizeScale`, `ui.lockMask`,
plus `stateVersion = 1`.

---

## 3. Phased milestones

Each phase ends with: **builds with zero warnings** (`/WX`), **unit tests green**, and from
Phase A onward **pluginval strictness 5+ clean**. I report actual output, failures included.

### Phase A — skeleton + passthrough

CMake with pinned JUCE 8.0.15 · `/W4 /WX` + `/Zc:__cplusplus` + `/permissive-`
· VST3 + Standalone · stereo main + optional stereo sidechain + MIDI in · full APVTS
layout (2.6) wired but inert · versioned `get`/`setStateInformation` · tests target
· `tools/get_pluginval.ps1` fetches pluginval, run at strictness 5 · loads in REAPER and FL
and passes audio transparently.

**Exit:** bit-exact passthrough, pluginval clean, loads in both DAWs.

*Risk:* CMake 4.4 is newer than JUCE 8.0.15 expects. If a policy error appears, the fix is
`CMAKE_POLICY_VERSION_MINIMUM`. This gets validated first, before anything else is built.

### Phase B — spectral engine + chord carrier

STFTEngine (WOLA per 1.2) · latency reported and impulse-verified (1.3) · four modes with
the phase-gradient fix (1.1) · envelope with both backends (1.10) · formant, tilt, freeze,
flip, freq shift · ChordCarrier with chord tables, spread, detune, MIDI override
· sidechain fallback + badge (1.9).

**Exit:** latency exact to the sample, unity-gain error < 0.01 dB, vocode audibly imprints
chords on a bass growl, no NaN across all four FFT sizes.

### Phase C — fractal slicer

`FractalPattern` as a pure, heavily unit-tested function: Cantor, Golden, Thue-Morse,
Sierpinski, seeded L-system -> `BandTable` · effective-depth clamp (1.4) · band-edge
crossfades + Grit · tempo-synced rotation and free Hz · fractal rhythm gate · Shatter
(mute / level-by-depth / bin-shift per 1.8).

**Exit:** golden-value pattern tests, no clicks on pattern/depth/rate change (measured as
inter-block discontinuity below threshold), depth clamp surfaced to the GUI.

### Phase D — delays

SpectralDelay (128-frame ring per 1.5) · StereoDelay (3 modes, loop HP/LP, saturation, wow,
diffusion, ducking, freeze, pre/post) · both OFF by default with skip-entirely bypass and
enable/disable crossfade + flush (1.6).

**Exit:** feedback 0.98 + Freeze + extreme params stays bounded for 60 s (peak < 4.0, all
finite); off-state CPU measured and recorded.

### Phase E — GUI

Renderer per 2.2 · resizable with fixed aspect, 75%–200%, default 1000x620
· `VbdLookAndFeel`: glowing arc rotaries, shift-drag fine, double-click reset, mouse wheel,
hover/drag readout, lit toggles · 60 fps FractalVisualizer (nested shapes, live band glow,
smooth morph on pattern change) · SpectrumAnalyzer (in/out/carrier, log-f, smoothed)
· five panels, delay panels visibly dim when off · top bar with preset browser, A/B,
randomize + per-section locks · `VisualBridge` consuming the 2.4 snapshot.

**Exit:** 60 fps held at default size with a real session running, dirty-region repaint only,
GUI open/close clean under pluginval.

### Phase E2 — character art layer

`python tools/build_assets.py` renders every declared layer (+`@2x`) through Blender with
cel shading and neon rim light, then Pillow trims, derives measured pivots and emits the
manifest (1.11) · `manifest.json` schema: per layer `name`, `file`, `z`, `pivot`,
`channels[]`, `blend`, `parent`
· `juce_add_binary_data` embedding with HiDPI selection · character layers: back
hair, body, outfit, face, eyes (open/half/closed), mouth (3 shapes), front hair,
accessories, glow FX · dragon layers: body segments, head, jaw, L/R wings, tail segments,
fire FX · character rig: idle breathing, random blink, spring-physics hair and outfit sway,
tempo head-bob from the playhead, eye and accessory glow following the spectral colour, sway
intensity following low-end energy · dragon: spline coil around the visualizer, secondary
motion, tempo-synced wing flap, transient-triggered particle fire coloured by the spectrum,
per-band scale lighting · delay tie-in: ghost trails whose count and fade follow feedback and
time, none when delays are off · interactivity: eyes track the mouse, dragon head turns to
the cursor, click cycles expressions, double-click fires the randomizer with a reaction
animation, dragon click = roar plus Spectral Delay toggle, tail-drag along an arc drives a
selectable macro (Shatter or Morph) · 3 parallax scenes · show/hide toggle plus Performance
Mode that disables all animation · art never overlaps controls: side panels or behind the
visualizer, with a subtle dark scrim behind text.

**Exit:** zero DSP-thread contact (inspection + the RT assert hook), 60 fps with the
character on, graceful behaviour when a manifest-declared asset is missing, and a verified
clean build **with Blender absent from PATH** (proving art generation is build-time only).

### Phase F — presets, polish, optimization

18 factory presets (chord-imprinted growls, shimmering fractal plucks, glitchy stutter
basses, wide spectral-delay tails) · user presets as XML · full versioned state recall
including scene and character visibility · CPU profiling and the "several instances at
2048" measurement · pluginval raised to strictness 10 · README.md + ASSETS.md.

**Exit:** every preset recalls bit-identically, documented CPU figures, pluginval 10 clean.

---

## 4. Test matrix (run every phase, not only at the end)

| Test | Assertion |
|---|---|
| Null test | `out.mix = 0` -> output equals latency-aligned dry, error < -120 dBFS |
| Latency | impulse in, peak lands at exactly `getLatencySamples()`, all 4 FFT sizes |
| Unity gain | passthrough WOLA gain error < 0.01 dB |
| NaN/Inf | silence, white noise, full-scale sine, DC, and a 0->1 step: all output finite |
| Extremes | every parameter at min and max, plus 200 randomized full-parameter states |
| Feedback bounded | fb 0.98 + freeze + diffusion + sat for 60 s: peak < 4.0, all finite |
| Block/rate sweep | blocks 1, 7, 16, 64, 512, 2048 x rates 44.1/48/88.2/96/176.4/192k |
| Fractal patterns | golden-value band tables per pattern and depth; clamp behaviour |
| Click-free | parameter and FFT-size changes produce no inter-block discontinuity |
| State round-trip | save -> load -> save yields identical XML; version field present |
| pluginval | strictness 5 from Phase A, 10 by Phase F, zero failures |

---

## 5. Open items

1. ~~Placeholder-art scope~~ — **resolved: Blender pipeline** (1.11), verified working.
2. ~~Ninja~~ — **resolved: installed**, 1.13.2, now the default generator.
3. **CLAP** — you have a `CLAP` folder and your spec did not ask for CLAP. Not in scope; it
   is a small addition later via `clap-juce-extensions` if you want it.
4. Everything marked **(A)** in section 1 proceeds as written unless you object now.

Nothing in section 1 is a blocker. On your OK, I start at Phase A.
