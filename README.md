# VIIBEDEALER

A spectral vocoder, fractal spectral slicer and delay pair for colour bass.

Imprints chords onto a bass growl, shatters the result along self-similar band patterns,
and pours it through spectral and stereo delays — with an illustrated character layer that
moves with the audio.

**FLOOD** · C++20 · JUCE 8.0.15 · VST3 + Standalone (+ AU on macOS)

---

## Build

### Requirements

| Platform | Needs |
|---|---|
| Windows | Visual Studio 2022 with the **Desktop development with C++** workload, CMake ≥ 3.22, Git |
| macOS | Xcode command line tools (`xcode-select --install`), CMake ≥ 3.22, Git |
| Linux | gcc 11+ or clang 14+, CMake ≥ 3.22, Git, plus the JUCE dependencies below |

JUCE is fetched automatically by CMake — there is nothing to install or vendor.
**Blender is *not* required to build**: the character art is committed and embedded. It is
only needed to regenerate that art (see [ASSETS.md](ASSETS.md)).

Linux dependencies:

```
sudo apt install libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
    libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev \
    libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```

### Building

```bash
cmake -B build-ninja -G "Ninja Multi-Config" .
cmake --build build-ninja --config Release
```

On Windows, Ninja needs the MSVC environment on PATH. Dot-source the helper first:

```powershell
. .\tools\dev-shell.ps1
cmake -B build-ninja -G "Ninja Multi-Config" .
cmake --build build-ninja --config Release
```

Or use the Visual Studio generator, which finds the toolchain itself:

```powershell
cmake -B build-vs -G "Visual Studio 17 2022" -A x64 .
cmake --build build-vs --config Release
```

> Never build into `build/` in this repo's parent directory — that name belongs to the
> `mgfx` project alongside it.

### Where the built plugin lands

```
build-ninja/viibedealer_artefacts/Release/
├── VST3/VIIBEDEALER.vst3          ← a bundle FOLDER, not a single file
└── Standalone/VIIBEDEALER.exe
```

On macOS an `AU/VIIBEDEALER.component` appears alongside them.

> **AU and Linux builds are written but unverified.** Everything in this repo was developed
> and tested on Windows. The CMake paths for the other platforms are correct by
> construction, not by observation.

### Installing into a DAW

```powershell
.\tools\install_vst3.ps1              # per-user
.\tools\install_vst3.ps1 -System      # C:\Program Files\Common Files\VST3
```

Or copy the whole `VIIBEDEALER.vst3` **folder** by hand:

| Platform | VST3 | AU |
|---|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` | — |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |
| Linux | `~/.vst3/` | — |

Then rescan: **FL Studio** → Options → Manage plugins → Find more plugins.
**REAPER** → Options → Preferences → Plug-ins → VST → Re-scan.

### Packaging for someone else

```powershell
.\tools\package.ps1 -Standalone
```

Produces `dist/VIIBEDEALER-<version>-win64.zip` with the bundle, the standalone and an
`INSTALL.txt`. The MSVC runtime is linked statically, so the recipient needs no
redistributable.

### Tests

```bash
./build-ninja/tests/.../viibedealer_tests            # unit tests
./build-ninja/tests/.../viibedealer_tests --bench    # CPU benchmark
./build-ninja/tests/.../viibedealer_tests --shot out.png 1000   # render the GUI
```

`tools/get_pluginval.ps1` downloads pluginval; the plugin is validated clean at
**strictness 10**.

---

## Using it

Put it on a bass. The defaults — Chord carrier, Vocode, Morph 100%, C minor, 100% wet —
imprint a chord immediately. Play MIDI chords in and they take over the harmony.

Three things that are deliberate and might otherwise read as faults:

- **Shatter is the fractal amount.** At 0 the FRACTAL section is bypassed and nothing in
  that panel does anything. Raise it to hear the slicer.
- **Both delays are off by default** and cost nothing while off.
- **Latency is 2048 samples** at the default FFT size, reported to the host so it
  compensates automatically. Larger FFT means more latency.

### Signal flow

```
input ──┬─> [Stereo Delay, if placement = Pre]
        │          │
        │          v
        │   modulator + carrier (sidechain / chord oscillator / self)
        │          │
        │          v
        │   STFT ─> Spectral Engine ─> Fractal Slicer ─> Spectral Delay ─> iSTFT
        │          │
        │          v
        │   Colour stage (saturate, width, lo/hi cut)
        │          │
        │          v
        │   [Stereo Delay, if placement = Post — the default]
        │          │
        └─> latency-aligned dry ──┴─> mix ─> gain ─> safety limiter ─> output
