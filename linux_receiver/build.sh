#!/usr/bin/env bash
set -euo pipefail

# Script de compilación para el receptor de datos ESP32

echo "=== Compilando receptor de datos ESP32 ==="
echo ""

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

# Generar archivos de compilación
echo "Generando archivos CMake..."
cmake "$SCRIPT_DIR"

# Compilar
echo "Compilando..."
cmake --build . -- -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

echo ""
echo "✓ Compilación exitosa"
echo "Ejecutable: $SCRIPT_DIR/build/esp32_receiver"
echo "Uso: $SCRIPT_DIR/run_auto.sh"
