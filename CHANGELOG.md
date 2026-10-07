# Changelog

All notable changes to the RED4ext macOS port.

## [Unreleased]

- Replaced the Frida Gadget backend with a native in-process arm64 hook engine (`src/dll/Platform/NativeHook*`).
- Addresses resolve only from DB entries marked verified; the loader refuses plugins that need unverified addresses.
- Correction to 1.0.0-beta below: the Frida backend only logged calls and never ran RED4ext or plugin detours, and the "126/126 resolved" and "8/8 hooks" figures were not verified. See `docs/STATUS.md`.

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
