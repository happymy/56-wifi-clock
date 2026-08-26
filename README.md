# 56-wifi-clock

复刻「56」WiFi 时钟的 51 单片机固件。硬件：STC15W408AS（1T 8051）+ TM1639 数码管驱动 + DS1302 实时时钟 + ESP-01S(ESP8266) WiFi。逐步复刻原版功能，并最终实现更人性化的按键交互。

## 硬件概览
- **MCU**：STC15W408AS — 1T 8051，内部 IRC ~35MHz，8KB Flash，512B RAM，5KB 片内 EEPROM（512B/扇区，起始 0x2000），10-bit ADC(P1.0–P1.7)，3 定时器，INT0/INT1(P3.2/P3.3)。
- **显示**：TM1639 三线驱动 8 位数码管（共阴）：
  - 主行 4 大管 LED2–5 → GRID1–GRID4（`FJ12101AH` 1.2"），显示时:分 HH:MM
  - 下排 `SMG1` → GRID5/6（`5612B` 双位），轮显日期 MMDD / 温度 °C
  - 上排 `SMG2` → GRID7/8（`5612B` 双位），显示秒 SS
  - 段序 SEG1=a … SEG8=dp（字节序 dp,g,f,e,d,c,b,a）
- **时钟**：DS1302，CE=P1.3 / DSDA=P1.4 / DSCL=P1.5，CR1220 备电走时。
- **WiFi**：ESP-01S(ESP8266)，UART(P3.0/P3.1)，AP 热点 `56dz network clock` / `56dz.com`，Web 配网 192.168.4.1；ESP 侧自写固件，与 51 用自定义串口协议通信。
- **按键**：UP(P3.2/INT0)、SET(P3.3/INT1)；两键同按 ≥5s 清除 WiFi 凭据并软复位进 AP 配网。
- **其它**：BEEP(P2.1) 蜂鸣器；GM(P1.0)/RM(P1.1) 光敏/热敏采样；LED_T(P1.2 红)；LED_W 在 ESP 侧（LED6 接 8266，勿与 8266 板载 LED 混淆）。
- 引脚明细与器件连接见 `plan/原理图.md`（权威）。

## 目录结构
```
firmware/        活动固件工程（SDCC），开发在此进行
  src/stc15.h        寄存器定义（__sfr __at；SDCC/Keil 通用宏）
  src/tm1639.h/.c    TM1639 三线驱动
  src/main.c         主程序（当前：点亮测试，8 位显示 1-8）
  build.bat          SDCC 编译 → firmware/out/firmware.hex
  .gitignore         忽略 out/ 编译产物
doc/                 数据手册（STC15W408AS.pdf、TM1639.pdf、STC15W408AS-硬件选项.md）+ 原版功能分析（原版时钟功能.md）
plan/                原理图.md（引脚映射，权威）、固件复刻计划.md（复刻路线）
```

## 构建
> 需 SDCC 4.5.0（本机已装在 `C:\Program Files\SDCC\bin`）；`firmware/build.bat` 会自动定位该路径，找不到时回退到 PATH。

```bat
cd firmware
build.bat            :: 生成 firmware/out/firmware.hex
```

## 烧录（冷启动 ISP）
STC 芯片靠串口下载，流程是“先点下载、再上电”：
1. USB-TTL 模块 TX→MCU RX(P3.0)、RX→MCU TX(P3.1)、GND 共地，MCU 接好电但先不上电；
2. 用 **STC-ISP**（官方 Windows 工具，选 STC15W408AS，IRC 频率按板子实际）打开 `firmware/out/firmware.hex`，点“下载/编程”；
3. 给 MCU 上电（或按复位）即完成烧录。
   - 命令行可选 `stcgal`：`python -m stcgal -P stc15 -p COMx firmware/out/firmware.hex`

## 当前进度
- ✅ **TM1639 驱动 + 点亮测试**：8 位显示 `1 2 3 4 5 6 7 8`，已用 SDCC 4.5.0 编译通过（生成 `firmware/out/firmware.hex`）。
- ⬜ DS1302 实时时钟接入 → 显示真实时分秒
- ⬜ 按键状态机（SET 单击/双击/长按、UP 增减）
- ⬜ 传感器（光敏自动亮度 / 热敏温度 + 补偿）
- ⬜ EEPROM 设置持久化（IAP，5K/512B/0x2000）
- ⬜ ESP8266 串口配网 / NTP 校时
- ⬜ 蜂鸣器 / 状态指示灯
- ⬜ 按键交互人性化重构

详细复刻路线见 `plan/固件复刻计划.md`。

## 说明
- 端口以 `plan/原理图.md` 为准；实测 LED / DS1302 不响应时优先复核 SOP28 pin5/pin6。
- SFR 用 `firmware/src/stc15.h` 自定义（SDCC：`__sfr __at(addr)`；Keil：`sfr addr = addr`），STC15 扩展 SFR 按模块追加并核对数据手册地址，避免凭记忆误写。
