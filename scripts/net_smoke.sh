#!/usr/bin/env bash
# Интеграционные смоук-тесты сети.
# Сценарий 1 (этап 1): хост + гость с неверным паролем + честный гость с эхо.
# Сценарий 2 (этапы 2–4): партия двух людей — барьер PhaseReady, таймеры фаз,
# у каждого участника свой остров с сеткой, гость строит ферму (повтор на
# занятом месте отклоняется), ферма видна обоим.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GODOT_BIN="${GODOT_BIN:-$ROOT/resources/Godot_v4.7.1-stable_linux.x86_64}"
GAME_DIR="$ROOT/game"
HOST_STARTUP_TIMEOUT_SEC=15
PROCESS_TIMEOUT_SEC=60
HOST_LOG="$(mktemp)"
trap 'rm -f "$HOST_LOG"; kill "$HOST_PID" 2>/dev/null || true' EXIT

run_godot() {
    timeout "$PROCESS_TIMEOUT_SEC" "$GODOT_BIN" --headless --path "$GAME_DIR" -- "--smoke=$1"
}

# Поднимает фоновый хост в режиме $1 и ждёт его маркер готовности $2.
start_host() {
    : >"$HOST_LOG"
    timeout "$PROCESS_TIMEOUT_SEC" "$GODOT_BIN" --headless --path "$GAME_DIR" -- "--smoke=$1" >"$HOST_LOG" 2>&1 &
    HOST_PID=$!
    for _ in $(seq "$HOST_STARTUP_TIMEOUT_SEC"); do
        grep -q "$2" "$HOST_LOG" && return 0
        kill -0 "$HOST_PID" 2>/dev/null || { cat "$HOST_LOG"; echo "ХОСТ УПАЛ НА СТАРТЕ"; exit 1; }
        sleep 1
    done
    grep -q "$2" "$HOST_LOG" || { cat "$HOST_LOG"; echo "ХОСТ НЕ ПОДНЯЛСЯ"; exit 1; }
}

# Ждёт завершения фонового хоста и проверяет его маркер успеха $1.
wait_host() {
    if ! wait "$HOST_PID"; then
        cat "$HOST_LOG"
        echo "ПРОВАЛ host"
        exit 1
    fi
    grep -q "$1" "$HOST_LOG" || { cat "$HOST_LOG"; echo "ХОСТ ЗАВЕРШИЛСЯ БЕЗ МАРКЕРА $1"; exit 1; }
}

echo "==> Сценарий 1: лобби (хост, отказ по паролю, гость с эхо)"
start_host host SMOKE_HOST_READY

echo "==> Гость с неверным паролем (ожидается отказ)"
run_godot badguest | grep -q "SMOKE_BADGUEST_OK" || { echo "ПРОВАЛ badguest"; exit 1; }

echo "==> Честный гость (стейт + эхо-намерение)"
run_godot guest | grep -q "SMOKE_GUEST_OK" || { echo "ПРОВАЛ guest"; exit 1; }

echo "==> Ожидание завершения хоста лобби"
wait_host SMOKE_HOST_OK

echo "==> Сценарий 2: партия двух людей (барьер PhaseReady + таймеры фаз)"
start_host mhost SMOKE_MHOST_READY

echo "==> Гость партии (держит барьер, затем готов)"
run_godot mguest | grep -q "SMOKE_MGUEST_OK" || { echo "ПРОВАЛ mguest"; exit 1; }

echo "==> Ожидание завершения хоста партии"
wait_host SMOKE_MHOST_OK

echo "OK: сетевые смоук-тесты пройдены (лобби; партия: фазы, острова, стройка)"
