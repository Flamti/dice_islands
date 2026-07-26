#!/usr/bin/env bash
# Перф-проход этапа 15 (SPEC §14): замер генерации 16 островов (прокси к
# нагрузке обзорной камеры) и прогон ядра под ASan/LSan/UBSan на утечки.
# Реальные 60 FPS на RTX 3050 Mobile проверяются вручную в обзорной камере.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/tools"
OUT_DIR="$ROOT/build/perf"
CXX="${CXX:-g++}"
ISLANDS=16

mkdir -p "$BUILD_DIR" "$OUT_DIR"

echo "==> Сборка CLI-генератора (O2)"
# shellcheck disable=SC2086
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/core/include" -I"$ROOT/core/src" \
    "$ROOT"/core/src/math/*.cpp "$ROOT"/core/src/gen/*.cpp "$ROOT"/core/src/save/*.cpp \
    "$ROOT/core/tools/gen_island.cpp" \
    -o "$BUILD_DIR/gen_island"

echo "==> Генерация $ISLANDS островов (замер времени и полигонов)"
START=$(date +%s.%N)
TOTAL_TRIS=0
for i in $(seq 1 "$ISLANDS"); do
    LINE="$("$BUILD_DIR/gen_island" --seed "$i" --config "$ROOT/data/generator.json" \
        --out "$OUT_DIR/island_$i.glb" | head -1)"
    TRIS="$(sed -n 's/.*треугольников=\([0-9]*\).*/\1/p' <<<"$LINE")"
    TOTAL_TRIS=$((TOTAL_TRIS + ${TRIS:-0}))
done
END=$(date +%s.%N)
ELAPSED="$(awk "BEGIN{printf \"%.3f\", $END-$START}")"
echo "    $ISLANDS островов за ${ELAPSED}s; суммарно треугольников: $TOTAL_TRIS"
# Порог-ориентир: генерация 16 островов должна укладываться в секунды, а не
# минуты (регресс-детектор, не абсолютный бенчмарк).
awk "BEGIN{exit !($ELAPSED < 20.0)}" || { echo "ПРОВАЛ: генерация слишком долгая"; exit 1; }

echo "==> Санитайзеры ядра (ASan+LSan+UBSan) — прогон unit-тестов"
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" \
    CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
    "$ROOT/scripts/test.sh" >/dev/null
echo "    ASan/LSan/UBSan: утечек и ошибок не обнаружено"

echo "OK: перф-проход пройден (генерация в бюджете, санитайзеры чисты)"
