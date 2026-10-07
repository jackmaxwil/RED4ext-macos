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

# The game binary's Mach-O UUID (LC_UUID), uppercase hex without dashes. Perl ships with macOS; dwarfdump/otool need
# the Xcode command line tools.
macho_uuid() {
    perl -e 'open F,"<",$ARGV[0] or exit 1; binmode F; read F,$h,32; ($m,$ct,$st,$ft,$n)=unpack("V5",$h);
        exit 1 unless $m==0xfeedfacf; $o=32; for(1..$n){seek F,$o,0; read F,$c,8; ($cmd,$sz)=unpack("V2",$c);
        if($cmd==0x1b){read F,$u,16; print uc unpack("H*",$u); exit 0} $o+=$sz} exit 1' "$1"
}
db_uuid() { grep -o '"uuid" *: *"[^"]*"' "$1" | head -1 | sed 's/.*"\([^"]*\)"$/\1/' | tr -d '-' | tr '[:lower:]' '[:upper:]'; }
game_uuid=$(macho_uuid "$BIN")
db_uuid=$(db_uuid "$DB")
if [[ "$game_uuid" != "$db_uuid" ]]; then
    echo "This release was verified against game build $db_uuid, but the installed game is $game_uuid."
    echo "Not changing anything. Use the release made for your game version."
    exit 1
fi

# Files unzipped with Finder carry the quarantine flag, and macOS then refuses to load the unsigned plugins.
xattr -dr com.apple.quarantine "$GAME/red4ext" "$GAME/launch_red4ext.sh" "$GAME/r6/input" 2>/dev/null || true

if [[ ! -f "$GAME/Cyberpunk2077.orig" ]]; then
    cp -p "$BIN" "$GAME/Cyberpunk2077.orig"
    echo "Backed up the game binary to Cyberpunk2077.orig"
fi

# Already set up (an earlier install, or an update): nothing to re-sign. Re-signing an app needs macOS's
# App Management permission for the terminal, so it is only done when the binary actually lacks the entitlements.
if codesign -d --entitlements - --xml "$BIN" 2>/dev/null | grep -q allow-unsigned-executable-memory; then
    echo "The game binary is already set up for RED4ext."
    echo "Done. Start the game with: \"$GAME/launch_red4ext.sh\""
    exit 0
fi

if ! "$HERE/codesign_macos.sh" exe "$BIN"; then
    echo "Could not re-sign the game binary."
    echo "If the error says \"Operation not permitted\": System Settings > Privacy & Security > App Management, allow"
    echo "your terminal app, then run this again."
    exit 1
fi
codesign -d --entitlements - --xml "$BIN" 2>/dev/null | grep -q allow-unsigned-executable-memory \
    || { echo "Re-signing did not apply the entitlements"; exit 1; }
echo "Done. Start the game with: \"$GAME/launch_red4ext.sh\""
