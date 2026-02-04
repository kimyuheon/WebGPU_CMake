#!/bin/bash
echo "========================================"
echo "Clean Build Directory"
echo "========================================"
echo ""

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -d "$SCRIPT_DIR/build" ]; then
    echo "Deleting build directory..."
    rm -rf "$SCRIPT_DIR/build"
    echo "Done!"
else
    echo "Build directory does not exist."
fi

echo ""
