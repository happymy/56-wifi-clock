@echo off
cd /d "%~dp0"
set PORT=%1
if "%PORT%"=="" set PORT=COM3
if not exist ".pio\build\esp01_1m\firmware.bin" (
    echo firmware.bin not found. Run build.bat first.
    exit /b 1
)
echo === Flashing 8266 to %PORT% (esptool.py, verified) ===
set CORE_DIR=%PLATFORMIO_CORE_DIR%
if "%CORE_DIR%"=="" set "CORE_DIR=%USERPROFILE%\.platformio"
set "ESPTOOL=%CORE_DIR%\packages\tool-esptoolpy\esptool.py"
if not exist "%ESPTOOL%" (
    echo esptool.py not found at %ESPTOOL%
    echo Install PlatformIO or set PLATFORMIO_CORE_DIR then retry.
    exit /b 1
)
python "%ESPTOOL%" --port %PORT% --baud 921600 --before default_reset write_flash 0x0 ".pio\build\esp01_1m\firmware.bin"
if errorlevel 1 (
    echo FLASH FAILED
    exit /b 1
)
echo FLASH OK