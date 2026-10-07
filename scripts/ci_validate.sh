#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${SDK_ROOT:-$ROOT_DIR/../RED4ext.SDK}"
BUILD_DIR="$ROOT_DIR/build-ci"

run_step() {
  local name="$1"
  shift
  echo "==> $name"
  if "$@"; then
    echo "[OK] $name"
  else
    echo "[FAIL] $name"
    return 1
  fi
}

fail=0

rm -rf "$BUILD_DIR"

run_step "Configure" cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DRED4EXT_ENABLE_TESTS=ON || fail=1
run_step "Build" cmake --build "$BUILD_DIR" -- -j"$(sysctl -n hw.ncpu)" || fail=1

if [[ -d "$SDK_ROOT" ]]; then
  run_step "Address DB validation" python3 "$SDK_ROOT/scripts/validate_addresses.py" --verified-only --quiet \
    --names "$ROOT_DIR/src/dll/Detail/AddressHashes.hpp" \
    "$SDK_ROOT/cyberpunk2077_addresses.json" || fail=1
else
  echo "[WARN] SDK root not found at $SDK_ROOT; skipping validate_addresses.py"
  fail=1
fi

if [[ -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
  run_step "CTest" ctest --test-dir "$BUILD_DIR" --output-on-failure || fail=1
else
  echo "[WARN] No CTestTestfile.cmake found; skipping tests"
fi

echo "==> Summary"
if [[ $fail -eq 0 ]]; then
  echo "[OK] CI validation succeeded"
else
  echo "[FAIL] CI validation failed"
fi

exit $fail
