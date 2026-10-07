# RED4ext Documentation

## User Documentation

| Document | Description |
|----------|-------------|
| [STATUS.md](STATUS.md) | Current port status |
| [../BUILDING.md](../BUILDING.md) | Build and install |
| [MACOS_CODE_SIGNING.md](MACOS_CODE_SIGNING.md) | Code signing requirements |

## Development Documentation

| Document | Description |
|----------|-------------|
| [porting/](porting/) | macOS port development history |

---

## Quick Links

### Installation

```bash
# One-command install
./scripts/macos_install.sh

# Or manually
cd /path/to/RED4ext
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
cd .. && ./scripts/macos_install.sh
```

### Launching

```bash
cd "~/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
./launch_red4ext.sh
```

### Building Plugins

See the plugin repos (TweakXL, ArchiveXL, ModMenu) for working CMake setups against `RED4ext.SDK`.
