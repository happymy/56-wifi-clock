# 51 固件编译环境（firmware/STC）

> 适用：STC15W408AS 产品固件（51 端）空间极度紧张——CODE 上限 8192B，当前构建末端 8182B（0x1FF6，解析 `out/firmware.ihx` 实测），**余量仅 10B**。任何让体积膨胀的改动都可能溢出。本文件固定编译环境、尺寸检查手法与省体积红线，避免"换台机器/改个选项就烧不进去"。

## 1. 工具链（务必对齐）

| 项 | 值 |
| --- | --- |
| 编译器 | **SDCC mcs51 4.5.0 #15242**（MINGW64 版） |
| 安装路径 | `C:\Program Files\SDCC\bin`（build.bat 自动前置到 PATH，找不到时回退 PATH） |
| 目标 | `-mmcs51`（8051 系，STC15 为 1T 8051） |
| 链接器 | SDCC 自带 `sdcc` 链接步骤 + `packihx`（生成 Intel HEX） |

> 不同 SDCC 版本 / 优化档生成的指令数不同，**固定用 4.5.0 + `--opt-code-size`**。换版本或升档（见 §3）会直接改变体积，可能破坏本仓库的"刚好塞进"平衡。

## 2. 构建命令

在 `firmware/STC` 目录下直接运行：

```bat
build.bat
```

产出：`firmware/STC/out/firmware.hex`（同时有 `out/firmware.ihx`、`out/*.rel`、`out/firmware.map`）。

`build.bat` 内部流程：

1. `chcp 936` + `cd /d %~dp0`，保证中文路径与编码正常（bat 须 ANSI/GBK，LF 会解析错行）。
2. 逐文件编译 `src/*.c` → `out/*.rel`，统一 `FLAGS=-mmcs51 --opt-code-size -I../../demo/_common`。
3. 链接：`sdcc %FLAGS% --code-size 8192 --iram-size 256 -o out\firmware.ihx out\*.rel`。
4. `packihx out\firmware.ihx > out\firmware.hex`。

> 公共驱动（`tm1639.c/ds1302.c/stc15.h` 等）来自 `../../demo/_common`，编译期通过 `-I` 引入；产品侧 `src/` 下是优化过的本地副本，二者并存。

## 3. 关体积的三个红线开关

| 开关 | 作用 | 红线 |
| --- | --- | --- |
| `--opt-code-size` | 体积优先优化 | **绝不可改 `--opt-code-size`→`--opt-code-speed`**，速度档体积明显膨胀，会直接溢出 8192B |
| `--code-size 8192` | 链接期 CODE 上限 | 硬约束；超了链接报错（见 §4），不可调大（芯片只有 8KB Flash） |
| `--iram-size 256` | 内部 RAM 上限 | STC15W408AS 片内 256B；超了同样链接失败 |

> 模型默认 `--model-small`（内部 data 空间），不要切 medium/large，否则会引入外部 RAM 访问开销并膨胀体积。

## 4. 溢出表现

链接阶段若 CODE 超 8192B，会看到：

```
?ASlink-Error-Insufficient ROM/EPROM/FLASH memory.
```

此时 `out/firmware.ihx` 可能仍生成但**不可用**，不要拿它烧录。先砍体积再重建（见 §6）。

## 5. 体积检查手法（每次构建后必看）

读链接映射的 `s_XINIT`（CODE 最后一段，含启动初始化数据）：

```bat
findstr /C:"s_XINIT" out\firmware.map
```

输出形如 `C:   00001FE7  s_XINIT`。再在 `.map` 的 `Area` 表里查 `XINIT` 行的 `Size`（当前 8 字节）。

```
CODE 末端(独占地址) = s_XINIT 地址 + XINIT Size
```

- 当前：`0x1FEE + 0x08 = 0x1FF6` = **8182 字节**。
- **合格判据**：末端 ≤ `0x2000`（8192）即不溢出；但见 §6 的工程安全线。

> 也可直接看 `.map` 末尾各 `Area` 表，确认没有其它 CODE 段越过 `0x2000`。

## 6. 工程安全线（关键！）

本机 SDCC 4.5.0 构建末端当前 8182B（0x1FF6，余 10B）。但**另一套环境（不同 SDCC 版本、或 STC-ISP 打包链路差异）实测会多产出 ~4 字节**。因此：

- 本机构建末端 8182B（0x1FF6，余 10B），用户环境约 +4B → 8186，仍过 8192 但**余量仅 6B**；建议保持 **≤ 0x1FF2（8178B）/ 余 ≥ 14B** 以内，给环境差量留缓冲。
- 只以"本机不报错"为通过标准是危险的——用户环境仍可能溢出。提交前确认 §5 算出的末端 +4 < 8192。

## 7. 省体积的惯例（改动前先想）

固件已为塞进 8KB 做过多处简化（详见 README"8KB ROM 容量下的精简清单"），新增代码请遵守：

- **禁止 `__mulint` 库**：整数乘法用移位替代（`hi*8+hi*2`、`d<<5 - d<<1 + d` 等），避免拉入 `*10/*100` 除法/乘法库（动辄几十~上百字节）。
- **复用已有字段 / 位**：状态机尽量用现有 `kst_t` 字段互标（如双击用 `long_fired` 做粘性标记），不轻易加新 `char`。
- **不新增冗余函数**：同一段逻辑（亮度预览、蜂鸣）已在主循环复用，复制即涨体积。
- **优先 `--opt-code-size`** 已全局开启；若某函数为了省字节用查表/位运算，不要为"可读性"回退成乘除法。
- **改动后必跑 §5**，并把新的末端字节数回填到 README 的"当前构建末端"行。

## 8. 烧录（冷启动 ISP）

STC 芯片靠串口下载（"先点下载、再上电"）：

1. USB-TTL：TX→MCU RX(P3.0)、RX→MCU TX(P3.1)、GND 共地，MCU 上电前先不上电；
2. STC-ISP（官方 Windows 工具，选 STC15W408AS，IRC 频率选 **11.051MHz**——板载实测值，见 `plan/硬件生产测试计划.md`）打开 `out/firmware.hex`，点"下载/编程"；
3. 给 MCU 上电（或按复位）即完成。

> 命令行可选 `stcgal`：`python -m stcgal -P stc15 -p COMx out\firmware.hex`
