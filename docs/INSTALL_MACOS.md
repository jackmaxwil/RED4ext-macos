# RED4ext for macOS: installation

This release is for **Cyberpunk 2077 2.3.1 (patch 2.31) on Apple silicon, Steam**. The address database was verified against that exact game build (UUID `A6656ADC-FBE2-36A4-9B9D-B4A9DE645089`). On any other build, the installer refuses to change anything.

## Quick install

Quit the game, open Terminal and run:

```bash
curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash
```

It downloads the newest release, verifies its checksum, checks your game build, copies the files into the game folder
and runs the one-time setup (step 3 below). Then start Steam and run `launch_red4ext.sh` (see [Play](#play)). Other
commands: add `-s -- update`, `-s -- uninstall`, `-s -- doctor` (checks your setup, changes nothing) or `-s -- play`
after `bash`. If the game is not in Steam's default folder, add `--game-dir "/path/to/Cyberpunk 2077"`.

The rest of this page is the same install done by hand.

## Contents

| Path | What it is |
| --- | --- |
| `red4ext/RED4ext.dylib` | The mod loader. It hooks the game natively. |
| `red4ext/VERSION`, `red4ext/BUILD_INFO.json` | The release version, and the commit of every component it was built from. |
| `red4ext/bin/x64/cyberpunk2077_addresses.json` | The game addresses. Only entries marked verified are ever used. |
| `red4ext/bin/red4ext_plugin_check` | Used by the launcher. It compiles a plugin's scripts only if RED4ext will load that plugin. |
| `red4ext/plugins/TweakXL` | TweakXL, which loads TweakDB tweaks from `r6/tweaks/*.yaml`. |
| `red4ext/plugins/ModMenu` | ModMenu. Press ` (the key left of 1) or F10 in game. |
| `red4ext/plugins/ArchiveXL` | ArchiveXL, which loads mod archives from `archive/pc/mod` and their `.xl` resource extensions. Hot reload is disabled on macOS. |
| `r6/input/modmenu.xml`, `r6/input/tweakxl.xml` | Key bindings: ModMenu (`` ` ``) and TweakXL hot reload (`\`). |
| `launch_red4ext.sh` | The launcher. |
| `red4ext/macos/scripts/install_macos.sh` | One-time setup that re-signs the game binary. |

## Install by hand

1. Quit the game.
2. Unzip the archive. It contains one folder, `RED4ext-macOS-arm64-VERSION`. Copy the contents of that folder into the game folder, which is usually `~/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077`. `launch_red4ext.sh` should end up next to `Cyberpunk2077.app`. In Terminal, from the folder that holds the zip:
   ```bash
   unzip -o RED4ext-macOS-arm64-VERSION.zip
   ditto RED4ext-macOS-arm64-VERSION "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
   ```
3. Run the one-time setup:
   ```bash
   "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/red4ext/macos/scripts/install_macos.sh"
   ```
   It backs up the game binary to `Cyberpunk2077.orig`. It then re-signs the binary so that RED4ext can be injected and can patch code.

## Play

Start Steam first and sign in, then run:

```bash
"$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/launch_red4ext.sh"
```

The launcher compiles the game's scripts together with the scripts of the plugins RED4ext will load. If compilation fails, it stops before starting the game. **The Steam Play button starts the game without mods.**

Logs are in `red4ext/logs/` and in each plugin's folder. A plugin that RED4ext refuses is named in `red4ext/logs/red4ext-*.log`, along with the reason. One possible reason is an unverified game address. The game still starts without that plugin.

## Editing tweaks while playing

TweakXL reloads `r6/tweaks` when you press `\` (backslash) in game, and shows "TweakXL: tweaks reloaded". Changed and added records apply at once. Objects that already copied a record, such as an equipped item or a spawned NPC, pick up the change when they are created again.

## Installing mods

Put each kind of mod where it would go on Windows, inside the game folder:

| Mod files | Folder | Needs |
| --- | --- | --- |
| `.archive` (and `.archive.xl`) | `archive/pc/mod/` | ArchiveXL. The macOS game does not read this folder itself; ArchiveXL loads it. |
| `.yaml` / `.tweak` tweaks | `r6/tweaks/` | TweakXL |
| `.reds` scripts | `r6/scripts/` | Nothing. The launcher compiles them. |
| RED4ext plugins (`.dylib`) | `red4ext/plugins/<Name>/` | A macOS build of the plugin. Windows `.dll` plugins do not work. |

## After a game update

A game patch changes the binary. RED4ext then refuses to hook anything, and the game runs unmodded, until a release for the new build is out. Do not edit the address database by hand.

## Uninstall

```bash
curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- uninstall
```

Or by hand:

1. Steam > Cyberpunk 2077 > Properties > Installed Files > **Verify integrity of game files**. This restores the original, unmodified binary.
2. Delete:
   - `red4ext/`
   - `launch_red4ext.sh`
   - `r6/input/modmenu.xml` and `r6/input/tweakxl.xml`
   - `r6/scripts/zz_red4ext_plugins/`
   - `Cyberpunk2077.orig`
