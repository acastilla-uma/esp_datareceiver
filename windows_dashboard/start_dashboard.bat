@echo off
cd /d "%~dp0"
python server.py --http-host 0.0.0.0 --http-port 8080
pause
