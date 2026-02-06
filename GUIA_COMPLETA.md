# Guía Completa: Envío de datos del ESP32 por USB-C

## Resumen

Has modificado tu placa ESP32 para que envíe constantemente los datos de sensores por el puerto USB-C. Ahora vas a crear un programa en Linux que reciba esos datos y los guarde automáticamente en archivos CSV.

**Arquitectura:**
```
[Placa ESP32] 
    ↓ USB-C (Serial 115200 baud)
[Programa C++ en Linux] 
    ↓ Lee datos 
[Archivo CSV]
```

## PASO 1: Modificaciones al Arduino ✓

Ya completadas. Se añadió una línea al código:

```cpp
Serial.print(texto);  // Enviar datos por USB-C
```

Esta línea envía los mismos datos que se guardan en la SD card al puerto serial (USB-C).

## PASO 2: Compilar el programa receptor en Linux

En tu máquina Linux donde vas a recibir los datos:

### Opción A - Compilación rápida con Make (recomendado)

```bash
cd esp32_data_receiver
make clean
make
```

### Opción B - Compilación con CMake

```bash
cd esp32_data_receiver
bash build.sh
cd build
```

Si te pide instalar cmake:
```bash
sudo apt update
sudo apt install cmake
```

Si te pide instalar g++:
```bash
sudo apt install build-essential
```

## PASO 3: Encontrar el puerto serial de la placa

Cuando conectes la placa por USB-C a Linux, aparecerá un puerto serial. Puedes verlo con:

```bash
# Ver todos los puertos
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null

# O ver en tiempo real (ejecuta antes de conectar, luego conecta)
dmesg -w
```

Típicamente será: `/dev/ttyACM0` o `/dev/ttyUSB0`

### Resolver problema de permisos

Si sale error "Permiso denegado":

```bash
# Opción recomendada: Añadir tu usuario al grupo dialout
sudo usermod -a -G dialout $USER

# Luego cierra y abre de nuevo la sesión
exit
# (vuelves a abrir terminal)
```

## PASO 4: Ejecutar el receptor

En el directorio `esp32_data_receiver`:

### Opción A - Ejecución automática (busca puerto automáticamente)

```bash
bash run_auto.sh
```

### Opción B - Especificar puerto manualmente

```bash
./esp32_receiver /dev/ttyACM0
```

### Opción C - Con Make

```bash
make run
```

## PASO 5: Verificar que funciona

Cuando ejecutes el programa deberías ver:

```
=== ESP32 Data Receiver - Linux ===
Puerto serial: /dev/ttyACM0
Velocidad: 115200 baud
Presiona Ctrl+C para detener
================================

Archivo creado: datos_20260206_143022.csv

Recibiendo datos...

Líneas recibidas: 10 - Última: 1.23; 4.56; -0.12; 0.45; ...
```

El archivo CSV se está guardando con timestamp automático.

## PASO 6: Detener grabación

Presiona **Ctrl+C** en la terminal. Verás:

```
^C
Iniciando cierre...

=== Cierre de sesión ===
Total de líneas recibidas: 125
Archivo guardado: datos_20260206_143022.csv
Puerto serial cerrado
```

## PASO 7: Ver los datos grabados

```bash
# Ver primeras líneas
head datos_20260206_143022.csv

# Ver últimas líneas
tail datos_20260206_143022.csv

# Contar líneas
wc -l datos_20260206_143022.csv

# Abrir en editor de texto
gedit datos_20260206_143022.csv

# O importar en LibreOffice Calc o Python Pandas
```

## Estructura del archivo CSV

**Primera línea (encabezado):**
```
ax; ay; az; gx; gy; gz; roll; pitch; yaw; timeantwifi; usciclo1; usciclo2; usciclo3; usciclo4; usciclo5; si; accmag; microsds; k3
```

