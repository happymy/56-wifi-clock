@echo off
chcp 936 >nul
setlocal
REM Build firmware for STC15W408AS with SDCC (https://sdcc.sourceforge.net)
REM SDCC 已装在 C:\Program Files\SDCC\bin；不存在时回退到 PATH。
set SDCC_BIN=C:\Program Files\SDCC\bin
if exist "%SDCC_BIN%\sdcc.exe" set "PATH=%SDCC_BIN%;%PATH%"
if not exist out mkdir out
sdcc -mmcs51 -c -o out\tm1639.rel src\tm1639.c
sdcc -mmcs51 -c -o out\main.rel src\main.c
sdcc -mmcs51 --code-size 8192 --iram-size 512 -o out\firmware.ihx out\tm1639.rel out\main.rel
packihx out\firmware.ihx > out\firmware.hex
echo Build done: out\firmware.hex
endlocal
