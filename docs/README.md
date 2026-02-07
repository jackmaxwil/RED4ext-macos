# RED4ext Documentation

## User Documentation

| Document | Description |
|----------|-------------|
| [STATUS.md](STATUS.md) | Current port status + verification checklist |
| [MACOS_PORT.md](MACOS_PORT.md) | **macOS Installation & Usage Guide** |
| [MACOS_CODE_SIGNING.md](MACOS_CODE_SIGNING.md) | Code signing requirements |
| [FRIDA_INTEGRATION.md](FRIDA_INTEGRATION.md) | Frida Gadget technical details |
| [api/](api/) | Plugin API documentation |

## Development Documentation

| Document | Description |
|----------|-------------|
| [porting/](porting/) | macOS port development history |
| [porting/HOOKING_ALTERNATIVES.md](porting/HOOKING_ALTERNATIVES.md) | Hooking approach analysis |

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

See [MACOS_PORT.md#plugin-development](MACOS_PORT.md#plugin-development)
