# RED4ext for macOS v${VERSION} (Beta)

Script extender for Cyberpunk 2077 on macOS Apple Silicon.

## ⚠️ Beta Release

This is an early macOS port. **Not all features have full parity with Windows.**

- Addresses resolved via pattern matching (not all runtime-verified)
- Only TweakXL plugin tested
- Different hook mechanism than Windows (native arm64 engine vs Detours)
- Please report issues on GitHub

## Downloads

| File | Description |
|------|-------------|
| `RED4ext-macOS-ARM64-v${VERSION}.zip` | Complete release package |

## Requirements

- macOS 12+ (Monterey or later)
- Apple Silicon Mac (M1/M2/M3/M4)
- Cyberpunk 2077 (Steam, macOS native)
- Xcode Command Line Tools

## Quick Install

```bash
unzip RED4ext-macOS-ARM64-v${VERSION}.zip
cd RED4ext-macOS-ARM64-v${VERSION}
./scripts/macos_install.sh
```

## What's Included

```
red4ext/
├── RED4ext.dylib                 # Main loader
├── bin/x64/cyberpunk2077_addresses.json  # Address database
└── plugins/                      # Plugin directory

scripts/
├── macos_install.sh              # Installer
├── generate_addresses.py         # Address regeneration
└── macos_resign_*.sh             # Code signing
```

## Compatibility

| Item | Status |
|------|--------|
| Game Version | Cyberpunk 2077 v2.3.1 |
| SDK Addresses | Only entries marked verified resolve |
| TweakXL | ✅ Basic functionality |
| Other plugins | ❓ Untested |

## Known Limitations

- Windows .dll plugins require recompilation
- Some SDK functions may behave differently
- Edge cases not tested

## SHA256

```
${SHA_ZIP}  RED4ext-macOS-ARM64-v${VERSION}.zip
```

