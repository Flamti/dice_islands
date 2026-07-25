#!/usr/bin/env bash
# Интеграционный смоук-тест сети этапа 1: хост + гость с неверным паролем +
# честный гость с эхо-намерением. Все три процесса должны выйти с кодом 0.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GODOT_BIN="${GODOT_BIN:-$ROOT/resources/Godot_v4.7.1-stable_linux.x86_64}"
GAME_DIR="$ROOT/game"
HOST_STARTUP_TIMEOUT_SEC=15
PROCESS_TIMEOUT_SEC=40
HOST_LOG="$(mktemp)"
trap 'rm -f "$HOST_LOG"; kill "$HOST_PID" 2>/dev/null || true' EXIT

run_godot() {
    timeout "$PROCESS_TIMEOUT_SEC" "$GODOT_BIN" --headless --path "$GAME_DIR" -- "--smoke=$1"
}

echo "==> Запуск смоук-хоста"
timeout "$PROCESS_TIMEOUT_SEC" "$GODOT_BIN" --headless --path "$GAME_DIR" -- --smoke=host >"$HOST_LOG" 2>&1 &
HOST_PID=$!

for _ in $(seq "$HOST_STARTUP_TIMEOUT_SEC"); do
    grep -q "SMOKE_HOST_READY" "$HOST_LOG" && break
    kill -0 "$HOST_PID" 2>/dev/null || { cat "$HOST_LOG"; echo "ХОСТ УПАЛ НА СТАРТЕ"; exit 1; }
    sleep 1
done
grep -q "SMOKE_HOST_READY" "$HOST_LOG" || { cat "$HOST_LOG"; echo "ХОСТ НЕ ПОДНЯЛСЯ"; exit 1; }

echo "==> Гость с неверным паролем (ожидается отказ)"
run_godot badguest | grep -q "SMOKE_BADGUEST_OK" || { echo "ПРОВАЛ badguest"; exit 1; }

echo "==> Честный гость (стейт + эхо-намерение)"
run_godot guest | grep -q "SMOKE_GUEST_OK" || { echo "ПРОВАЛ guest"; exit 1; }

echo "==> Ожидание завершения хоста"
if ! wait "$HOST_PID"; then
    cat "$HOST_LOG"
    echo "ПРОВАЛ host"
    exit 1
fi
grep -q "SMOKE_HOST_OK" "$HOST_LOG" || { cat "$HOST_LOG"; echo "ХОСТ ЗАВЕРШИЛСЯ БЕЗ МАРКЕРА"; exit 1; }

echo "OK: сетевой смоук-тест пройден (хост, отказ по паролю, гость с эхо)"
