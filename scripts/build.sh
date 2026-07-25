#!/usr/bin/env bash
# Сборка C++ ядра (GDExtension). Дампит API из бинарника движка при отсутствии.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GODOT_BIN="${GODOT_BIN:-$ROOT/resources/Godot_v4.7.1-stable_linux.x86_64}"
API_DIR="$ROOT/core/extern/api"
JOBS="${JOBS:-$(nproc)}"

if [[ ! -f "$API_DIR/extension_api.json" || ! -f "$API_DIR/gdextension_interface.json" ]]; then
    echo "==> Дамп GDExtension API из $GODOT_BIN"
    mkdir -p "$API_DIR"
    (cd "$API_DIR" && "$GODOT_BIN" --headless --dump-extension-api --dump-gdextension-interface-json)
fi

echo "==> Сборка ядра (scons -j$JOBS)"
scons -C "$ROOT/core" -j"$JOBS" compiledb=yes "$@"

# Рантайм грузит GDExtension по списку res://.godot/extension_list.cfg,
# который создаётся только импортом редактора (см. gdextension_manager.cpp).
echo "==> Импорт Godot-проекта (регистрация GDExtension)"
"$GODOT_BIN" --headless --path "$ROOT/game" --import >/dev/null 2>&1 || true
if [[ ! -f "$ROOT/game/.godot/extension_list.cfg" ]]; then
    echo "ОШИБКА: импорт не зарегистрировал GDExtension" >&2
    exit 1
fi
echo "==> Готово"
