# RED4ext for macOS: installation

This release is for **Cyberpunk 2077 2.3.1 (patch 2.31) on Apple silicon, Steam**. The address database was verified against that exact game build (UUID `A6656ADC-FBE2-36A4-9B9D-B4A9DE645089`). On any other build, the installer refuses to change anything.

## Contents

| Path | What it is |
| --- | --- |
| `red4ext/RED4ext.dylib` | The mod loader. It hooks the game natively, without Frida. |
| `red4ext/bin/x64/cyberpunk2077_addresses.json` | The game addresses. Only entries marked verified are ever used. |
| `red4ext/bin/red4ext_plugin_check` | Used by the launcher. It compiles a plugin's scripts only if RED4ext will load that plugin. |
| `red4ext/plugins/TweakXL` | TweakXL, which loads TweakDB tweaks from `r6/tweaks/*.yaml`. |
| `red4ext/plugins/ModMenu` | ModMenu. Press ` (the key left of 1) or F10 in game. |
| `red4ext/plugins/ArchiveXL` | ArchiveXL, which loads `.xl` resource extensions from mods. The packed `ArchiveXL.archive` (built with WolvenKit) is not included yet, so its built-in character-customization fixes log "not ready" errors. Hot reload is disabled on macOS. |
| `r6/input/modmenu.xml`, `r6/input/tweakxl.xml` | Key bindings: ModMenu (`` ` ``) and TweakXL hot reload (`\`). |
| `launch_red4ext.sh` | The launcher. |
| `red4ext/macos/scripts/install_macos.sh` | One-time setup that re-signs the game binary. |

## Install

1. Quit the game.
2. Unzip the archive over the game folder, which is usually `~/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077`. `launch_red4ext.sh` should end up next to `Cyberpunk2077.app`.
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

## After a game update

A game patch changes the binary. RED4ext then refuses to hook anything, and the game runs unmodded, until a release for the new build is out. Do not edit the address database by hand.

## Uninstall

1. Steam > Cyberpunk 2077 > Properties > Installed Files > **Verify integrity of game files**. This restores the original, unmodified binary.
2. Delete:
   - `red4ext/`
   - `launch_red4ext.sh`
   - `r6/input/modmenu.xml` and `r6/input/tweakxl.xml`
   - `r6/scripts/zz_red4ext_plugins/`
   - `Cyberpunk2077.orig`
