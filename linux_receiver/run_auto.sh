#!/usr/bin/env bash
set -euo pipefail

# Script para encontrar automáticamente el puerto serial de la placa ESP32

echo "=== Buscando puerto serial ESP32 ==="
echo ""

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
EXECUTABLE="$SCRIPT_DIR/build/esp32_receiver"

if [[ ! -x "$EXECUTABLE" ]]; then
    echo "El receptor no está compilado; compilando ahora..."
    "$SCRIPT_DIR/build.sh"
fi

cd "$SCRIPT_DIR"
exec "$EXECUTABLE" auto "$@"
