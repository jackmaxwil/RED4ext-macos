# Cyberpunk 2077 macOS Internals Reference

> Technical documentation of Cyberpunk 2077's internal architecture on macOS ARM64,
> compiled during the RED4ext/TweakXL porting effort.

## Table of Contents

1. [Binary Architecture](#binary-architecture)
2. [Memory Layout](#memory-layout)
3. [Address Resolution System](#address-resolution-system)
4. [TweakDB System](#tweakdb-system)
5. [StatsDataSystem](#statsdatasystem)
6. [RTTI System](#rtti-system)
7. [Singleton Patterns](#singleton-patterns)
8. [Function Discovery Techniques](#function-discovery-techniques)
9. [Hooking on macOS](#hooking-on-macos)
10. [Tools & Commands](#tools--commands)

---

## Binary Architecture

### Platform Details

| Property | Value |
|----------|-------|
| Architecture | ARM64 (AArch64) |
| Format | Mach-O 64-bit |
| Image Base | `0x100000000` |
| Entry Point | Offset `0x31E18` |
| Binary Size | ~150MB |

### Mach-O Segments

```
__TEXT      0x100000000  Code (read-only, executable)
__DATA_CONST            VTables, RTTI, constant pointers
__DATA                  Mutable globals, singletons
__LINKEDIT              Symbol tables, fixups
```

### Chained Fixups (macOS 12+)

Modern macOS uses chained fixups for relocatable pointers. Raw values in `__DATA_CONST` 
must be decoded:

```c
// Decode chained fixup pointer
uintptr_t decode_pointer(uint64_t raw, uintptr_t image_base) {
    // Extract 32-bit offset from lower bits
    uint32_t offset = raw & 0xFFFFFFFF;
    return image_base + offset;
}
```

**Important**: VTable entries and RTTI pointers use this encoding. Direct memory reads
will show encoded values, not actual addresses.

---

## Memory Layout

### Key Address Ranges (Game Version 2.x)

| Component | Range | Size |
|-----------|-------|------|
| Main entry | 0x31E18 | - |
| RTTI System | 0x4D3xxx - 0x4D5xxx | ~8KB |
| TweakDB | 0x2B73xxx - 0x2B7Dxxx | ~40KB |
| StatsDataSystem | 0x3A93xxx - 0x3A94xxx | ~8KB |

### Function Clustering

Related functions are grouped together in memory. When you find one function in a 
subsystem, search nearby addresses (±64KB) for related functions.

```
TweakDB Cluster Example:
  0x2B737AC  TweakDB_CreateRecord
  0x2B79AC0  TweakDB_Init
  0x2B7BAB0  TweakDB_TryLoad
  0x2B7BE94  TweakDB_Load
  0x2B7D228  TweakDBID_Derive
```

---

## Address Resolution System

### Hash-Based Lookup

RED4ext uses **FNV1a-32** hashes to identify functions. This avoids hardcoding offsets
that change between game versions.

```cpp
// FNV1a-32 hash computation
constexpr uint32_t fnv1a_32(const char* str) {
    uint32_t hash = 0x811C9DC5;  // FNV offset basis
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 0x01000193;  // FNV prime
    }
    return hash;
}
```

### Address JSON Format

The `cyberpunk2077_addresses.json` file maps hashes to offsets:

```json
{
  "Addresses": [
    {
      "hash": "3062572522",
      "offset": "1:0x2B79AC0"
    }
  ]
}
```

Format: `segment:hex_offset`
- Segment `1` = `__TEXT` (code)
- Offset is relative to segment base

### SDK Address Hashes

126 addresses defined in `RED4ext/Detail/AddressHashes.hpp`:

```cpp
namespace RED4ext::Detail::AddressHashes {
    constexpr uint32_t CRTTISystem_Get = 0x4A610F64;
    constexpr uint32_t TweakDB_CreateRecord = 0x3201127A;
    constexpr uint32_t CBaseFunction_InternalExecute = 0x1817231D;
    // ... 123 more
}
```

### TweakXL Custom Hashes

TweakXL defines 11 additional hashes in `src/Red/Addresses/Library.hpp`:

| Function | Hash (Decimal) | Hash (Hex) |
|----------|----------------|------------|
| Main | 240386859 | 0x0E54032B |
| TweakDB_Init | 3062572522 | 0xB6832FEA |
| TweakDB_Load | 3602585178 | 0xD6B1DB5A |
| TweakDB_TryLoad | 3512345737 | 0xD16A2999 |
| TweakDB_CreateRecord | 838931066 | 0x31FB0F6A |
| TweakDBID_Derive | 326438016 | 0x137620C0 |
| StatsDataSystem_InitializeRecords | 1299190886 | 0x4D6E8066 |
| StatsDataSystem_InitializeParams | 3652194890 | 0xD9B5924A |
| StatsDataSystem_GetStatRange | 1444748215 | 0x5620D3B7 |
| StatsDataSystem_GetStatFlags | 3123320294 | 0xBA1CE5E6 |
| StatsDataSystem_CheckStatFlag | 2954893634 | 0xB01D2542 |

---

## TweakDB System

### Overview

TweakDB is Cyberpunk 2077's data definition system. It stores game data like item stats,
vehicle properties, NPC attributes, etc. Mods use TweakXL to modify these values at runtime.

### Key Structures

```cpp
// TweakDBID - 64-bit identifier for records/flats
struct TweakDBID {
    uint32_t nameHash;    // FNV1a hash of name
    uint8_t  nameLength;  // Length of original name
    uint8_t  tdbOffset[3]; // Internal offset
};

// TweakDB singleton access
class TweakDB {
public:
    static TweakDB* Get();  // Singleton getter
    
    void Init();
    void Load(const CString& path);      // Load .bin
    bool TryLoad(const CString& path);   // Load .tweak
    void CreateRecord(uint32_t type, TweakDBID id);
};
```

### Function Offsets

| Function | Offset | Purpose |
|----------|--------|---------|
| `TweakDB::Init` | 0x2B79AC0 | Initialize TweakDB system |
| `TweakDB::Load` | 0x2B7BE94 | Load optimized .bin file |
| `TweakDB::TryLoad` | 0x2B7BAB0 | Load .tweak text files |
| `TweakDB::CreateRecord` | 0x2B737AC | Create new record entry |
| `TweakDBID::Derive` | 0x2B7D228 | Derive child ID from parent |

### String Markers for Discovery

These strings appear near TweakDB code and can be used to locate functions:

```
".tweak"          - File extension check in TryLoad
"TweakDB"         - Class name in RTTI
"LoadOptimized"   - Binary loading path
"flatData"        - Flat data structure
"recordData"      - Record data structure
```

---

## StatsDataSystem

### Overview

Handles game statistics (health, damage, armor, etc.). TweakXL hooks this to support
custom stat types defined in mods.

### Memory Layout

```cpp
// Offsets from StatsDataSystem base pointer
struct StatsDataSystem {
    // ... other members ...
    DynArray<TweakDBID> statRecords;  // 0xD8
    DynArray<StatParams> statParams;  // 0xE8
    SharedMutex statLock;             // 0xFC
};

// Stat parameter structure (packed)
#pragma pack(push, 1)
struct StatParams {
    union {
        uint64_t range;
        struct {
            float min;
            float max;
        };
    };
    uint32_t flags;
};
#pragma pack(pop)
```

### Function Offsets

| Function | Offset | Signature |
|----------|--------|-----------|
| `InitializeRecords` | 0x3A939B8 | `void(StatsDataSystem*)` |
| `InitializeParams` | 0x3A932C4 | `void(StatsDataSystem*)` |
| `GetStatRange` | 0x3A94744 | `uint64_t*(StatsDataSystem*, uint64_t*, uint32_t)` |
| `GetStatFlags` | 0x3A93F00 | `uint32_t(StatsDataSystem*, uint32_t)` |
| `CheckStatFlag` | 0x3A93E7C | `bool(StatsDataSystem*, uint32_t, uint32_t)` |

### Finding Stats Functions

Stats functions access member offsets 0xD8, 0xE8, 0xFC. Search for:

```asm
; Pattern: LDR from offset 0xD8/0xE8/0xFC
LDR   X?, [X0, #0xD8]   ; Load statRecords
LDR   X?, [X0, #0xE8]   ; Load statParams
LDRB  W?, [X0, #0xFC]   ; Load statLock
```

---

## RTTI System

### CName Hashing

Game uses FNV1a-64 for type name hashing:

```cpp
constexpr uint64_t FNV1A64_PRIME = 0x00000100000001B3;
constexpr uint64_t FNV1A64_BASIS = 0xCBF29CE484222325;

uint64_t fnv1a_64(const char* str) {
    uint64_t hash = FNV1A64_BASIS;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= FNV1A64_PRIME;
    }
    return hash;
}
```

### CClass Structure

```cpp
struct CClass {
    void* vtable;           // 0x00 - Virtual function table
    CName name;             // 0x08 - Type name hash
    CClass* parent;         // 0x10 - Parent class
    // ... properties, functions, etc.
};
```

### Key RTTI Functions

| Function | Offset | Purpose |
|----------|--------|---------|
| `CRTTISystem::Get` | 0x3452734 | Get RTTI singleton |
| `CClass::GetProperty` | 0x4D3E80 | Get property by name |
| `CClass::GetFunction` | 0x4D4278 | Get method by name |
| `CClass::CreateInstance` | 0x4D3F38 | Instantiate class |

---

## Singleton Patterns

### Pattern 1: Lazy Initialization (ADRP + LDR + CBZ)

Most common pattern for singletons:

```asm
; TweakDB::Get() example
_TweakDB_Get:
    ADRP  X8, #g_tweakdb@PAGE
    LDR   X0, [X8, #g_tweakdb@PAGEOFF]
    CBZ   X0, .Linit
    RET
.Linit:
    ; Initialize singleton...
```

### Pattern 2: Direct Global Access

For pre-initialized globals:

```asm
_GetGlobal:
    ADRP  X8, #g_global@PAGE
    ADD   X0, X8, #g_global@PAGEOFF
    RET
```

### Pattern 3: Thread-Safe Initialization

Uses atomic compare-exchange:

```asm
    LDAXR X0, [X8]
    CBZ   X0, .Linit
    RET
.Linit:
    ; ... create instance ...
    STLXR W9, X0, [X8]
    CBNZ  W9, .Lretry
```

### Known Singletons

| Singleton | Getter Pattern | Purpose |
|-----------|---------------|---------|
| TweakDB | Lazy init | Game data definitions |
| CRTTISystem | Lazy init | Type reflection |
| ResourceDepot | Direct | Asset loading |
| JobDispatcher | Direct | Async job queue |

---

## Function Discovery Techniques

### Technique 1: String References (Most Effective)

**Success Rate: ~85%**

1. Find unique string in binary:
   ```bash
   strings Cyberpunk2077 | grep -i "tweakdb"
   ```

2. Get string address:
   ```bash
   otool -s __TEXT __cstring Cyberpunk2077 | grep "tweak"
   ```

3. Find ADRP+ADD referencing string:
   ```bash
   # Search for ADRP with page of string address
   otool -tV Cyberpunk2077 | grep "adrp.*0x..."
   ```

4. Walk backwards to function prologue:
   ```asm
   ; Look for this pattern
   STP   X29, X30, [SP, #-0x??]!  ; Frame pointer setup
   MOV   X29, SP
   ```

### Technique 2: Member Access Patterns

For class methods, search for access to known offsets:

```bash
# Find functions accessing offset 0xD8
otool -tV Cyberpunk2077 | grep -B20 "0xd8\]"
```

### Technique 3: Function Proximity

Related functions cluster together. After finding one:

```bash
# Disassemble region around known function
otool -tV -p __Z... Cyberpunk2077 | head -500
```

### Technique 4: VTable Analysis

1. Find class name string
2. Locate RTTI registration code
3. Extract vtable pointer assignment
4. Decode chained fixups for method addresses

### ARM64 Function Prologues

Valid function entry points:

```asm
; Standard frame setup
STP   X29, X30, [SP, #-0x??]!
MOV   X29, SP

; No frame pointer
SUB   SP, SP, #0x??
STP   X??, X??, [SP, #0x??]

; Leaf function (no calls)
; May have no prologue - just code
```

---

## Hooking on macOS

### Memory Protection (W^X)

macOS enforces Write XOR Execute. You cannot:
- Write to executable memory
- Execute writable memory
- Directly patch code at runtime

### Solution: Frida Gadget

Frida provides runtime code modification that works with W^X:

```javascript
// red4ext_hooks.js
Interceptor.attach(ptr("0x102B7BAB0"), {
    onEnter: function(args) {
        console.log("TweakDB::TryLoad called");
    },
    onLeave: function(retval) {
        console.log("Result:", retval);
    }
});
```

### RED4ext Hooking API

```cpp
// Hook with trampoline (preserves original)
HookAfter<Raw::TweakDB_TryLoad>([](bool& result) {
    // Called after original function
    if (result) {
        // Process loaded tweaks
    }
}).OrThrow();

// Replace function entirely
Hook<Raw::GetStatFlags>(&MyGetStatFlags).OrThrow();
```

### Double-Load Guard

RED4ext may call plugin entry multiple times. Always guard:

```cpp
static bool g_initialized = false;

bool Main(PluginHandle, EMainReason reason, const Sdk*) {
    if (reason == EMainReason::Load) {
        if (g_initialized) return true;  // Skip duplicate
        g_initialized = true;
        // ... initialize ...
    }
    return true;
}
```

---

## Tools & Commands

### Binary Analysis

```bash
# List all symbols
nm -n Cyberpunk2077 | head -1000

# Demangle C++ symbols
nm Cyberpunk2077 | c++filt | grep "TweakDB"

# Disassemble specific function
otool -tV -p _functionName Cyberpunk2077

# Show Mach-O segments
otool -l Cyberpunk2077 | grep -A5 "segname"

# Find strings
strings Cyberpunk2077 | grep -i "pattern"

# Get string section addresses
otool -s __TEXT __cstring Cyberpunk2077
```

### Address Calculation

```python
# Python helper for address math
IMAGE_BASE = 0x100000000

def offset_to_addr(offset):
    """Convert file offset to runtime address"""
    return IMAGE_BASE + offset

def addr_to_offset(addr):
    """Convert runtime address to file offset"""
    return addr - IMAGE_BASE

# Example
print(hex(offset_to_addr(0x2B79AC0)))  # 0x102B79AC0
```

### Verification

```bash
# Verify function prologue at offset
xxd -s 0x2B79AC0 -l 16 Cyberpunk2077 | head -1
# Should show: fd 7b xx a9 (STP X29, X30, ...)

# Check for valid ARM64 instructions
otool -tV Cyberpunk2077 -p __text | tail -n +$((0x2B79AC0/4))
```

---

## Appendix: Complete Address Table

### SDK Addresses (126 total)

See `RED4ext.SDK/include/RED4ext/Detail/AddressHashes.hpp` for complete list.

### TweakXL Addresses (12 total)

| Hash | Offset | Function |
|------|--------|----------|
| 0x0E54032B | 0x31E18 | Main |
| 0xB6832FEA | 0x2B79AC0 | TweakDB_Init |
| 0xD6B1DB5A | 0x2B7BE94 | TweakDB_Load |
| 0xD16A2999 | 0x2B7BAB0 | TweakDB_TryLoad |
| 0x31FB0F6A | 0x2B737AC | TweakDB_CreateRecord |
| 0x137620C0 | 0x2B7D228 | TweakDBID_Derive |
| 0x4D6E8066 | 0x3A939B8 | StatsDataSystem_InitializeRecords |
| 0xD9B5924A | 0x3A932C4 | StatsDataSystem_InitializeParams |
| 0x5620D3B7 | 0x3A94744 | StatsDataSystem_GetStatRange |
| 0xBA1CE5E6 | 0x3A93F00 | StatsDataSystem_GetStatFlags |
| 0xB01D2542 | 0x3A93E7C | StatsDataSystem_CheckStatFlag |
| 0x1817231D | 0x94FE44 | CBaseFunction_InternalExecute |

---

## Version History

| Date | Game Version | Notes |
|------|--------------|-------|
| 2026-01-03 | 2.x | Initial documentation |

---

*This document is part of the RED4ext macOS port. For updates, see the repository.*
