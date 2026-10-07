#!/bin/bash
#
# RED4ext macOS Release Packager
# Creates a distributable release archive
#

set -e

VERSION="${1:-1.0.0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
RELEASE_DIR="$PROJECT_DIR/release"
RELEASE_NAME="RED4ext-macOS-ARM64-v${VERSION}"

echo "=== RED4ext macOS Release Builder ==="
echo "Version: $VERSION"
echo ""

# Build if needed
if [ ! -f "$BUILD_DIR/libs/RED4ext.dylib" ]; then
    echo "Building RED4ext..."
    cd "$BUILD_DIR"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(sysctl -n hw.ncpu)
fi

# Create release directory
rm -rf "$RELEASE_DIR/$RELEASE_NAME"
mkdir -p "$RELEASE_DIR/$RELEASE_NAME"
mkdir -p "$RELEASE_DIR/$RELEASE_NAME/red4ext"
mkdir -p "$RELEASE_DIR/$RELEASE_NAME/red4ext/plugins"
mkdir -p "$RELEASE_DIR/$RELEASE_NAME/scripts"

echo "Packaging release..."

# Copy main dylib
cp "$BUILD_DIR/libs/RED4ext.dylib" "$RELEASE_DIR/$RELEASE_NAME/red4ext/"



# Copy address database
cp "$SCRIPT_DIR/cyberpunk2077_addresses.json" "$RELEASE_DIR/$RELEASE_NAME/red4ext/"

# Copy essential scripts
cp "$SCRIPT_DIR/macos_install.sh" "$RELEASE_DIR/$RELEASE_NAME/scripts/"
cp "$SCRIPT_DIR/check_requirements.sh" "$RELEASE_DIR/$RELEASE_NAME/scripts/"
cp "$SCRIPT_DIR/macos_resign_for_hooks.sh" "$RELEASE_DIR/$RELEASE_NAME/scripts/"
cp "$SCRIPT_DIR/macos_resign_backup.sh" "$RELEASE_DIR/$RELEASE_NAME/scripts/"
cp "$SCRIPT_DIR/macos_resign_restore.sh" "$RELEASE_DIR/$RELEASE_NAME/scripts/"
cp "$SCRIPT_DIR/generate_addresses.py" "$RELEASE_DIR/$RELEASE_NAME/scripts/"
cp "$SCRIPT_DIR/manual_addresses_template.json" "$RELEASE_DIR/$RELEASE_NAME/scripts/"

# Copy documentation
cp "$PROJECT_DIR/README.md" "$RELEASE_DIR/$RELEASE_NAME/"
cp "$PROJECT_DIR/LICENSE.md" "$RELEASE_DIR/$RELEASE_NAME/"
cp "$PROJECT_DIR/THIRD_PARTY_LICENSES.md" "$RELEASE_DIR/$RELEASE_NAME/"
cp -r "$PROJECT_DIR/docs" "$RELEASE_DIR/$RELEASE_NAME/"

# Create installation instructions
cat > "$RELEASE_DIR/$RELEASE_NAME/INSTALL.md" << 'EOF'
# RED4ext macOS Installation Guide

## Prerequisites

- Cyberpunk 2077 installed via Steam on macOS
- macOS 12+ on Apple Silicon (M1/M2/M3/M4)
- Xcode Command Line Tools: `xcode-select --install`

## Quick Install

```bash
cd scripts
./macos_install.sh
```

This will:
1. Detect your Cyberpunk 2077 installation
2. Copy RED4ext files to the game directory
3. Create the `launch_red4ext.sh` script
4. Set up plugin directories

## Launching the Game

### Option 1: Launch Script (Recommended)

```bash
cd "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
./launch_red4ext.sh
```

Or with full path:
```bash
"$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/launch_red4ext.sh"
```

### Option 2: Launch via Steam

Launch normally from Steam. Mods load automatically.

## Verify Installation

After launching, check that mods loaded:

```bash
# View RED4ext log
cat "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/red4ext/logs/red4ext.log"
```

You should see:
```
[RED4ext] Initializing...
[RED4ext] Loading plugins...
```

## Installing Plugins

Place .dylib plugins in:
```
Cyberpunk 2077/red4ext/plugins/PluginName/PluginName.dylib
```

**Note:** Windows .dll plugins won't work - they must be recompiled for macOS.

## Troubleshooting

### Mods not loading
- Check `red4ext/logs/red4ext.log` for errors
- Verify `launch_red4ext.sh` exists and is executable
- Re-run `./scripts/macos_install.sh`

### "Library not loaded" errors
```bash
./scripts/macos_resign_for_hooks.sh
```

### Game crashes on launch
- Check if addresses need regeneration (after game update)
- Disable plugins one by one to find conflicts

## After Game Update

Regenerate addresses:
```bash
python3 scripts/generate_addresses.py \
    "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077" \
    --manual scripts/manual_addresses_template.json \
    --output "$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077/red4ext/bin/x64/cyberpunk2077_addresses.json"
```
EOF

# Create archive
cd "$RELEASE_DIR"
zip -r "${RELEASE_NAME}.zip" "$RELEASE_NAME"
tar -czvf "${RELEASE_NAME}.tar.gz" "$RELEASE_NAME"

echo ""
echo "=== Release Created ==="
echo "Directory: $RELEASE_DIR/$RELEASE_NAME"
echo "Archives:"
ls -la "$RELEASE_DIR"/*.zip "$RELEASE_DIR"/*.tar.gz 2>/dev/null
echo ""
echo "SHA256 checksums:"
shasum -a 256 "$RELEASE_DIR"/*.zip "$RELEASE_DIR"/*.tar.gz 2>/dev/null
