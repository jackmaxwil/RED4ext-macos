# Release Process

## Prerequisites

1. Ensure all tests pass
2. Update version in CHANGELOG.md
3. Build RED4ext.dylib

## Creating a Release

### 1. Build Release Package

```bash
./scripts/create_release.sh 1.0.0
```

This creates:
- `release/RED4ext-macOS-ARM64-v1.0.0.zip`
- `release/RED4ext-macOS-ARM64-v1.0.0.tar.gz`

### 2. Tag the Release

```bash
git add -A
git commit -m "Release v1.0.0"
git tag -a v1.0.0 -m "RED4ext macOS v1.0.0 - Initial Release"
git push origin main --tags
```

### 3. Create GitHub Release

1. Go to GitHub → Releases → New Release
2. Select tag `v1.0.0`
3. Copy release notes from `.github/RELEASE_TEMPLATE.md`
4. Upload:
   - `RED4ext-macOS-ARM64-v1.0.0.zip`
   - `RED4ext-macOS-ARM64-v1.0.0.tar.gz`
5. Publish

## Version Numbering

- `MAJOR.MINOR.PATCH` (SemVer)
- Major: Breaking changes
- Minor: New features, backwards compatible
- Patch: Bug fixes

## Checklist

Before release:
- [ ] Every address hash each shipped plugin needs is verified (`deps/red4ext.sdk/scripts/plugin_requirements.py`)
- [ ] TweakXL plugin tested
- [ ] macos_install.sh works on clean system
- [ ] README updated
- [ ] CHANGELOG updated

## Updating Address Database

After game updates, regenerate addresses:

```bash
python3 scripts/generate_addresses.py "/path/to/Cyberpunk2077" \
    --manual scripts/manual_addresses_template.json \
    --output deps/red4ext.sdk/cyberpunk2077_addresses.json
```
