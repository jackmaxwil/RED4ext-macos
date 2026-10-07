#!/bin/bash
#
# RED4ext macOS Installer
# One-command installation for Cyberpunk 2077 modding on macOS
#
# Usage:
#   ./scripts/macos_install.sh [OPTIONS]
#
# Options:
#   --game-dir PATH    Path to Cyberpunk 2077 directory
#   --build            Build RED4ext from source first
#   --help             Show this help
#

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RED4EXT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default game directory
DEFAULT_GAME_DIR="$HOME/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# ============================================================================
# Helper Functions
# ============================================================================

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

print_header() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║              RED4ext macOS Installer                         ║"
    echo "║              Cyberpunk 2077 Modding Framework                ║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo ""
}

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --game-dir PATH    Path to Cyberpunk 2077 directory"
    echo "  --build            Build RED4ext from source first"
    echo "  --help             Show this help"
    echo ""
    echo "Default game directory:"
    echo "  $DEFAULT_GAME_DIR"
}

check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check macOS
    if [[ "$(uname)" != "Darwin" ]]; then
        log_error "This script is for macOS only"
        exit 1
    fi
    
    # Check Apple Silicon
    if [[ "$(uname -m)" != "arm64" ]]; then
        log_warn "Not running on Apple Silicon - hooks may not work"
    fi
    
    # Check SDK submodule exists
    if [[ ! -d "$RED4EXT_ROOT/deps/red4ext.sdk/include" ]]; then
        log_warn "SDK submodule not found - initializing submodules..."
        (cd "$RED4EXT_ROOT" && git submodule update --init --recursive) || {
            log_error "Failed to initialize submodules"
            log_info "Run: git submodule update --init --recursive"
            exit 1
        }
    fi
    
    # Check for required tools
    local missing=()
    command -v curl &>/dev/null || missing+=("curl")
    command -v xz &>/dev/null || missing+=("xz")
    
    if [[ ${#missing[@]} -gt 0 ]]; then
        log_error "Missing required tools: ${missing[*]}"
        log_info "Install with: brew install ${missing[*]}"
        exit 1
    fi
    
    log_success "Prerequisites OK"
}

find_game_dir() {
    local game_dir="$1"
    
    if [[ -n "$game_dir" ]] && [[ -d "$game_dir" ]]; then
        echo "$game_dir"
        return 0
    fi
    
    # Try default Steam location
    if [[ -d "$DEFAULT_GAME_DIR" ]]; then
        echo "$DEFAULT_GAME_DIR"
        return 0
    fi
    
    # Try to find via mdfind
    local found
    found=$(mdfind "kMDItemDisplayName == 'Cyberpunk 2077' && kMDItemContentType == 'com.apple.application-bundle'" 2>/dev/null | head -1)
    if [[ -n "$found" ]]; then
        echo "$(dirname "$found")"
        return 0
    fi
    
    return 1
}

build_red4ext() {
    log_info "Building RED4ext..."
    
    local build_dir="$RED4EXT_ROOT/build"
    mkdir -p "$build_dir"
    
    cd "$build_dir"
    
    if [[ ! -f "Makefile" ]]; then
        log_info "Running CMake..."
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi
    
    log_info "Compiling..."
    make -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -5
    
    if [[ ! -f "$build_dir/libs/RED4ext.dylib" ]]; then
        log_error "Build failed - RED4ext.dylib not found"
        exit 1
    fi
    
    cd "$RED4EXT_ROOT"
    log_success "Build complete"
}

install_files() {
    local game_dir="$1"
    local red4ext_dir="$game_dir/red4ext"
    local bin_dir="$red4ext_dir/bin/x64"
    
    log_info "Installing to: $red4ext_dir"
    
    # Create directories
    mkdir -p "$red4ext_dir"
    mkdir -p "$bin_dir"
    mkdir -p "$red4ext_dir/plugins"
    mkdir -p "$red4ext_dir/logs"
    
    # Install RED4ext.dylib
    local dylib="$RED4EXT_ROOT/build/libs/RED4ext.dylib"
    if [[ -f "$dylib" ]]; then
        cp -f "$dylib" "$red4ext_dir/"
        log_success "Installed RED4ext.dylib"
    else
        log_error "RED4ext.dylib not found - run with --build first"
        exit 1
    fi

    # The launcher uses this to compile only the scripts of plugins RED4ext will load
    local check="$RED4EXT_ROOT/build/bin/red4ext_plugin_check"
    if [[ -x "$check" ]]; then
        cp -f "$check" "$red4ext_dir/bin/"
        log_success "Installed red4ext_plugin_check"
    else
        log_warn "red4ext_plugin_check not found - the launcher will not compile any plugin scripts"
    fi
    
    # Remove leftovers from the Frida-based setup
    rm -f "$red4ext_dir/FridaGadget.dylib" "$red4ext_dir/FridaGadget.config" "$red4ext_dir/red4ext_hooks.js"

    # Install the single canonical address database (from the SDK submodule) and drop stale copies.
    local sdk_db="$SCRIPT_DIR/../deps/red4ext.sdk/cyberpunk2077_addresses.json"
    rm -f "$red4ext_dir/cyberpunk2077_addresses.json" "$bin_dir/cyberpunk2077_addresses.loader.json"
    if [[ -f "$sdk_db" ]]; then
        cp -f "$sdk_db" "$bin_dir/cyberpunk2077_addresses.json"
        log_success "Installed address database"
    else
        log_error "Address database not found at $sdk_db (run: git submodule update --init)"
        return 1
    fi

    # Generate symbol mappings if needed
    local symbols_file="$bin_dir/cyberpunk2077_symbols.json"
    if [[ ! -f "$symbols_file" ]]; then
        local game_binary="$game_dir/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077"
        if [[ -f "$game_binary" ]] && [[ -f "$SCRIPT_DIR/generate_symbol_mapping.py" ]]; then
            log_info "Generating symbol mappings (this may take a minute)..."
            python3 "$SCRIPT_DIR/generate_symbol_mapping.py" "$game_binary" \
                --output "$symbols_file" 2>/dev/null || log_warn "Could not generate symbols"
        fi
    fi
    
    if [[ -f "$symbols_file" ]]; then
        log_success "Symbol mappings ready"
    else
        log_warn "Symbol mappings not found - some features may not work"
    fi
    
    # Install launcher script
    local launcher="$game_dir/launch_red4ext.sh"
    cp -f "$RED4EXT_ROOT/scripts/launch_red4ext.sh" "$launcher"
    chmod +x "$launcher"
    log_success "Installed launcher script"
}

print_summary() {
    local game_dir="$1"
    
    echo ""
    echo "════════════════════════════════════════════════════════════════"
    echo ""
    log_success "RED4ext installation complete!"
    echo ""
    echo "To launch the game with mods:"
    echo ""
    echo "  cd \"$game_dir\""
    echo "  ./launch_red4ext.sh"
    echo ""
    echo "Launching from Steam does NOT load mods (Steam does not pass DYLD_INSERT_LIBRARIES)."
    echo "RED4ext writes a default red4ext/config.ini on first launch."
    echo ""
    echo "Log files: $game_dir/red4ext/logs/"
    echo "Plugins:   $game_dir/red4ext/plugins/"
    echo ""
    echo "════════════════════════════════════════════════════════════════"
}

# ============================================================================
# Main
# ============================================================================

main() {
    local game_dir=""
    local do_build=false
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --game-dir)
                game_dir="$2"
                shift 2
                ;;
            --build)
                do_build=true
                shift
                ;;
            --help|-h)
                print_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                print_usage
                exit 1
                ;;
        esac
    done
    
    print_header
    check_prerequisites
    
    # Find game directory
    game_dir=$(find_game_dir "$game_dir") || {
        log_error "Could not find Cyberpunk 2077 installation"
        log_info "Specify with: $0 --game-dir /path/to/game"
        exit 1
    }
    log_success "Found game at: $game_dir"
    
    # Build if requested or if dylib doesn't exist
    if [[ "$do_build" == "true" ]] || [[ ! -f "$RED4EXT_ROOT/build/libs/RED4ext.dylib" ]]; then
        build_red4ext
    fi
    
    # Install files
    install_files "$game_dir"
    
    print_summary "$game_dir"
}

main "$@"
