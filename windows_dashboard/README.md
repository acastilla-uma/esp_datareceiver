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

Abre `http://127.0.0.1:8080`.

Por defecto el panel aprende automáticamente la IP de la Jetson a partir del
primer datagrama. También puedes fijarla o cambiar los puertos:

```powershell
python .\server.py --http-port 8080 --udp-port 50100 --jetson-ip 192.168.8.174 --command-port 50101
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
