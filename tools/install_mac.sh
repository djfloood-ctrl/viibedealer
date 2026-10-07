#!/usr/bin/env bash
#
# Copies the built macOS bundles into the folders a DAW scans.
#
#     ./tools/install_mac.sh                # per-user (~/Library), no password needed
#     ./tools/install_mac.sh --system       # /Library, asks for your password
#     ./tools/install_mac.sh --debug
#
# COPY_PLUGIN_AFTER_BUILD is off in CMakeLists on purpose, so this is an explicit step.
# It also clears the Gatekeeper quarantine flag, which is what stops an ad-hoc signed
# plugin from loading after it has been downloaded or unzipped.

set -euo pipefail

config="Release"
build_dir="build-mac"
scope="user"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)     config="Debug" ;;
        --system)    scope="system" ;;
        --build-dir) build_dir="$2"; shift ;;
        -h|--help)   sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artefacts="$root/$build_dir/viibedealer_artefacts/$config"

if [[ ! -d "$artefacts" ]]; then
    echo "Not built yet: $artefacts" >&2
    echo "Run: ./tools/build_mac.sh" >&2
    exit 1
fi

if [[ "$scope" == "system" ]]; then
    prefix="/Library/Audio/Plug-Ins"
else
    prefix="$HOME/Library/Audio/Plug-Ins"
fi

# sudo only for the system prefix; a per-user install must never ask for a password.
#
# A function rather than an array of command prefixes, because macOS still ships bash 3.2
# as /bin/bash, where expanding an empty array under `set -u` aborts the script with
# "unbound variable" -- so the per-user path, the common one, would be the broken one.
as_root() {
    if [[ "$scope" == "system" ]]; then
        sudo "$@"
    else
        "$@"
    fi
}

install_bundle() {
    local src="$1" dest_dir="$2" name
    name="$(basename "$src")"

    if [[ ! -d "$src" ]]; then
        echo "  skipped $name (not in this build)"
        return
    fi

    as_root mkdir -p "$dest_dir"

    # These are bundles (directories), so replace wholesale rather than merging an old
    # build's leftover resources into a new one.
    as_root rm -rf "$dest_dir/$name"
    as_root cp -R "$src" "$dest_dir/"

    # An ad-hoc signature plus a quarantine flag means the plugin is refused with no
    # useful error. Stripping the flag on something we just built ourselves is safe.
    as_root xattr -dr com.apple.quarantine "$dest_dir/$name" 2>/dev/null || true

    echo "  $dest_dir/$name"
}

echo "Installing $config build:"
install_bundle "$artefacts/VST3/VIIBEDEALER.vst3"      "$prefix/VST3"
install_bundle "$artefacts/AU/VIIBEDEALER.component"   "$prefix/Components"

if [[ -d "$artefacts/Standalone/VIIBEDEALER.app" ]]; then
    rm -rf "$HOME/Applications/VIIBEDEALER.app"
    mkdir -p "$HOME/Applications"
    cp -R "$artefacts/Standalone/VIIBEDEALER.app" "$HOME/Applications/"
    xattr -dr com.apple.quarantine "$HOME/Applications/VIIBEDEALER.app" 2>/dev/null || true
    echo "  $HOME/Applications/VIIBEDEALER.app"
fi

cat <<'MSG'

Now rescan in your DAW:
  FL Studio  - Options > Manage plugins > Find more plugins
  Logic      - it rescans AUs on launch; quit and reopen it
  Ableton    - Preferences > Plug-Ins > Rescan
  REAPER     - Preferences > Plug-ins > VST > Re-scan

It appears as VIIBEDEALER by FLOOD, as an audio effect (not a generator).
MSG

# auval is the AU validator Logic and GarageBand gate on. Flod/Vbdl are the manufacturer
# and plugin codes from CMakeLists; aufx is the effect type.
if command -v auval >/dev/null 2>&1 && [[ -d "$prefix/Components/VIIBEDEALER.component" ]]; then
    echo ""
    echo "To validate the AU:  auval -v aufx Vbdl Flod"
fi
