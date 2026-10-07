#!/usr/bin/env bash
#
# Builds a zip you can hand to someone on a Mac.
#
#     ./tools/package_mac.sh                      # VST3 + AU + standalone
#     ./tools/package_mac.sh --version 1.0.1
#     ./tools/package_mac.sh --no-standalone
#
# Produces dist/VIIBEDEALER-<version>-macOS.zip containing both plugin bundles and an
# INSTALL.txt. Uses ditto rather than zip so bundle resource forks and the code signature
# survive the round trip -- a plain `zip` can break an ad-hoc signed bundle.

set -euo pipefail

config="Release"
build_dir="build-mac"
version="1.0.0"
standalone=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)          config="Debug" ;;
        --version)        version="$2"; shift ;;
        --build-dir)      build_dir="$2"; shift ;;
        --no-standalone)  standalone=0 ;;
        -h|--help)        sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)                echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

artefacts="$build_dir/viibedealer_artefacts/$config"
vst3="$artefacts/VST3/VIIBEDEALER.vst3"
au="$artefacts/AU/VIIBEDEALER.component"

if [[ ! -d "$vst3" ]]; then
    echo "Not built: $vst3" >&2
    echo "Run: ./tools/build_mac.sh" >&2
    exit 1
fi

# The staging directory name is what the recipient sees when they unzip, so give it the
# release name rather than a scratch name.
stage="dist/VIIBEDEALER-$version-macOS"
zip="dist/VIIBEDEALER-$version-macOS.zip"

rm -rf "$stage"
mkdir -p "$stage"

cp -R "$vst3" "$stage/"

if [[ -d "$au" ]]; then
    cp -R "$au" "$stage/"
else
    echo "warning: no AU bundle in this build -- FL Studio will still see the VST3" >&2
fi

if [[ $standalone -eq 1 && -d "$artefacts/Standalone/VIIBEDEALER.app" ]]; then
    mkdir -p "$stage/Standalone"
    cp -R "$artefacts/Standalone/VIIBEDEALER.app" "$stage/Standalone/"
fi

# Record what the bundle actually contains, so "it won't load on my Intel Mac" is
# answerable without a rebuild.
archs="$(lipo -archs "$vst3/Contents/MacOS/VIIBEDEALER" 2>/dev/null || echo unknown)"

# A one-shot installer, so the recipient does not have to know about xattr.
cat > "$stage/install.command" <<'INSTALLER_EOF'
#!/usr/bin/env bash
# Double-click this to install. It copies the plugins into your own Library folder and
# clears the quarantine flag macOS puts on anything downloaded.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

plugins="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$plugins/VST3" "$plugins/Components"

for pair in "VIIBEDEALER.vst3:$plugins/VST3" "VIIBEDEALER.component:$plugins/Components"; do
    name="${pair%%:*}"; dest="${pair##*:}"
    if [[ -d "$name" ]]; then
        rm -rf "$dest/$name"
        cp -R "$name" "$dest/"
        xattr -dr com.apple.quarantine "$dest/$name" 2>/dev/null || true
        echo "installed $dest/$name"
    fi
done

if [[ -d "Standalone/VIIBEDEALER.app" ]]; then
    mkdir -p "$HOME/Applications"
    rm -rf "$HOME/Applications/VIIBEDEALER.app"
    cp -R "Standalone/VIIBEDEALER.app" "$HOME/Applications/"
    xattr -dr com.apple.quarantine "$HOME/Applications/VIIBEDEALER.app" 2>/dev/null || true
    echo "installed $HOME/Applications/VIIBEDEALER.app"
fi

echo ""
echo "Done. Now rescan plugins in your DAW (FL Studio: Options > Manage plugins >"
echo "Find more plugins). You can close this window."
INSTALLER_EOF
chmod +x "$stage/install.command"

cat > "$stage/INSTALL.txt" <<INSTALL_EOF
VIIBEDEALER $version  -  macOS  (VST3 + AU, $archs)
Spectral vocoder, fractal spectral slicer and delay pair for colour bass.

