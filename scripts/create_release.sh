#!/bin/bash
# Builds the macOS release zip: RED4ext, the plugins that pass the offline gate, the canonical address DB, the
# launcher and the signing helper.
#
#   scripts/create_release.sh VERSION
#
# Builds everything in Release mode and runs tools/cp-gate on the built plugins. Nothing is packaged unless the gate
# passes. On a machine without the game (CI), CP_GATE_NO_GAME=1 skips only the gate's installed-game check.
# Writes the release assets: release/RED4ext-macOS-arm64-VERSION.zip, release/install.sh and release/SHA256SUMS.
# Zip layout (its top-level folder's contents go into the game folder):
#   launch_red4ext.sh
#   red4ext/VERSION, red4ext/BUILD_INFO.json     (version, build date, commit of every component)
#   red4ext/RED4ext.dylib, red4ext/bin/red4ext_plugin_check, red4ext/bin/x64/cyberpunk2077_addresses.json
#   red4ext/plugins/<Plugin>/...
#   r6/input/*.xml                              (plugin input bindings)
#   red4ext/macos/scripts/install_macos.sh      (backs up and re-signs the game binary)
set -euo pipefail

VERSION=${1:?usage: create_release.sh VERSION}
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS="$ROOT/.."
OUT="$ROOT/release"
NAME="RED4ext-macOS-arm64-$VERSION"
STAGE="$OUT/$NAME"

build() { # <source dir> <build dir>
    # Every repo builds against the workspace SDK checkout (the one the address database and gates come from).
    cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DRED4EXT_SDK_DIR="$WS/RED4ext.SDK" >/dev/null
    cmake --build "$2" -j "$(sysctl -n hw.ncpu)" >"$2.log" 2>&1 || { tail -20 "$2.log"; exit 1; }
}

echo "[release] building"
build "$ROOT" "$ROOT/build-release"
build "$WS/cp2077-tweak-xl" "$WS/cp2077-tweak-xl/build-release"
build "$WS/cp2077-modmenu" "$WS/cp2077-modmenu/build-release"
build "$WS/cp2077-archive-xl-macos" "$WS/cp2077-archive-xl-macos/build-release"

TWEAKXL="$WS/cp2077-tweak-xl/build-release/TweakXL.dylib"
MODMENU="$WS/cp2077-modmenu/build-release/libModMenu.dylib"
ARCHIVEXL="$WS/cp2077-archive-xl-macos/build-release/ArchiveXL.dylib"

echo "[release] offline gate"
CP2077_GATE_CHECK="$ROOT/build-release/bin/red4ext_plugin_check" \
    "$ROOT/tools/cp-gate" "$TWEAKXL:$WS/cp2077-tweak-xl/src" "$MODMENU:$WS/cp2077-modmenu/src" \
    "$ARCHIVEXL:$WS/cp2077-archive-xl-macos/src"

echo "[release] staging $NAME"
rm -rf "$STAGE"
R4E="$STAGE/red4ext"
mkdir -p "$R4E/bin/x64" "$R4E/plugins/TweakXL" "$R4E/plugins/ModMenu" "$R4E/macos/scripts" "$STAGE/r6"

install -m 755 "$ROOT/build-release/libs/RED4ext.dylib" "$R4E/RED4ext.dylib"
install -m 755 "$ROOT/build-release/bin/red4ext_plugin_check" "$R4E/bin/red4ext_plugin_check"
cp "$WS/RED4ext.SDK/cyberpunk2077_addresses.json" "$R4E/bin/x64/"
install -m 755 "$ROOT/scripts/launch_red4ext.sh" "$STAGE/launch_red4ext.sh"
install -m 755 "$ROOT/scripts/codesign_macos.sh" "$R4E/macos/scripts/codesign_macos.sh"
cp "$ROOT/scripts/red4ext_entitlements.plist" "$R4E/macos/scripts/"
install -m 755 "$ROOT/scripts/install_macos.sh" "$R4E/macos/scripts/install_macos.sh"

install -m 755 "$TWEAKXL" "$R4E/plugins/TweakXL/TweakXL.dylib"
cp -R "$WS/cp2077-tweak-xl/scripts" "$R4E/plugins/TweakXL/Scripts"
rm -rf "$R4E/plugins/TweakXL/Scripts/r6"
cp -R "$WS/cp2077-tweak-xl/scripts/r6/." "$STAGE/r6/"
cp -R "$WS/cp2077-tweak-xl/data" "$R4E/plugins/TweakXL/Data"

