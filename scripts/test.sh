#!/usr/bin/env bash
# Unit-тесты чистого C++ ядра: собираются g++ без Godot и запускаются.
# Каждый tests/test_*.cpp — самостоятельный бинарник со своим main().
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/tests"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined}"

CORE_SOURCES=(
    "$ROOT"/core/src/*.cpp
    "$ROOT"/core/src/ecs/*.cpp
    "$ROOT"/core/src/turn/*.cpp
    "$ROOT"/core/src/net/*.cpp
    "$ROOT"/core/src/math/*.cpp
    "$ROOT"/core/src/gen/*.cpp
    "$ROOT"/core/src/save/*.cpp
    "$ROOT"/core/src/systems/*.cpp
    "$ROOT"/core/src/systems/combat/*.cpp
)
INCLUDES=(
    -I"$ROOT/core/include"
    -I"$ROOT/core/src"
    -I"$ROOT/core/extern/entt/single_include"
)

mkdir -p "$BUILD_DIR"

for test_src in "$ROOT"/core/tests/test_*.cpp; do
    name="$(basename "$test_src" .cpp)"
    echo "==> Сборка $name"
    sources=()
    for pattern in "${CORE_SOURCES[@]}"; do
        [[ -f "$pattern" ]] && sources+=("$pattern")
    done
    # shellcheck disable=SC2086  # CXXFLAGS намеренно разворачивается по словам
    "$CXX" $CXXFLAGS "${INCLUDES[@]}" "${sources[@]}" "$test_src" -o "$BUILD_DIR/$name"
    echo "==> Запуск $name"
    "$BUILD_DIR/$name"
done

echo "OK: все тестовые бинарники ядра пройдены"
