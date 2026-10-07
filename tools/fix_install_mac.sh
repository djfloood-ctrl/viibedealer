#!/usr/bin/env bash
#
# Installs VIIBEDEALER and reports exactly what happened, in one pass.
#
#     bash fix_install_mac.sh
#
# Use this instead of install.command when Gatekeeper keeps refusing to open that file.
# A script run through `bash` is not subject to the quarantine prompt that blocks a
# double-clicked .command, so this works where that does not.
#
# Needs no developer tools. Installs nothing but the plugin itself.

echo "=================================================="
echo " VIIBEDEALER install + diagnose"
echo "=================================================="
echo "macOS $(sw_vers -productVersion 2>/dev/null)  ($(uname -m))"
echo ""

plugins="$HOME/Library/Audio/Plug-Ins"

# ------------------------------------------------- 1. find the unzipped bundles
# Searched rather than assumed: the zip could have been unpacked anywhere, and a wrong
# guess here would look identical to a broken plugin.
echo "---- 1. LOCATING THE UNZIPPED FILES ----"
vst3_src=""
au_src=""
for root in "$HOME/Downloads" "$HOME/Desktop" "$HOME" "/Volumes"; do
    [ -d "$root" ] || continue
    [ -z "$vst3_src" ] && vst3_src="$(find "$root" -maxdepth 4 -name 'VIIBEDEALER.vst3' -type d -print 2>/dev/null | head -1)"
    [ -z "$au_src" ]   && au_src="$(find "$root" -maxdepth 4 -name 'VIIBEDEALER.component' -type d -print 2>/dev/null | head -1)"
done

echo "  VST3 source: ${vst3_src:-NOT FOUND}"
echo "  AU   source: ${au_src:-NOT FOUND}"

if [ -z "$vst3_src" ] && [ -z "$au_src" ]; then
    echo ""
    echo "  >>> Could not find the unzipped plugin anywhere under Downloads, Desktop,"
    echo "  >>> your home folder, or a mounted volume."
    echo "  >>>"
    echo "  >>> Double-click VIIBEDEALER-1.0.0-macOS.zip in Finder to unpack it, then"
    echo "  >>> run this script again. Do not open the .vst3 itself -- it is a folder"
    echo "  >>> that macOS displays as a single item, which is normal."
    exit 1
fi
echo ""

# ------------------------------------------------- 2. install
echo "---- 2. INSTALLING ----"
install_one() {
    src="$1"; dest_dir="$2"
    [ -n "$src" ] || return 0
    name="$(basename "$src")"
    mkdir -p "$dest_dir"
    rm -rf "$dest_dir/$name"
    # ditto rather than cp: it preserves the bundle's metadata and signature seal.
    if ditto "$src" "$dest_dir/$name" 2>/dev/null; then
        echo "  installed  $dest_dir/$name"
    else
        echo "  FAILED to copy to $dest_dir/$name"
        return 0
    fi
    # The quarantine flag is the single most common reason a correctly installed
    # plugin never appears. Strip it from the bundle and everything inside it.
    xattr -dr com.apple.quarantine "$dest_dir/$name" 2>/dev/null || true
}
install_one "$vst3_src" "$plugins/VST3"
install_one "$au_src"   "$plugins/Components"
echo ""

# ------------------------------------------------- 3. verify
echo "---- 3. VERIFYING ----"
for t in "$plugins/VST3/VIIBEDEALER.vst3" "$plugins/Components/VIIBEDEALER.component"; do
    [ -d "$t" ] || { echo "  $t -- NOT PRESENT"; continue; }
    echo "  $t"
    q="$(xattr -p com.apple.quarantine "$t" 2>/dev/null || true)"
    echo "      quarantine:  ${q:-clear}"
    inner="$(find "$t" -exec xattr -p com.apple.quarantine {} \; 2>/dev/null | wc -l | tr -d ' ')"
    echo "      inner flags: $inner"
    bin="$t/Contents/MacOS/VIIBEDEALER"
    if [ -f "$bin" ]; then
        echo "      binary:      present"
        echo "      archs:       $(file -b "$bin" 2>&1 | cut -c1-120)"
    else
        echo "      binary:      MISSING -- bundle is malformed"
    fi
    if codesign --verify --deep --strict "$t" 2>/dev/null; then
        echo "      signature:   valid"
    else
        echo "      signature:   INVALID -- re-signing ad-hoc now"
        codesign --force --deep --sign - "$t" 2>&1 | sed 's/^/        /'
        if codesign --verify --deep --strict "$t" 2>/dev/null; then
            echo "      signature:   repaired"
        else
            echo "      signature:   STILL INVALID"
        fi
    fi
done
echo ""

# ------------------------------------------------- 4. will FL even accept it
#
# This is the check that matters if everything above looks fine. A host built with the
# hardened runtime and WITHOUT the disable-library-validation entitlement will refuse to
# load any plugin that is not signed by the same team as the host itself. An ad-hoc
# signature can never satisfy that, no matter how correct the install is -- the only fix
# is signing with a paid Apple Developer ID. So it is worth knowing before chasing
# anything else.
echo "---- 4. HOST SIGNING POLICY ----"
shopt -s nullglob 2>/dev/null || true
hosts=(/Applications/FL\ Studio*.app \
       /Applications/FL\ Studio*/FL\ Studio*.app \
       /Applications/Image-Line/*/FL\ Studio*.app)
if [ ${#hosts[@]} -eq 0 ]; then
    echo "  No FL Studio found in /Applications."
    ls -1 /Applications 2>/dev/null | grep -i -E 'fl stud|image' | sed 's/^/      /' || true
else
    for h in "${hosts[@]}"; do
        echo "  host: $h"
        ents="$(codesign -d --entitlements - "$h" 2>/dev/null || true)"
        if printf '%s' "$ents" | grep -q 'disable-library-validation'; then
            echo "      library validation DISABLED -> ad-hoc signed plugins are allowed."
        elif [ -z "$ents" ]; then
            echo "      could not read entitlements (inconclusive)"
        else
            echo "      >>> disable-library-validation NOT present."
            echo "      >>> This host may refuse any plugin not signed by its own"
            echo "      >>> developer. If everything above is valid and it still does"
            echo "      >>> not appear, this is the reason, and the fix is a paid"
            echo "      >>> Apple Developer ID signature -- not anything you can do."
        fi
    done
fi
echo ""

echo "---- 5. WHAT TO DO NEXT ----"
echo "  In FL Studio: Options > Manage plugins > Find more plugins (full rescan)."
echo "  Make sure both VST3 and AU scanning are enabled in that window, and that"
echo "  $plugins/VST3 is in the plugin search paths."
echo "  It appears as VIIBEDEALER by FLOOD, as an EFFECT -- put it on a mixer insert."
echo ""
echo "=================================================="
echo " paste everything above"
echo "=================================================="
