@echo off
REM Build firmware for STC15W408AS with SDCC (https://sdcc.sourceforge.net)
REM Run this from the firmware/ directory. SDCC must be in PATH.
if not exist out mkdir out
sdcc -mmcs51 -c -o out\tm1639.rel src\tm1639.c
sdcc -mmcs51 -c -o out\main.rel src\main.c
sdcc -mmcs51 --code-size 8192 --iram-size 512 -o out\firmware.ihx out\tm1639.rel out\main.rel
packihx out\firmware.ihx > out\firmware.hex
echo Build done: out\firmware.hex
