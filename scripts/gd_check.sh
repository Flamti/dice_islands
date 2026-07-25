#!/usr/bin/env bash
# Проверка GDScript headless-запуском проекта: главная сцена загружается,
# все её скрипты и autoload-синглтоны компилируются, движок выходит (--quit).
# Примечание: --check-only здесь непригоден — он проверяет файл в отрыве от
# проекта и не видит autoload-синглтоны (NetSession).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GODOT_BIN="${GODOT_BIN:-$ROOT/resources/Godot_v4.7.1-stable_linux.x86_64}"
GAME_DIR="$ROOT/game"
TIMEOUT_SEC=60

output="$(timeout "$TIMEOUT_SEC" "$GODOT_BIN" --headless --path "$GAME_DIR" --quit 2>&1)" || {
    echo "$output"
    echo "ПРОВАЛ: движок завершился с ошибкой"
    exit 1
}

if grep -qE "SCRIPT ERROR|Compile Error|Parse Error|Failed to load script" <<<"$output"; then
    echo "$output"
    echo "ПРОВАЛ: обнаружены ошибки скриптов"
    exit 1
fi

echo "OK: проект загружается headless без ошибок скриптов"
