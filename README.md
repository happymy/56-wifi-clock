# 56-wifi-clock

复刻「56」WiFi 时钟的 51 单片机固件。硬件：STC15W408AS（1T 8051）+ TM1639 数码管驱动 + DS1302 实时时钟 + ESP-01S(ESP8266) WiFi。逐步复刻原版功能，并最终实现更人性化的按键交互。

## 硬件概览
  - **MCU**：STC15W408AS — 1T 8051，内部 IRC 实测 11.051MHz，8KB Flash，512B RAM，5KB 片内 EEPROM（512B/扇区，起始 0x0000，地址空间 0x0000–0x13FF），10-bit ADC(P1.0–P1.7)，3 定时器，INT0/INT1(P3.2/P3.3)。
  - **CODE 容量已探测确认 = 8KB（8192B）**：5KB EEPROM 为**数据空间**，芯片不能从中取指执行，故无法用于缩减 CODE；仅作 M11 配置持久化。固件区上限 **8192B（8KB Flash）**，当前构建末端 **0x1FF6（8182B）**，余 10B（用户环境约 +4B → 8186 仍过 8192，零错误零告警，SDCC `--opt-code-size`）。计时器（51 原生）已实装、倒计时显示接管移 8266（见 `plan/新版时钟功能.md` §四/§五/§十八 容量取舍）；亮度主用 TM1639 自带 8 档硬件占空比（自动/手动）；极暗软件 PWM（≈1/1024）仅在 `demo/clock-bringup` 验证，**产品固件未移植**（见 `plan/新版时钟功能.md` §十九）。
- **显示**：TM1639 三线驱动 8 位数码管（共阴）：
  - 主行 4 大管 LED2–5 → GRID1–GRID4（`FJ12101AH` 1.2"）：整屏三态，**手动 UP 切换**（单击循环 TIME→DATE→TEMP→TIME，双击恢复走时；无自动整屏轮显）——TIME `HH:MM`（冒号=GRID2/GRID3 小数点，GRID3 倒装）、DATE `MMDD`（月左/日右镜像）、TEMP `[符号][2位温度][C/F]`（NTC 无效兜底 25°C；°F 由 `cfg.temp_unit` 切换）。
  - 下排 `SMG1` → GRID5/6（`5612B` 双位）：TIME 模式由 `smg1_mode`（8266 下发，偏移21）固定选显 **温度 °C / 日期(日 DD)**（0=温度、1=日期，不轮换）；DATE 显星期 1–7；TEMP 灭。**SMG1 凡显温度个位(GRID5)小数点恒亮**。
  - 上排 `SMG2` → GRID7/8（`5612B` 双位）：TIME 显秒 `SS`、DATE 显年份 `YY`、TEMP 灭。
  - 段序 SEG1=a … SEG8=dp（字节序 dp,g,f,e,d,c,b,a）
- **时钟**：DS1302，CE=P1.3 / DSDA=P1.4 / DSCL=P1.5，CR1220 备电走时。
- **WiFi**：ESP-01S(ESP8266)，UART(P3.0/P3.1)，AP 热点 `56dz network clock`（无密码，`192.168.4.1` 配网页），STA 配置页为同网段 IP；ESP 侧自写固件（`firmware/8266`），与 51 用自定义串口协议通信。
  - **按键**：UP(P3.2/INT0)、SET(P3.3/INT1)。产品固件 `firmware/STC` 语义：单击 SET→手动调亮度、双击 SET→显示联网 IP 末段(+对时)、长按 SET→手动设时间(字段循环,闰年感知)；单击 UP→循环整屏模式(TIME→DATE→TEMP)、双击 UP→恢复走时、长按 UP→计时器(起/停/复位)；两键同按持续 ≥5s→清 WiFi 凭据并重进 AP 配网(带 850ms 手势锁,整段手势直到两键都松)；任意键停响铃、UP 贪睡。`demo/clock-bringup` 测试固件按键语义见各自目录。