INSTALL -- the easy way
  Double-click install.command. If macOS refuses to open it, right-click it and choose
  Open, then confirm. That is the Gatekeeper prompt described below.

INSTALL -- by hand
  1. Copy VIIBEDEALER.vst3     into  ~/Library/Audio/Plug-Ins/VST3/
     Copy VIIBEDEALER.component into  ~/Library/Audio/Plug-Ins/Components/
     These are bundle FOLDERS. Copy the whole folder.
  2. Open Terminal and clear the quarantine flag, or the plugin will not load:
         xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/VIIBEDEALER.vst3
         xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/VIIBEDEALER.component
  3. Rescan: FL Studio > Options > Manage plugins > Find more plugins.
     Logic rescans AUs on launch. REAPER: Preferences > Plug-ins > VST > Re-scan.
  4. It appears as VIIBEDEALER by FLOOD, as an audio EFFECT (not a generator).

WHY THE QUARANTINE STEP
  This build is ad-hoc signed, not signed with a paid Apple Developer ID, and not
  notarised. macOS flags anything downloaded from the internet and refuses to load an
  unnotarised plugin until that flag is cleared. Step 2 clears it for this plugin only.
  Nothing else on your system is affected. In FL Studio the symptom without it is that
  the plugin simply never appears after a scan, with no error.

REQUIREMENTS
  macOS 10.13 or later. Runs natively on Apple Silicon and Intel.
  In FL Studio, use it on a mixer insert, not as a channel generator.

WHAT IT DOES
  Carrier   - sidechain / internal chord oscillator / self; chord types, spread, detune,
              MIDI override (play chords in to set the harmony)
  Engine    - Vocode, Mag Morph, Phase Morph, Cross; formant shift, spectral tilt,
              envelope resolution, sensitivity gate, freeze, flip, frequency shift
  Fractal   - Cantor / Golden / Thue-Morse / Sierpinski / L-System band patterns, depth,
              split ratio and asymmetry, frequency bounds, invert, Shatter, tempo-synced
              or free animation, rhythm gate, Grit
  Delays    - spectral (per-band, fractal-following) and stereo (Stereo / Ping-Pong /
              Dual, loop filters, saturation, wow, diffusion, ducking, freeze, pre or
              post placement). BOTH OFF BY DEFAULT - turn them on.
  Output    - saturation (soft clip / tube / wavefold), drive, width, low/high cut,
              dry/wet mix, output gain, safety limiter
  Interface - fractal visualiser, spectrum analyser, 18 factory presets, A/B compare,
              Randomize with per-section locks, and an animated character layer. Click
              the character to cycle expressions, the dragon to roar and toggle the
              spectral delay, drag its tail for a macro. Resizable 75% to 200%.

START HERE
  Put it on a bass. The defaults - chord carrier, Vocode, C minor, 100% wet - imprint a
  chord straight away. Three things are deliberate and might read as faults:
    - Shatter is the fractal amount. At 0 the whole FRACTAL panel does nothing.
    - Both delays are off by default.
    - Latency is 2048 samples at the default FFT size. It is reported to the host, so
      FL Studio compensates automatically. Larger FFT means more latency.
  If it sounds gated or sputtery, lower Sensitivity. If the chord drones through the
  gaps, raise it.

PRESETS
  Your own presets are saved as XML in:
      ~/Library/Application Support/FLOOD/VIIBEDEALER/Presets/

FEEDBACK WANTED
  This is the first macOS build of this plugin and it has not been tested on a Mac. The
  most useful things to report:
    - Does it scan and load at all, in VST3, in AU, or neither?
    - Apple Silicon or Intel, which macOS version, which FL Studio version?
    - Sample rate and buffer size, and whether the GUI draws correctly on a Retina
      display (the art ships at 1x and 2x).
    - Any crashes, clicks, stuck notes or CPU spikes.
INSTALL_EOF

rm -f "$zip"

# ditto -c -k --sequesterRsrc keeps bundle metadata and signatures intact.
ditto -c -k --sequesterRsrc --keepParent "$stage" "$zip"

rm -rf "$stage"

size="$(du -h "$zip" | cut -f1)"
echo "Packaged: $zip  ($size, $archs)"
