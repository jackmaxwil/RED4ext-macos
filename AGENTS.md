# RED4ext macOS port: agent guide

RED4ext is the Cyberpunk 2077 plugin loader, ported to macOS arm64. Target: Cyberpunk 2077 2.3.1 (patch 2.31), Steam,
Apple silicon. See `README.md` for install, build and the test loop.

## Rules

1. **macOS first.** New code must build and run on macOS arm64. Keep Windows code paths compiling where they exist, but
   do not put Win32 types or APIs in shared code. Use `#ifdef __APPLE__` / `#ifdef _WIN32` for platform code.
2. **Fail closed.** Addresses come only from `cyberpunk2077_addresses.json` entries marked `"verified": true`; anything
   else resolves to 0. The loader refuses a plugin unless every address hash compiled into it is verified
   (`src/dll/Platform/PluginRequirements.cpp`). Never mark an entry verified without evidence from the current game
   binary (`RED4ext.SDK/docs/re/*.md`, `RED4ext.SDK/docs/ADDRESS_AUDIT.md`). Never add a fallback that guesses.
3. **Native hooks only.** Hooks go through the in-process arm64 engine in `src/dll/Platform/NativeHook*`. Do not add
   Detours on macOS, any external instrumentation framework, or any external injector.
4. **Never launch the game or Steam from tooling,** except through `tools/cp-run` (directly or via `tools/cp-dev`), which
   runs the game in background mode (`RED4EXT_BACKGROUND=1`) and never takes focus, the keyboard or the mouse. Do not
   use osascript, `open`, or the Steam Play button. `tools/cp-gate` is offline and always safe to run.
5. **Commits:** one logical change per commit, plain messages, no attribution or co-author lines. Work on `main`; never
   force-push it.

## Layout

| Path | Contents |
| --- | --- |
| `src/dll/` | The loader (`RED4ext.dylib`). `Platform/` has the macOS code: `NativeHook*` (hook engine), `Hooking.cpp`, `PluginRequirements.cpp` (address gate), `CrashHandler`, `RuntimeValidation`. |
| `src/plugin_check/` | `red4ext_plugin_check DB plugin.dylib...`: the loader's address gate as a CLI. Used by the launcher, `cp-gate` and the release script. |
| `src/loader/` | Windows `winmm.dll` proxy (upstream, not built on macOS). |
| `deps/` | Submodules (see `.gitmodules`): `red4ext.sdk` (jackmaxwil/RED4ext.SDK-macos, holds the address DB), `fishhook`, `fmt`, `spdlog`, `simdjson`, `toml11`, `ordered-map`, `redscript`, and Windows-only `detours`, `wil`. |
| `scripts/` | `install_macos.sh` (release one-time setup), `macos_install.sh` (install from a source build), `launch_red4ext.sh`, `codesign_macos.sh` + `red4ext_entitlements.plist`, `create_release.sh`. |
| `tools/` | `cp-dev` (build, sign, install, test), `cp-run` (unattended in-game test), `cp-gate` (offline gate), `cp-rollback` (restore stock binary), `autotest/` (redscript test driver and scenarios). |
| `tests/` | CTest suites, including `native_hook_tests` (run in CI). |

The sibling repos `../RED4ext.SDK`, `../cp2077-tweak-xl`, `../cp2077-archive-xl-macos` and `../cp2077-modmenu` are used
by `cp-dev`, `cp-gate` and `create_release.sh`. `cp-gate` and the release read the address DB from `../RED4ext.SDK`.

## Checks before handing back

- `cmake -S . -B build-dev && cmake --build build-dev -j8` builds with no errors.
- `ctest --test-dir build-dev --output-on-failure`.
- `tools/cp-gate` passes (needs the plugins built in their `build-dev` folders).
- To list a plugin's unverified addresses: `python3 ../RED4ext.SDK/scripts/plugin_requirements.py <plugin.dylib>`.

## Pitfalls

- `Resolve()` returning 0 means the hash is missing from the DB or not verified. Verify it; do not work around it.
- Hooks fail if the game binary lacks `allow-unsigned-executable-memory`: run `scripts/codesign_macos.sh exe <binary>`.
- A game patch changes every address. Follow the patch-day steps in the header of `tools/cp-gate`.
- Plugin scripts that declare natives of a plugin that is not loaded stop the game at script initialization. The
  launcher and `cp-run` compile a plugin's scripts only if `red4ext_plugin_check` passes for it.

Binary layout and reverse-engineering notes: `docs/CYBERPUNK_INTERNALS.md`.