- **其它**：BEEP(P2.1) 蜂鸣器；GM(P1.0)/RM(P1.1) 光敏/热敏采样；LED_T(P1.2 红)；LED_WIFI = LED6（蓝色，由 ESP GPIO2/引脚2 驱动）在 ESP 侧，勿与 8266 板载 LED 混淆。
- 引脚明细与器件连接见 `plan/原理图.md`（权威）。

## 8KB ROM 容量下的精简清单

固件须塞进 8KB Flash（实测 8192B 上限）。下列为**因 ROM 容量做出的功能/代码简化**，功能验证在 `firmware/STC` 下完成：

| 精简项 | 原因 / 取舍 | 节省 |
| --- | --- | --- |
| 软件 PWM 极暗调光（T0 中断 1/1024 占空比） | STC15W408AS 无 Timer1，T0 已被占用；改为 TM1639 硬件 8 档占空比（0=1/16…7=满），自动/手动亮度用户可见功能保留，仅丢失低于 1/16 的软件极暗档。〔demo 100% / 产品固件 0%，见 `plan/新版时钟功能.md` §十九〕 | ~700B |
| 配网 IP 显示：4 段滚动 + 除 10/100 | 同网段前三段固定，仅显 **`P` + IP 末段**（0–255）；去掉 4 段滚动与 `*26>>8` 除法。〔4 段滚动曾实现后简化，见 `plan/新版时钟功能.md` §十九〕 | ~115B |
| 独立计时器（长按 UP，M4） | 保留为单模态计时（起/停/复位），免 `*10` 除法省 `__mulint` 库 | ~30B |
| `bcd2bin` / NTC 温度 `*10`·`*58`·`*29` | 改用移位（`hi*8+hi*2`、`(d<<5)-(d<<1)+d`、`off<<3+off<<1`）替代整数乘法，去除 `__mulint` 库 | ~29B |
| `tm1639_set_brightness`/`set_light` 冗余 STB | 每帧 `tm1639_write_display` 已写 `0x88\|g_bright`，二者仅更新 `g_bright` 变量，不再单独发 STB 事务 | ~17B |
| M11 EEPROM 驱动 8 位地址计数 + 直接读写 `cfg` | 配置固定存扇区 0（地址 <256），用 8 位计数；读/写直接落到 `cfg` 结构，免去 55B 中转缓冲 + memcpy | ~95B |

> 5KB EEPROM 是数据空间，不能执行代码，无法减小 CODE，仅用于 M11 配置持久化（扇区 0，不越界 0x13FF）。

## 目录结构
```
AGENTS.md          项目规则（AI/协作必读：IO 安全红线、时钟映射、ROM 约束）
README.md          本文件（总览 + 目录说明）
demo/             测试/功能验证固件（每个独立目录，各有 build.bat）
  _common/         共享 STC 驱动（被各 demo 引用，不单独编译）
    stc15.h            寄存器定义（Catium2006 官方 SDCC 头）
    tm1639.h/.c        TM1639 三线驱动（段拆分 + GRID3 倒装补偿）
    ds1302.h/.c        DS1302 实时时钟驱动（突发读 + 单字节写，已烧录验证）
  clock-bringup/   显示点亮验证：滚动自测 + 光敏/热敏显示 + 亮度自动 + 蜂鸣器
  ds1302-clock/    实时时钟：读 DS1302 显示时分秒 + 传统手动设置日期时间
  uart-test/       硬件串口回环验证：UART1 自发自收（Timer2 波特源 9600 8N1）
  hw-test/         产线全功能自检（HWTEST），见 plan/硬件生产测试计划.md
  测试用固件禁止连接8266！！！.txt  防烧写误连警告（强推挽烧 8266 的提醒）
doc/              数据手册 + 原版功能分析
  STC15W408AS.pdf / TM1639.pdf      芯片/驱动手册
  doc_uart_raw.txt / upload_*.pdf   原版时钟/协议资料
  STC15W408AS-硬件选项.md            硬件配置项说明
  原版时钟功能.md                    原版（被复刻对象）功能分析
plan/             设计文档（权威依据）
  原理图.md            引脚映射与器件连接（权威）
  串口通信协议.md       51↔8266 串口协议帧格式（含 SET_CFG 54B 偏移）
  固件复刻计划.md       复刻路线与里程碑
  新版时钟功能.md        51 产品固件功能/容量取舍/温度补偿
  8266串口测试计划.md    串口联调测试计划 + 实测结果记录（§9）
  硬件生产测试计划.md    产线物理按键测试
firmware/         正式产品固件（按 MCU 分目录）
  STC/            51 产品固件（STC15W408AS，主项目）
    src/              固件源码（见下方）
    test/             测试脚本（联调 + 离线逻辑，test/README.md 有明细）
       uart_8266_sim.py      主驱动：冒充 8266 联调（settime/setcfg/send/...）
       com_cfg.py            回读 54B 配置并解码（闹钟时/分按 BCD 显示）
       display_logic_test.py 离线验证 display.c 显示逻辑
       keys_fsm_test.py      离线验证按键 FSM
       ring_alarm_fsm_test.py 离线验证闹钟/响铃 FSM
       README.md              test 目录说明
    BUILD.md        构建环境与 CODE 上限 8192B 红（改动前必读）
    编程计划.md      固件（再）开发计划
    build.bat       编译脚本（SDCC，正式构建入口）
  8266/              ESP-01S 固件（PlatformIO，配网/Web 配置/倒计时权威/伪待机/NTP）
```

