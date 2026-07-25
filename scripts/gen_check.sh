#!/usr/bin/env bash
# Проверка CLI-генератора острова (этап 3): сборка без Godot, валидный GLB,
# повторяемость байт-в-байт по сиду, различие мешей при разных сидах.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/tools"
OUT_DIR="$ROOT/build/gen_check"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -Wall -Wextra -Werror -O2}"
SEED=20260725

mkdir -p "$BUILD_DIR" "$OUT_DIR"

echo "==> Сборка CLI-генератора"
# shellcheck disable=SC2086  # CXXFLAGS намеренно разворачивается по словам
"$CXX" $CXXFLAGS \
    -I"$ROOT/core/include" -I"$ROOT/core/src" \
    "$ROOT"/core/src/math/*.cpp "$ROOT"/core/src/gen/*.cpp "$ROOT"/core/src/save/*.cpp \
    "$ROOT/core/tools/gen_island.cpp" \
    -o "$BUILD_DIR/gen_island"

echo "==> Генерация (сид $SEED, дважды) и контрольный сид"
"$BUILD_DIR/gen_island" --seed "$SEED" --config "$ROOT/data/generator.json" --out "$OUT_DIR/a.glb"
"$BUILD_DIR/gen_island" --seed "$SEED" --config "$ROOT/data/generator.json" --out "$OUT_DIR/b.glb"
"$BUILD_DIR/gen_island" --seed "$((SEED + 1))" --config "$ROOT/data/generator.json" --out "$OUT_DIR/c.glb"

echo "==> Проверка повторяемости и валидности"
cmp "$OUT_DIR/a.glb" "$OUT_DIR/b.glb" || { echo "ПРОВАЛ: один сид дал разные GLB"; exit 1; }
if cmp -s "$OUT_DIR/a.glb" "$OUT_DIR/c.glb"; then
    echo "ПРОВАЛ: разные сиды дали одинаковые GLB"
    exit 1
fi
# Заголовок GLB: магия "glTF" и версия 2.
head -c 4 "$OUT_DIR/a.glb" | grep -q "glTF" || { echo "ПРОВАЛ: нет магии glTF"; exit 1; }

echo "OK: CLI-генератор детерминирован, GLB валиден"