```

### CPU

Measured on a Ryzen AI 9 365, 48 kHz, 512-sample blocks, stereo, Shatter at 60%:

| Configuration | Realtime factor | Per instance | Instances per core |
|---|---|---|---|
| FFT 512 | 49× | 2.04% | ~48 |
| FFT 1024 | 48× | 2.07% | ~48 |
| FFT 2048 | 44× | 2.25% | ~44 |
| FFT 4096 | 43× | 2.35% | ~42 |
| FFT 2048, both delays on | 38× | 2.64% | ~37 |

Reproduce with `viibedealer_tests --bench`.

---

## Parameter reference

All parameters are automatable and have units. IDs are stable and will not change.

### CARRIER

| Parameter | ID | Range | Default | Notes |
|---|---|---|---|---|
| Carrier Source | `carrier.source` | Sidechain / Chord / Self | Chord | Sidechain falls back to Chord when nothing is patched in, with an amber badge |
| Chord Osc | `chord.osc` | Supersaw / Saw / Square / Noise | Supersaw | polyBLEP anti-aliased |
| Chord Root | `chord.root` | C … B | C | |
| Chord Type | `chord.type` | Maj / Min / Maj7 / Min9 / Sus2 / Add9 / Power / Custom | Min | Custom is a wide minor 11 |
| Chord Octave | `chord.octave` | −2 … +2 | 0 | Root sits at C2 |
| Chord Spread | `chord.spread` | 0 … 100 % | 35 | Widens the voicing and the stereo field |
| Chord Detune | `chord.detune` | 0 … 50 cents | 12 | Unison detune |
| Chord Level | `chord.level` | −60 … +12 dB | 0 | |
| MIDI Override | `chord.midiOverride` | on / off | on | Held notes replace Root + Type |

### ENGINE

| Parameter | ID | Range | Default | Notes |
|---|---|---|---|---|
| FFT Size | `eng.fftSize` | 512 / 1024 / 2048 / 4096 | 2048 | Sets latency; changing it re-reports to the host |
| Engine Mode | `eng.mode` | Vocode / Mag Morph / Phase Morph / Cross | Vocode | |
| Morph | `eng.morph` | 0 … 100 % | 100 | |
| Formant Shift | `eng.formant` | −24 … +24 st | 0 | Resamples the spectral envelope |
| Spectral Tilt | `eng.tilt` | −6 … +6 dB/oct | 0 | Pivots at 1 kHz |
| Envelope Resolution | `eng.envRes` | 0 … 100 % | 50 | Low is broad and formant-like |
| Cepstral Envelope | `eng.envCepstral` | on / off | off | Truer formants, roughly double the engine cost |
| Phase Lock | `eng.phaseLock` | 0 … 100 % | 50 | Phase Morph only: gradient blend vs unit-circle blend |
| Sensitivity | `eng.sens` | −80 … 0 dB | −60 | Gate threshold on the modulator |
| Freeze | `eng.freeze` | on / off | off | Holds the modulator's magnitude frame |
| Flip | `eng.flip` | on / off | off | Swaps modulator and carrier |
| Freq Shift | `eng.freqShift` | −500 … +500 Hz | 0 | A true **inharmonic** shift, quantised to the bin width |

### FRACTAL

| Parameter | ID | Range | Default | Notes |
|---|---|---|---|---|
| Pattern | `frac.pattern` | Cantor / Golden / Thue-Morse / Sierpinski / L-System | Cantor | |
| Depth | `frac.depth` | 1 … 7 | 3 | Clamped by pattern and bin resolution; the resolved value is shown |
| Seed | `frac.seed` | 0 … 9999 | 1 | L-System only |
| Split Ratio | `frac.ratio` | 0 … 100 % | 50 | |
| Split Asymmetry | `frac.asym` | −100 … +100 % | 0 | Alternates the split between levels |
| Low Bound | `frac.loHz` | 20 Hz … 20 kHz | 60 | |
| High Bound | `frac.hiHz` | 20 Hz … 20 kHz | 12 k | |
| Invert | `frac.invert` | on / off | off | Complements which bands survive |
| **Shatter** | `frac.shatter` | 0 … 100 % | 0 | **The amount.** 0 is bypass; above 50% starts bin-shifting |
| Sync | `frac.sync` | on / off | on | |
| Sync Rate | `frac.div` | 1/1 … 1/64, dotted and triplet | 1/8 | |
| Free Rate | `frac.rateHz` | 0.01 … 50 Hz | 2 | Used when Sync is off |
| Rhythm Gate | `frac.gate` | 0 … 100 % | 0 | Stutters bands on the same self-similar sequence |
| Grit | `frac.grit` | 0 … 100 % | 0 | Sharpens band edges and gate transitions |
| Edge Smoothing | `frac.smooth` | 0.1 … 50 ms | 6 | |
| Fractal Mix | `frac.mix` | 0 … 100 % | 100 | |

### SPECTRAL DELAY

| Parameter | ID | Range | Default | Notes |
|---|---|---|---|---|
| On | `sdly.on` | on / off | **off** | |
| Time | `sdly.timeMs` | 1 … 3000 ms | 250 | Capped by a 128-frame ring; the resolved time is shown |
| Spread | `sdly.spread` | 0 … 100 % | 50 | How far per-band delays diverge |
| Feedback | `sdly.fb` | 0 … 100 % | 40 | Internally capped at 0.95 |
| Damping | `sdly.damp` | 0 … 100 % | 30 | Treble loss per repeat |
| Mix | `sdly.mix` | 0 … 100 % | 50 | |

### STEREO DELAY

| Parameter | ID | Range | Default | Notes |
|---|---|---|---|---|
| On | `dly.on` | on / off | **off** | |
| Mode | `dly.mode` | Stereo / Ping-Pong / Dual | Ping-Pong | |
| Placement | `dly.place` | Pre / Post | Post | Pre feeds the engine, so repeats get vocoded |
| Sync | `dly.sync` | on / off | on | |
| Div L / R | `dly.divL`, `dly.divR` | 1/1 … 1/64 | 1/8, 1/16 | |
| Time L / R | `dly.msL`, `dly.msR` | 1 … 4000 ms | 375, 500 | |
| Feedback | `dly.fb` | 0 … 100 % | 45 | |
| Loop HP / LP | `dly.hp`, `dly.lp` | 20 Hz–2 kHz / 200 Hz–20 kHz | 120, 8 k | |
| Saturation | `dly.sat` | 0 … 100 % | 20 | Drive into an always-present soft clipper |
| Mod Rate / Depth | `dly.modRate`, `dly.modDepth` | 0.01–10 Hz / 0–100 % | 0.4, 15 | Wow and chorus |
| Diffusion | `dly.diffuse` | 0 … 100 % | 25 | Four-stage allpass |
| Ducking | `dly.duck` | 0 … 100 % | 0 | Repeats duck under the input |
| Freeze | `dly.freeze` | on / off | off | Infinite hold; still bounded |
| Width | `dly.width` | 0 … 200 % | 100 | |
| Mix | `dly.mix` | 0 … 100 % | 35 | |

### OUTPUT

| Parameter | ID | Range | Default | Notes |
|---|---|---|---|---|
| Saturation Type | `col.satType` | Soft Clip / Tube / Wavefold | Soft Clip | Wavefold is periodic, so Drive is not monotonic in loudness |
| Drive | `col.drive` | 0 … 36 dB | 0 | |
| Stereo Width | `col.width` | 0 … 200 % | 100 | |
| Low Cut | `col.loCut` | 20 … 1000 Hz | 20 | Bypassed at the minimum |
| High Cut | `col.hiCut` | 1 k … 20 kHz | 20 k | Bypassed at the maximum |
| Dry/Wet Mix | `out.mix` | 0 … 100 % | 100 | Dry path is latency-aligned, so 0 is a true null |
| Output Gain | `out.gain` | −24 … +12 dB | 0 | |
| Safety Limiter | `out.limiter` | on / off | on | |

---

## Interface

- **Top bar** — preset browser (prev / next / list / save), A/B compare with A>B copy,
  character layer show-hide, Performance Mode, scene switcher, and Randomize with
  per-section locks.
- **Fractal visualiser** — the live band subdivision as a nested skyline; column tops sit
  at each band's depth in the cascade, energy fills from the floor.
- **Spectrum analyser** — input, output and carrier overlaid on a log axis.
- **Character layer** — an original character and dragon behind the visualiser. Click the
  character to cycle expressions, double-click to randomize; click the dragon to roar and
  toggle the Spectral Delay; drag its tail to drive a macro.

Knobs: drag to adjust, **shift-drag for fine control**, double-click to reset, mouse wheel
supported, value readout on hover.

Resizable from 75% to 200% with a fixed aspect ratio. Size, scene and character visibility
are saved with the plugin state.

### Randomize

Deliberately conservative. It never switches a delay on, never disables the limiter, never
moves FFT size (that would shift latency), and wanders within ±45% of current values rather
than rolling uniformly — a uniform roll mostly produces unusable patches.

### Presets

18 factory presets across five categories. User presets are XML:

| Platform | Location |
|---|---|
| Windows | `%APPDATA%\FLOOD\VIIBEDEALER\Presets\` |
| macOS | `~/Library/Application Support/FLOOD/VIIBEDEALER/Presets/` |
| Linux | `~/.config/FLOOD/VIIBEDEALER/Presets/` |

Full state saves and restores with the host session, carrying a version number for future
compatibility.

---

## Repository layout

```
src/dsp/          one class per DSP module
src/gui/          look and feel, visualisers, panels — no DSP
src/character/    the illustrated layer — reads only the lock-free snapshot
src/state/        presets and the randomiser
tools/            build, install, package, and the Blender art pipeline
assets/           committed character art + manifest.json
tests/            unit tests, CPU benchmark, GUI screenshot
```

Real-time safety: no allocation, locks, file I/O or logging on the audio thread; everything
preallocated in `prepareToPlay`; `ScopedNoDenormals` throughout; NaN/Inf scrubbed; every
feedback loop soft-saturated and hard-clamped. Audio reaches the GUI only through a
lock-free seqlock snapshot.

## Licensing

Built against JUCE 8, which offers both commercial and GPLv3 terms. **Check which applies
to you before distributing binaries** — the GPL route obliges you to release your source
under the GPL to anyone you give the plugin to.

## Credits

Original work for FLOOD. The character and dragon are original designs; no existing anime
character, studio artwork, plugin layout or logo is reproduced.