**`firmware/STC/src/` 源码明细**：

| 文件 | 作用 |
|---|---|
| `main.c` | 主循环 + 串口协议解析（UART1/Timer2 波特源）+ 状态机/闹钟/倒计时接管 |
| `config.c/.h` | 54B 配置结构（`SET_CFG` 布局）+ 默认值 |
| `display.c/.h` | TM1639 渲染 + 整屏三态（TIME/DATE/TEMP）+ SMG1/SMG2 映射 + °C/°F |
| `ds1302.c/.h` | DS1302 实时时钟读写（BCD） |
| `tm1639.c/.h` | TM1639 三线驱动 + 8 档亮度 + 段拆分/GRID3 倒装 |
| `keys.c/.h` | 按键 FSM（单击/双击/长按） |
| `eeprom.c/.h` | 片内 Data Flash（IAP 0x0000–0x13FF）配置持久化 |

## 构建
> 需 SDCC（本机已装在 `C:\Program Files\SDCC\bin`）；各 demo 的 `build.bat` 会自动定位该路径，找不到时回退到 PATH。

```bat
cd demo/ds1302-clock
build.bat            :: 生成 demo/ds1302-clock/out/firmware.hex
```

> 其它 demo（`clock-bringup`、`uart-test`）同理：进入对应目录运行 `build.bat`。
> 产品固件（51 端，`firmware/STC`）构建环境与尺寸红线见 **`firmware/STC/BUILD.md`**（CODE 上限 8192B、当前余量极小，改动前必读）。

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
- ✅ **51 产品固件（firmware/STC，v1.0.5）**：完整按键语义（SET 单/双/长按、UP 单/双/长按进计时器、双键≥5s 重配网）、配置 54B EEPROM 持久化（IAP 0x0000–0x13FF）、SET_TIME/SET_CFG/NET_STAT/STA_IP/REQ_CFG/REQ_TIME/HEARTBEAT/ENTER_AP/CD_CTRL/DISP_OVERRIDE 协议收发、SMG1 温度/日期选显、闰年感知、大屏整屏自动轮播（`display_mode`）、红色状态灯使能（`led_en`）、倒计时显示接管（DISP_OVERRIDE 驱动）。CODE 余量极小（余量以 `firmware/STC/BUILD.md` 为准），改动前必读 `firmware/STC/BUILD.md`。
- ✅ **传感器热敏温度 + 单点补偿**（temp_offset 由 8266 下推）、EEPROM 持久化（IAP）、三组闹钟、状态指示灯重构、计时器（51 原生 MM:SS 封顶 99:59）——均已完成。
- ✅ **51 固件研发结束（v1.0.7）**：功能与硬件实机测试全部完成——串口协议帧（SET_CFG/对时/自动轮显/闹钟贪睡/计时器/°F 换算/日期/温度）逐项实机验证通过；`temp_offset` 单位（整数°C）实测标定；`SET_CFG` 读回防清零修复。51 端开发**收尾结束**，后续仅有随 8266 联调的排障性微调，不再有新功能开发。CODE 8182/8192B（余 10B）。
- ⚠️ **已裁撤 / 未移植功能（代码完成度见 `plan/新版时钟功能.md` §十九）**：整点报时（曾 100% 实现后移除）、事项提醒 rem1–5（0% 从未实现）、计时器「时」位（0%）、IP 4 段滚动（曾实现后简化为 P+末段）、极暗软件 PWM（仅 demo 验证，产品未移植）。
- ✅ **ESP8266 固件（firmware/8266，已实现）**：NTP 校时、两页 Web 配置（AP 配网 + STA 全功能）、STA_IP 推送、倒计时 tick 权威（DISP_OVERRIDE 显示接管）、伪待机（CPU 常驻 + RF 关 + 闲置 5min 断网）。`pio run` 零错零告警，真机 `link_hw_test.py` S1–S10 全绿。协议设计命令 ACK/NAK/BTN_EVENT/AP_READY 等**按要求未实现/未消费**（见 `plan/串口通信协议.md` 与 §8266 下述）。参考脚本见 `firmware/STC/test/uart_8266_sim.py`、`plan/8266串口测试计划.md`。

