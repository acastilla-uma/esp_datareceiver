# DOBACK UDP Dashboard

Panel local para Windows que recibe telemetría JSON por UDP. Destaca en la
parte superior el índice de estabilidad (SI), los tres ejes del acelerómetro y
del giróscopo, y la orientación roll, pitch y yaw. Los campos secundarios se
muestran de forma compacta al final y las mediciones pueden guardarse en CSV.

## Protocolo

Telemetría esperada en UDP `50100`:

```json
{
  "type": "telemetry",
  "sequence": 1,
  "doback_timestamp_utc": "2026-09-25T10:00:00.000000Z",
  "measurement": {
    "ax_g": 0.01,
    "ay_g": -0.02,
    "az_g": 1.0,
    "gx_deg_s": 0.5,
    "gy_deg_s": -0.3,
    "gz_deg_s": 0.1,
    "si": 0.94
  },
  "orientation": {"roll_deg": 1.2, "pitch_deg": -0.4, "yaw_deg": 0.1},
  "gps": {"latitude": 40.0, "longitude": -3.0},
  "physics": null
}
```

Comandos enviados a la Jetson por UDP `50101`:

```json
{"type":"calibrate"}
```

```json
{
  "type": "config",
  "mass_kg": 50.0,
  "track_width_m": 0.47,
  "cg_height_m": 0.25,
  "roll_inertia_kg_m2": 0.0
}
```

El panel tolera campos ausentes. La calibración se ejecuta en la Jetson: allí
se guardan los offsets de roll, pitch y yaw, y se conservan también los valores
brutos en la telemetría para mantener trazabilidad.

### Unidades de sensores

Para evitar ambigüedad, los emisores nuevos deben usar `*_g` para aceleración y
`*_deg_s` para velocidad angular. El panel sigue aceptando la telemetría
histórica del ESP32: `ax`, `ay`, `az` se interpretan como **mg** y `gx`, `gy`,
`gz` como **mdps**; los convierte a `g` y `°/s` sólo para la visualización y
para el cálculo. Por ejemplo, `gy: -189.09` se muestra como `-0.189 °/s`.

## Ejecutar en Windows

Desde PowerShell:

```powershell
cd C:\ruta\a\esp_datareceiver\windows_dashboard
python .\server.py
```

También puedes ejecutar `start_dashboard.bat`. El lanzador muestra en la
consola las IPv4 detectadas del PC y las URLs completas que puedes abrir en la
tablet.

En el mismo ordenador puedes abrir `http://127.0.0.1:8080`. El servidor escucha
por defecto en todas las interfaces (`0.0.0.0`) para permitir el acceso desde
una tablet conectada a la Wi-Fi del AGV.

Para acceder desde la tablet:

1. Conecta la tablet y el ordenador a la Wi-Fi del AGV.
2. Ejecuta `ipconfig` y localiza la IPv4 del adaptador Wi-Fi del ordenador.
3. Abre `http://IP_DEL_ORDENADOR:8080` en la tablet, por ejemplo
   `http://192.168.8.100:8080`.
4. Crea una regla de Firewall para el puerto 8080. Como la Wi-Fi del AGV
   puede aparecer como red pública, abre PowerShell como administrador y ejecuta:

```powershell
New-NetFirewallRule -DisplayName "DOBACK UDP Dashboard 8080" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8080 -Profile Any
```

Por defecto el panel aprende automáticamente la IP de la Jetson a partir del
primer datagrama. También puedes fijarla o cambiar los puertos:

```powershell
python .\server.py --http-host 0.0.0.0 --http-port 8080 --udp-port 50100 --jetson-ip 192.168.8.174 --command-port 50101
```

Los CSV se guardan en `measurements/` por defecto. Puedes cambiarlo con
`--output-dir`.

## Fórmulas

Con `M = mass_kg`, `S = track_width_m`, `Hg = cg_height_m` e
`Inercia = roll_inertia_kg_m2`:

- `D1 = sqrt(Hg^2 + (S/2)^2)`
- `Ixx = M * D1^2 + Inercia`
- `FIc = atan(S / (2 * Hg)) * 180 / pi`
- `Coeff_SI = 2 * M * 9.81 / Ixx`
- `Alfa = 90 - FIc`

Además, el bloque **Ecuación de estabilidad lateral** muestra el mismo modelo
que el firmware, con sus dos penalizaciones por separado:

- `SI = 1 - E_estática - E_dinámica`
- `E_estática = k1 * φ / φcrit`, donde `φ = |atan(ax / az)|` y
  `φcrit = atan((S/2) / Hg)`.
- `E_dinámica = k2 * (ω / ωcrit)^2`, donde `ω = |Gy|` y
  `ωcrit = sqrt(Coeff_SI * S * αv / 4) * 360 / 6.28`.

`Gy` y `ωcrit` se presentan en `°/s` (el firmware original emplea mdps y
multiplica `ωcrit` por 1000). `αv = Alfa + margen αv`; el margen, `k1` y `k2`
se pueden modificar desde **Parámetros dinámicos** y se aplican al instante en
la ecuación. El comando UDP conserva sus cuatro parámetros de geometría y masa;
estos tres factores se mantienen en el panel para el cálculo visual.

## Tests

```powershell
python -m unittest discover -s tests
```
## Reproducción histórica de CSV

La pestaña **Reproducir CSV** permite cargar una medición desde el navegador y recorrerla muestra a muestra, hacia delante o hacia atrás, sin enviar el archivo al servidor. También incluye reproducción automática y un deslizador temporal.

El lector detecta automáticamente CSV separados por comas o por punto y coma. Las columnas con formato `seccion.campo` se asignan a las mismas secciones de telemetría que usa el modo en directo (`measurement`, `orientation`, `gps` y `physics`). Por ejemplo:

```text
received_utc,measurement.ax,measurement.ay,measurement.az,orientation.gy,orientation.roll
2026-09-29T11:17:54Z,0.01,0.03,1.01,42.5,-0.7
```

También se reconocen `doback_timestamp_utc` y `timestamp` como fecha de la muestra. El resto de la interfaz —acelerómetro, giroscopio en °/s, orientación, ecuación de estabilidad y términos estático/dinámico— se actualiza con la muestra seleccionada. El botón **En directo** devuelve el dashboard al flujo UDP normal.

Los CSV guardados con **Guardar** conservan las columnas originales y añaden dos grupos calculados por el mismo contrato que usa el JavaScript:

- `calculated.*`: sensores convertidos, `φ`, `φcrit`, `ωcrit`, penalizaciones y `si_js`.
- `js.*`: parámetros activos usados por la ecuación (`k1`, `k2`, geometría, coeficiente e `αv`).

Así se puede comparar el `measurement.si` recibido del firmware con `calculated.si_js`, que es el índice recalculado por el dashboard.
