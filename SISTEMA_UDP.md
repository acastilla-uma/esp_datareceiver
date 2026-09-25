# Sistema DOBACK por UDP

El sistema tiene dos procesos:

1. La Jetson lee DOBACK y GPS, calibra la orientación, calcula los parámetros
   físicos y envía cada muestra por UDP.
2. El PC Windows recibe la telemetría, sirve el panel HTML y guarda las
   mediciones en CSV.

## Puertos

- Jetson → PC: UDP `50100` (telemetría JSON).
- PC → Jetson: UDP `50101` (calibración y configuración).
- Solo en el PC: TCP `8080` para `http://127.0.0.1:8080`.

Los comandos de control de la Jetson solo se aceptan desde la IP configurada
como PC, `192.168.8.20` por defecto.

## Arranque

En Windows, permite a Python usar redes privadas cuando lo solicite el firewall
y ejecuta `windows_dashboard\start_dashboard.bat`. También puedes usar:

```powershell
cd C:\ruta\esp_datareceiver\windows_dashboard
python .\server.py
```

En la Jetson:

```bash
cd /home/agilex/Documents/PhDAlex/esp_datareceiver/linux_receiver
./run_auto.sh --udp-host 192.168.8.20
```

Después abre `http://127.0.0.1:8080` en Windows. La IP de la Jetson se aprende
automáticamente al recibir la primera muestra. Si se desea fijar manualmente:

```powershell
python .\server.py --jetson-ip 192.168.8.X
```

## Controles

- **Empezar** inicia el registro con el nombre indicado.
- **Guardar** detiene el registro y crea un CSV en `measurements\`.
- **Calibrar** toma el último roll, pitch y yaw como cero en la Jetson. La
  telemetría conserva simultáneamente los valores brutos.
- **Enviar configuración** valida los cuatro parámetros y calcula:

```text
D1       = sqrt(Hg² + (S/2)²)
Ixx      = M·D1² + Inercia
FIc      = atan(S/(2·Hg))·180/pi
Coeff_SI = 2·M·9.81/Ixx
Alfa     = 90 - FIc
```

`Coeff_SI` se mantiene separado del campo `si` recibido del firmware: no son el
mismo dato y el sistema no sustituye uno por el otro.

