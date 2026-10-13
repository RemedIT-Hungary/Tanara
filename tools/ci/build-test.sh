#!/usr/bin/env bash
# Configure, build and test Tanara headless (no display needed).
# Usage: tools/ci/build-test.sh [build-dir] [extra cmake args...]
#   build-dir   default: <repo>/build
# Environment:
#   JOBS        parallel build/test jobs (default 4)
#   CMAKE_BUILD_TYPE (default Release)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${1:-$ROOT/build}"
shift || true
JOBS="${JOBS:-4}"

export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
# Sandbox: tests must never touch real user data.
TANARA_HOME="$(mktemp -d)"
export TANARA_HOME
trap 'rm -rf "$TANARA_HOME"' EXIT

LAUNCHER=()
if command -v ccache >/dev/null 2>&1; then
    LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" "${LAUNCHER[@]}" "$@"
cmake --build "$BUILD" -j"$JOBS"

# Run all tests, then summarize skips even when a test failed.
status=0
ctest --test-dir "$BUILD" -j"$JOBS" --output-on-failure || status=$?
python3 "$ROOT/tools/ci/qskip-summary.py" "$BUILD/Testing/Temporary/LastTest.log" || true
exit "$status"
