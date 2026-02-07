# RED4ext Scripts

This directory contains essential scripts for building, installing, and maintaining RED4ext on macOS.

For the current port status and “what to validate” checklist, see [../docs/STATUS.md](../docs/STATUS.md).

## Installation Scripts

| Script | Description |
|--------|-------------|
| `macos_install.sh` | Main installation script - detects game, copies files, signs binaries |
| `setup_frida_gadget.sh` | Sets up Frida Gadget for function hooking |

## Code Signing Scripts

| Script | Description |
|--------|-------------|
| `macos_resign_for_hooks.sh` | Re-signs game binary and libraries for mod support |
| `macos_resign_backup.sh` | Backs up original code signatures before modification |
| `macos_resign_restore.sh` | Restores original code signatures |

## Address Generation Scripts

| Script | Description |
|--------|-------------|
| `generate_addresses.py` | Generates `cyberpunk2077_addresses.json` from game binary |
| `generate_symbol_mapping.py` | Extracts symbol information from binary |
| `convert_addresses_hpp_to_json.py` | Converts Windows address header to JSON format |
| `download_windows_addresses.py` | Downloads Windows address library for reference |

## Release Scripts

| Script | Description |
|--------|-------------|
| `create_release.sh` | Creates distributable release package |

## Data Files

| File | Description |
|------|-------------|
| `cyberpunk2077_addresses.json` | **Essential** - Address database for SDK |
| `manual_addresses_template.json` | Source of truth for manual addresses |
| `cyberpunk2077_symbols.json` | Symbol mapping (can be regenerated) |

## Frida Directory

The `frida/` subdirectory contains:
- `FridaGadget.dylib` - Frida runtime library
- `FridaGadget.config` - Frida configuration
- `red4ext_hooks.js` - Hook scripts for game functions

## Development Scripts

Development and analysis scripts are in `dev/` (not included in releases).

## Usage Examples

### Fresh Installation

```bash
# Install RED4ext to game directory
./macos_install.sh

# If signing issues occur
./macos_resign_for_hooks.sh
```

### After Game Update

```bash
# Regenerate addresses for new game version
python3 generate_addresses.py \
    "/path/to/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077" \
    --manual manual_addresses_template.json \
    --output cyberpunk2077_addresses.json
```

### Create Release Package

```bash
./create_release.sh 1.0.0
# Creates release/RED4ext-macOS-ARM64-v1.0.0.zip
```
