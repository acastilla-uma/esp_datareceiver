@echo off
cd /d "%~dp0"
echo.
echo ================================================
echo   DOBACK UDP Dashboard
echo ================================================
echo.
echo Conecta la tablet a la misma Wi-Fi del AGV.
echo Direcciones para abrir en la tablet:
powershell -NoProfile -ExecutionPolicy Bypass -Command "$adapters=@(Get-NetIPConfiguration -ErrorAction SilentlyContinue | Where-Object { $_.IPv4Address -and $_.IPv4DefaultGateway } | Sort-Object @{Expression={ if($_.InterfaceAlias -match 'Wi-Fi|Wireless'){0}else{1} }}); if($adapters.Count -eq 0){ Write-Host '  No se ha detectado una interfaz Wi-Fi con IPv4.' } else { $adapters | ForEach-Object { $ip=$_.IPv4Address.IPAddress; Write-Host ('  Interfaz: ' + $_.InterfaceAlias); Write-Host ('  IP del PC: ' + $ip); Write-Host ('  Dashboard: http://' + $ip + ':8080') } }"
echo.
echo En el navegador de la tablet usa una de las URLs anteriores.
echo.
python server.py --http-host 0.0.0.0 --http-port 8080
pause
