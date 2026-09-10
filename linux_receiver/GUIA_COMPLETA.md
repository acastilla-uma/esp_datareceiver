# Guía completa: monitor serial del ESP32 por USB-C

## Resumen

El receptor Linux lee las líneas que envía la placa ESP32 por USB-C y las
muestra en la terminal. No crea archivos CSV ni guarda datos al detenerse.

```text
[Placa ESP32]
      ↓ USB-C (Serial 115200 baud)
[Programa C++ en Linux]
      ↓
[Terminal]
```

## Compilar el receptor

Desde el directorio `linux_receiver`:

```bash
./build.sh
```

También puede compilarse manualmente:

```bash
mkdir -p build
cd build
cmake ..
make
```

En Ubuntu/Debian, si faltan herramientas de compilación:

```bash
sudo apt update
sudo apt install build-essential cmake
```

## Encontrar el puerto serial

Conecta la placa por USB-C y consulta los puertos disponibles:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Normalmente será `/dev/ttyACM0` o `/dev/ttyUSB0`. También se puede observar
el dispositivo al conectarlo con:

```bash
dmesg -w
```

Si aparece un error de permisos, añade el usuario al grupo `dialout` y vuelve a
iniciar sesión:

```bash
sudo usermod -a -G dialout $USER
```

## Ejecutar el receptor

Autodetección de puerto:

```bash
./run_auto.sh
```

Puerto indicado manualmente:

```bash
./build/esp32_receiver /dev/ttyUSB0
```

Salida esperada:

```text
ESP32 · MONITOR DE ESTABILIDAD
Estado: ● RECIBIENDO DATOS
Puerto: /dev/ttyUSB0 · 115200 baud
Muestra: 2 · Actualizada: 12:34:56

MEDICIÓN ACTUAL
Aceleración X [ax]  1.23
Aceleración Y [ay]  4.56
...
```

El panel se actualiza en el mismo lugar: solo permanece visible la medición más
reciente. El número de columnas se adapta al ancho de la terminal.

## Detener el monitor

Pulsa `Ctrl+C`. El receptor cierra el puerto serial y muestra el total de
muestras recibidas. No guarda un archivo al cerrar ni durante la ejecución.

```text
Sesión finalizada. Muestras recibidas: 125
```

## Comprobación del firmware

El firmware debe iniciar el puerto a la misma velocidad y enviar una línea
terminada en salto de línea:

```cpp
Serial.begin(115200);
Serial.println("1.0;2.0;3.0");
```

Si no aparece ninguna línea, cierra Arduino IDE, PlatformIO, `screen` u otro
monitor que pueda estar ocupando el puerto. Después pulsa RESET/EN en la placa.

## Problemas habituales

### No se detecta el puerto

- Comprueba que el cable USB-C transmite datos.
- Prueba otro puerto USB.
- Ejecuta `ls -la /dev/ttyACM* /dev/ttyUSB*` después de conectar la placa.

### Permiso denegado

Comprueba que `dialout` aparece en la salida de `id`. Si acabas de añadir el
grupo, cierra la sesión y vuelve a entrar.

### Datos ilegibles o incompletos

- Confirma `Serial.begin(115200)` en el firmware.
- Reinicia la placa.
- Verifica que cada muestra se envía con `Serial.println(...)`.

## Parámetros que se pueden ajustar

- Puerto: argumento de `esp32_receiver`, por ejemplo `/dev/ttyUSB0`.
- Velocidad serial: `B115200` en `main.cpp`; debe coincidir con el firmware.
- Límite de línea: el receptor descarta líneas de más de 64 KiB para evitar
  agotar memoria.
