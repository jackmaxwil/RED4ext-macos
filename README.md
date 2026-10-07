# RED4ext (macOS)

Script extender and mod loader for Cyberpunk 2077 on macOS ARM64.

**Status:** Production ready — 11/11 hooks active, 5 plugins validated, 120s+ stable runtime.

## What it does

RED4ext loads `.dylib` plugins into the Cyberpunk 2077 process at startup using `DYLD_INSERT_LIBRARIES` injection. It provides the hooking infrastructure (via Frida Gadget) and address resolution system that all other mods depend on.

## Prerequisites

- macOS 14+ (Apple Silicon)
- Cyberpunk 2077 (Steam, macOS build v2.3.1+)
- CMake 3.24+, Clang 15+
- FridaGadget.dylib (placed in `red4ext/`)

## Build

```bash
mkdir build-macos && cd build-macos
cmake .. -DCMAKE_BUILD_TYPE=Release -DRED4EXT_USE_FRIDA_GUM=OFF
make -j$(sysctl -n hw.ncpu)
```

## Install

```bash
./scripts/macos_install.sh
```

Or manually copy `build-macos/libs/RED4ext.dylib` to `<game>/red4ext/`.

## Launch

```bash
cd "~/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
./launch_red4ext.sh
```

## Key files

| File | Purpose |
|------|---------|
| `src/dll/` | Core loader: plugin loading, state machine, hooking |
| `red4ext_hooks.js` | Frida hook definitions loaded at runtime |
| `deps/red4ext.sdk/cyberpunk2077_addresses.json` | Canonical address database (installed to `red4ext/bin/x64/`) |
| `scripts/macos_install.sh` | One-command installer |
| `launch_red4ext.sh` | Game launcher with DYLD injection |
| `docs/STATUS.md` | Detailed port status |

## Related projects

| Project | Description |
|---------|-------------|
| [RED4ext.SDK](../RED4ext.SDK) | C++ headers for building plugins |
| [TweakXL](../cp2077-tweak-xl) | TweakDB modification plugin |
| [ArchiveXL](../cp2077-archive-xl-macos) | Custom resource loading plugin |
| [ModMenu](../cp2077-modmenu) | In-game mod settings menu |
| [MetalFX Denoiser](../cp2077-metalfx-denoiser) | MetalFX-based RT denoising |
| [CyberMod Studio](../cybermod-studio) | macOS mod manager app |

## Attribution

Forked from [WopsS/RED4ext](https://github.com/WopsS/RED4ext). macOS port by memaxo.
