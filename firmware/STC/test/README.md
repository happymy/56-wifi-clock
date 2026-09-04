# STC15 测试脚本说明

## 串口联调（连 51 硬件测试时用）

| 文件 | 作用 |
|---|---|
| `uart_8266_sim.py` | **主驱动脚本**，冒充 ESP8266 走 `micro_uart` 串口（默认 COM3 @ 9600 8N1）。子命令：`monitor`（监听）、`server`（冒充 ESP 自动握手应答）、`boot`、`send`（任意帧）、`settime`（对时）、`setcfg`（13B 配置）、`staip`、`cd`/`cdctrl`（倒计时显示接管）。详见各子命令 `-h`。 |
| `com_cfg.py` | 回读 13B 配置并解码打印：连上即发 `BOOT(0x8F)`+`REQ_CFG(0x87)`，按 `config.h` 偏移解码。 |

## 离线逻辑测试（不连硬件，验证固件状态机）

| 文件 | 作用 |
|---|---|
| `display_logic_test.py` | 纯逻辑验证 `display.c` 显示状态机/渲染（不依赖串口）。 |
| `keys_fsm_test.py` | 纯逻辑验证 `keys.c` 按键 FSM（单击/双击/长按识别）。 |
| `ring_alarm_fsm_test.py` | 纯逻辑验证闹钟/响铃状态机（触发/贪睡/取消）。 |

## 已移除（功能被 `uart_8266_sim.py` 取代）

`com_probe.py` / `com_rw.py` / `com_settime.py` 的探测、读写、对时功能已并入
`uart_8266_sim.py` 的 `send` / `setcfg` / `settime` 子命令，故删除。
