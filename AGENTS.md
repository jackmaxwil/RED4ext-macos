# RED4ext macOS Port - Agent Guidelines

## Project Context

RED4ext is a script extender/mod loader for Cyberpunk 2077, ported from Windows to macOS ARM64. This port replaces Windows-specific APIs (Detours, PE loading) with macOS equivalents (Frida/fishhook, Mach-O parsing).

## Current Status (Canonical)

See `docs/STATUS.md` for the up-to-date “what works / what’s risky” snapshot and the quick validation checklist.

## Development Practices

### Platform Awareness

1. **macOS-first.** All new code must compile and run on macOS ARM64. Windows compatibility is secondary.
2. **No Windows APIs.** Never use `<windows.h>`, Win32 types (`DWORD`, `HANDLE`), or PE-specific structures in shared code paths.
3. **Conditional compilation.** Use `#ifdef __APPLE__` for macOS-specific code; mirror with `#ifdef _WIN32` for Windows.
4. **Universal headers.** Prefer POSIX APIs (`<unistd.h>`, `<dlfcn.h>`, `<mach-o/dyld.h>`) over platform-specific ones.

### Hooking and Injection

1. **Frida for hooks.** Use Frida Gadget (`FridaGadget.dylib`) for runtime function hooking via JavaScript (`red4ext_hooks.js`).
2. **fishhook for symbols.** Use fishhook for rebinding dyld symbol stubs when Frida is unavailable.
3. **No Detours.** Microsoft Detours is Windows-only; never reference it in macOS code paths.
4. **Address resolution.** All function addresses come from `cyberpunk2077_addresses.json` via the SDK's `UniversalRelocBase::Resolve`.

### Binary Analysis

1. **Mach-O format.** The game binary is Mach-O ARM64, not PE. Use `otool`, `nm`, and custom Python scripts for analysis.
2. **Chained fixups.** macOS uses chained fixups for pointers in `__DATA_CONST`. Decode with `(raw & 0xFFFFFFFF) + BASE`.
3. **ASLR handling.** Always compute addresses relative to image base from `_dyld_get_image_header(0)`.
4. **Scripts location.** Address discovery scripts live in `scripts/`. Run `generate_addresses.py` to regenerate the address database.

## Architecture

### Directory Structure

```
src/
├── dll/           # Main loader logic (entry point, plugin loading)
├── hooks/         # Function hooks and trampolines
├── macos/         # macOS-specific implementations
└── shared/        # Cross-platform utilities
deps/
├── fishhook/      # Symbol rebinding for macOS
├── red4ext.sdk/   # Embedded SDK copy
└── frida/         # Frida Gadget (runtime)
scripts/           # Python analysis tools for address discovery
```

### Import Rules

1. **SDK isolation.** RED4ext.SDK headers are in `deps/red4ext.sdk/include/`. Never modify them directly; submit SDK changes upstream.
2. **No circular deps.** `src/` depends on `deps/`; `deps/` never imports from `src/`.
3. **Platform abstraction.** Platform-specific code in `src/macos/` or `src/windows/`; shared interfaces in `src/shared/`.

## Code Standards

### Naming

1. **Files.** Lowercase with underscores: `address_resolver.cpp`, `hook_manager.hpp`.
2. **Classes.** PascalCase: `PluginManager`, `HookTransaction`.
3. **Functions.** PascalCase for public, camelCase for private: `LoadPlugin()`, `parseHeader()`.
4. **Constants.** SCREAMING_SNAKE: `BASE_ADDRESS`, `MAX_PLUGINS`.

### Memory and Safety

1. **RAII everywhere.** Use smart pointers; raw `new`/`delete` requires justification.
2. **No exceptions in hooks.** Hook callbacks must not throw; catch and log internally.
3. **Validate addresses.** Before dereferencing any resolved address, verify it's within valid segments.
4. **Atomic plugin ops.** Plugin load/unload must be atomic with proper mutex guards.

### Logging

1. **spdlog only.** Use `spdlog` for all logging; never `printf` or `std::cout`.
2. **Log levels.** `trace` for hooks, `debug` for loading, `info` for lifecycle, `warning` for fallbacks, `error` for failures.
3. **Context in messages.** Include plugin name, address, or hash in log messages for debugging.

## Testing

1. **Build test.** `cmake .. && make` must succeed with zero errors on macOS.
2. **Runtime test.** Launch game via `launch_red4ext.sh` and verify logs show successful hook attachment.
3. **Address validation.** Run `scripts/generate_addresses.py` and verify all 126 SDK addresses resolve.

## Address Discovery

### Adding New Addresses

1. **Find via strings.** Search for error message strings, trace ADRP+ADD references to function prologues.
2. **VTable extraction.** Identify vtables in `__DATA_CONST`, decode chained fixup entries.
3. **Update manual file.** Add discoveries to `scripts/manual_addresses_template.json` with status and evidence.
4. **Regenerate.** Run `python3 scripts/generate_addresses.py` to update the final `cyberpunk2077_addresses.json`.

### Verification