## 版本里程碑
| Tag | 说明 |
| --- | --- |
| v0.1 | 原理图引脚修正 + 原版功能说明 + 51 固件复刻计划 |
| v0.2 | 硬件映射定稿，开始固件复刻（点亮数码管） |
| v0.3 / display-bringup | TM1639 显示驱动 + 显示顺序自测通过，数码管点亮 |
| v0.4 / restructure | 整理项目结构，测试代码归入 demo，固件按 MCU 分目录 |
| v0.5 / ds1302-v0.5-timekeeping | DS1302 走时 + 断电保持 + 传统手动设置完成 |
| v0.6 / uart-loopback-v0.6 | 硬件串口回环测试固件完成 |
| v1.0.3 / wifi-clock-51 | 51 产品固件完成：按键语义/配置持久化/串口协议/SMG1/计时器/倒计时接管；配套 8266 模拟测试脚本 |
| v1.0.4 / wifi-clock-51-v1.0.4 | 51 产品固件功能全部完成（含大屏自动轮播 `display_mode`、红灯使能 `led_en`、计时器、倒计时接管），CODE 8175B；进入第一轮全面测试 |
| v1.0.5 / wifi-clock-51-v1.0.5 | **修复 P0：SET_CFG 配置推送到不了 51**（`uart_poll` 仅在外层 ~240ms 主循环调用，RX 环 `urx[32]` 装不下 59 字节 SET_CFG 帧而溢出丢头，`apply_set_cfg` 永不触发）。改在 10ms 内层扫描循环高频 `uart_poll`，环不再溢出；XRAM 仅 256B 故未扩环（会越界挂死）。已用 PC 模拟 8266 验证 SET_CFG 可下发并 EEPROM 持久化、红灯亮。CODE 8178B |
| v1.0.7 / wifi-clock-51-v1.0.7 | **51 固件研发收尾**：实机测试全部通过（串口协议/闹钟贪睡/计时器/°F/日期/温度/自动轮显），`temp_offset` 实测标定（整数°C），SET_CFG 读回防清零修复；精简 test 目录+README、实测 CODE 8182/8192B（余 10B）并统一全文档。**51 端开发结束**，无新功能开发，后续仅 8266 联调排障 |

详细复刻路线见 `plan/固件复刻计划.md`。

## 说明
- 端口以 `plan/原理图.md` 为准；实测 LED / DS1302 不响应时优先复核 SOP28 pin5/pin6。
- SFR 用 `demo/_common/stc15.h` 自定义（SDCC：`__sfr __at(addr)`；Keil：`sfr addr = addr`），STC15 扩展 SFR 按模块追加并核对数据手册地址，避免凭记忆误写。
