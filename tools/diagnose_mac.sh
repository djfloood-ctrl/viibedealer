#!/usr/bin/env bash
#
# Read-only diagnostic for "the plugin does not show up in my DAW" on macOS.
#
# Changes nothing. Prints a report to paste back. Run it as the user who installed the
# plugin -- it inspects that user's own Library folder.
#
#     bash diagnose_mac.sh
#
# Checks, in the order things actually go wrong:
#   1. Is the bundle even there (did the installer run at all)
#   2. Is it still quarantined (Gatekeeper refuses it, silently)
#   3. Is the code signature intact (survived the zip and the copy)
#   4. Does it contain this Mac's architecture
#   5. Is the bundle internally shaped the way a plugin must be
#   6. What does Gatekeeper itself say about it

echo "=============================================="
echo " VIIBEDEALER macOS install diagnostic"
echo "=============================================="
echo "date:        $(date)"
echo "macOS:       $(sw_vers -productVersion 2>/dev/null) ($(uname -m))"
echo "shell:       ${BASH_VERSION:-unknown}"
echo ""

plugins="$HOME/Library/Audio/Plug-Ins"

# ---------------------------------------------------------------- 1. is it installed
echo "---- 1. INSTALLED LOCATIONS --------------------"
found_any=0
for p in \
    "$plugins/VST3/VIIBEDEALER.vst3" \
    "$plugins/Components/VIIBEDEALER.component" \
    "/Library/Audio/Plug-Ins/VST3/VIIBEDEALER.vst3" \
    "/Library/Audio/Plug-Ins/Components/VIIBEDEALER.component"
do
    if [ -d "$p" ]; then
        echo "  PRESENT  $p"
        found_any=1
    else
        echo "  missing  $p"
    fi
done

if [ "$found_any" -eq 0 ]; then
    echo ""
    echo "  >>> Nothing is installed. The installer did not run."
    echo "  >>> macOS blocks .command files from the internet: double-clicking shows an"
    echo "  >>> 'unidentified developer' box and copies nothing. Right-click"
    echo "  >>> install.command and choose Open instead, then re-run this script."
    echo ""
    echo "  For reference, what IS in those folders:"
    ls -1 "$plugins/VST3" 2>/dev/null | sed 's/^/      VST3: /' || echo "      (no VST3 folder)"
    ls -1 "$plugins/Components" 2>/dev/null | sed 's/^/      AU:   /' || echo "      (no Components folder)"
    echo ""
    echo "=============================================="
    exit 0
fi
echo ""

# Everything below inspects whichever copies exist.
for target in \
    "$plugins/VST3/VIIBEDEALER.vst3" \
    "$plugins/Components/VIIBEDEALER.component" \
    "/Library/Audio/Plug-Ins/VST3/VIIBEDEALER.vst3" \
    "/Library/Audio/Plug-Ins/Components/VIIBEDEALER.component"
do
    [ -d "$target" ] || continue

    echo "=============================================="
    echo " $target"
    echo "=============================================="

    # ------------------------------------------------------------ 2. quarantine
    echo "---- 2. QUARANTINE -----------------------------"
    q="$(xattr -p com.apple.quarantine "$target" 2>/dev/null || true)"
    if [ -n "$q" ]; then
        echo "  QUARANTINED: $q"
        echo "  >>> This alone stops it loading, with no error anywhere."
        echo "  >>> Fix:  xattr -dr com.apple.quarantine \"$target\""
    else
        echo "  clear (no quarantine flag on the bundle)"
    fi
    # The flag can also sit on files inside the bundle, which is just as fatal.
    inner="$(find "$target" -exec xattr -p com.apple.quarantine {} \; 2>/dev/null | wc -l | tr -d ' ')"
    echo "  files inside still carrying the flag: $inner"
    if [ "$inner" != "0" ]; then
        echo "  >>> Fix:  xattr -dr com.apple.quarantine \"$target\""
    fi
    echo ""

    # ------------------------------------------------------------ 3. signature
    echo "---- 3. CODE SIGNATURE -------------------------"
    if codesign --verify --deep --strict "$target" 2>&1; then
        echo "  signature VALID"
    else
        echo "  >>> signature INVALID or MISSING (see message above)."
        echo "  >>> On Apple Silicon this is fatal: the kernel refuses the binary."
        echo "  >>> Fix:  codesign --force --deep --sign - \"$target\""
    fi
    codesign -dv "$target" 2>&1 | sed -n '1,8p' | sed 's/^/      /'
    echo ""

    # ------------------------------------------------------------ 4. architecture
    echo "---- 4. ARCHITECTURE ---------------------------"
    bin="$target/Contents/MacOS/VIIBEDEALER"
    if [ -f "$bin" ]; then
        echo "  binary:  $bin"
        echo "  archs:   $(lipo -archs "$bin" 2>&1)"
        echo "  this Mac needs: $(uname -m)"
        echo "  min macOS:  $(otool -l "$bin" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')"
    else
        echo "  >>> NO BINARY at $bin -- the bundle is malformed or truncated."
        echo "  >>> Most likely the zip was extracted and re-zipped on Windows."
        echo "  what is actually in Contents:"
        ls -1 "$target/Contents" 2>/dev/null | sed 's/^/      /'
    fi
    echo ""

    # ------------------------------------------------------------ 5. bundle shape
    echo "---- 5. BUNDLE STRUCTURE -----------------------"
    for f in "Contents/Info.plist" "Contents/MacOS/VIIBEDEALER" "Contents/_CodeSignature/CodeResources"; do
        [ -e "$target/$f" ] && echo "  ok       $f" || echo "  MISSING  $f"
    done
    if [ -f "$target/Contents/Info.plist" ]; then
        echo "  bundle id:  $(defaults read "$target/Contents/Info" CFBundleIdentifier 2>/dev/null || echo '(unreadable)')"
        echo "  version:    $(defaults read "$target/Contents/Info" CFBundleShortVersionString 2>/dev/null || echo '(unreadable)')"
    fi
    echo ""

    # ------------------------------------------------------------ 6. gatekeeper
    echo "---- 6. GATEKEEPER VERDICT ---------------------"
    spctl -a -vvv -t install "$target" 2>&1 | sed 's/^/      /'
    echo ""
done

# ------------------------------------------------------------ 7. AU registration
echo "=============================================="
echo "---- 7. AU REGISTRATION ------------------------"
if command -v auval >/dev/null 2>&1; then
    echo "  auval -v aufx Vbdl Flod:"
    auval -v aufx Vbdl Flod 2>&1 | tail -20 | sed 's/^/      /'
else
    echo "  auval not found"
fi
echo ""

echo "---- 8. FL STUDIO ------------------------------"
ls -d /Applications/FL\ Studio*.app 2>/dev/null | sed 's/^/      installed: /' || echo "      no FL Studio in /Applications"
fldb="$HOME/Documents/Image-Line/FL Studio/Presets/Plugin database"
[ -d "$fldb" ] && echo "      plugin database present: $fldb" || echo "      no FL plugin database at the usual path"
echo ""
echo "=============================================="
echo " end of report -- paste everything above"
echo "=============================================="
