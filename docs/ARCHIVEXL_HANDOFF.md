# RED4ext macOS Port - Phase 2.2 ArchiveXL Handoff Document

**Date:** February 8, 2026  
**Status:** TweakXL Complete, ArchiveXL Ready to Start  
**Prepared for:** Next AI Agent Session

---

## Current Status Summary

### ✅ Completed (Phase 1 & 2.1)

1. **RED4ext Foundation** (Phase 1)
   - 90/126 addresses validated (100% functional)
   - Frida Gadget hooking operational (11/11 hooks)
   - Plugin system stable
   - TweakXL loads and operates correctly

2. **TweakXL** (Phase 2.1) - 100% Complete
   - All 7 features bootstrapped
   - 3 StatService hooks active
   - Tweak file loading verified
   - Record creation tested
   - TweakDBID derivation working
   - 120+ seconds stable runtime

### 🔄 Next: Phase 2.2 - ArchiveXL

**Current State:** 113/130 addresses resolved (17 unresolved)  
**Goal:** 130/130 addresses resolved, all services enabled  
**Estimated Time:** 22 hours per EXECPLAN

---

## ArchiveXL Project Structure

```
~/Development/cyberpunk/cp2077-archive-xl-macos/
├── src/
│   ├── Red/Addresses/Library.hpp          # 130 hash constants
│   ├── App/Extensions/                    # Extension services
│   │   ├── ExtensionService.hpp           # Main service (DISABLED)
│   │   ├── ResourceMeta/                  # Resource metadata
│   │   ├── ResourcePatch/                 # Resource patching
│   │   ├── WorldStreaming/                # World streaming
│   │   ├── Garment/                       # Garment system
│   │   ├── PuppetState/                   # Puppet state
│   │   ├── QuestPhase/                    # Quest phases
│   │   ├── Localization/                  # Localization
│   │   ├── Transmog/                      # Transmog
│   │   └── FactoryIndex/                  # Factory index
│   └── App/Application.cpp                # Main application
├── lib/Support/macOS/
│   └── ArchiveXLAddressResolver.cpp       # Address resolver (needs updating)
├── tools/
│   └── macos_discover_archivexl_offsets.py # Discovery tool (needs ADRP+LDR)
└── docs/
    ├── STATUS.md                          # Current status
    └── MACOS_ADDRESS_DISCOVERY.md         # Discovery guide
```

---

## Task Breakdown (Per EXECPLAN)

### Task 1: Triage 17 Unresolved Addresses (1h)

**Location:** `src/Red/Addresses/Library.hpp`

**Current Status:** 113/130 hashes have offsets in the resolver
**Missing:** 17 addresses need categorization

**Action:**
1. Check which hashes resolve to 0 at runtime
2. Categorize each:
   - (a) Discoverable via vtable extraction
   - (b) Discoverable via string reference
   - (c) Discoverable via caller-chasing
   - (d) Requires manual reverse engineering

**Files to Check:**
- Run game with ArchiveXL and check RED4ext logs for "NULL address" errors
- Cross-reference with `Library.hpp` to identify which hashes fail

### Task 2: Expand Discovery Tool - ADRP+LDR Support (3h)

**Location:** `tools/macos_discover_archivexl_offsets.py`

**Current:** Tool only scans `ADRP+ADD` patterns
**Needed:** Add `ADRP+LDR` pattern matching

**ARM64 Patterns:**
```asm
; Current (ADD)
ADRP Xd, #imm          ; 0x90000000 pattern
ADD  Xd, Xd, #imm      ; 0x91000000 pattern

; Needed (LDR)
ADRP Xd, #imm          ; 0x90000000 pattern
LDR  Xt, [Xd, #imm]    ; 0xF9400000 pattern (64-bit)
LDR  Wt, [Xd, #imm]    ; 0xB9400000 pattern (32-bit)
```

**Action:**
1. Open `macos_discover_archivexl_offsets.py`
2. Find the pattern matching section
3. Add LDR pattern detection
4. Test against game binary

### Task 3: Vtable Extraction for ResourceDepot (3h)

**Target:** `ResourceDepot_InitializeArchives`, `ResourceDepot_LoadArchives`

**Method:**
1. Parse `__DATA_CONST` segment in game binary
2. Find `ResourceDepot` vtable
3. Map virtual methods to hash IDs

**Tools:**
```bash
# List vtables
otool -l Cyberpunk2077 | grep -A5 "__DATA_CONST"

# Find ResourceDepot references
strings Cyberpunk2077 | grep -i "resourcedepot"

# Disassemble around potential vtable
otool -tV Cyberpunk2077 | grep -A20 "ResourceDepot"
```

**Expected Output:**
- Vtable base address
- Offset for each virtual method
- Mapping to ArchiveXL hash IDs

### Task 4: Resolve Remaining Addresses (4h)

**Techniques:**

1. **String Reference Chasing**
   ```bash
   # Find error message strings
   strings Cyberpunk2077 | grep "archive"
   
   # Find ADRP referencing string
   # Walk backwards to function prologue
   ```

