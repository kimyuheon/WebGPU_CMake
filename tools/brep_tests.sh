#!/bin/bash
# B-rep 기하 테스트 (네이티브 tests/brep_tests.cpp 와 같은 기대값) - 빌드하고 Node 로 돌린다.
#   bash tools/brep_tests.sh
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/../emsdk/emsdk_env.sh" >/dev/null 2>&1
emcmake cmake -S "$ROOT/tests" -B "$ROOT/build-tests" >/dev/null
cmake --build "$ROOT/build-tests" --target brep_tests
NODE="${NODE:-$(command -v node || ls "$ROOT"/../emsdk/node/*/bin/node 2>/dev/null | head -1)}"
"$NODE" "$ROOT/build-tests/brep_tests.js"
