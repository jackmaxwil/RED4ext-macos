#!/bin/bash
# One-time setup after unzipping the release over the game folder:
#   red4ext/macos/scripts/install_macos.sh
# 1. Checks that the game binary is the build the address database was verified against. A different game patch
#    means RED4ext would refuse to hook anything; wait for an updated release instead.
# 2. Backs up the game binary once (Cyberpunk2077.orig), then re-signs it with the entitlements RED4ext needs
#    (dyld environment variables, unsigned executable memory, disabled library validation).
# Undo: Steam > Cyberpunk 2077 > Properties > Installed Files > Verify integrity restores the original binary.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME="$(cd "$HERE/../../.." && pwd)"
BIN="$GAME/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"
DB="$GAME/red4ext/bin/x64/cyberpunk2077_addresses.json"

[[ -f "$BIN" ]] || { echo "Cyberpunk2077.app not found next to red4ext/ ($GAME)"; exit 1; }

normalize() { tr -d '-' | tr '[:lower:]' '[:upper:]'; }
game_uuid=$(dwarfdump --uuid "$BIN" | awk '/arm64/{print $2}' | normalize)
db_uuid=$(grep -o '"uuid" *: *"[^"]*"' "$DB" | head -1 | sed 's/.*"\([^"]*\)"$/\1/' | normalize)
if [[ "$game_uuid" != "$db_uuid" ]]; then
    echo "This release was verified against game build $db_uuid, but the installed game is $game_uuid."
    echo "Not changing anything. Use the release made for your game version."
    exit 1
fi

if [[ ! -f "$GAME/Cyberpunk2077.orig" ]]; then
    cp -p "$BIN" "$GAME/Cyberpunk2077.orig"
    echo "Backed up the game binary to Cyberpunk2077.orig"
fi

"$HERE/codesign_macos.sh" exe "$BIN"
codesign -d --entitlements - --xml "$BIN" 2>/dev/null | grep -q allow-unsigned-executable-memory \
    || { echo "Re-signing did not apply the entitlements"; exit 1; }
echo "Done. Start the game with: \"$GAME/launch_red4ext.sh\""
