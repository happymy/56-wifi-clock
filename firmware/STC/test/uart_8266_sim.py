#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ESP8266 串口协议模拟器 / 51 固件测试工具。

用 PC + USB-TTL 冒充 ESP8266，经 UART1 驱动 51 固件全部协议逻辑，免真实 ESP 即可验证。
帧格式（与 firmware/STC/src/main.c、plan/串口通信协议.md 一致）：
    [0xAA][0x55][CMD][LEN][PAYLOAD...][CHK]
    CHK = XOR(CMD, LEN, PAYLOAD[0..LEN-1])

命令码严格对齐 firmware/STC/src/main.c（51 当前已实现）：
    0x01 REQ_TIME(MCU→)  0x02 HEARTBEAT(MCU→)  0x04 ENTER_AP(MCU→)
    0x05 CD_CTRL(MCU→)   0x81 SET_TIME(←)       0x82 SET_CFG(双向)
    0x83 NET_STAT(←)     0x87 REQ_CFG(←)        0x88 STA_IP(←)
    0x89 DISP_OVERRIDE(←) 0x8F BOOT(←)

依赖：pip install pyserial
用法：
    python uart_8266_sim.py monitor  COM3
    python uart_8266_sim.py server   COM3
    python uart_8266_sim.py boot     COM3
    python uart_8266_sim.py send     COM3 81 0812240312345008
    python uart_8266_sim.py settime  COM3 12:34:56 --tz 8
    python uart_8266_sim.py setcfg   COM3 cfg.bin
    python uart_8266_sim.py setcfg   COM3 --smg1 1 --temp-unit 1
    python uart_8266_sim.py setcfg   COM3 --rotate 1        # 开启大屏自动轮显(每分钟整分轮换, 落盘)
    python uart_8266_sim.py setcfg   COM3 --led 0            # 关闭红色状态灯(落盘); --led 1 恢复
    python uart_8266_sim.py staip    COM3 192.168.1.10
    python uart_8266_sim.py cd       COM3 on 01 30      # DISP_OVERRIDE 倒计时接管 MM:SS
    python uart_8266_sim.py cd       COM3 off           # DISP_OVERRIDE 释放
    python uart_8266_sim.py cd       COM3 beep          # DISP_OVERRIDE 归零响铃
    python uart_8266_sim.py cdctrl   COM3 0             # CD_CTRL 暂停/恢复
    python uart_8266_sim.py cdctrl   COM3 1             # CD_CTRL 取消
