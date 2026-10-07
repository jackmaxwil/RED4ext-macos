#!/bin/sh
# Sign the game executable with the 3-key hardened-runtime plist, or sign a
# plugin dylib ad-hoc. Usage:
#   scripts/codesign_macos.sh exe <path-to-Cyberpunk2077>
#   scripts/codesign_macos.sh dylib <path-to.dylib> [more.dylib ...]
#   scripts/codesign_macos.sh verify <path-to-executable>
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
plist="$root/scripts/red4ext_entitlements.plist"

if [ "$#" -lt 2 ]; then
    echo "usage: $0 exe <executable> | dylib <dylib> [dylib ...] | verify <executable>" >&2
    exit 2
fi

mode=$1
shift

case "$mode" in
exe)
    codesign -f -s - -o runtime --entitlements "$plist" "$1"
    ;;
dylib)
    for dylib in "$@"; do
        codesign -f -s - "$dylib"
    done
    ;;
verify)
    echo "entitlements:"
    codesign -d --entitlements - --xml "$1"
    codesign --verify --verbose=2 "$1"
    ;;
*)
    echo "unknown mode: $mode" >&2
    exit 2
    ;;
esac
