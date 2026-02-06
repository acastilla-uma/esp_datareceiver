#!/bin/bash

# Script para encontrar automáticamente el puerto serial de la placa ESP32

echo "=== Buscando puerto serial ESP32 ==="
echo ""

# Buscar dispositivos seriales
PORTS=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null)

if [ -z "$PORTS" ]; then
    echo "No se encontraron puertos seriales"
    echo ""
    echo "Verifica:"
    echo "  1. La placa está conectada por USB-C"
    echo "  2. Están instalados los drivers"
    echo ""
    echo "Puertos conocidos:"
    ls /dev/tty* 2>/dev/null | grep -E "(ACM|USB)"
    exit 1
fi

echo "Puertos encontrados:"
echo "$PORTS"
echo ""

# Usar el primer puerto encontrado
SELECTED_PORT=$(echo "$PORTS" | head -n1)
echo "Usando puerto: $SELECTED_PORT"
echo ""
echo "Iniciando receptor..."
echo ""

# Ejecutar el receptor
./esp32_receiver "$SELECTED_PORT"
