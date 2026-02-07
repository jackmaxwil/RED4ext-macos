# RED4ext macOS Port — Status

> **Last updated:** 2026-02-01  
> **Target game build:** Cyberpunk 2077 macOS **v2.3.1** (Steam)  
> **Target arch:** Apple Silicon (arm64)  

## What works

- **Loader injection**: Uses `DYLD_INSERT_LIBRARIES` via `launch_red4ext.sh`.
- **Hooking**: Uses **Frida Gadget** (JIT trampolines) and `red4ext_hooks.js`.
- **Plugin loading**: Loads `.dylib` plugins from `red4ext/plugins/<PluginName>/`.
- **SDK address resolution**: Uses `cyberpunk2077_addresses.json` (SDK) + optional symbol mapping.

## What is still risky / incomplete

- **Address correctness**: some resolved addresses are not yet comprehensively runtime-verified.
- **Plugin compatibility coverage**: only a subset of ports are validated end-to-end.
- **Hook edge cases**: Frida-based hooking differs from Windows Detours and may diverge under heavy mod stacks.

## Quick “is it working?” checklist

1. **Install** with `./scripts/macos_install.sh`.
2. **Launch** with `./launch_red4ext.sh`.
3. Confirm `red4ext/logs/red4ext.log` contains:
   - RED4ext loaded
   - Frida gadget loaded
   - plugins discovered/loaded (if installed)

## Key files (source of truth)

- **Hook scripts**
  - `red4ext_hooks.js`: hook definitions loaded by Frida Gadget.
- **Address databases**
  - `cyberpunk2077_addresses.json`: SDK address map used by plugins/SDK relocation.
  - `cyberpunk2077_symbols.json` (optional): hash→symbol mapping to improve resolution when symbols exist.
- **Launcher**
  - `launch_red4ext.sh`: sets env + launches the game.

## Scripts you’ll actually run

- **After a game update (address refresh)**:

```bash
python3 scripts/generate_addresses.py \
  "/path/to/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077" \
  --manual scripts/manual_addresses_template.json \
  --output cyberpunk2077_addresses.json
```

- **Optional: generate symbol mappings** (useful for exported symbols):

```bash
python3 scripts/generate_symbol_mapping.py \
  "/path/to/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077" \
  --output cyberpunk2077_symbols.json \
  --game-version 2.3.1
```

## Documentation index

- **Install / usage**: `docs/MACOS_PORT.md`
- **Hooking**: `docs/FRIDA_INTEGRATION.md`
- **Code signing**: `docs/MACOS_CODE_SIGNING.md`
- **Cyberpunk internals notes**: `docs/CYBERPUNK_INTERNALS.md`
- **Historical port notes**: `docs/porting/` (archived process docs)

