# Downloads pluginval into tools/bin (gitignored) and reports its path.
#
#     .\tools\get_pluginval.ps1            # fetch if missing
#     .\tools\get_pluginval.ps1 -Force     # re-download

[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $root 'tools\bin'
$exe    = Join-Path $binDir 'pluginval.exe'

if ((Test-Path $exe) -and -not $Force) {
    Write-Host "pluginval already present: $exe" -ForegroundColor DarkGray
    return $exe
}

New-Item -ItemType Directory -Force -Path $binDir | Out-Null

# Tracktion publishes a rolling "latest_release" tag with per-platform zips.
$url = 'https://github.com/Tracktion/pluginval/releases/download/v1.0.3/pluginval_Windows.zip'
$zip = Join-Path $env:TEMP 'pluginval_Windows.zip'

Write-Host "Downloading pluginval..." -ForegroundColor Cyan
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

Expand-Archive -Path $zip -DestinationPath $binDir -Force
Remove-Item $zip -Force

if (-not (Test-Path $exe)) {
    # Some releases nest the exe one level down.
    $found = Get-ChildItem $binDir -Recurse -Filter 'pluginval.exe' | Select-Object -First 1
    if ($found) {
        Move-Item $found.FullName $exe -Force
    } else {
        throw "pluginval.exe not found in the downloaded archive."
    }
}

Write-Host "pluginval ready: $exe" -ForegroundColor Green
return $exe