**Líneas de datos (ejemplo):**
```
 1.23;  4.56; -0.12;  0.45;  0.23; -0.56;  2.34; -1.23;  0.00;123456;   0.12;   0.34;   0.45;   0.56;   0.67;   0.89;  4.56;  1234;  1.15
 2.34;  5.67; -0.23;  0.56;  0.34; -0.67;  2.45; -1.34;  0.11;234567;   0.23;   0.45;   0.56;   0.67;   0.78;   1.00;  5.67;  2345;  1.15
```

### Significado de columnas:
- **ax, ay, az**: Aceleración (m/s²)
- **gx, gy, gz**: Velocidad angular (°/s)
- **roll, pitch, yaw**: Ángulos de orientación (°)
- **timeantwifi**: Tiempo desde último dato WiFi (µs)
- **usciclo1-5**: Tiempos de ciclo (µs)
- **si**: Índice de estabilidad
- **accmag**: Magnitud de aceleración
- **microsds**: Tiempo de ciclo SD (µs)
- **k3**: Factor de calibración

## Usar datos en Python

```python
import pandas as pd

# Leer archivo
df = pd.read_csv('datos_20260206_143022.csv', sep=';', skipinitialspace=True)

# Ver primeras filas
print(df.head())

# Estadísticas
print(df.describe())

# Crear gráficos
import matplotlib.pyplot as plt

plt.figure(figsize=(12, 6))
plt.plot(df['roll'], label='Roll')
plt.plot(df['pitch'], label='Pitch')
plt.plot(df['yaw'], label='Yaw')
plt.legend()
plt.xlabel('Muestra')
plt.ylabel('Ángulo (°)')
plt.title('Orientación de la placa')
plt.grid()
plt.show()
```

## Troubleshooting

### Error: "Error abriendo puerto /dev/ttyACM0"

```bash
# Verifica que el puerto existe
ls /dev/ttyACM* /dev/ttyUSB*

# A veces es /dev/ttyUSB0 en lugar de /dev/ttyACM0
./esp32_receiver /dev/ttyUSB0
```

### Error: "Permiso denegado"

```bash
# Opción 1 (recomendado)
sudo usermod -a -G dialout $USER
exit
# (reabre terminal)

# Opción 2 (temporal)
sudo ./esp32_receiver /dev/ttyACM0
```

### No recibe datos

1. Verifica que el Arduino está cargado correctamente
2. Verifica que la línea `Serial.print(texto);` está en el código Arduino
3. Compila de nuevo el Arduino
4. Reinicia la placa (botón de reset)
5. Reconecta el USB

### Datos basura o caracteres extraños

- La velocidad serial debe ser 115200
- Verifica en Arduino: `Serial.begin(115200);`
- Verifica conexión USB-C (prueba con otro cable)
- Reinicia el Arduino con el botón reset

## Pasos rápidos de referencia

```bash
# Compilar (una sola vez)
cd esp32_data_receiver
make clean && make

# Ejecutar (cada vez que grabes)
bash run_auto.sh

# O manualmente
./esp32_receiver /dev/ttyACM0

# Ver datos después de detener (Ctrl+C)
head datos_*.csv
```

## Próximos pasos

Una vez que tengas los datos grabados en CSV, puedes:

1. **Analizar con Python**: `pandas`, `matplotlib`, `scipy`
2. **Visualizar**: Excel, LibreOffice Calc, Grafana
3. **Base de datos**: InfluxDB, SQL
4. **Streaming**: Enviar directamente a servidor por Ethernet
5. **Sincronización**: Combinar con datos de otras sensores

## Ayuda y preguntas

Si necesitas modificar el programa:

- **Cambiar puerto por defecto**: Edita `main.cpp` línea con `/dev/ttyACM0`
- **Cambiar velocidad**: Edita `main.cpp` línea con `B115200`
- **Cambiar formato**: Edita `main.cpp` sección de escritura de CSV
- **Cambiar nombre archivo**: Edita `generateFileName()` en `main.cpp`

---

**Estado:** ✓ Completado
- ✓ Arduino modificado para enviar por USB-C
- ✓ Programa C++ creado
- ✓ Compilable en Linux
- ✓ Lista para recibir datos
