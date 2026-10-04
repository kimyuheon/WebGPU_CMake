#!/bin/bash
# 회귀 테스트 한 번에 (macOS / Linux): 서버 + 헤드리스 크롬 띄우고 regress.mjs 돌리고 정리.
#
#   tools/regress.sh            전부
#   tools/regress.sh sketch     이름 필터
#
# build/ 가 있어야 한다 (./build.sh 먼저). 크롬은 CHROME 환경변수, 없으면 흔한 자리를 찾는다.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
if [ ! -f "$BUILD/WebGPUApp.html" ]; then
    echo "build/WebGPUApp.html 이 없습니다 - ./build.sh 를 먼저 돌리세요" >&2
    exit 1
fi

# 빌드가 소스보다 오래됐으면 멈춘다 (옛 빌드로 시험하면 시간을 통째로 날린다)
if [ -n "$(find "$ROOT/src" "$ROOT/shaders" -type f -newer "$BUILD/WebGPUApp.js" -print -quit 2>/dev/null)" ]; then
    echo "빌드가 소스보다 오래됐습니다 - ./build.sh 를 먼저 돌리세요" >&2
    exit 1
fi

NODE="${NODE:-$(command -v node || ls "$ROOT"/../emsdk/node/*/bin/node 2>/dev/null | head -1)}"
PYTHON="${PYTHON:-$(command -v python3 || command -v python)}"
CHROME="${CHROME:-}"
if [ -z "$CHROME" ]; then
    for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
             "$(command -v google-chrome)" "$(command -v google-chrome-stable)" "$(command -v chromium)" \
             "$(command -v chromium-browser)" "$(command -v microsoft-edge)"; do
        if [ -n "$c" ] && [ -x "$c" ]; then CHROME="$c"; break; fi
    done
fi
[ -z "$NODE" ] && { echo "node 를 찾지 못했습니다" >&2; exit 1; }
[ -z "$CHROME" ] && { echo "Chrome 을 찾지 못했습니다 (CHROME=경로 로 지정)" >&2; exit 1; }

PORT=8123
CDP=9222
PROFILE="${TMPDIR:-/tmp}/lot-regress-chrome"

(cd "$BUILD" && "$PYTHON" -m http.server "$PORT" >/dev/null 2>&1) &
SERVER=$!
"$CHROME" --headless=new --remote-debugging-port="$CDP" --enable-unsafe-swiftshader --enable-unsafe-webgpu \
    --window-size=1100,850 --user-data-dir="$PROFILE" "http://localhost:$PORT/WebGPUApp.html?layout=classic" >/dev/null 2>&1 &
BROWSER=$!
trap 'kill $BROWSER $SERVER 2>/dev/null' EXIT

sleep 4
CDP_PORT="$CDP" "$NODE" "$ROOT/tools/regress.mjs" "${1:-}"