2. **Function Clustering**
   - Related functions grouped in memory
   - If one found, search ±64KB for others

3. **Caller-Chasing**
   - For addresses resolving to `brk`/assert stubs
   - Walk call graph backwards
   - Find real calling function

4. **Cross-Reference with Windows**
   - Use Windows IDA/Ghidra database
   - Pattern match ARM64 equivalents

### Task 5: Re-enable ExtensionService (2h)

**Location:** `src/App/Application.cpp`

**Current Status:** ExtensionService is guarded/disabled

**Action:**
1. Find `#ifdef` guards around ExtensionService
2. Remove guards or add macOS support
3. Ensure all hooks use `.OrThrow()`
4. Build and test

**Verification:**
- Launch game
- Check logs for ExtensionService hooks
- Verify no NULL address errors

### Task 6: Integration Test (2h)

**Setup:**
1. Install ArchiveXL alongside RED4ext + TweakXL
2. Install a mod using ArchiveXL (custom appearance/vehicle)
3. Launch game

**Verification:**
- [ ] ArchiveXL loads without errors
- [ ] All 130 addresses resolve
- [ ] Hooks attach successfully
- [ ] Custom resources load in-game
- [ ] No crashes during gameplay

---

## Key Files Reference

### Address Resolution

**Hash Constants:**
```cpp
// src/Red/Addresses/Library.hpp
constexpr uint32_t ResourceDepot_InitializeArchives = 2923109755;
constexpr uint32_t ResourceDepot_LoadArchives = 3729789488;
// ... 130 total
```

**Resolver Implementation:**
```cpp
// lib/Support/macOS/ArchiveXLAddressResolver.cpp
// Maps hash -> offset for macOS
```

### Hook Attachment Pattern

```cpp
// Example from ExtensionService
auto hookInit = HookAfter<Raw::ResourceDepot_InitializeArchives>([&]() {
    // Implementation
}).OrThrow("Failed to hook ResourceDepot_InitializeArchives");
```

---

## Common Issues & Solutions

### Issue 1: Address Resolves to 0

**Cause:** Hash not in resolver table or wrong offset
**Solution:**
1. Check if hash exists in `Library.hpp`
2. Add/update entry in `ArchiveXLAddressResolver.cpp`
3. Rebuild and test

### Issue 2: Hook Fails with NULL Parameter

**Cause:** Address resolved to invalid location (assert stub, string, etc.)
**Solution:**
1. Verify address points to valid ARM64 prologue
2. Use string reference or caller-chasing to find real function
3. Update resolver with correct offset

### Issue 3: ExtensionService Crashes on Load

**Cause:** Missing address or incompatible struct layout
**Solution:**
1. Check logs for which hook failed
2. Verify all required addresses resolve
3. Check struct offsets match macOS binary

---

## Quick Commands

```bash
# Build ArchiveXL
cd ~/Development/cyberpunk/cp2077-archive-xl-macos
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

# Install to game
GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
cp build/ArchiveXL.dylib "$GAME_DIR/red4ext/plugins/ArchiveXL/"

# Launch game
cd "$GAME_DIR"
./launch_red4ext.sh

# Check logs
cat "$GAME_DIR/red4ext/logs/red4ext.log" | grep -i archive
cat "$GAME_DIR/red4ext/plugins/ArchiveXL/ArchiveXL.log" 2>/dev/null
```

---

## Success Criteria

ArchiveXL Phase 2.2 is complete when:

1. ✅ All 130 addresses resolve to non-zero
2. ✅ All hooks attach without NULL errors
3. ✅ ExtensionService loads and operates
4. ✅ Integration test passes (custom resources load)
5. ✅ No crashes during extended gameplay

---

## Next Phases After ArchiveXL

### Phase 2.3 - ModMenu (22h)
- Complete UI implementation
- REDscript menu integration
- Native↔REDscript bridge
- Settings persistence

### Phase 3.1 - MetalFX Denoiser (36h)
- Buffer structure reverse engineering
- MetalFX pipeline integration
- Performance benchmarking

### Phase 4 - CyberMod Studio (90h+)
- Mod Manager module
- Game Runner module
- CyberModDaemon
- Debug Studio
- Creation Studio
- Porting Studio

---

## Resources

- **EXECPLAN:** `~/Development/cyberpunk/cybermod-studio/docs/EXECPLAN.md`
- **ArchiveXL Status:** `~/Development/cyberpunk/cp2077-archive-xl-macos/docs/STATUS.md`
- **Address Discovery:** `~/Development/cyberpunk/cp2077-archive-xl-macos/docs/MACOS_ADDRESS_DISCOVERY.md`
- **RED4ext Validation:** `~/Development/cyberpunk/RED4ext/docs/VALIDATION_REPORT.md`

---

**End of Handoff Document**

**Next Agent:** Start with Task 1 - Triage the 17 unresolved addresses by running ArchiveXL and checking which hashes fail to resolve.
