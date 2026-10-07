#!/bin/bash
# RED4ext for macOS: install, update, uninstall, check and play, in one command.
#
#   curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- VERB [OPTIONS]
#
# Verbs:   install (default), update, uninstall, doctor, play
# Options: --version X      a release version, e.g. 0.1.0-rc4 (default: the newest release)
#          --game-dir PATH  the Cyberpunk 2077 folder (default: Steam's; or set CP2077_GAME_DIR)
#
# Needs nothing beyond macOS itself (curl, shasum, ditto, perl, codesign, xattr).

# Everything is inside main(), so bash reads the whole script before running any of it (it arrives through a pipe).
main() {
    set -euo pipefail

    local REPO=jackmaxwil/RED4ext-macos
    local ONE_LINER="curl -fsSL https://raw.githubusercontent.com/$REPO/main/install.sh | bash"
    local GAME="${CP2077_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077}"
    local VERB=install VERSION=""

    while [[ $# -gt 0 ]]; do
        case $1 in
        install | update | uninstall | doctor | play) VERB=$1 ;;
        --version)
            [[ $# -ge 2 ]] || die "--version needs a value, for example: --version 0.1.0-rc4"
            VERSION=${2#v}
            shift
            ;;
        --game-dir)
            [[ $# -ge 2 ]] || die "--game-dir needs a folder, for example: --game-dir \"/Volumes/Games/Cyberpunk 2077\""
            GAME=${2%/}
            shift
            ;;
        -h | --help)
            echo "Usage: $ONE_LINER -s -- [install|update|uninstall|doctor|play] [--version X] [--game-dir PATH]"
            return 0
            ;;
        *) die "Unknown argument '$1'. Use one of: install, update, uninstall, doctor, play, --version X, --game-dir PATH." ;;
        esac
        shift
    done

    BIN="$GAME/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"
    case $VERB in
    install) do_install ;;
    update) do_update ;;
    uninstall) do_uninstall ;;
    doctor) do_doctor ;;
    play) do_play ;;
    esac
}

die() {
    echo "Error: $*" >&2
    exit 1
}

need_game() {
    [[ -f "$BIN" ]] || die "Cyberpunk 2077 not found in \"$GAME\". In Steam, right-click Cyberpunk 2077 > Manage > Browse local files shows its folder; run the command again with --game-dir \"<that folder>\" at the end (after: bash -s -- install)."
}

game_running() { pgrep -xq Cyberpunk2077; }

refuse_if_running() {
    if game_running; then die "Cyberpunk 2077 is running. Quit the game, then run this again."; fi
}

# The Mach-O UUID (LC_UUID) of a binary, uppercase hex without dashes.
macho_uuid() {
    perl -e 'open F,"<",$ARGV[0] or exit 1; binmode F; read F,$h,32; ($m,$ct,$st,$ft,$n)=unpack("V5",$h);
        exit 1 unless $m==0xfeedfacf; $o=32; for(1..$n){seek F,$o,0; read F,$c,8; ($cmd,$sz)=unpack("V2",$c);
        if($cmd==0x1b){read F,$u,16; print uc unpack("H*",$u); exit 0} $o+=$sz} exit 1' "$1"
}
db_uuid() { grep -o '"uuid" *: *"[^"]*"' "$1" | head -1 | sed 's/.*"\([^"]*\)"$/\1/' | tr -d '-' | tr '[:lower:]' '[:upper:]'; }
is_resigned() { codesign -d --entitlements - --xml "$1" 2>/dev/null | grep -q allow-unsigned-executable-memory; }
installed_version() { cat "$GAME/red4ext/VERSION" 2>/dev/null || true; }

