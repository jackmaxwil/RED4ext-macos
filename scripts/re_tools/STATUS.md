# RE Pipeline Status

## Static Analysis ✅ READY

**Status**: All blockers cleared, fully functional

### Completed Fixes
- ✅ Fixed `otool -tV` parsing (tab-separated address/instruction)
- ✅ Fixed string extraction (`otool -s __TEXT __cstring`) with proper little-endian parsing
- ✅ Function discovery via ARM64 prologue pattern matching
- ✅ Segment parsing and address resolution

### Current Capabilities
- Discovers functions via prologue patterns (`STP X29, X30`, `SUB SP, SP`)
- Extracts strings from `__TEXT __cstring` section
- Parses Mach-O segments (`__TEXT`, `__DATA_CONST`, etc.)
- Exports to JSON format compatible with dynamic analysis

### Usage
```bash
python3 static_analysis.py \
  --binary "/path/to/Cyberpunk2077" \
  --output static_functions.json
```

### Output
- `static_functions.json` (~92MB) with discovered functions
- Ready to use as input for dynamic analysis

---

## Dynamic Analysis ✅ READY (with safety features)

**Status**: Ready with crash-prevention fixes and burst mode

### Completed Fixes
- ✅ **Removed Stalker.follow()** - Was crashing the game (traced ALL execution)
- ✅ **Fixed Interceptor.attach(moduleBase)** - Invalid, now hooks specific addresses
- ✅ **Targeted hooking** - Only hooks functions from static analysis
- ✅ **Error handling** - Try/catch blocks prevent crashes
- ✅ **Lightweight hooks** - Minimal work in callbacks

### Safety Features

#### 1. Targeted Hooking (Default)
- Only hooks specific functions from static analysis
- Limits number of hooks (`--max-functions`, default: 1000)
- Prevents hooking invalid addresses

#### 2. Burst Mode (Recommended for safety)
**NEW**: Hook in short bursts, then unhook to let game run normally

```bash
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --burst-mode \
  --burst-duration 5 \
  --burst-interval 10 \
  --trace-duration 60
```

**How it works**:
- Hooks functions for `--burst-duration` seconds (default: 5s)
- Collects call data
- Unhooks (stops tracking) for `--burst-interval` seconds (default: 10s)
- Repeats until `--trace-duration` expires
- Accumulates results across all bursts

**Benefits**:
- Reduces hook overhead (game runs normally between bursts)
- Prevents crashes from long-term hooking
- Still collects comprehensive data
- Game can recover between hooking periods

#### 3. Continuous Mode (Use with caution)
```bash
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --max-functions 100 \
  --trace-duration 30
```

**Warning**: Continuous hooking may cause instability with many hooks.

### Usage Examples

**Safe burst mode (recommended)**:
```bash
# Hook 100 functions in 5s bursts, 10s rest, for 2 minutes total
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --max-functions 100 \
  --burst-mode \
  --burst-duration 5 \
  --burst-interval 10 \
  --trace-duration 120
```

**Minimal continuous mode**:
```bash
# Hook only 50 functions continuously for 30s
python3 dynamic_analysis.py \
  --process-name "Cyberpunk" \
  --static-functions static_functions.json \
  --max-functions 50 \
  --trace-duration 30
```

### Best Practices

1. **Always use `--static-functions`** - Hooks only known-good addresses
2. **Start with burst mode** - Safest approach
3. **Limit `--max-functions`** - Start with 50-100, increase gradually
4. **Short `--burst-duration`** - 3-5 seconds is usually enough
5. **Longer `--burst-interval`** - 10-15 seconds gives game time to recover
6. **Monitor game stability** - If crashes occur, reduce `--max-functions` or increase `--burst-interval`

### Output
- `dynamic_functions.json` with call counts and call graph
- Compatible with `merge_results.py` for final database

---

## Merge Script ⚠️ PENDING

**Status**: Not yet implemented

### Required
- Load static and dynamic JSON files
- Merge function data (combine static + dynamic discovery)
- Apply confidence scoring
- Export unified database

---

## Summary

| Component | Status | Notes |
|-----------|--------|-------|
| Static Analysis | ✅ Ready | All blockers cleared |
| Dynamic Analysis | ✅ Ready | Safe with burst mode |
| Merge Script | ⚠️ Pending | Not yet implemented |

### Recommended Workflow

1. **Run static analysis** (no game needed):
   ```bash
   python3 static_analysis.py --binary Cyberpunk2077 --output static_functions.json
   ```

2. **Run dynamic analysis in burst mode** (game running):
   ```bash
   python3 dynamic_analysis.py \
     --process-name "Cyberpunk" \
     --static-functions static_functions.json \
     --burst-mode \
     --burst-duration 5 \
     --burst-interval 10 \
     --max-functions 100 \
     --trace-duration 120
   ```

3. **Merge results** (when merge script is ready):
   ```bash
   python3 merge_results.py \
     --static static_functions.json \
     --dynamic dynamic_functions.json \
     --output merged_database.json
   ```

### Safety Recommendations

- ✅ **Use burst mode** for dynamic analysis
- ✅ **Start with small `--max-functions`** (50-100)
- ✅ **Monitor game stability** during hooking
- ✅ **Always provide `--static-functions`** to avoid hooking invalid addresses
- ⚠️ **Avoid continuous mode** with many hooks (>200)
- ⚠️ **Don't use Stalker** - It crashes the game
