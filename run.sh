#!/bin/bash
echo "========================================"
echo "WebGPU App Runner"
echo "========================================"
echo ""

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -f "$SCRIPT_DIR/build/WebGPUApp.html" ]; then
    echo "ERROR: WebGPUApp.html not found!"
    echo "Please run build.sh first."
    exit 1
fi

cd "$SCRIPT_DIR/build"

echo "Starting web server at http://localhost:8000"
echo "Open: http://localhost:8000/WebGPUApp.html"
echo "Press Ctrl+C to stop"
echo "========================================"
echo ""

python3 -m http.server 8000
