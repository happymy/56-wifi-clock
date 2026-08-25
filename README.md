# 56-wifi-clock

STC15W408AS（8051 内核）开源开发环境。工具链：SDCC + stcgal。

## 工具链
- **SDCC** 4.5.0：C 编译器（winget 安装，已就绪）
- **stcgal**：串口烧录工具（`pip install stcgal`，已就绪；用 `python -m stcgal` 调用）
- 硬件：CH340 / CP2102 等 USB 转串口模块

## 目录
```
src/main.c   闪烁示例（P1.0 低电平点亮 LED）
build.bat    编译 / 烧录脚本
```

## 编译
```bat
build.bat            :: 生成 firmware.hex
```

## 烧录（冷启动）
STC 芯片靠串口 ISP 下载，流程是“先点下载、再上电”：
1. 串口模块 TX→MCU RX(P3.0)、RX→MCU TX(P3.1)、GND 共地，MCU 电源接好但先不供电
2. 执行：
   ```bat
   build.bat flash COM3
   ```
3. stcgal 提示握手时，给 MCU 上电（或按复位），自动完成烧录

## 说明
- 内核 8051 的 `P0`–`P3` 等 SFR 地址与 STC15 完全一致，直接复用 SDCC 自带 `<8051.h>`
- 端口上电为准双向口，可直接驱动 LED；若需强推挽输出，再配置 `P1M0/P1M1`
- 内部 IRC 振荡器出厂默认约 24MHz，`main.c` 里的 `delay_ms` 为粗略值，按需调 `j` 上限
- 下一步可加 UART（P3.0/P3.1）与 WiFi 模块（如 ESP-01）通信，作为时钟数据来源
