# ESP32 Data Receiver - Linux

Programa en C++ que recibe datos continuamente desde la placa ESP32 (cabina_v2_4) por puerto USB-C y los guarda en archivos CSV.

## Requisitos

- Linux (Ubuntu, Debian, etc.)
- GCC/G++ con C++17
- CMake 3.10+
- Puerto USB para conectar la placa ESP32

## Dependencias

Ninguna dependencia adicional requerida. Solo librerías estándar de C++.

## Compilación

### Opción 1: Usando el script de compilación

```bash
chmod +x build.sh
./build.sh
cd build
```

### Opción 2: Compilación manual

```bash
mkdir -p build
cd build
cmake ..
make
```

## Uso

### Identificar el puerto serial

Es importante identificar correctamente el puerto donde está conectada la placa ESP32:

```bash
# Listar puertos COM en Linux
ls /dev/ttyACM*
ls /dev/ttyUSB*

# O usar dmesg para ver los cambios cuando conectes
dmesg | tail -20
```

La placa ESP32 generalmente aparecerá como `/dev/ttyACM0` o `/dev/ttyUSB0`.

### Ejecutar el programa

**Usar puerto por defecto (/dev/ttyACM0):**

```bash
./esp32_receiver
```

**Usar puerto personalizado:**

```bash
./esp32_receiver /dev/ttyUSB0
```

### Ejemplo completo

```bash
# 1. Compilar
./build.sh

# 2. Cambiar a directorio de construcción
cd build

# 3. Conectar la placa ESP32 por USB-C

# 4. Ejecutar
./esp32_receiver /dev/ttyACM0

# Output esperado:
# === ESP32 Data Receiver - Linux ===
# Puerto serial: /dev/ttyACM0
# Velocidad: 115200 baud
# Presiona Ctrl+C para detener
# ================================
# 
# Archivo creado: datos_20260206_143022.csv
# 
# Recibiendo datos...
# 
# Líneas recibidas: 10 - Última: 1.23; 4.56; -0.12; 0.45; ...
```

## Archivos de salida

El programa crea archivos CSV con nombre: `datos_YYYYMMDD_HHMMSS.csv`

Cada archivo contiene:

- **Primera línea (encabezado):** Nombres de las columnas del ESP32
- **Demás líneas:** Datos en formato semicolon-separated (`;`)

Ejemplo de contenido:

```
ax; ay; az; gx; gy; gz; roll; pitch; yaw; timeantwifi; usciclo1; usciclo2; usciclo3; usciclo4; usciclo5; si; accmag; microsds; k3
1.23; 4.56; -0.12; 0.45; 0.23; -0.56; 2.34; -1.23; 0.00; 123456; 0.12; 0.34; 0.45; 0.56; 0.67; 0.89; 4.56; 1234; 1.15
2.34; 5.67; -0.23; 0.56; 0.34; -0.67; 2.45; -1.34; 0.11; 234567; 0.23; 0.45; 0.56; 0.67; 0.78; 1.00; 5.67; 2345; 1.15
```

## Características

- ✓ Conexión serial a 115200 baud (configurable en código)
- ✓ Crea archivos CSV automáticamente con timestamp
- ✓ Muestra progreso cada 10 líneas recibidas
- ✓ Limpieza correcta al presionar Ctrl+C
- ✓ Manejo de señales (SIGINT, SIGTERM)
- ✓ Sin formatos de salida redundantes (elimina \r)

## Solución de problemas

### "Error abriendo puerto /dev/ttyACM0: Permiso denegado"

**Solución:**
```bash
# Opción 1: Añadir usuario al grupo dialout
sudo usermod -a -G dialout $USER
# Reiniciar sesión o ejecutar: newgrp dialout

# Opción 2: Usar sudo (no recomendado)
sudo ./esp32_receiver
```

### No se detecta el puerto

**Verificar:**
- La placa está conectada por USB-C
- Drivers de USB están instalados
- Otro programa no está usando el puerto (Arduino IDE, etc.)

```bash
# Ver todos los puertos
ls -la /dev/tty*

# Ver solo USB/ACM
ls -la /dev/ttyACM* /dev/ttyUSB*
```

### Datos incompletos o basura

Si recibe caracteres extraños:
- Verificar que la velocidad serial en Arduino es 115200
- Verificar conexión USB-C
- Reiniciar placa (botón de reset)

## Configuración del ESP32

Asegúrate de que el código Arduino (cabina_v2_4.ino) tenga configurado:

```cpp
Serial.begin(115200);  // ✓ Confirmado
Serial.print(texto);   // ✓ Confirmado en modificación
```

## Notas de desarrollo

- El programa usa librerías estándar POSIX (termios, fcntl)
- Compatible solo con Linux
- Para Windows usar Visual Studio + libserialport
- Para macOS adaptar rutas de puerto (/dev/cu.* en lugar de /dev/tty*)

## Licencia

MIT

## Autor

Programa para recepción de datos de proyecto Jetson-DOBACK cabina_v2_4
