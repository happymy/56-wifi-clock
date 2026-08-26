@echo off
chcp 936 >nul
cd /d %~dp0
setlocal
REM Build STC15W408AS demo (ds1302-clock) with SDCC
REM shared drivers live in ../_common
set SDCC_BIN=C:\Program Files\SDCC\bin
if exist "%SDCC_BIN%\sdcc.exe" set "PATH=%SDCC_BIN%;%PATH%"
if not exist out mkdir out
sdcc -mmcs51 -I../_common -c -o out\tm1639.rel ../_common\tm1639.c
sdcc -mmcs51 -I../_common -c -o out\ds1302.rel ../_common\ds1302.c
sdcc -mmcs51 -I../_common -c -o out\main.rel src\main.c
sdcc -mmcs51 --code-size 8192 --iram-size 512 -o out\firmware.ihx out\tm1639.rel out\ds1302.rel out\main.rel
packihx out\firmware.ihx > out\firmware.hex
echo Build done: out\firmware.hex
endlocal
