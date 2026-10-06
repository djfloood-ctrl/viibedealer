# Builds a zip you can hand to someone else.
#
#     .\tools\package.ps1                 # Release, from build-ninja
#     .\tools\package.ps1 -Standalone     # include the standalone app too
#
# Produces dist\VIIBEDEALER-<version>-win64.zip containing the VST3 bundle and an
# INSTALL.txt. The plugin statically links the MSVC runtime, so the recipient needs no
# redistributable -- see the CRT note in CMakeLists.txt.

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [string]$BuildDir = 'build-ninja',
    [switch]$Standalone,
    [string]$Version = '0.1.0-phaseB'
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$artefacts = Join-Path $root "$BuildDir\viibedealer_artefacts\$Config"
$vst3      = Join-Path $artefacts 'VST3\VIIBEDEALER.vst3'

if (-not (Test-Path $vst3)) {
    throw "Not built: $vst3`nRun: cmake --build $BuildDir --config $Config"
}

$distDir = Join-Path $root 'dist'
$stage   = Join-Path $distDir "stage-$Version"
$zip     = Join-Path $distDir "VIIBEDEALER-$Version-win64.zip"

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $vst3 $stage -Recurse

if ($Standalone) {
    $exe = Join-Path $artefacts 'Standalone\VIIBEDEALER.exe'
    if (Test-Path $exe) {
        New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Standalone') | Out-Null
        Copy-Item $exe (Join-Path $stage 'Standalone')
    }
}

$install = @"
VIIBEDEALER $Version  -  Windows x64 VST3
Spectral vocoder / fractal slicer for color bass.

INSTALL
  1. Copy the whole VIIBEDEALER.vst3 FOLDER into:
         C:\Program Files\Common Files\VST3\
     It is a bundle, not a single file. Copy the folder, not just the file inside it.
  2. In FL Studio: Options > Manage plugins > Find more plugins (a full rescan).
     In REAPER:    Options > Preferences > Plug-ins > VST > Re-scan.
  3. It appears as VIIBEDEALER by FLOOD, as an audio effect (not a generator).

REQUIREMENTS
  Windows 10 or later, 64-bit. No Visual C++ redistributable needed -- the runtime is
  linked statically.

WHAT WORKS IN THIS BUILD
  Carrier    - sidechain / internal chord oscillator / self, chord types, spread,
               detune, MIDI override (play chords in to set the harmony)
  Engine     - Vocode, Mag Morph, Phase Morph, Cross; formant shift, spectral tilt,
               envelope resolution, sensitivity gate, freeze, flip, freq shift
  Fractal    - Cantor / Golden / Thue-Morse / Sierpinski / L-System band patterns,
               depth, split ratio and asymmetry, frequency bounds, invert, Shatter,
               tempo-synced or free animation, rhythm gate, Grit
  Delays     - spectral (per-band, fractal-following) and stereo (Stereo / Ping-Pong /
               Dual, loop filters, saturation, wow, diffusion, ducking, freeze,
               pre or post placement). BOTH OFF BY DEFAULT - turn them on.
  Output     - saturation (soft clip / tube / wavefold), drive, width, low/high cut,
               dry/wet mix, output gain, safety limiter

START HERE
  Shatter is the fractal amount: at 0 the fractal section is bypassed, so nothing in
  that group does anything until you raise it. Same for the two delay On switches.

NOT FINISHED YET
  The interface is a plain parameter list on purpose. The designed GUI (fractal
  visualiser, spectrum analyser, animated character layer) is not built yet. There are
  no presets yet either, though full state saves and reloads with the project.

NOTES
  - Latency is 2048 samples at the default FFT size, and the plugin reports it, so the
    host compensates automatically. Larger FFT = more latency.
  - Put it on a bass. Default is a C minor chord carrier in Vocode mode at 100% wet, so
    it imprints a chord immediately. Play MIDI chords into it to change the harmony.
  - If it sounds gated or sputtery, lower Sensitivity. If the chord drones through the
    gaps, raise it.
  - Unsigned build. Windows may warn the first time; that is expected for a dev build.

FEEDBACK WANTED
  Does it load and scan cleanly? Any crashes, clicks, stuck notes, or CPU spikes?
  Which FL version and sample rate / buffer size?
"@

Set-Content -Path (Join-Path $stage 'INSTALL.txt') -Value $install -Encoding utf8

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Remove-Item $stage -Recurse -Force

$size = '{0:N1} MB' -f ((Get-Item $zip).Length / 1MB)

Write-Host "Packaged: $zip  ($size)" -ForegroundColor Green
