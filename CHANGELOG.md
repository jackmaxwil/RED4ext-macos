# Changelog

All notable changes to the RED4ext macOS port.

## [Unreleased]

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
- The Frida backend only logged calls and never ran RED4ext or plugin detours. The "126/126 resolved" and "8/8 hooks" figures were not verified.

### Tooling
- `launch_red4ext.sh` compiles only the scripts of plugins RED4ext will load, and refuses to start on a compile error. It cleans up on exit, so a later Steam launch stays vanilla.
- `install_macos.sh` checks the game build, backs up the binary and re-signs it.
- `tools/cp-gate` is the offline patch-day and release gate.

## [1.0.0-beta] - 2026-01-03 - Initial macOS Release (Beta)

> ⚠️ **Beta Release** - Not all features verified. Please report issues.

### Features

- **macOS ARM64 Support**
  - Native Apple Silicon (M1/M2/M3/M4) support
  - Mach-O binary format handling
  - POSIX API implementations

- **Complete Address Resolution**
  - 126/126 SDK addresses resolved
  - Symbol mapping via `cyberpunk2077_symbols.json` (21,332 symbols)
  - Address database via `cyberpunk2077_addresses.json`
  - Runtime address resolution from JSON database

- **Function Hooking via Frida**
  - 8/8 required hooks functional
  - Frida Gadget integration for W^X compatible hooking
  - JavaScript-based hook scripts
  - Dynamic trampoline generation

- **Plugin System**
  - .dylib plugin loading
  - Full SDK compatibility
  - TweakXL support verified

- **Build System**
  - CMake configuration for macOS/ARM64
  - Xcode toolchain support
  - Automated release packaging

- **Scripts**
  - `macos_install.sh` - One-command installation
  - `generate_addresses.py` - Address database generation
  - `create_release.sh` - Release packaging
  - Code signing scripts for mod support

- **Documentation**
  - Complete installation guide
  - Frida integration documentation
  - Plugin development examples
  - Address discovery techniques

### Compatibility

- **Game Version:** Cyberpunk 2077 v2.3.1 (macOS)
- **Platform:** macOS 12+ on Apple Silicon
- **Verified Plugins:** TweakXL

### Technical Details

#### Hook Functions (8/8 Working)
| Function | Purpose |
|----------|---------|
| Main | Entry point hook |
| CBaseEngine_InitScripts | Script initialization |
| CBaseEngine_LoadScripts | Script loading |
| CGameApplication_AddState | State management |
| AssertionFailed | Error handling |
| ScriptValidator_Validate | Script validation |
| GameInstance_CollectSaveableSystems | Save system |
| TweakDB hooks | TweakDB modification |

#### Address Resolution
- All 126 SDK addresses successfully resolved
- Automated discovery via string patterns and function analysis
- Manual verification for critical paths

### Known Limitations

- Windows .dll plugins require recompilation for macOS
- Some advanced mods may need porting
- Code signing required for modifications

---

## Upstream

This port is based on [RED4ext](https://github.com/WopsS/RED4ext) by WopsS.
See upstream repository for Windows version history.