1. **Cross-reference.** Compare discovered addresses against Windows IDA/Ghidra databases when available.
2. **Prologue check.** Valid function addresses should point to `STP X29, X30` or `SUB SP, SP` prologues.
3. **Segment bounds.** Text addresses must be within `__TEXT` segment; data pointers within `__DATA*`.

## Git Practices

1. **Atomic commits.** One logical change per commit; separate platform-specific changes.
2. **Branch naming.** `macos/feature-name` for macOS work; `fix/issue-description` for bugs.
3. **No force push.** Never force-push to `macos-port` or `main` branches.

## Common Pitfalls

1. **Null address resolution.** If `Resolve()` returns 0, the address is missing from the JSON—add it.
2. **Frida not loading.** Ensure `FridaGadget.dylib` is signed and in the correct path.
3. **Plugin crash on load.** Check plugin's address resolver override matches current game version.
4. **Hooks not triggering.** Verify address offset is correct and hook script is loaded in `red4ext_hooks.js`.

## Cyberpunk 2077 Internals Knowledge

### Binary Architecture

- **Format**: Mach-O ARM64, ~150MB executable
- **Image Base**: `0x100000000`
- **Entry Point**: Offset `0x31E18` (via LC_MAIN)
- **Chained Fixups**: Modern macOS uses `(raw & 0xFFFFFFFF) + base` for pointers

### Function Clustering

Related functions are grouped in memory. Key clusters:

| Subsystem | Range | Size |
|-----------|-------|------|
| RTTI | 0x4D3xxx - 0x4D5xxx | ~8KB |
| TweakDB | 0x2B73xxx - 0x2B7Dxxx | ~40KB |
| StatsDataSystem | 0x3A93xxx - 0x3A94xxx | ~8KB |

### Address Discovery Techniques

**Most Effective: String References (~85% success)**

1. Find string with `strings Cyberpunk2077 | grep "pattern"`
2. Locate ADRP+ADD referencing string address
3. Walk backwards to function prologue (STP X29, X30)

**Member Offset Access**

Search for LDR instructions with known struct offsets:
```asm
LDR X?, [X0, #0xD8]   ; StatsDataSystem.statRecords
LDR X?, [X0, #0xE8]   ; StatsDataSystem.statParams
```

**Function Proximity**

After finding one function, search nearby (±64KB) for related functions.

### Key Data Structures

```cpp
// StatsDataSystem layout
struct StatsDataSystem {
    // ...
    DynArray<TweakDBID> statRecords;  // 0xD8
    DynArray<StatParams> statParams;  // 0xE8
    SharedMutex statLock;             // 0xFC
};

// TweakDBID
struct TweakDBID {
    uint32_t nameHash;    // FNV1a hash
    uint8_t  nameLength;
    uint8_t  tdbOffset[3];
};
```

### Valid ARM64 Prologues

```asm
STP X29, X30, [SP, #-0x??]!   ; Frame setup
SUB SP, SP, #0x??              ; Stack allocation
```

### Tools Reference

```bash
strings Cyberpunk2077 | grep "pattern"  # Find strings
otool -tV Cyberpunk2077                 # Disassemble
nm -n Cyberpunk2077 | c++filt          # List symbols
otool -l Cyberpunk2077                  # Segment info
```

See `docs/CYBERPUNK_INTERNALS.md` for complete reference.

## Learned User Preferences

- When asked to "create a plan", make all design decisions autonomously — do not ask clarifying questions
- When implementing a plan, do not edit the plan file itself
- When a plan has todos, do not recreate them — mark them as `in_progress` sequentially and work through all of them without stopping
- Prefer clean two-pass pipeline runs: generate base data without own prior output, then run analysis, then final merge — avoids stale-data oscillation
- Commit changes in logical groups, not as a single monolithic commit
- Standardize branch names before pushing (use `macos/feature-name` convention)
- Run test commands in foreground with adequate `block_until_ms` — do not background with sleep-poll loops

## Learned Workspace Facts

- All cyberpunk repos live under `~/Development/cyberpunk/` (RED4ext, RED4ext.SDK, cp2077-tweak-xl, cp2077-archive-xl-macos, cp2077-metalfx-denoiser, cp2077-modmenu, macos-modmanager, cybermod-studio)
- Reverse engineering analysis scripts live in `scripts/re_tools/` with JSON databases in the same directory
- The RE pipeline order is: `cname_hash_scanner.py` → `vtable_propagator.py` → `vtable_hierarchy.py` → `callgraph_builder.py` → `cross_reference.py`
- `function_map.json` is the unified output of `cross_reference.py` — all other tools read from it as seed data
- The vtable propagator must treat names sourced only from `{vtable, vtable_propagated, vtable_hierarchy}` as regenerable to avoid stale-data loops
- ARM64 CName hashes are primarily built via `MOVZ+MOVK` instruction sequences (not `ADRP+LDR`) — the `MOVZ+MOVK` scanner yields ~25K hits vs ~2 from `ADRP+LDR`
- NativeDB (`nativedb_data/classes.json`, `globals.json`) provides 15,832 classes, 51,292 functions with parameter types, and class hierarchy
- Current binary coverage: 176,330 discovered functions, 117,541 named (66.7%), with 10 analysis sources feeding the unified function map
- The game binary is at `~/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077`
