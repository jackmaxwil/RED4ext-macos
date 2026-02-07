#!/bin/bash
#
# RED4ext Address Pipeline Runner
# 
# Generates the cyberpunk2077_addresses.json from the macOS game binary
# Usage: ./run_address_pipeline.sh [path_to_Cyberpunk2077_binary]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_PATH="${1:-/Applications/Cyberpunk\ 2077/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077}"
OUTPUT_DIR="$SCRIPT_DIR/outputs"
TMP_DIR="$SCRIPT_DIR/.tmp"

# Create directories
mkdir -p "$OUTPUT_DIR"
mkdir -p "$TMP_DIR"

echo "=========================================="
echo "RED4ext Address Pipeline"
echo "=========================================="
echo "Binary: $BINARY_PATH"
echo "Output: $OUTPUT_DIR/cyberpunk2077_addresses.json"
echo ""

# Check if binary exists
if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary not found at $BINARY_PATH"
    echo "Usage: $0 [path_to_Cyberpunk2077]"
    exit 1
fi

echo "Step 1: Generating address database..."
cd "$SCRIPT_DIR"
python3 generate_addresses.py \
    "$BINARY_PATH" \
    --manual manual_addresses_template.json \
    --output "$OUTPUT_DIR/cyberpunk2077_addresses.json" \
    --verbose

echo ""
echo "Step 2: Validating output..."
if [ -f "$OUTPUT_DIR/cyberpunk2077_addresses.json" ]; then
    COUNT=$(python3 -c "import json; data=json.load(open('$OUTPUT_DIR/cyberpunk2077_addresses.json')); print(len(data.get('Addresses', [])))" 2>/dev/null || echo "0")
    echo "✓ Generated $COUNT addresses"
else
    echo "✗ Failed to generate address database"
    exit 1
fi

echo ""
echo "=========================================="
echo "Pipeline Complete"
echo "=========================================="
echo ""
echo "To install into the game:"
echo "  cp '$OUTPUT_DIR/cyberpunk2077_addresses.json' \\"
echo "     '/path/to/Cyberpunk2077.app/Contents/red4ext/bin/x64/'"
echo ""
