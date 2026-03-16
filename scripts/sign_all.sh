#!/bin/bash
set -euo pipefail

GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
TARGET_DIR="$GAME_DIR/red4ext"

if [[ ! -d "$TARGET_DIR" ]]; then
  echo "[ERROR] RED4ext directory not found: $TARGET_DIR"
  exit 1
fi

dylibs=()
while IFS= read -r dylib; do
  dylibs+=("$dylib")
done < <(find "$TARGET_DIR" -type f -name "*.dylib" | sort)

if [[ ${#dylibs[@]} -eq 0 ]]; then
  echo "[WARN] No dylib files found under $TARGET_DIR"
  exit 0
fi

fail=0

for dylib in "${dylibs[@]}"; do
  echo "[INFO] Signing: $dylib"
  if ! codesign -s - --force "$dylib"; then
    echo "[FAIL] codesign failed: $dylib"
    fail=1
    continue
  fi

  if ! codesign -v "$dylib"; then
    echo "[FAIL] codesign verify failed: $dylib"
    fail=1
    continue
  fi

  echo "[OK] Signed: $dylib"
done

exit $fail
