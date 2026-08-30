@echo off
cd /d "%~dp0"
set PORT=%1
if "%PORT%"=="" set PORT=COM3
if not exist ".pio\build\esp01_1m\firmware.bin" (
    echo firmware.bin not found. Run build.bat first.
    exit /b 1
)
echo === Flashing 8266 to %PORT% (esptool.py, verified) ===
python "C:\Users\GAME\.platformio\packages\tool-esptoolpy\esptool.py" --port %PORT% --baud 921600 --before default_reset write_flash 0x0 ".pio\build\esp01_1m\firmware.bin"
if errorlevel 1 (
    echo FLASH FAILED
    exit /b 1
)
echo FLASH OK