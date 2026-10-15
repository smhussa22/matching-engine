#!/usr/bin/env bash
# usage: ./build.sh                → configure + build + run all tests via ctest
#        ./build.sh all            → same as above
#        ./build.sh <ClassName>    → build and run tests/<ClassName>_test
# examples: ./build.sh OrderBook, ./build.sh ExchangeListener

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

# first-time config: generate build system
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    cmake -S "$ROOT" -B "$BUILD"
fi

# always re-run cmake so new test files / targets get picked up
cmake -S "$ROOT" -B "$BUILD" > /dev/null

TARGET="${1:-all}"

if [ "$TARGET" = "all" ]; then
    cmake --build "$BUILD"
    (cd "$BUILD" && ctest --output-on-failure)
else
    cmake --build "$BUILD" --target "${TARGET}_test"
    "$BUILD/${TARGET}_test"
fi
