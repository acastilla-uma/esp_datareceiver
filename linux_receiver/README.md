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

El receptor inicia también `../GPS_CSG/gps_realtime.py`, añade a cada muestra
DOBACK su timestamp UTC de recepción y selecciona la coordenada GPS cuyo campo
`ts` sea el más cercano. La tolerancia predeterminada es de 10 segundos.

Además, envía cada muestra por UDP al PC `192.168.8.20:50100` y escucha los
comandos de calibración/configuración en `50101`. Consulta
`../SISTEMA_UDP.md` para arrancar el panel de Windows.

```bash
# Filtrar un dispositivo GPS y ajustar la tolerancia a 5 segundos
./build/esp32_receiver auto --gps-device GPSTEST001 --gps-max-delta-ms 5000

# Ejecutar temporalmente sin GPS
./build/esp32_receiver auto --no-gps
```

Si `GPS_CSG` está en otra ubicación, usa `--gps-script RUTA`. El panel muestra
`doback_timestamp_utc`, `gps_timestamp_utc`, la diferencia en milisegundos,
latitud, longitud, tipo de fix y número de satélites.

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

# Se abre un panel con el estado de conexión y la medición más reciente.
```

## Archivos de salida

El programa no crea archivos ni guarda datos al detenerse. El panel reemplaza
la medición anterior con la más reciente, identifica cada valor por su nombre e
ignora los mensajes de diagnóstico del arranque del ESP32.

El firmware nuevo envía líneas de texto con este formato:

- **Primera línea (encabezado):** Nombres de las columnas del ESP32
- **Demás líneas:** Datos en formato semicolon-separated (`;`)

Ejemplo de contenido:

```
timestamp_us; ax_g; ay_g; az_g; gx_deg_s; gy_deg_s; gz_deg_s; roll_deg; pitch_deg; yaw_deg; usciclo1_us; usciclo2_us; usciclo3_us; usciclo4_us; usciclo5_us; si; accmag_g; microsds_us
1234567; 0.01; -0.02; 1.00; 0.12; -0.05; 0.03; 1.20; -0.40; 0.10; 20000; 20001; 19999; 20000; 20002; 0.89; 1.00025; 850
```

También se conserva la detección automática del formato anterior de 19 columnas.
Si el receptor se conecta después de que el ESP32 haya enviado el encabezado,
identifica el esquema por el número de valores numéricos.

## Características

- ✓ Conexión serial a 115200 baud (configurable en código)
- ✓ Muestra datos en tiempo real sin guardar archivos CSV
- ✓ Panel de terminal con una única medición actual y nombres de campos
- ✓ Diseño adaptable a una, dos o tres columnas según el ancho de la terminal
- ✓ Filtra los mensajes de arranque que no son mediciones
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
