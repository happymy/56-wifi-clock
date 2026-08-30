@echo off
cd /d "%~dp0"
echo === Building 8266 firmware (esp01_1m) ===
python -m platformio run
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD OK: .pio\build\esp01_1m\firmware.bin