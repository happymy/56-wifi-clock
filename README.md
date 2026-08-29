# 56-wifi-clock

复刻「56」WiFi 时钟的 51 单片机固件。硬件：STC15W408AS（1T 8051）+ TM1639 数码管驱动 + DS1302 实时时钟 + ESP-01S(ESP8266) WiFi。逐步复刻原版功能，并最终实现更人性化的按键交互。

## 硬件概览
  - **MCU**：STC15W408AS — 1T 8051，内部 IRC 实测 11.051MHz，8KB Flash，512B RAM，5KB 片内 EEPROM（512B/扇区，起始 0x0000，地址空间 0x0000–0x13FF），10-bit ADC(P1.0–P1.7)，3 定时器，INT0/INT1(P3.2/P3.3)。
  - **CODE 容量已探测确认 = 8KB（8192B）**：5KB EEPROM 为**数据空间**，芯片不能从中取指执行，故无法用于缩减 CODE；仅作 M11 配置持久化。固件区上限 **8192B（8KB Flash）**，当前构建末端 **0x1FCB（8139B）**，余 53B（本会话多次重建验证），零错误零告警（SDCC `--opt-code-size`）。计时器（51 原生）已实装、倒计时显示接管移 8266（见 `plan/新版时钟功能.md` §四/§五/§十八 容量取舍）；亮度主用 TM1639 自带 8 档硬件占空比（自动/手动）；极暗档另由 T0 软件 PWM 把有效占空比压到 ≈1/1024（见 AGENTS.md §6）。
- **显示**：TM1639 三线驱动 8 位数码管（共阴）：
  - 主行 4 大管 LED2–5 → GRID1–GRID4（`FJ12101AH` 1.2"）：整屏三态，**手动 UP 切换**（单击循环 TIME→DATE→TEMP→TIME，双击恢复走时；无自动整屏轮显）——TIME `HH:MM`（冒号=GRID2/GRID3 小数点，GRID3 倒装）、DATE `MMDD`（月左/日右镜像）、TEMP `[符号][2位温度][C]`（NTC 无效兜底 25°C，恒°C）。
  - 下排 `SMG1` → GRID5/6（`5612B` 双位）：TIME 模式按 `cycle_flags`（8266 下发）子轮换 **温度 °C / 星期 1–7**（bit0=温度、bit1=日期；同置每 ~2s 轮换，单置固定，均未置默认温度）；DATE 显星期 1–7；TEMP 灭。**SMG1 凡显温度（TIME 且 cycle_flags 选温度）个位(GRID5)小数点恒亮**。
  - 上排 `SMG2` → GRID7/8（`5612B` 双位）：TIME 显秒 `SS`、DATE 显年份 `YY`、TEMP 灭。
  - 段序 SEG1=a … SEG8=dp（字节序 dp,g,f,e,d,c,b,a）
- **时钟**：DS1302，CE=P1.3 / DSDA=P1.4 / DSCL=P1.5，CR1220 备电走时。
- **WiFi**：ESP-01S(ESP8266)，UART(P3.0/P3.1)，AP 热点 `56dz network clock` / `56dz.com`，Web 配网 192.168.4.1；ESP 侧自写固件，与 51 用自定义串口协议通信。
  - **按键**：UP(P3.2/INT0)、SET(P3.3/INT1)。当前测试固件：单击 SET→光敏显示、单击 UP→热敏显示、两键同按≥2s→回滚动并响蜂鸣器；产品语义（同按≥5s 清 WiFi 凭据）见计划，尚未实现。
- **其它**：BEEP(P2.1) 蜂鸣器；GM(P1.0)/RM(P1.1) 光敏/热敏采样；LED_T(P1.2 红)；LED_WIFI = LED6（蓝色，由 ESP GPIO2/引脚2 驱动）在 ESP 侧，勿与 8266 板载 LED 混淆。
- 引脚明细与器件连接见 `plan/原理图.md`（权威）。

## 8KB ROM 容量下的精简清单

固件须塞进 8KB Flash（实测 8192B 上限）。下列为**因 ROM 容量做出的功能/代码简化**，功能验证在 `firmware/STC` 下完成：

| 精简项 | 原因 / 取舍 | 节省 |
| --- | --- | --- |
| 软件 PWM 极暗调光（T0 中断 1/1024 占空比） | STC15W408AS 无 Timer1，T0 已被占用；改为 TM1639 硬件 8 档占空比（0=1/16…7=满），自动/手动亮度用户可见功能保留，仅丢失低于 1/16 的软件极暗档 | ~700B |
| 配网 IP 显示：4 段滚动 + 除 10/100 | 同网段前三段固定，仅显 **`P` + IP 末段**（0–255）；去掉 4 段滚动与 `*26>>8` 除法 | ~115B |
| 独立计时器（长按 UP，M4） | 与倒计时（M5）功能重叠，长按 UP 计时器入口取消 | ~30B |
| `bcd2bin` / NTC 温度 `*10`·`*58`·`*29` | 改用移位（`hi*8+hi*2`、`(d<<5)-(d<<1)+d`、`off<<3+off<<1`）替代整数乘法，去除 `__mulint` 库 | ~29B |
| `tm1639_set_brightness`/`set_light` 冗余 STB | 每帧 `tm1639_write_display` 已写 `0x88\|g_bright`，二者仅更新 `g_bright` 变量，不再单独发 STB 事务 | ~17B |
| M11 EEPROM 驱动 8 位地址计数 + 直接读写 `cfg` | 配置固定存扇区 0（地址 <256），用 8 位计数；读/写直接落到 `cfg` 结构，免去 55B 中转缓冲 + memcpy | ~95B |

