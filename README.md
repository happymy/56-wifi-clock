# 56-wifi-clock

复刻「56」WiFi 时钟的 51 单片机固件。硬件：STC15W408AS（1T 8051）+ TM1639 数码管驱动 + DS1302 实时时钟 + ESP-01S(ESP8266) WiFi。逐步复刻原版功能，并最终实现更人性化的按键交互。

## 硬件概览
  - **MCU**：STC15W408AS — 1T 8051，内部 IRC 实测 11.051MHz，8KB Flash，512B RAM，5KB 片内 EEPROM（512B/扇区，起始 0x2000），10-bit ADC(P1.0–P1.7)，3 定时器，INT0/INT1(P3.2/P3.3)。
- **显示**：TM1639 三线驱动 8 位数码管（共阴）：
  - 主行 4 大管 LED2–5 → GRID1–GRID4（`FJ12101AH` 1.2"），显示时:分 HH:MM
  - 下排 `SMG1` → GRID5/6（`5612B` 双位），轮显日期 MMDD / 温度 °C
  - 上排 `SMG2` → GRID7/8（`5612B` 双位），显示秒 SS
  - 段序 SEG1=a … SEG8=dp（字节序 dp,g,f,e,d,c,b,a）
- **时钟**：DS1302，CE=P1.3 / DSDA=P1.4 / DSCL=P1.5，CR1220 备电走时。
- **WiFi**：ESP-01S(ESP8266)，UART(P3.0/P3.1)，AP 热点 `56dz network clock` / `56dz.com`，Web 配网 192.168.4.1；ESP 侧自写固件，与 51 用自定义串口协议通信。
  - **按键**：UP(P3.2/INT0)、SET(P3.3/INT1)。当前测试固件：单击 SET→光敏显示、单击 UP→热敏显示、两键同按≥2s→回滚动并响蜂鸣器；产品语义（同按≥5s 清 WiFi 凭据）见计划，尚未实现。
- **其它**：BEEP(P2.1) 蜂鸣器；GM(P1.0)/RM(P1.1) 光敏/热敏采样；LED_T(P1.2 红)；LED_W 在 ESP 侧（LED6 接 8266，勿与 8266 板载 LED 混淆）。
- 引脚明细与器件连接见 `plan/原理图.md`（权威）。

## 目录结构
```
demo/             测试/功能验证代码（每个 demo 独立目录）
  _common/        共享 STC 驱动（被各 demo 引用，不单独编译）
    stc15.h          寄存器定义（__sfr __at；SDCC/Keil 通用宏）
    tm1639.h/.c      TM1639 三线驱动
    ds1302.c/.h      DS1302 实时时钟驱动（未提交/未验证）
  clock-bringup/  当前 demo：滚动显示 + SET/UP 切换光敏/热敏显示 + 亮度自动调节 + 蜂鸣器
    src/main.c      主程序
    build.bat       SDCC 编译 → demo/clock-bringup/out/firmware.hex
firmware/         正式产品固件（按 MCU 分目录，当前为空占位）
  STC/   (.gitkeep)  未来 STC15 侧产品固件
  8266/  (.gitkeep)  未来 ESP8266 侧产品固件
doc/              数据手册 + 原版功能分析
plan/             原理图.md（引脚映射，权威）、固件复刻计划.md（复刻路线）
```

## 构建
> 需 SDCC 4.5.0（本机已装在 `C:\Program Files\SDCC\bin`）；`demo/clock-bringup/build.bat` 会自动定位该路径，找不到时回退到 PATH。

```bat
cd demo/clock-bringup
build.bat            :: 生成 demo/clock-bringup/out/firmware.hex
```

## 烧录（冷启动 ISP）
STC 芯片靠串口下载，流程是“先点下载、再上电”：
1. USB-TTL 模块 TX→MCU RX(P3.0)、RX→MCU TX(P3.1)、GND 共地，MCU 接好电但先不上电；
2. 用 **STC-ISP**（官方 Windows 工具，选 STC15W408AS，IRC 频率按板子实际）打开 `demo/clock-bringup/out/firmware.hex`，点“下载/编程”；
3. 给 MCU 上电（或按复位）即完成烧录。
   - 命令行可选 `stcgal`：`python -m stcgal -P stc15 -p COMx demo/clock-bringup/out/firmware.hex`

## 当前进度
- ✅ **TM1639 驱动 + 显示点亮**：滚动 `12345678` 自测 + 段拆分 + GRID3 倒装补偿，SDCC 4.5.0 编译通过。
- ✅ **光敏实时调亮度**：10 位 ADC(P1.0) 标定，遮住→0 档(1/16 最暗)、强光→7 档(14/16 最亮)。
- ✅ **蜂鸣器**：S9012 PNP active-low，启动 2 秒 BB（显示先出后响）、两键同按持续响。
- ✅ **按键（测试占用）**：20ms 快扫；单击 SET→光敏显示、单击 UP→热敏显示、同按≥2s→回滚动。
- ⬜ DS1302 实时时钟 → 显示真实时分秒（`ds1302.c/h` 已写，未提交/未验证）
- ⬜ 按键产品语义（SET 单/双/长按设置、UP 增减）
- ⬜ 传感器热敏温度 + 补偿、EEPROM 持久化（IAP 5K/0x2000）
- ⬜ ESP8266 串口配网 / NTP 校时
- ⬜ 状态指示灯 / 按键人性化重构

详细复刻路线见 `plan/固件复刻计划.md`。

## 说明
- 端口以 `plan/原理图.md` 为准；实测 LED / DS1302 不响应时优先复核 SOP28 pin5/pin6。
- SFR 用 `demo/_common/stc15.h` 自定义（SDCC：`__sfr __at(addr)`；Keil：`sfr addr = addr`），STC15 扩展 SFR 按模块追加并核对数据手册地址，避免凭记忆误写。
