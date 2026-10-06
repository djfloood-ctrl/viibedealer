# Copies the built VST3 into a location your DAW scans.
#
#     .\tools\install_vst3.ps1                  # per-user install (no admin needed)
#     .\tools\install_vst3.ps1 -System          # system-wide (needs an elevated shell)
#     .\tools\install_vst3.ps1 -Config Debug
#
# COPY_PLUGIN_AFTER_BUILD is off in CMakeLists on purpose: the system VST3 folder needs
# elevation, and a build that fails on a permission error every time is worse than an
# explicit install step.

[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [switch]$System,
    [string]$BuildDir = 'build-ninja'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "$BuildDir\viibedealer_artefacts\$Config\VST3\VIIBEDEALER.vst3"

if (-not (Test-Path $src)) {
    throw "Not built yet: $src`nRun: cmake --build $BuildDir --config $Config"
}

if ($System) {
    $dest = "$env:CommonProgramFiles\VST3"

    # Probe for actual write access rather than demanding elevation. The system VST3
    # folder is often loosened by plugin installers, in which case an ordinary shell can
    # write there and insisting on admin would be a pointless obstacle. Fail early with a
    # clear message if it genuinely is read-only.
    try {
        $probe = Join-Path $dest ".vbd_write_probe"
        New-Item -ItemType File -Path $probe -ErrorAction Stop | Out-Null
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
    } catch {
        throw "Cannot write to $dest. Re-run from an elevated PowerShell, or omit -System for a per-user install."
    }
} else {
    $dest = "$env:LOCALAPPDATA\Programs\Common\VST3"
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null

$target = Join-Path $dest 'VIIBEDEALER.vst3'

# The VST3 is a bundle (a directory) on Windows too, so replace it wholesale.
if (Test-Path $target) {
    Remove-Item $target -Recurse -Force
}

Copy-Item $src $dest -Recurse -Force

Write-Host "Installed $Config build to:" -ForegroundColor Green
Write-Host "  $target"
if (-not $System) {
    Write-Host ""
    Write-Host "This is a per-user path. If your DAW does not see it, add it to the" -ForegroundColor Yellow
    Write-Host "VST3 search paths (REAPER: Options > Preferences > Plug-ins > VST)." -ForegroundColor Yellow
}
