# Building RED4ext for macOS

> **This is the macOS port.** For Windows, see [WopsS/RED4ext](https://github.com/WopsS/RED4ext).

---

## Prerequisites

### Required

| Tool | Version | Install |
|------|---------|---------|
| macOS | 12+ (Monterey) | - |
| Apple Silicon | M1/M2/M3/M4 | - |
| Xcode CLI Tools | Latest | `xcode-select --install` |
| CMake | 3.23+ | `brew install cmake` |
| Git | Any | Included with Xcode CLI |

### Optional

| Tool | Purpose | Install |
|------|---------|---------|
| Python 3.10+ | Address regeneration | `brew install python@3.12` |
| xz | Frida download | `brew install xz` |

### Verify Prerequisites

```bash
# Check all requirements
xcode-select -p          # Should show path to CommandLineTools
cmake --version          # Need 3.23+
git --version            # Any version
python3 --version        # Need 3.10+ for address scripts

# Install missing tools
xcode-select --install   # If Xcode CLI not installed
brew install cmake xz    # If CMake/xz missing
```

---

## Quick Build (Recommended)

```bash
# 1. Clone with all submodules
git clone --recursive https://github.com/memaxo/RED4ext.git
cd RED4ext

# 2. Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

# 3. Install to game
cd ..
./scripts/macos_install.sh
```

**Important:** The `--recursive` flag is required to fetch all dependencies.

---

## Step-by-Step Build

### 1. Clone Repository

```bash
git clone https://github.com/memaxo/RED4ext.git
cd RED4ext
```

### 2. Initialize Submodules

```bash
git submodule update --init --recursive
```

This fetches:
- `deps/red4ext.sdk` — macOS-compatible SDK
- `deps/fishhook` — Mach-O function rebinding
- `deps/spdlog` — Logging library
- `deps/fmt` — String formatting
- `deps/simdjson` — JSON parsing
- `deps/toml11` — Config parsing

### 3. Configure Build

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

CMake options:
- `-DCMAKE_BUILD_TYPE=Debug` — Debug build with symbols
- `-DCMAKE_BUILD_TYPE=Release` — Optimized release build
- `-DRED4EXT_EXTRA_WARNINGS=OFF` — Disable extra warnings
- `-DRED4EXT_TREAT_WARNINGS_AS_ERRORS=ON` — Fail on warnings

### 4. Compile

```bash
make -j$(sysctl -n hw.ncpu)
```

Or with verbose output:
```bash
make VERBOSE=1 -j$(sysctl -n hw.ncpu)
```

### 5. Verify Build

```bash
# Check output exists
ls -la libs/RED4ext.dylib

# Verify architecture
file libs/RED4ext.dylib
# Should show: Mach-O 64-bit dynamically linked shared library arm64
```

---

## Installation

### Automatic Installation

```bash
cd /path/to/RED4ext
./scripts/macos_install.sh
```

The installer will:
1. Detect Cyberpunk 2077 installation
2. Copy RED4ext.dylib
3. Download and install Frida Gadget
4. Create launch script
5. Set up directories

### Manual Installation

```bash
GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"

# Create directories
mkdir -p "$GAME_DIR/red4ext/plugins"
mkdir -p "$GAME_DIR/red4ext/logs"
mkdir -p "$GAME_DIR/red4ext/bin/x64"

# Copy main library
cp build/libs/RED4ext.dylib "$GAME_DIR/red4ext/"

# Copy address database
cp deps/red4ext.sdk/cyberpunk2077_addresses.json "$GAME_DIR/red4ext/bin/x64/"

# Set up Frida (downloads ~50MB)
./scripts/setup_frida_gadget.sh

# Copy Frida config
cp scripts/frida/FridaGadget.config "$GAME_DIR/red4ext/"
cp scripts/frida/red4ext_hooks.js "$GAME_DIR/red4ext/"
```

### Installer Options

```bash
./scripts/macos_install.sh --help

Options:
  --game-dir PATH    Specify game location
  --build            Build from source first
  --skip-frida       Don't install Frida Gadget
```

---

## Build Outputs

```
build/
└── libs/
    └── RED4ext.dylib    # Main loader (2.1 MB)
```

Installed to game:
```
Cyberpunk 2077/
├── red4ext/
│   ├── RED4ext.dylib
│   ├── FridaGadget.dylib
│   ├── FridaGadget.config
│   ├── red4ext_hooks.js
│   ├── bin/x64/
│   │   └── cyberpunk2077_addresses.json
│   ├── plugins/
│   └── logs/
└── launch_red4ext.sh
```

---

## Troubleshooting

### "SDK not found" or "submodule" errors

```bash
# Reset and reinitialize submodules
git submodule deinit -f .
git submodule update --init --recursive
```

### "CMake version too old"

```bash
brew upgrade cmake
# Or install specific version
brew install cmake@3.28
```

### Build fails with missing headers

```bash
# Ensure Xcode CLI is fully installed
xcode-select --install
sudo xcode-select --reset

# Accept license if needed
sudo xcodebuild -license accept
```

### "Library not loaded" at runtime

```bash
# Re-sign the library
codesign -s - build/libs/RED4ext.dylib
codesign -s - "$GAME_DIR/red4ext/FridaGadget.dylib"
```

### "No such file: Cyberpunk 2077"

```bash
# Specify game path manually
./scripts/macos_install.sh --game-dir "/path/to/Cyberpunk 2077"
```

### Frida download fails

```bash
# Install xz if missing
brew install xz

# Manual Frida download
curl -L -o /tmp/frida.xz "https://github.com/frida/frida/releases/download/17.5.2/frida-gadget-17.5.2-macos-universal.dylib.xz"
xz -d /tmp/frida.xz
mv /tmp/frida "$GAME_DIR/red4ext/FridaGadget.dylib"
codesign -s - "$GAME_DIR/red4ext/FridaGadget.dylib"
```

### Clean rebuild

```bash
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

---

## Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(sysctl -n hw.ncpu)
```

Debug output goes to:
- `<game>/red4ext/logs/red4ext.log`

---

## After Game Updates

When Cyberpunk updates, addresses may change:

```bash
cd scripts
python3 generate_addresses.py \
    "$GAME_DIR/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077" \
    --manual manual_addresses_template.json \
    --output cyberpunk2077_addresses.json

# Copy updated addresses
cp cyberpunk2077_addresses.json "$GAME_DIR/red4ext/bin/x64/"
```

---

## Dependencies

| Dependency | License | Purpose |
|------------|---------|---------|
| red4ext.sdk | MIT | Game SDK |
| fishhook | BSD-3 | Symbol rebinding |
| spdlog | MIT | Logging |
| fmt | MIT | Formatting |
| simdjson | Apache-2.0 | JSON parsing |
| toml11 | MIT | Config parsing |

---

## See Also

- [README.md](README.md) — Overview and quick start
- [docs/MACOS_PORT.md](docs/MACOS_PORT.md) — Port technical details
- [docs/FRIDA_INTEGRATION.md](docs/FRIDA_INTEGRATION.md) — Frida hooking
- [scripts/README.md](scripts/README.md) — Script documentation
