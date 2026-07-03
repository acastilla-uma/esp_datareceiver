.PHONY: all clean run run_auto help

all:
	./build.sh

clean:
	@if [ -d build ]; then cmake --build build --target clean; else echo "Nada que limpiar"; fi

run: all
	./run_auto.sh

run_auto: run

help:
	@echo "Objetivos disponibles:"
	@echo "  all/run    - Compilar con CMake y autodetectar el puerto"
	@echo "  clean      - Limpiar el artefacto de CMake"
