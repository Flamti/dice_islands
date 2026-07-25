#!/usr/bin/env bash
# Unit-тесты чистого C++ ядра: собираются g++ без Godot и запускаются.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/tests"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined}"

mkdir -p "$BUILD_DIR"

echo "==> Сборка тестов ядра"
# shellcheck disable=SC2086  # CXXFLAGS намеренно разворачивается по словам
"$CXX" $CXXFLAGS \
    -I"$ROOT/core/include" \
    "$ROOT/core/src/core.cpp" \
    "$ROOT"/core/tests/test_*.cpp \
    -o "$BUILD_DIR/core_tests"

echo "==> Запуск тестов ядра"
"$BUILD_DIR/core_tests"
