# Imports the MSVC x64 environment into the current PowerShell session.
#
# The Visual Studio CMake generator locates the toolchain on its own, but Ninja does not --
# it needs cl.exe, the linker and the Windows SDK on PATH/INCLUDE/LIB. Dot-source this
# before configuring a Ninja build tree:
#
#     . .\tools\dev-shell.ps1
#     cmake -B build-ninja -G "Ninja Multi-Config" .
#
# Idempotent: re-running it is harmless.

$ErrorActionPreference = 'Stop'

if ($env:VBD_DEV_SHELL -eq '1') {
    Write-Host "MSVC environment already imported." -ForegroundColor DarkGray
    return
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Is Visual Studio installed?"
}

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) {
    throw "No Visual Studio install with the C++ x64 toolset was found."
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at: $vcvars"
}

# Run vcvars in cmd, then copy the resulting environment back into this session.
# `call` matters: vcvars64.bat shells out to vswhere, and without `call` that inner
# invocation breaks the && chain and the marker never gets echoed.
$marker = '---VBD-ENV---'
$output = & cmd.exe /c "call `"$vcvars`" >nul 2>&1 && echo $marker && set" 2>$null

$seen = $false
foreach ($line in $output) {
    if (-not $seen) {
        # Trim matters: `echo X && set` in cmd emits the space before the && as part of X.
        if ($line.Trim() -eq $marker) { $seen = $true }
        continue
    }
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:\$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
    }
}

if (-not $seen) {
    throw "Failed to capture the vcvars environment."
}

# Ninja installed via winget is not on the machine PATH until a new shell starts.
$ninjaDir = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
if ((Test-Path $ninjaDir) -and ($env:Path -notlike "*$ninjaDir*")) {
    $env:Path = "$ninjaDir;$env:Path"
}

$env:VBD_DEV_SHELL = '1'

# Deliberately not shelling out to `cl.exe` for a version banner: it writes that banner to
# stderr, which $ErrorActionPreference='Stop' promotes to a terminating error.
Write-Host "MSVC environment ready." -ForegroundColor Green
Write-Host "  toolset $env:VCToolsVersion   SDK $env:WindowsSDKVersion   host->$env:VSCMD_ARG_TGT_ARCH" -ForegroundColor DarkGray
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    Write-Host "  ninja $(ninja --version)" -ForegroundColor DarkGray
}