# The newest release: the highest vX.Y.Z or vX.Y.Z-rcN. Release candidates count until a 1.0 or later final release
# exists.
latest_version() {
    curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=50" | perl -MJSON::PP -e '
        local $/; my @v;
        for (@{decode_json(<STDIN>)}) {
            next if $_->{draft} or $_->{tag_name} !~ /^v(\d+)\.(\d+)\.(\d+)(?:-rc(\d+))?$/;
            push @v, [$1, $2, $3, defined $4 ? 0 : 1, $4 // 0, substr($_->{tag_name}, 1)];
        }
        my @final = grep { $_->[3] && $_->[0] >= 1 } @v;
        @v = @final if @final;
        @v = sort { $b->[0] <=> $a->[0] || $b->[1] <=> $a->[1] || $b->[2] <=> $a->[2] || $b->[3] <=> $a->[3]
            || $b->[4] <=> $a->[4] } @v;
        print $v[0][5] if @v;'
}

resolve_version() {
    if [[ -z "$VERSION" ]]; then
        VERSION=$(latest_version) ||
            die "Could not ask GitHub for the newest release (no internet, or rate-limited). Try again in a few minutes, or pass --version X (see https://github.com/$REPO/releases)."
        [[ -n "$VERSION" ]] || die "No release found at https://github.com/$REPO/releases. Try again later."
    fi
}

do_install() {
    [[ "$(uname -s)" == Darwin && "$(sysctl -n hw.optional.arm64 2>/dev/null)" == 1 ]] ||
        die "RED4ext for macOS needs an Apple silicon Mac (M1 or later)."
    refuse_if_running
    need_game
    resolve_version

    local name="RED4ext-macOS-arm64-$VERSION" url="https://github.com/$REPO/releases/download/v$VERSION"
    tmp=$(mktemp -d) # global: the EXIT trap runs after this function returns
    trap 'rm -rf "$tmp"' EXIT

    echo "Downloading RED4ext $VERSION..."
    curl -fL --progress-bar -o "$tmp/$name.zip" "$url/$name.zip" ||
        die "Could not download $url/$name.zip. Check the version at https://github.com/$REPO/releases and your internet connection."

    # Every release since 0.1.0-rc4 has SHA256SUMS; older ones have a .zip.sha256 next to the zip.
    local want=""
    if curl -fsSL -o "$tmp/SHA256SUMS" "$url/SHA256SUMS"; then
        want=$(awk -v f="$name.zip" '$2 == f { print $1 }' "$tmp/SHA256SUMS")
    elif curl -fsSL -o "$tmp/zip.sha256" "$url/$name.zip.sha256"; then
        want=$(awk '{ print $1; exit }' "$tmp/zip.sha256")
    fi
    [[ -n "$want" ]] || die "Could not get the checksum for $name.zip. Nothing was changed. Try again in a few minutes."
    [[ "$(shasum -a 256 "$tmp/$name.zip" | awk '{ print $1 }')" == "$want" ]] ||
        die "The download does not match its published checksum (corrupt or tampered). Nothing was changed. Run the command again; if this repeats, open an issue at https://github.com/$REPO/issues."
    echo "Checksum OK."

    ditto -x -k "$tmp/$name.zip" "$tmp" || die "Could not unpack $name.zip. Nothing was changed. Run the command again."
    [[ -d "$tmp/$name/red4ext" ]] || die "$name.zip does not have the expected layout. Nothing was changed. Please open an issue at https://github.com/$REPO/issues."

    # Check the game build before changing anything. install_macos.sh checks again.
    local game_uuid want_uuid
    game_uuid=$(macho_uuid "$BIN") || die "Could not read the game binary at \"$BIN\". In Steam, use Verify integrity of game files, then run this again."
    want_uuid=$(db_uuid "$tmp/$name/red4ext/bin/x64/cyberpunk2077_addresses.json")
    [[ "$game_uuid" == "$want_uuid" ]] ||
        die "RED4ext $VERSION supports game build $want_uuid (Cyberpunk 2077 2.3.1, Steam), but yours is $game_uuid. Nothing was changed. Wait for a RED4ext release for your game version."

    echo "Copying into \"$GAME\"..."
    ditto "$tmp/$name" "$GAME" || die "Could not copy into \"$GAME\". Check that the folder is writable, then run this again."
    local out
    out=$("$GAME/red4ext/macos/scripts/install_macos.sh" </dev/null 2>&1) || {
        echo "$out" >&2
        die "The one-time setup (install_macos.sh) failed; its output is above. Fix that, then run this command again."
    }

    echo
    echo "RED4ext $VERSION is installed. Next:"
    echo "  1. Start Steam and sign in."
    echo "  2. Start the game with mods by running:"
    echo "     \"$GAME/launch_red4ext.sh\""
    echo "Steam's Play button starts the game without mods."
}

do_update() {
    need_game
    local have
    have=$(installed_version)
    resolve_version
    if [[ -n "$have" && "$have" == "$VERSION" ]]; then
        echo "RED4ext $have is installed, the newest release. Nothing to do."
        return 0
    fi
    echo "Installed: ${have:-none or older than 0.1.0-rc4}. Updating to $VERSION."
    do_install
}

do_uninstall() {
    refuse_if_running
    need_game
    local orig="$GAME/Cyberpunk2077.orig"
    if [[ ! -e "$GAME/red4ext" && ! -e "$GAME/launch_red4ext.sh" && ! -f "$orig" ]]; then
        echo "RED4ext is not installed in \"$GAME\". Nothing to do."
        return 0
    fi
    rm -rf "$GAME/red4ext" "$GAME/launch_red4ext.sh" "$GAME/INSTALL_MACOS.md" "$GAME/r6/input/modmenu.xml" \
        "$GAME/r6/input/tweakxl.xml" "$GAME/r6/scripts/zz_red4ext_plugins"
    echo "Removed RED4ext, its plugins and their key bindings. Your mods in archive/pc/mod, r6/scripts and r6/tweaks are untouched."

    # Restore the stock binary only if the backup is the same game build. After a Steam update the backup is older
    # than the game, and Steam has already replaced the re-signed binary.
    if [[ -f "$orig" ]]; then
        if is_resigned "$BIN" && [[ "$(macho_uuid "$orig")" == "$(macho_uuid "$BIN")" ]]; then
            mv -f "$orig" "$BIN"
            echo "Restored the original game binary from Cyberpunk2077.orig."
        else
            rm -f "$orig"
            echo "Removed the backup Cyberpunk2077.orig (Steam already replaced or restored the game binary)."
        fi
    fi
    # Recompile the game's scripts without the plugins' scripts, as the launcher does when the game exits.
    if [[ -x "$GAME/engine/tools/scc" ]]; then
        "$GAME/engine/tools/scc" -compile "$GAME/r6/scripts" </dev/null >/dev/null 2>&1 || true
    fi
    echo "Done. Steam's Play button starts the game as before. Steam > Cyberpunk 2077 > Properties > Installed Files > Verify integrity of game files also restores the original game files."
}

# Read-only: one line per check.
do_doctor() {
    local fail=0
    ok() { echo "OK    $*"; }
    bad() {
        echo "FAIL  $*"
        fail=1
    }
    info() { echo "--    $*"; }

    if [[ -f "$BIN" ]]; then ok "Game found: $GAME"; else
        bad "Game not found in \"$GAME\". Pass --game-dir \"<folder>\" (Steam > Cyberpunk 2077 > Manage > Browse local files)."
        return 1
    fi

    local have db="$GAME/red4ext/bin/x64/cyberpunk2077_addresses.json"
    have=$(installed_version)
    if [[ -n "$have" ]]; then ok "RED4ext $have installed"
    elif [[ -f "$GAME/red4ext/RED4ext.dylib" ]]; then ok "RED4ext installed (version older than 0.1.0-rc4; update: $ONE_LINER -s -- update)"
    else bad "RED4ext is not installed. Install: $ONE_LINER"
    fi

    local game_uuid db_id
    game_uuid=$(macho_uuid "$BIN" || echo unreadable)
    if [[ -f "$db" ]]; then
        db_id=$(db_uuid "$db")
        if [[ "$game_uuid" == "$db_id" ]]; then ok "Game build $game_uuid is the one RED4ext supports"
        else bad "Game build $game_uuid, but RED4ext supports $db_id. Wait for a RED4ext release for your game version."
        fi
    else info "Game build $game_uuid (RED4ext not installed, nothing to compare)"
    fi

    if is_resigned "$BIN"; then ok "Game binary is set up for RED4ext (re-signed)"
    else bad "Game binary is not set up for RED4ext (a Steam update or Verify integrity replaced it). Run: \"$GAME/red4ext/macos/scripts/install_macos.sh\""
    fi

    local plugins
    plugins=$(find "$GAME/red4ext/plugins" -mindepth 2 -maxdepth 2 -name '*.dylib' -exec basename {} .dylib \; 2>/dev/null | sort | tr '\n' ' ' || true)
    if [[ -n "$plugins" ]]; then ok "Plugins: ${plugins% }"; else bad "No plugins in red4ext/plugins. Reinstall: $ONE_LINER"; fi

    if pgrep -q steam_osx; then ok "Steam is running"; else info "Steam is not running. Start Steam and sign in before playing."; fi
    if game_running; then info "The game is running"; fi
    return $fail
}

do_play() {
    need_game
    [[ -x "$GAME/launch_red4ext.sh" ]] || die "RED4ext is not installed. Install it first: $ONE_LINER"
    if game_running; then die "Cyberpunk 2077 is already running."; fi
    exec "$GAME/launch_red4ext.sh" </dev/null
}

main "$@"
