# macOS Port - Development Documentation

This directory contains development notes and planning documents for the RED4ext macOS port. These are archived for reference but are **not required for users**.

**For current status, see: [../STATUS.md](../STATUS.md)**  
**For installation and usage, see: [../MACOS_PORT.md](../MACOS_PORT.md)**

---

## Directory Structure

```
porting/
├── README.md                       # This file
├── HOOKING_ALTERNATIVES.md         # Analysis of hooking approaches
├── MACOS_PORT_COMPLETE_DEFINITION.md  # Port completion criteria
└── archive/                        # Historical planning documents
    ├── MACOS_PORTING_PLAN.md
    ├── MACOS_PORTING_PROGRESS.md
    ├── PHASE*_*.md
    └── ...
```

## Key Findings

### Hooking Solution

After extensive research, **Frida Gadget** was selected as the hooking solution for macOS:

| Approach | Viability | Notes |
|----------|-----------|-------|
| Direct code patching | ❌ | Blocked by W^X kernel enforcement |
| Developer Certificate | ⚠️ | Works but requires $99/year from each user |
| Frida Gadget | ✅ | Free, works via JIT memory APIs |
| DYLD Interpose | ⚠️ | Only for exported symbols (not sufficient) |

See [HOOKING_ALTERNATIVES.md](HOOKING_ALTERNATIVES.md) for detailed analysis.

### Architecture Changes

The macOS port required:

1. **Platform Abstraction** (`src/dll/Platform/`)
   - Mach VM APIs instead of Windows VirtualProtect
   - `dlopen`/`dlsym` instead of LoadLibrary/GetProcAddress
   - Unix paths instead of Windows paths

2. **Hook System** (`src/dll/Platform/Hooking.cpp`)
   - ARM64 trampoline generation
   - Frida Gadget integration for actual interception

3. **SDK Compatibility** (`RED4ext.SDK/`)
   - pthread instead of Windows threading primitives
   - `__atomic_*` instead of Interlocked* functions
   - macOS equivalents for Win32 APIs

4. **Address Resolution** (`src/dll/Addresses.cpp`)
   - Mach-O parsing instead of PE parsing
   - Segment:offset address format
   - Symbol mapping via `dlsym()`

---

## Archive Contents

The `archive/` directory contains historical documents from the porting process:

- **Planning**: Original porting plan and phases
- **Progress**: Status updates during development  
- **Research**: Investigation notes on various approaches
- **Summaries**: Phase completion notes

These documents are preserved for historical reference and to document design decisions.
