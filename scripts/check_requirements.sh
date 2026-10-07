#!/bin/bash
#
# RED4ext Build Requirements Checker
# Verifies all prerequisites are installed before building
#

set -uo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║           RED4ext macOS Build Requirements Check             ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

ERRORS=0
WARNINGS=0

check_pass() { echo -e "${GREEN}✓${NC} $1"; }
check_fail() { echo -e "${RED}✗${NC} $1"; ((ERRORS++)); }
check_warn() { echo -e "${YELLOW}!${NC} $1"; ((WARNINGS++)); }

# ============================================================================
# System Checks
# ============================================================================

echo "=== System ==="

# macOS version
if [[ "$(uname)" == "Darwin" ]]; then
    MACOS_VERSION=$(sw_vers -productVersion)
    MAJOR_VERSION=$(echo "$MACOS_VERSION" | cut -d. -f1)
    if [[ $MAJOR_VERSION -ge 12 ]]; then
        check_pass "macOS $MACOS_VERSION (12+ required)"
    else
        check_fail "macOS $MACOS_VERSION (need 12+)"
    fi
else
    check_fail "Not running macOS"
fi

# Architecture
ARCH=$(uname -m)
if [[ "$ARCH" == "arm64" ]]; then
    check_pass "Apple Silicon ($ARCH)"
else
    check_fail "Architecture: $ARCH (need arm64)"
fi

echo ""

# ============================================================================
# Tool Checks
# ============================================================================

echo "=== Build Tools ==="

# Xcode CLI
if xcode-select -p &>/dev/null; then
    XCODE_PATH=$(xcode-select -p)
    check_pass "Xcode CLI Tools ($XCODE_PATH)"
else
    check_fail "Xcode CLI Tools not installed"
    echo "       Install: xcode-select --install"
fi

# CMake
if command -v cmake &>/dev/null; then
    CMAKE_VERSION=$(cmake --version | head -1 | awk '{print $3}')
    CMAKE_MAJOR=$(echo "$CMAKE_VERSION" | cut -d. -f1)
    CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)
    if [[ $CMAKE_MAJOR -gt 3 ]] || [[ $CMAKE_MAJOR -eq 3 && $CMAKE_MINOR -ge 23 ]]; then
        check_pass "CMake $CMAKE_VERSION (3.23+ required)"
    else
        check_fail "CMake $CMAKE_VERSION (need 3.23+)"
        echo "       Upgrade: brew upgrade cmake"
    fi
else
    check_fail "CMake not installed"
    echo "       Install: brew install cmake"
fi

# Git
if command -v git &>/dev/null; then
    GIT_VERSION=$(git --version | awk '{print $3}')
    check_pass "Git $GIT_VERSION"
else
    check_fail "Git not installed"
fi

# Make
if command -v make &>/dev/null; then
    check_pass "Make available"
else
    check_fail "Make not found"
fi

echo ""

# ============================================================================
# Optional Tools
# ============================================================================

echo "=== Optional Tools ==="

# Python
if command -v python3 &>/dev/null; then
    PYTHON_VERSION=$(python3 --version 2>&1 | awk '{print $2}')
    PYTHON_MAJOR=$(echo "$PYTHON_VERSION" | cut -d. -f1)
    PYTHON_MINOR=$(echo "$PYTHON_VERSION" | cut -d. -f2)
    if [[ $PYTHON_MAJOR -ge 3 && $PYTHON_MINOR -ge 10 ]]; then
        check_pass "Python $PYTHON_VERSION (for address regeneration)"
    else
        check_warn "Python $PYTHON_VERSION (3.10+ recommended)"
    fi
else
    check_warn "Python not installed (needed for address regeneration)"
fi


# curl
if command -v curl &>/dev/null; then
    check_pass "curl available"
else
    check_warn "curl not installed"
fi

echo ""

# ============================================================================
# Repository Checks
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RED4EXT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Repository ==="

# Check we're in the right place
if [[ -f "$RED4EXT_ROOT/CMakeLists.txt" ]]; then
    check_pass "RED4ext repository found"
else
    check_fail "Not in RED4ext repository"
fi

# Check submodules
MISSING_SUBMODULES=()

check_submodule() {
    local path="$1"
    local name="$2"
    if [[ -d "$RED4EXT_ROOT/$path" ]] && [[ -n "$(ls -A "$RED4EXT_ROOT/$path" 2>/dev/null)" ]]; then
        return 0
    else
        MISSING_SUBMODULES+=("$name")
        return 1
    fi
}

check_submodule "deps/red4ext.sdk/include" "red4ext.sdk" && check_pass "SDK submodule" || check_fail "SDK submodule missing"
check_submodule "deps/fishhook" "fishhook" && check_pass "fishhook submodule" || check_fail "fishhook submodule missing"
check_submodule "deps/spdlog" "spdlog" && check_pass "spdlog submodule" || check_fail "spdlog submodule missing"
check_submodule "deps/fmt" "fmt" && check_pass "fmt submodule" || check_fail "fmt submodule missing"
check_submodule "deps/simdjson" "simdjson" && check_pass "simdjson submodule" || check_fail "simdjson submodule missing"

if [[ ${#MISSING_SUBMODULES[@]} -gt 0 ]]; then
    echo ""
    echo "       To fix submodules:"
    echo "       git submodule update --init --recursive"
fi

echo ""

# ============================================================================
# Game Check
# ============================================================================

echo "=== Game Installation ==="

GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
if [[ -d "$GAME_DIR" ]]; then
    check_pass "Cyberpunk 2077 found at default location"
    
    # Check game binary
    GAME_BINARY="$GAME_DIR/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"
    if [[ -f "$GAME_BINARY" ]]; then
        check_pass "Game binary exists"
    else
        check_warn "Game binary not found (may be different path)"
    fi
else
    check_warn "Cyberpunk 2077 not at default Steam location"
    echo "       You'll need to specify --game-dir during installation"
fi

echo ""

# ============================================================================
# Summary
# ============================================================================

echo "════════════════════════════════════════════════════════════════"
echo ""

if [[ $ERRORS -eq 0 ]]; then
    if [[ $WARNINGS -eq 0 ]]; then
        echo -e "${GREEN}All checks passed!${NC} Ready to build."
    else
        echo -e "${GREEN}Ready to build${NC} (with $WARNINGS warnings)"
    fi
    echo ""
    echo "Next steps:"
    echo "  mkdir build && cd build"
    echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
    echo "  make -j\$(sysctl -n hw.ncpu)"
    echo ""
    exit 0
else
    echo -e "${RED}$ERRORS error(s) found.${NC} Please fix before building."
    echo ""
    exit 1
fi
