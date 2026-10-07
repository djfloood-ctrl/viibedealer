#!/usr/bin/env bash
#
# Configures and builds VIIBEDEALER on macOS.
#
#     ./tools/build_mac.sh                  # Release, universal (arm64 + x86_64)
#     ./tools/build_mac.sh --native         # this Mac's architecture only, much faster
#     ./tools/build_mac.sh --debug
#     ./tools/build_mac.sh --no-werror      # do not let a warning fail the build
#     ./tools/build_mac.sh --tests          # build and run the unit tests too
#
# Requirements: Xcode command line tools (xcode-select --install), CMake >= 3.22, Git.
# JUCE is fetched by CMake -- there is nothing to install or vendor first.

set -euo pipefail

config="Release"
build_dir="build-mac"
archs="arm64;x86_64"
werror="ON"
run_tests=0
codesign_identity="-"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)      config="Debug" ;;
        --native)     archs="$(uname -m)" ;;
        --no-werror)  werror="OFF" ;;
        --tests)      run_tests=1 ;;
        --build-dir)  build_dir="$2"; shift ;;
        --identity)   codesign_identity="$2"; shift ;;
        -h|--help)    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)            echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script builds the macOS artefacts and must run on macOS." >&2
    echo "There is no cross-compile path from Windows or Linux: the AU and VST3" >&2
    echo "bundles need the macOS SDK and codesign. Use the GitHub Actions workflow" >&2
    echo "in .github/workflows/macos.yml to build on a hosted Mac instead." >&2
    exit 1
fi

echo "==> configuring ($config, archs: $archs, VBD_WERROR=$werror)"
cmake -B "$build_dir" -G "Ninja Multi-Config" \
    -DCMAKE_OSX_ARCHITECTURES="$archs" \
    -DVBD_WERROR="$werror" \
    -DVBD_CODESIGN_IDENTITY="$codesign_identity" \
    -DVBD_BUILD_TESTS=$([[ $run_tests -eq 1 ]] && echo ON || echo OFF) \
    .

echo "==> building"
if ! cmake --build "$build_dir" --config "$config"; then
    if [[ "$werror" == "ON" ]]; then
        echo ""
        echo "If the failure above is a warning rather than a real error, this code was" >&2
        echo "written under MSVC and Clang is stricter in places. Re-run with:" >&2
        echo "    ./tools/build_mac.sh --no-werror" >&2
    fi
    exit 1
fi

if [[ $run_tests -eq 1 ]]; then
    echo "==> running tests"
    ctest --test-dir "$build_dir" --build-config "$config" --output-on-failure
fi

artefacts="$build_dir/viibedealer_artefacts/$config"

echo ""
echo "Built into $artefacts:"
for p in "VST3/VIIBEDEALER.vst3" "AU/VIIBEDEALER.component" "Standalone/VIIBEDEALER.app"; do
    if [[ -e "$artefacts/$p" ]]; then
        echo "  $p"
        lipo -archs "$artefacts/$p/Contents/MacOS/VIIBEDEALER" 2>/dev/null \
            | sed 's/^/      architectures: /' || true
    fi
done

echo ""
echo "Next:  ./tools/install_mac.sh --build-dir $build_dir     (install for this user)"
echo "       ./tools/package_mac.sh --build-dir $build_dir     (zip for someone else)"
