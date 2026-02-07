# Symbol Mapping Generation Tool

## Overview

The `generate_symbol_mapping.py` script generates a hash-to-symbol mapping file for RED4ext's macOS address resolution system. This mapping allows RED4ext to resolve function addresses using symbol names instead of relying solely on the address database.

For the current macOS port status and recommended validation steps, see [../docs/STATUS.md](../docs/STATUS.md).

## Prerequisites

- Python 3.6+
- Xcode Command Line Tools (for `nm` and `c++filt`)
- Access to the Cyberpunk 2077 macOS binary

## Usage

```bash
python3 scripts/generate_symbol_mapping.py <binary_path> [options]
```

### Options

- `--output`, `-o`: Output JSON file path (default: `cyberpunk2077_symbols.json`)
- `--filter`, `-f`: Filter symbols by pattern (e.g., `"CGameApplication"`)
- `--limit`: Process only the first N C++ symbols (useful for quick testing)
- `--dry-run`: Run generation but do not write the output file
- `--game-version`: Game version string to embed in output JSON (default: `2.3.1`)
- `--demangle-batch-size`: Symbols per `c++filt` invocation (default: `5000`)
- `--demangle-timeout`: Timeout (seconds) per `c++filt` batch (default: `120`)
- `--demangle-cache`: Path to a SQLite cache for demangled names (recommended for reruns)
- `--resume`: If output exists, load it and continue adding new mappings (best with `--demangle-cache`)
- `--progress-every`: Print progress every N seconds (set `0` to disable)
- `--verify`: Verify known hashes against generated mappings

### Example

```bash
# Generate symbol mapping for the game binary
python3 scripts/generate_symbol_mapping.py \
    "/Applications/Cyberpunk 2077.app/Contents/MacOS/Cyberpunk2077" \
    --output cyberpunk2077_symbols.json

# Quick smoke test: only process first 1000 C++ symbols and don't write output
python3 scripts/generate_symbol_mapping.py \
    "/Applications/Cyberpunk 2077.app/Contents/MacOS/Cyberpunk2077" \
    --limit 1000 \
    --dry-run

# Generate with caching + resume (recommended for long runs / interruptions)
python3 scripts/generate_symbol_mapping.py \
    "/Applications/Cyberpunk 2077.app/Contents/MacOS/Cyberpunk2077" \
    --output cyberpunk2077_symbols.json \
    --demangle-cache .demangle_cache.sqlite3 \
    --resume

# Generate with filtering (useful for testing)
python3 scripts/generate_symbol_mapping.py \
    "/Applications/Cyberpunk 2077.app/Contents/MacOS/Cyberpunk2077" \
    --filter "CGameApplication" \
    --verify
```

## How It Works

1. **Parse Symbol Table**: Uses `nm -g -j` to extract external symbols from the Mach-O binary
2. **Demangle Symbols**: Uses `c++filt` to convert mangled C++ names to readable format
3. **Generate Hash Variants**: Converts demangled names to multiple hash name formats:
   - `CGameApplication::AddState` → `CGameApplication_AddState`
   - `CGameApplication::AddState` → `CGameApplication::AddState` (keep `::`)
   - Function name only (e.g., `Main`)
4. **Compute Hashes**: Generates FNV1a32 hashes for each variant
5. **Output JSON**: Creates a JSON file mapping hashes to mangled symbol names

## Output Format

```json
{
  "version": "1.0",
  "game_version": "2.3.1",
  "mappings": [
    {
      "hash": "0xFBC216B3",
      "symbol": "_ZN16CGameApplication8AddStateEPNS_9IGameStateE"
    },
    ...
  ]
}
```

## Integration

The generated JSON file should be placed in the same directory as `cyberpunk2077_addresses.json`.

- **macOS (this port)**: `<game_root>/red4ext/bin/x64/`
- **Windows (typical layout)**: `<game_root>/bin/x64/`

## Known Limitations

1. **Hash Name Format**: The exact format used to generate hashes may not match all known hash values. The tool generates multiple variants to increase match probability.

2. **Hash Collisions**: With 32-bit hashes, collisions are possible but rare. If a collision occurs, the first matching symbol is used.

3. **Non-Exported Symbols**: Symbols that are not exported (not in the symbol table) cannot be resolved this way and must use the address database.

4. **Symbol Name Matching**: The tool attempts to match demangled names to known hash patterns, but some hashes may use different naming conventions that require manual mapping.

5. **Filtering vs Speed**: `--filter` is applied after demangling (since patterns usually target demangled names), so it does not significantly reduce demangling work.

## Troubleshooting

### No symbols found
- Verify the binary path is correct
- Check that `nm` is available: `which nm`
- Ensure the binary is not stripped (macOS binaries are typically not stripped)

### Hash verification fails
- The hash name format may differ from expected
- Try generating without filtering to get all symbols
- Check if the symbol exists in the binary: `nm -g <binary> | grep <symbol>`

### c++filt not found
- Install Xcode Command Line Tools: `xcode-select --install`
- Or use Homebrew: `brew install binutils` (provides `c++filt`)

## Future Improvements

- Direct Mach-O parsing (avoid external tools)
- Support for multiple hash name formats
- Incremental updates (only process new symbols)
- Symbol name pattern matching for better hash generation
