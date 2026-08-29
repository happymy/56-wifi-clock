@echo off
chcp 936 >nul
cd /d %~dp0
setlocal
REM Build STC15W408AS product firmware (51 side)
REM 产品本地驱动(optimized copy)在 src/；demo/_common 仍保留供 demo 单独验证
set SDCC_BIN=C:\Program Files\SDCC\bin
if exist "%SDCC_BIN%\sdcc.exe" set "PATH=%SDCC_BIN%;%PATH%"
if not exist out mkdir out
set FLAGS=-mmcs51 --opt-code-size -I../../demo/_common
sdcc %FLAGS% -c -o out\tm1639.rel ..\\..\\demo\_common\tm1639.c
sdcc %FLAGS% -c -o out\ds1302.rel ..\\..\\demo\_common\ds1302.c
sdcc %FLAGS% -c -o out\display.rel src\display.c
sdcc %FLAGS% -c -o out\config.rel src\config.c
sdcc %FLAGS% -c -o out\keys.rel src\keys.c
sdcc %FLAGS% -c -o out\main.rel src\main.c
sdcc %FLAGS% --code-size 8192 --iram-size 256 -o out\firmware.ihx out\tm1639.rel out\ds1302.rel out\display.rel out\config.rel out\keys.rel out\main.rel
packihx out\firmware.ihx > out\firmware.hex
echo Build done: out\firmware.hex
endlocal
