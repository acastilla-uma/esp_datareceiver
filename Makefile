# Makefile simple para compilar el receptor ESP32

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
SOURCES = main.cpp
EXECUTABLE = esp32_receiver

all: $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(EXECUTABLE) $(SOURCES)
	@echo "✓ Compilación completada: $(EXECUTABLE)"

clean:
	rm -f $(EXECUTABLE)
	@echo "✓ Archivos limpios"

run: $(EXECUTABLE)
	./$(EXECUTABLE) /dev/ttyACM0

run_auto: $(EXECUTABLE)
	bash run_auto.sh

.PHONY: all clean run run_auto

help:
	@echo "Hacer (make) objetivos disponibles:"
	@echo "  all        - Compilar el programa"
	@echo "  clean      - Limpiar archivos compilados"
	@echo "  run        - Compilar y ejecutar con puerto /dev/ttyACM0"
	@echo "  run_auto   - Compilar y ejecutar detectando puerto automáticamente"
	@echo "  help       - Mostrar esta ayuda"