mkdir -p "$R4E/plugins/ArchiveXL/Bundle"
install -m 755 "$ARCHIVEXL" "$R4E/plugins/ArchiveXL/ArchiveXL.dylib"
cp -R "$WS/cp2077-archive-xl-macos/scripts" "$R4E/plugins/ArchiveXL/Scripts"
cp -R "$WS/cp2077-archive-xl-macos/bundle/source/resources/." "$R4E/plugins/ArchiveXL/Bundle/"
"$WS/cp2077-archive-xl-macos/tools/fetch-bundle-archive.sh"
cp "$WS/cp2077-archive-xl-macos/bundle/packed/archive/pc/mod/ArchiveXL.archive" "$R4E/plugins/ArchiveXL/Bundle/"

install -m 755 "$MODMENU" "$R4E/plugins/ModMenu/ModMenu.dylib"
cp -R "$WS/cp2077-modmenu/scripts/Scripts/ModMenu" "$R4E/plugins/ModMenu/Scripts"
cp -R "$WS/cp2077-modmenu/scripts/r6/." "$STAGE/r6/"

"$ROOT/scripts/codesign_macos.sh" dylib "$R4E/RED4ext.dylib" "$R4E/plugins/TweakXL/TweakXL.dylib" \
    "$R4E/plugins/ModMenu/ModMenu.dylib" "$R4E/plugins/ArchiveXL/ArchiveXL.dylib"
codesign -f -s - "$R4E/bin/red4ext_plugin_check"

# Players' Macs have only the system libraries: refuse any binary that links something else (e.g. from Homebrew).
for bin in "$R4E/RED4ext.dylib" "$R4E/bin/red4ext_plugin_check" "$R4E/plugins/"*/*.dylib; do
    id=$(otool -D "$bin" 2>/dev/null | tail -n +2)
    foreign=$(otool -L "$bin" | tail -n +2 | awk '{ print $1 }' | grep -v -e '^/usr/lib/' -e '^/System/' | grep -vxF "${id:-none}" || true)
    if [[ -n "$foreign" ]]; then
        echo "[release] $bin links libraries players don't have:"; echo "$foreign"; exit 1
    fi
done
"$R4E/bin/red4ext_plugin_check" "$R4E/bin/x64/cyberpunk2077_addresses.json" "$R4E/plugins/"*/*.dylib

cp "$ROOT/docs/INSTALL_MACOS.md" "$STAGE/INSTALL_MACOS.md"

# Traceability: the commit of every repo that went into this build (-dirty if it had uncommitted changes).
rev() { echo "$(git -C "$1" rev-parse HEAD)$(git -C "$1" diff --quiet HEAD || echo -dirty)"; }
echo "$VERSION" >"$R4E/VERSION"
cat >"$R4E/BUILD_INFO.json" <<JSON
{
  "version": "$VERSION",
  "date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "game_uuid": "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["uuid"])' "$WS/RED4ext.SDK/cyberpunk2077_addresses.json")",
  "commits": {
    "RED4ext": "$(rev "$ROOT")",
    "RED4ext.SDK": "$(rev "$WS/RED4ext.SDK")",
    "cp2077-tweak-xl": "$(rev "$WS/cp2077-tweak-xl")",
    "cp2077-archive-xl-macos": "$(rev "$WS/cp2077-archive-xl-macos")",
    "cp2077-modmenu": "$(rev "$WS/cp2077-modmenu")"
  },
  "archivexl_upstream_archive": "$(sed -n 's/^VERSION=//p' "$WS/cp2077-archive-xl-macos/tools/fetch-bundle-archive.sh")"
}
JSON
python3 -m json.tool "$R4E/BUILD_INFO.json" >/dev/null

(cd "$OUT" && rm -f "$NAME.zip" && ditto -c -k --norsrc --noextattr --noqtn --keepParent "$NAME" "$NAME.zip")
install -m 755 "$ROOT/install.sh" "$OUT/install.sh"
(cd "$OUT" && shasum -a 256 "$NAME.zip" install.sh | tee SHA256SUMS)
echo "[release] wrote $OUT/$NAME.zip, install.sh, SHA256SUMS"
