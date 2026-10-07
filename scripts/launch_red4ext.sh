#!/bin/bash
# RED4ext macOS Launcher
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RED4EXT_DIR="$SCRIPT_DIR/red4ext"
GAME_BINARY="$SCRIPT_DIR/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"

echo "=== RED4ext macOS Launcher ==="


# Compile REDscript together with the scripts of the plugins RED4ext will load (staged under r6/scripts because scc
# takes one folder). A plugin's scripts declare its natives, and the game stops with "Failed to initialize scripts
# data!" when a declared native's plugin is not loaded. So a plugin's scripts are staged only if config.ini does not
# ignore it and it passes the loader's address gate (red4ext_plugin_check).
STAGE="$SCRIPT_DIR/r6/scripts/zz_red4ext_plugins"
CHECK="$RED4EXT_DIR/bin/red4ext_plugin_check"
DB="$RED4EXT_DIR/bin/x64/cyberpunk2077_addresses.json"
IGNORED=$(awk '/^ignored *=/{f=1} f{print} f&&/\]/{exit}' "$RED4EXT_DIR/config.ini" 2>/dev/null | grep -o '"[^"]*"' | tr -d '"' || true)
# When the launcher exits (after the game, or on a compile failure), take the plugin scripts out again and recompile. Otherwise a later launch from Steam (without
# RED4ext) would load scripts that declare natives of plugins that are not loaded and stop at script initialization.
cleanup() {
    rm -rf "$STAGE"
    [[ -x "$SCRIPT_DIR/engine/tools/scc" ]] && "$SCRIPT_DIR/engine/tools/scc" -compile "$SCRIPT_DIR/r6/scripts" >/dev/null 2>&1 || true
}
trap cleanup EXIT
rm -rf "$STAGE"
for dir in "$RED4EXT_DIR"/plugins/*/; do
    name=$(basename "$dir")
    [[ -d "$dir/Scripts" ]] || continue
    if grep -qxF "$name" <<<"$IGNORED"; then
        echo "Not compiling $name's scripts: ignored in config.ini"
        continue
    fi
    dylibs=("$dir"*.dylib)
    if [[ ! -x "$CHECK" ]] || ! "$CHECK" "$DB" "${dylibs[@]}" >/dev/null 2>&1; then
        echo "Not compiling $name's scripts: RED4ext will not load it (unverified addresses)"
        continue
    fi
    mkdir -p "$STAGE" && cp -R "$dir/Scripts" "$STAGE/$name"
done
if [[ -x "$SCRIPT_DIR/engine/tools/scc" ]]; then
    SCC_OUT=$("$SCRIPT_DIR/engine/tools/scc" -compile "$SCRIPT_DIR/r6/scripts" 2>&1 || true)
    echo "$SCC_OUT" | tail -3
    if grep -q "Compilation failed" <<<"$SCC_OUT"; then
        echo "$SCC_OUT" | grep -A3 "^\[ERROR" | head -40
        echo "REDscript compilation failed; not launching (the game would stop at the same errors)."
        exit 1
    fi
fi

# Process input mappings
[[ -x "$SCRIPT_DIR/engine/tools/inputloader.pl" ]] && "$SCRIPT_DIR/engine/tools/inputloader.pl" 2>&1 || true

echo "Launching with RED4ext..."
DYLD_INSERT_LIBRARIES="$RED4EXT_DIR/RED4ext.dylib" "$GAME_BINARY" "$@"
