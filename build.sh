#!/bin/bash

# Script de compilación para el receptor de datos ESP32

echo "=== Compilando receptor de datos ESP32 ==="
echo ""

# Crear directorio de construcción
if [ ! -d "build" ]; then
    mkdir build
    echo "Directorio 'build' creado"
fi

cd build

# Generar archivos de compilación
echo "Generando archivos CMake..."
cmake ..

# Compilar
echo "Compilando..."
make

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ Compilación exitosa"
    echo "Ejecutable: ./esp32_receiver"
    echo ""
    echo "Uso: ./esp32_receiver [puerto_serial]"
    echo "Ej:  ./esp32_receiver /dev/ttyACM0"
else
    echo "✗ Error en compilación"
    exit 1
fi