详见 plan/8266串口测试计划.md。
"""
import sys
import time
import argparse
import threading

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial：请先 `pip install pyserial`")

SYNC0, SYNC1 = 0xAA, 0x55
BAUD = 9600

# 命令码（与 firmware/STC/src/main.c 完全一致）
CMD_REQ_TIME  = 0x01
CMD_HEARTBEAT = 0x02
CMD_ENTER_AP  = 0x04
CMD_CD_CTRL   = 0x05   # MCU→ESP: 倒计时控制(0=暂停/恢复,1=取消)
CMD_SET_TIME  = 0x81
CMD_SET_CFG   = 0x82
CMD_NET_STAT  = 0x83
CMD_REQ_CFG   = 0x87
CMD_STA_IP    = 0x88
CMD_DISP_OVERRIDE = 0x89  # ESP→MCU: 显示接管(倒计时/响铃)
CMD_BOOT      = 0x8F

NAME = {v: k for k, v in list(globals().items()) if k.startswith("CMD_")}


def encode(cmd, payload=b""):
    payload = bytes(payload)
    chk = cmd ^ len(payload)
    for b in payload:
        chk ^= b
    return bytes([SYNC0, SYNC1, cmd, len(payload)]) + payload + bytes([chk & 0xFF])


def bcd(n):
    n = int(n)
    return ((n // 10) << 4) | (n % 10)


def now_set_time_payload(tz=8):
    import datetime
    t = datetime.datetime.now()
    wd = (t.isoweekday() % 7) + 1  # DS1302: 1=周日..7=周六
    return bytes([bcd(t.year % 100), bcd(t.month), bcd(t.day), bcd(wd),
                   bcd(t.hour), bcd(t.minute), bcd(t.second), tz & 0xFF])


class Parser:
    """增量解析 51 发来的帧；完整帧通过 cb(cmd, payload) 回调。"""
    def __init__(self, cb):
        self.cb = cb
        self.reset()

    def reset(self):
        self.st = 0
        self.cmd = 0
        self.plen = 0
        self.idx = 0
        self.chk = 0
        self.buf = bytearray()

    def feed(self, data):
        for b in data:
            if self.st == 0:
                if b == SYNC0:
                    self.st = 1
            elif self.st == 1:
                self.st = 2 if b == SYNC1 else 0
            elif self.st == 2:
                self.cmd = b
                self.chk = b
                self.st = 3
            elif self.st == 3:
                self.plen = b
                self.chk ^= b
                self.idx = 0
                self.buf = bytearray()
                self.st = 5 if b == 0 else 4
            elif self.st == 4:
                self.buf.append(b)
                self.chk ^= b
                if len(self.buf) >= self.plen:
                    self.st = 5
            elif self.st == 5:
                if b == self.chk:
                    self.cb(self.cmd, bytes(self.buf))
                else:
                    print("  [BAD CHK] cmd=0x%02X len=%d got=0x%02X want=0x%02X"
                          % (self.cmd, self.plen, b, self.chk))
                self.reset()


def open_port(port, baud=BAUD):
    ser = serial.Serial(port, baud, timeout=0.1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def fmt_frame(cmd, payload):
    return "[%s] 0x%02X len=%d %s" % (
        NAME.get(cmd, "?"), cmd, len(payload), payload.hex(" ").upper())


class Sim:
    def __init__(self, port, baud=BAUD):
        self.ser = open_port(port, baud)
        self.our_cfg = bytearray(54)     # 我们下发给 51 的 SET_CFG（用于回读）
        self.our_cfg[20] = 1             # led_en 默认开(偏移20)
        self.echo_cfg = None            # 51 回的 SET_CFG（REQ_CFG 应答）
        self.stop = threading.Event()
        self.parser = Parser(self._on_frame)
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self):
        while not self.stop.is_set():
            n = self.ser.in_waiting
            if n:
                self.parser.feed(self.ser.read(n))
            else:
                time.sleep(0.005)

    def _on_frame(self, cmd, payload):
        print("  RX", fmt_frame(cmd, payload))
        # 自动应答（server 模式覆盖此方法）
        if cmd == CMD_REQ_CFG:
            print("  TX", fmt_frame(CMD_SET_CFG, self.our_cfg))
            self.ser.write(encode(CMD_SET_CFG, self.our_cfg))
        elif cmd == CMD_SET_CFG:
            self.echo_cfg = payload
            print("    (51 回的 SET_CFG 已记录，长度 %d)" % len(payload))
        elif cmd == CMD_HEARTBEAT:
            print("  TX", fmt_frame(CMD_NET_STAT, bytes([3])))
            self.ser.write(encode(CMD_NET_STAT, bytes([3])))
        elif cmd == CMD_REQ_TIME:
            p = now_set_time_payload()
            print("  TX", fmt_frame(CMD_SET_TIME, p))
            self.ser.write(encode(CMD_SET_TIME, p))
        elif cmd == CMD_CD_CTRL:
            print("    (51 发来倒计时控制 val=%d)" % (payload[0] if payload else -1))

    def send(self, cmd, payload=b""):
        f = encode(cmd, payload)
        print("  TX", fmt_frame(cmd, payload))
        self.ser.write(f)

    def close(self):
        self.stop.set()
        self.thread.join(0.2)
        self.ser.close()


def cmd_monitor(args):
    sim = Sim(args.port, args.baud)
    print("monitor %s @ %d — Ctrl-C 退出" % (args.port, args.baud))
    try:
        while True:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        sim.close()


def cmd_server(args):
    sim = Sim(args.port, args.baud)
    if args.rotate is not None:
        sim.our_cfg[0] = args.rotate & 1                     # 随 REQ_CFG 下推 display_mode（保留其它字节/led_en 默认）
    if args.led is not None:
        sim.our_cfg[20] = args.led & 1                      # 随 REQ_CFG 下推 led_en
    if args.boot:
        time.sleep(0.05)
        sim.send(CMD_BOOT)
    print("server %s @ %d — 自动应答 REQ_TIME/HEARTBEAT/REQ_CFG，Ctrl-C 退出"
          % (args.port, args.baud))
    try:
        while True:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        sim.close()


def cmd_boot(args):
    sim = Sim(args.port, args.baud)
    sim.send(CMD_BOOT)
    time.sleep(0.3)
    sim.close()


def cmd_send(args):
    sim = Sim(args.port, args.baud)
    cmd = int(args.cmd, 16)
    payload = bytes.fromhex(args.payload) if args.payload else b""
    sim.send(cmd, payload)
    time.sleep(0.3)
    sim.close()


def cmd_settime(args):
    sim = Sim(args.port, args.baud)
    h, m, s = args.time.split(":")
    import datetime
    t = datetime.datetime.now()
    wd = (t.isoweekday() % 7) + 1
    payload = bytes([bcd(t.year % 100), bcd(t.month), bcd(t.day), bcd(wd),
                     bcd(h), bcd(m), bcd(s), args.tz & 0xFF])
    sim.send(CMD_SET_TIME, payload)
    time.sleep(0.3)
    sim.close()


def cmd_setcfg(args):
    sim = Sim(args.port, args.baud)
    if args.file:
        with open(args.file, "rb") as f:
            data = bytearray(f.read(54))
        if len(data) < 54:
            data = data + bytes(54 - len(data))
    else:
        data = bytearray(54)
    if args.smg1 is not None:
        data[21] = args.smg1 & 0xFF        # smg1_mode @ 偏移21（0=温度 1=日期）
    if args.temp_unit is not None:
        data[53] = args.temp_unit & 0xFF    # temp_unit @ 偏移53（0=°C 1=°F）
    if args.rotate is not None:
        data[0] = args.rotate & 0xFF        # display_mode @ 偏移0（0=不自动轮显 1=自动轮显）
    if args.led is not None:
        data[20] = args.led & 0xFF          # led_en @ 偏移20（1=开 0=关红灯）
    elif not args.file:
        data[20] = 1                         # 默认开(联网亮灯)
    sim.our_cfg = bytes(data)
    sim.send(CMD_SET_CFG, bytes(data))
    time.sleep(0.3)
    sim.close()


def cmd_staip(args):
    sim = Sim(args.port, args.baud)
    octets = [int(x) for x in args.ip.split(".")]
    if len(octets) != 4:
        sys.exit("IP 格式应为 a.b.c.d")
    sim.send(CMD_STA_IP, bytes(octets))
    time.sleep(0.3)
    sim.close()


def cmd_cd(args):
    sim = Sim(args.port, args.baud)
    if args.mode == "off":
        sim.send(CMD_DISP_OVERRIDE, bytes([0]))
    elif args.mode == "beep":
        sim.send(CMD_DISP_OVERRIDE, bytes([2]))
    else:  # on MM SS（十六进制）
        mm = int(args.mm, 16) if args.mm else 0
        ss = int(args.ss, 16) if args.ss else 0
        sim.send(CMD_DISP_OVERRIDE, bytes([1, mm & 0xFF, ss & 0xFF]))
    time.sleep(0.3)
    sim.close()


def cmd_cdctrl(args):
    sim = Sim(args.port, args.baud)
    sim.send(CMD_CD_CTRL, bytes([args.val & 0xFF]))
    time.sleep(0.3)
    sim.close()


def main():
    ap = argparse.ArgumentParser(description="ESP8266 串口协议模拟器 / 51 测试工具")
    ap.add_argument("--baud", type=int, default=BAUD)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("monitor", help="仅监听并打印 51 发出的所有帧")
    p.add_argument("port"); p.set_defaults(func=cmd_monitor)

    p = sub.add_parser("server", help="冒充 ESP：自动发 BOOT 并应答 51 请求")
    p.add_argument("port")
    p.add_argument("--no-boot", dest="boot", action="store_false",
                   help="不在连接时自动发 BOOT")
    p.add_argument("--rotate", type=int, choices=[0, 1],
                   help="display_mode @ 偏移0：1=自动轮显(随 REQ_CFG 下推)")
    p.add_argument("--led", type=int, choices=[0, 1],
                   help="led_en @ 偏移20：1=开红灯(默认) 0=关红灯")
    p.set_defaults(func=cmd_server, boot=True)

    p = sub.add_parser("boot", help="发送 BOOT(0x8F) 握手帧")
    p.add_argument("port"); p.set_defaults(func=cmd_boot)

    p = sub.add_parser("send", help="发送任意帧（cmd 为十六进制，payload 可选十六进制）")
    p.add_argument("port"); p.add_argument("cmd"); p.add_argument("payload", nargs="?")
    p.set_defaults(func=cmd_send)

    p = sub.add_parser("settime", help="发送 SET_TIME(0x81) 对时下推")
    p.add_argument("port"); p.add_argument("time", help="HH:MM:SS")
    p.add_argument("--tz", type=int, default=8); p.set_defaults(func=cmd_settime)

    p = sub.add_parser("setcfg", help="发送 SET_CFG(0x82) 54 字节配置")
    p.add_argument("port"); p.add_argument("file", nargs="?",
                   help="可选 .bin 配置文件（不足 54B 补零）")
    p.add_argument("--smg1", type=int, choices=[0, 1],
                   help="smg1_mode @ 偏移21：0=温度 1=日期")
    p.add_argument("--temp-unit", dest="temp_unit", type=int, choices=[0, 1],
                   help="temp_unit @ 偏移53：0=°C 1=°F")
    p.add_argument("--rotate", type=int, choices=[0, 1],
                   help="display_mode @ 偏移0：0=不自动轮显 1=自动轮显(每分钟整分轮换)")
    p.add_argument("--led", type=int, choices=[0, 1],
                   help="led_en @ 偏移20：1=开红灯(默认) 0=关红灯(联网也不亮)")
    p.set_defaults(func=cmd_setcfg)

    p = sub.add_parser("staip", help="发送 STA_IP(0x88) 供双击 SET 显示 P+IP末段")
    p.add_argument("port"); p.add_argument("ip"); p.set_defaults(func=cmd_staip)

    p = sub.add_parser("cd", help="发送 DISP_OVERRIDE(0x89) 倒计时显示接管")
    p.add_argument("port"); p.add_argument("mode", choices=["on", "off", "beep"])
    p.add_argument("mm", nargs="?", help="on 模式 MM(十六进制)")
    p.add_argument("ss", nargs="?", help="on 模式 SS(十六进制)")
    p.set_defaults(func=cmd_cd)

    p = sub.add_parser("cdctrl", help="发送 CD_CTRL(0x05) 倒计时控制(设备键 P2 下发)")
    p.add_argument("port"); p.add_argument("val", type=int, choices=[0, 1],
                   help="0=暂停/恢复 1=取消")
    p.set_defaults(func=cmd_cdctrl)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
