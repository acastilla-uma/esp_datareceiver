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
    "ax": 0.01,
    "ay": -0.02,
    "az": 1.0,
    "gx": 0.5,
    "gy": -0.3,
    "gz": 0.1,
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

## Tests

```powershell
python -m unittest discover -s tests
```
