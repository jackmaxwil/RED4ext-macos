# Changelog

All notable changes to the RED4ext macOS port.

## [Unreleased]

- **Upstream sync:** RED4ext 1.30.0, RED4ext.SDK 1.0 (API v1), TweakXL 1.11.4, ArchiveXL 1.27.4. Plugins built against SDK 1.0 load; v0 plugins still load. Tested with `tools/cp-regress` (all scenarios) and 73 mod archives in the world.
- **Off on macOS until verified:** the `QuickExit` hook (shutdown stays on `atexit`), ArchiveXL's collision-shape deletions and its nails-colour fallback.

## [0.1.0-rc3] - 2026-10-07

Tested with `tools/cp-regress` (gate plus every in-game scenario: TweakXL, ArchiveXL, ModMenu, ModMenu UI, TweakXL hot reload, loading a save).

- **Mods in `archive/pc/mod` now load.** The macOS game never reads that folder; ArchiveXL now loads it (in name order), together with the mods' `.archive.xl` files. Tested with 73 mod archives in the world.
- **ModMenu:** no longer logs every key press; the overlay is clickable (the game shows its cursor and routes input to the menu while it is open); `` ` ``, Esc or the Close button closes it; toggles work; entries match their types.
- **ArchiveXL's packed resources are included** (`ArchiveXL.archive`, from upstream's 1.26.1 release with a pinned checksum), so its character-customization fixes load instead of logging "not ready" errors.
- **TweakXL hot reload:** press `\` in game to reload `r6/tweaks`.
- **Key bindings from `r6/input` now take effect.** The launcher merged them from the wrong folder, so they were silently ignored.
- **Launcher:** stops with a clear message when Steam replaced the game binary (run `install_macos.sh` again) or updated the game (wait for a release).
- **Installer:** no longer needs the Xcode command line tools; clears the quarantine flag Finder adds to unzipped files.
- **Docs:** README and INSTALL_MACOS are quickstarts; the install steps match the zip layout (it has a top-level folder); where each kind of mod goes.

## [0.1.0-rc2] - 2026-10-07

Tested in game: RED4ext with ModMenu, TweakXL and ArchiveXL loaded together. All TweakXL checks (6) and ArchiveXL checks (8) pass, and the game exits cleanly.

- **ArchiveXL 1.26.1 (macOS) is ported to the arm64 ABI:**
  - x8 struct returns, argument fixes, and virtual slots shifted by +8;
  - inlined game functions replaced;
  - the macOS layouts of the classes it uses;
  - RedLib registration;
  - static game objects are kept, not released, at exit.
- **SDK:**
  - `CGameEngine::framework` is at +0x338 on macOS;
  - TransactionSystem `MatchVisualTag` slot order and ItemID-by-value signatures;
  - macOS layouts for 1,503 generated classes, from the live RTTI dump;
  - the address table now outlives static destructors.
- **Loader:** hooks attach from their final storage. The hook engine used to keep a pointer to a temporary, so detaching failed and changes to a hook chain wrote through a dangling address.
- **Launcher:** passes Steam's app ID when Steam is running, so the game's Steam features work (saves, achievements).

## [0.1.0-rc1] - 2026-10-07

Tested in game on Cyberpunk 2077 2.3.1 (Steam, UUID A6656ADC), Apple silicon: main menu, TweakXL and ModMenu loaded together, and all autotest checks pass (`tools/cp-run tweakxl`).

### Loader
- The native arm64 hook engine supports near (4-byte `B`) and far (12-byte `ADRP`/`ADD`/`BR x17`) patches. It refuses any target it cannot relocate exactly.
- Fail-closed address resolution: only DB entries with `"verified": true` resolve. The evidence for each entry is in `RED4ext.SDK/docs/ADDRESS_AUDIT.md` and `docs/re/`.
- The loader refuses any plugin that uses an unverified address. `red4ext_plugin_check` applies the same gate before launch.
- Core hooks are attempted only when their address is verified. The script-pipeline hooks are not needed on macOS, because `scc` compiles scripts before launch. AssertionFailed, CollectSaveableSystems and SessionActive stay off until a plugin needs them.
- Plugins are never `dlclose`d, and hooks stay installed until the process exits.
- `RED4EXT_DUMP_RTTI=1` writes the live class layouts (`logs/rtti_layout_macos.json`).

### SDK (macOS ABI)
- Fixed the SharedSpinLock encoding (writer bit 0x80).
- Functions that return through x8 now declare it: `JobQueue::Capture`, `ISerializable::sub_78`/`sub_B0`/`sub_C8`, and others.
- `Handle(T*)` is inlined, because the game has no out-of-line copy.
- `CClassFunction` construction now takes a member-function pointer.
- Removed heuristic TLS discovery.
- Fixed `FlatValue::GetTypeName`.

### Plugins
- **TweakXL 1.11.3 (macOS):**
  - verified addresses and the arm64 signatures for stats ranges;
  - RedLib type registration, which needed explicit template instantiation under clang;
  - clang enum names;
  - the game root path inside the `.app`;
  - fail-closed guards.
- **ModMenu 0.1.0:** native bridge plus an F10 binding in `r6/input/modmenu.xml`.
- **ArchiveXL:** not included yet (its macOS ABI port is in progress).

### Correction to 1.0.0-beta
- The 1.0.0-beta hooking backend only logged calls and never ran RED4ext or plugin detours. Its "126/126 resolved" and "8/8 hooks" figures were not verified. It is replaced by the native arm64 hook engine.

### Tooling
- `launch_red4ext.sh` compiles only the scripts of plugins RED4ext will load, and refuses to start on a compile error. It cleans up on exit, so a later Steam launch stays vanilla.
- `install_macos.sh` checks the game build, backs up the binary and re-signs it.
- `tools/cp-gate` is the offline patch-day and release gate.

## [1.0.0-beta] - 2026-01-03 - Withdrawn

The first experimental macOS build. Its hooking backend never ran detours (see the correction above), and its address figures were not verified. Superseded by 0.1.0-rc1.
