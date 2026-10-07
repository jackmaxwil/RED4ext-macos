# RED4ext for macOS

RED4ext is the script extender and plugin loader for Cyberpunk 2077. This is its macOS port. It loads `.dylib` plugins
into the game and hooks game functions natively, with an in-process arm64 hook engine. The release also contains
TweakXL, ArchiveXL and ModMenu.

Supported: **Cyberpunk 2077 2.3.1 (patch 2.31), Steam, Apple silicon Mac.** Other game builds are refused.

RED4ext is fail-closed. It uses only game addresses that have been verified for this exact game build, and it refuses to
load a plugin that needs any unverified address. The game then starts without that plugin.

## Install

Quit the game, open Terminal and run:

```bash
curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash
```

- Update: `curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- update`
- Uninstall: `curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- uninstall`
- Check your setup: `curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- doctor`
- Play with mods: `curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- play`

**Requirements:** Cyberpunk 2077 2.3.1 (Steam) on an Apple silicon Mac.

Prefer to do it by hand? See [docs/INSTALL_MACOS.md](docs/INSTALL_MACOS.md).

## Play

1. Start Steam and sign in.
2. Run:
   ```bash
   "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/launch_red4ext.sh"
   ```

The Steam **Play** button starts the game **without** mods. Always use `launch_red4ext.sh`.

In game: press `` ` `` (the key left of 1) or F10 for ModMenu. Press `\` to reload TweakXL tweaks from `r6/tweaks`.

## Installing mods

Put each kind of mod where it would go on Windows, inside the game folder:

| Mod files | Folder | Needs |
| --- | --- | --- |
| `.archive` (and `.archive.xl`) | `archive/pc/mod/` | ArchiveXL. The macOS game does not read this folder itself; ArchiveXL loads it. |
| `.yaml` / `.tweak` tweaks | `r6/tweaks/` | TweakXL |
| `.reds` scripts | `r6/scripts/` | Nothing. The launcher compiles them. |
| RED4ext plugins (`.dylib`) | `red4ext/plugins/<Name>/` | A macOS build of the plugin. Windows `.dll` plugins do not work. |

## Uninstall

```bash
curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- uninstall
```

It removes RED4ext, its plugins and their key bindings, and restores the original game binary. Your mods in
`archive/pc/mod`, `r6/scripts` and `r6/tweaks` stay. Steam > Cyberpunk 2077 > Properties > Installed Files > **Verify
integrity of game files** also restores the original game binary. To uninstall by hand, see
[docs/INSTALL_MACOS.md](docs/INSTALL_MACOS.md#uninstall).

## Troubleshooting

First run `curl -fsSL https://raw.githubusercontent.com/jackmaxwil/RED4ext-macos/main/install.sh | bash -s -- doctor`. It changes nothing and says what is wrong.

- **The game starts without mods:** you started it from Steam. Start it with `launch_red4ext.sh`.
- **`install_macos.sh` says the game build does not match:** your game version is not supported by this release. Wait
  for a release for your game version.
- **A plugin does not load:** open the newest `red4ext/logs/red4ext-*.log` in the game folder. It names each refused
  plugin and the reason, usually unverified addresses. That plugin needs an update; the game runs without it.
- **After a game update, no mods work:** a game patch changes the binary, and RED4ext refuses all hooks until its
  addresses are verified again for the new build. Wait for a new release. Do not edit the address database.
- **The launcher stops with "REDscript compilation failed":** a mod's scripts have errors. The printed errors name the
  file. Remove that mod, then launch again.
- **Saves or achievements are missing:** Steam was not running. Start Steam and sign in first.

## Build from source (developers)

Requires Xcode command line tools, CMake 3.23+ and Python 3. Clone the plugin repos next to this one; the test and
release tools expect these folder names:

```bash
mkdir cyberpunk && cd cyberpunk
git clone --recursive https://github.com/jackmaxwil/RED4ext-macos.git RED4ext
git clone https://github.com/jackmaxwil/RED4ext.SDK-macos.git RED4ext.SDK
git clone --recursive https://github.com/jackmaxwil/cp2077-tweak-xl-macos.git cp2077-tweak-xl
git clone --recursive https://github.com/jackmaxwil/cp2077-archive-xl-macos cp2077-archive-xl-macos
git clone https://github.com/jackmaxwil/cp2077-modmenu.git cp2077-modmenu
cd RED4ext
cmake -S . -B build-dev && cmake --build build-dev -j8
ctest --test-dir build-dev --output-on-failure
```

Test loop (needs the game installed and re-signed once with `scripts/codesign_macos.sh exe <game binary>`):

```bash
tools/cp-dev tweakxl --plugins TweakXL,ArchiveXL,ModMenu
```

`tools/cp-dev` builds RED4ext and the listed plugins, signs and installs them into the game folder, then runs
`tools/cp-run`, which starts the game in the background and runs an unattended in-game test. Scenarios are in
`tools/autotest/scenarios` (`tweakxl`, `tweakxl-reload`, `archivexl`, `modmenu`, `modmenu-ui`) plus `boot` and `load`.
Results go to `runs/<timestamp>-<scenario>/`. Exit code 0 means pass. `tools/cp-rollback` restores the stock game
binary.

`tools/cp-regress` runs the gate and every scenario with the plugins it needs (about 10 minutes, all in the
background) and prints a pass/fail table. Run it before every release.

`tools/cp-gate` is the offline release and patch-day gate. It never starts the game. It checks that the installed game
matches the address database, that the verified entries validate, and that every address each plugin uses is verified.
After a game update, follow the patch-day steps in its header.

Release: add a `## [X.Y.Z]` section to `CHANGELOG.md`, then push the tag `vX.Y.Z` (or `vX.Y.Z-rcN`, a prerelease).
`.github/workflows/release.yml` builds every component at `main`, runs the gate (without the installed-game check) and
publishes the zip, `install.sh` and `SHA256SUMS`. `scripts/create_release.sh VERSION` does the same build locally into
`release/`. Each zip has `red4ext/VERSION` and `red4ext/BUILD_INFO.json` (the commit of every component).

## Repository layout

| Path | Contents |
| --- | --- |
| `src/dll/` | The loader. `Platform/NativeHook*` is the arm64 hook engine, `Platform/PluginRequirements.cpp` the plugin address gate. |
| `src/plugin_check/` | `red4ext_plugin_check`, the same address gate as a command-line tool. |
| `deps/` | Git submodules. `deps/red4ext.sdk` is the macOS SDK fork and holds `cyberpunk2077_addresses.json`. |
| `install.sh` | The one-command installer (install, update, uninstall, doctor, play). Also attached to each release. |
| `scripts/` | Install, launch, signing and release scripts. |
| `tools/` | `cp-dev`, `cp-run`, `cp-gate`, `cp-rollback` and the in-game autotest scenarios. |
| `tests/` | Unit, integration and native hook tests (CTest). |
| `docs/` | Install guide and reference notes. |

## Credits

Forked from [WopsS/RED4ext](https://github.com/WopsS/RED4ext) by WopsS and contributors. MIT license, see
[LICENSE.md](LICENSE.md) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