> 5KB EEPROM 是数据空间，不能执行代码，无法减小 CODE，仅用于 M11 配置持久化（扇区 0，不越界 0x13FF）。

## 目录结构
```
demo/             测试/功能验证代码（每个 demo 独立目录，均有自己的 build.bat）
  _common/        共享 STC 驱动（被各 demo 引用，不单独编译）
    stc15.h          寄存器定义（Catium2006 官方 SDCC 头）
    tm1639.h/.c      TM1639 三线驱动（段拆分 + GRID3 倒装补偿）
    ds1302.c/.h      DS1302 实时时钟驱动（突发读 + 单字节写，已烧录验证）
  clock-bringup/  显示点亮验证：滚动自测 + 光敏/热敏显示 + 亮度自动调节 + 蜂鸣器
    src/main.c
    build.bat
  ds1302-clock/    实时时钟：读取 DS1302 显示时分秒，传统按键手动设置日期/时间
    src/main.c
    build.bat
  uart-test/      硬件串口回环验证：UART1 自发自收，大屏左发右收、串口打印结果
    src/main.c
    build.bat
  hw-test/         产线全功能自检（HWTEST）：见 plan/硬件生产测试计划.md
    src/main.c
    build.bat
  firmware/         正式产品固件（按 MCU 分目录，当前为空占位）
    STC/   (.gitkeep)  未来 STC15 侧产品固件
    8266/  (.gitkeep)  未来 ESP8266 侧产品固件
  doc/              数据手册 + 原版功能分析
  plan/             原理图.md（引脚映射，权威）、固件复刻计划.md（复刻路线）
```

## 构建
> 需 SDCC（本机已装在 `C:\Program Files\SDCC\bin`）；各 demo 的 `build.bat` 会自动定位该路径，找不到时回退到 PATH。

```bat
cd demo/ds1302-clock
build.bat            :: 生成 demo/ds1302-clock/out/firmware.hex
```

> 其它 demo（`clock-bringup`、`uart-test`）同理：进入对应目录运行 `build.bat`。

## 烧录（冷启动 ISP）
STC 芯片靠串口下载，流程是“先点下载、再上电”：
1. USB-TTL 模块 TX→MCU RX(P3.0)、RX→MCU TX(P3.1)、GND 共地，MCU 接好电但先不上电；
2. 用 **STC-ISP**（官方 Windows 工具，选 STC15W408AS，IRC 频率按板子实际）打开 `demo/ds1302-clock/out/firmware.hex`，点“下载/编程”；
3. 给 MCU 上电（或按复位）即完成烧录。
   - 命令行可选 `stcgal`：`python -m stcgal -P stc15 -p COMx demo/ds1302-clock/out/firmware.hex`

## 当前进度
- ✅ **TM1639 驱动 + 显示点亮**（display-bringup / v0.3）：滚动 `12345678` 自测 + 段拆分 + GRID3 倒装补偿，SDCC 编译通过。
  - ✅ **光敏实时调亮度**：10 位 ADC(P1.0) 标定，遮住→0 档、强光→7 档；全暗环境下用硬件最低档 1/16 占空比（不另起软件 PWM 以保 8KB）。
- ✅ **蜂鸣器**：S9012 PNP active-low，启动 2 秒 BB（显示先出后响）、两键同按持续响。
- ✅ **按键（clock-bringup 测试）**：20ms 快扫；单击 SET→光敏显示、单击 UP→热敏显示、同按≥2s→回滚动。
- ✅ **DS1302 实时时钟 + 断电能走时**（v0.5）：突发读判定 CH 位、上电首读加延时与重试（修复断电清零）、单字节写清 CH 保证连续走时；`ds1302.c/h` 已提交并烧录验证。
- ✅ **传统手动设置**（v0.5）：SET 单击进入并循环切换字段（时→分→秒→日→月→年→星期），UP 在当前字段加值（独立回绕、闰年感知），末字段 SET 保存+响蜂鸣。
- ✅ **硬件 UART1 回环验证**（uart-loopback-v0.6 / v0.6）：STC15W408AS 无 Timer1，UART1 改用 Timer2 波特源（9600 8N1），自发自收比对，大屏左发/右收、SMG 显 0/E、串口打印 `TX=.. RX=.. OK/FAIL`。
- ⬜ 传感器热敏温度 + 补偿、EEPROM 持久化（IAP 5K/0x0000）
- ⬜ ESP8266 串口配网 / NTP 校时（UART 收发原语 `uart_send`/`uart_recv` 已就绪，可作为接 ESP 的基础）
- ⬜ 按键产品语义（SET 单/双/长按设置、三组闹钟）、状态指示灯人性化重构

## 版本里程碑
| Tag | 说明 |
| --- | --- |
| v0.1 | 原理图引脚修正 + 原版功能说明 + 51 固件复刻计划 |
| v0.2 | 硬件映射定稿，开始固件复刻（点亮数码管） |
| v0.3 / display-bringup | TM1639 显示驱动 + 显示顺序自测通过，数码管点亮 |
| v0.4 / restructure | 整理项目结构，测试代码归入 demo，固件按 MCU 分目录 |
| v0.5 / ds1302-v0.5-timekeeping | DS1302 走时 + 断电保持 + 传统手动设置完成 |
| v0.6 / uart-loopback-v0.6 | 硬件串口回环测试固件完成 |

详细复刻路线见 `plan/固件复刻计划.md`。

## 说明
- 端口以 `plan/原理图.md` 为准；实测 LED / DS1302 不响应时优先复核 SOP28 pin5/pin6。
- SFR 用 `demo/_common/stc15.h` 自定义（SDCC：`__sfr __at(addr)`；Keil：`sfr addr = addr`），STC15 扩展 SFR 按模块追加并核对数据手册地址，避免凭记忆误写。
