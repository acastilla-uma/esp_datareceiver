# ESP32 Data Receiver - Linux

Programa en C++ que recibe datos continuamente desde la placa ESP32 (cabina_v2_4) por puerto USB-C y los muestra en la terminal. No crea archivos CSV.

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
```

### Opción 2: Compilación manual

```bash
mkdir -p build
cd build
cmake ..
make
cd ..
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

**Autodetectar el puerto (recomendado):**

```bash
./run_auto.sh
```

**Usar puerto personalizado:**

```bash
./build/esp32_receiver /dev/ttyUSB0
```

El receptor busca primero `/dev/serial/by-id/*` (nombre estable) y después
`/dev/ttyACM*` y `/dev/ttyUSB*`. Si pasan cinco segundos sin datos, muestra una
advertencia sin cerrar la captura.

## Comprobación física y del firmware

En esta máquina la placa aparece como un conversor CH341 (`1a86:7523`) en
`/dev/ttyUSB0`. Que aparezca el puerto confirma el enlace USB, pero no confirma
que el firmware esté enviando datos.

1. Cierra Arduino IDE, PlatformIO, `screen` y cualquier otro monitor serial.
2. Ejecuta `./run_auto.sh` y pulsa una vez el botón **RESET/EN** de la placa.
3. Si no aparecen líneas, carga temporalmente este firmware mínimo usando la
   misma interfaz USB:

   ```cpp
   void setup() {
     Serial.begin(115200);
     delay(1000);
     Serial.println("PRUEBA_SERIAL_OK");
   }

   void loop() {
     Serial.println("1.0;2.0;3.0;4.0;5.0;6.0;0.98");
     delay(500);
   }
   ```

4. Vuelve a ejecutar `./run_auto.sh`. Debe mostrar `PRUEBA_SERIAL_OK` y una
   muestra cada medio segundo.
5. Si la prueba mínima funciona, revisa en el firmware real que se ejecute
   `Serial.begin(115200)`, que las muestras terminen con `Serial.println(...)`
   (salto de línea) y que ninguna espera de Wi-Fi, sensor o calibración bloquee
   el `loop()` antes del envío.
6. Si la prueba mínima tampoco funciona, prueba otro cable USB-C **de datos**,
   otro puerto USB y confirma que el firmware se cargó en la misma placa/puerto.

Para comprobar permisos en una terminal normal:

```bash
id
ls -l /dev/ttyUSB0
```

El usuario debe tener activo el grupo `dialout`. Si `dialout` figura en
`/etc/group` pero no en la salida de `id`, cierra sesión y vuelve a entrar.

### Ejemplo completo

```bash
# 1. Compilar
./build.sh

# 2. Conectar la placa ESP32 por USB-C

# 3. Ejecutar con autodetección
./run_auto.sh

# Output esperado:
# === Receptor de estabilidad ESP32 ===
# Puerto: /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
# Configuración: 115200 baud, 8N1
# Ctrl+C para detener
# =====================================
# Mostrando datos en pantalla; no se crearán archivos CSV.
# Esperando datos...
# [1] 1.23; 4.56; -0.12; 0.45; ...
```

## Archivos de salida

El programa no crea archivos ni guarda datos al detenerse. Cada línea serial
completa se muestra en pantalla y cualquier línea parcial se descarta al cerrar.

El firmware esperado envía líneas de texto con este formato:

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
- ✓ Muestra datos en tiempo real sin guardar archivos CSV
- ✓ Muestra cada línea recibida en tiempo real
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
sudo ./build/esp32_receiver /dev/ttyUSB0
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
