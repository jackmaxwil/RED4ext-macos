# RED4ext macOS Port: Status

Target: Cyberpunk 2077 macOS 2.3.1 (Steam, arm64).

The authoritative progress table is §0 of `~/Development/cyberpunk/RESUME_PLAN.md` (a workspace file outside this repo). Address evidence is in [`deps/red4ext.sdk/docs/ADDRESS_AUDIT.md`](../deps/red4ext.sdk/docs/ADDRESS_AUDIT.md).

## Current state

- RED4ext boots the game to the main menu on macOS. Core hooks are installed only when their address is verified, and the SDK self-checks pass.
- Hooks use the native in-process engine in `src/dll/Platform/NativeHook*`. Frida is no longer used.
- Addresses resolve only from `cyberpunk2077_addresses.json` entries marked `"verified": true`. Everything else resolves to 0.
- The loader refuses any plugin whose compiled-in address hashes are not all verified (`src/dll/Platform/PluginRequirements.cpp`).
- ModMenu: required addresses verified, loads in game.
- TweakXL and ArchiveXL: not loadable yet. Their unverified addresses are being worked through.

## Running

- `scripts/macos_install.sh` installs RED4ext, the address DB and `launch_red4ext.sh`.
- Hooking needs the game re-signed with `scripts/red4ext_entitlements.plist` (`scripts/macos_resign_for_hooks.sh`), which adds `allow-unsigned-executable-memory`.
- `tools/cp-run` and `tools/cp-dev` run unattended test launches.
