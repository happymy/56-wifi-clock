@echo off
rem STC15W408AS build & flash script (Windows). Assumes SDCC + stcgal installed.
set SDCC=C:\Program Files\SDCC\bin
set PATH=%SDCC%;%PATH%

if "%1"=="flash" goto flash
if "%1"=="clean" goto clean

echo [build] sdcc src\main.c
sdcc -mmcs51 --model-small -I. src\main.c
if errorlevel 1 exit /b 1
echo [build] packihx -^> firmware.hex
packihx main.ihx > firmware.hex
echo [build] done: firmware.hex
goto :eof

:flash
if "%2"=="" (
  echo usage: build.bat flash COMx
  exit /b 1
)
echo [flash] stcgal -P stc15 -p %2 firmware.hex
python -m stcgal -P stc15 -p %2 firmware.hex
goto :eof

:clean
del /q main.asm main.lst main.map main.mem main.ihx main.rel main.sym firmware.hex 2>nul
echo [clean] done
