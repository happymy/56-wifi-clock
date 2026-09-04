#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""8266 真机串口联调（PC 冒充 51 → 测真实 ESP-01S 下行行为）。

复用 uart_8266_sim.py 的编解码与帧解析器；本脚本反客为主：
主动发 51 上行帧（HEARTBEAT/REQ_TIME/CD_CTRL/SET_CFG...），断言 8266
下行（NET_STAT/REQ_CFG/DISP_OVERRIDE/SET_TIME...）符合协议。

破坏性项（不改配置的 / 会清 WiFi 凭据的）不在本脚本：
  - ENTER_AP(0x04) 会清凭据进 AP 配网 → 需用户在场单独测
  - 倒计时 start 仅 Web 触发 → Web 项需浏览器另测
  - AP 配网页 / STA 配置页需浏览器另测

用法：
  python link_hw_test.py COM3
  依赖：pip install pyserial
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "STC", "test"))
from uart_8266_sim import encode, Parser, fmt_frame, open_port, NAME  # noqa: E402

CFG_LEN = 13
DO_MODE_FREE, DO_MODE_CD, DO_MODE_RING = 0x00, 0x01, 0x02

fails = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok  {name}")
    else:
        print(f" FAIL {name}  {detail}")
        fails.append(name)


def collect(ser, seconds):
    """读串口 seconds 秒，返回 [(cmd,payload)]（坏帧跳过）。"""
    parser = Parser(lambda c, p: None)  # 先用哑回调
    frames = []

    def cb(cmd, payload):
        frames.append((cmd, payload))
    parser.cb = cb
    t0 = time.time()
    while time.time() - t0 < seconds:
        n = ser.in_waiting
        if n:
            parser.feed(ser.read(n))
        else:
            time.sleep(0.01)
    return frames


def count(frames, cmd):
    return sum(1 for c, _ in frames if c == cmd)


def expect_frame_after(ser, timeout, cmd_pred, prewait=0.2):
    """等待并返回首个满足 cmd_pred 的 (cmd,payload)，超时抛 TimeoutError。"""
    parser = Parser(lambda c, p: None)
    hit = []

    def cb(c, p):
        if cmd_pred(c, p):
            hit.append((c, p))
    parser.cb = cb
    t0 = time.time() + prewait
    if prewait:
        time.sleep(prewait)
    while time.time() < t0 + timeout:
        n = ser.in_waiting
        if n:
            parser.feed(ser.read(n))
        else:
            time.sleep(0.01)
        if hit:
            return hit[0]
    raise TimeoutError(f"wait {cmd_pred}")


def main():
    if len(sys.argv) < 2:
        sys.exit("用法: python link_hw_test.py COM3")
    port = sys.argv[1]
    ser = open_port(port)
    print(f"== 8266 真机串口联调 @{port} 9600 ==")

    # S1 上电见 BOOT 周期帧 + 帧结构（逐字节解码已验证 CHK）
    print("[S1] BOOT 周期帧")
    f0 = collect(ser, 6.5)
    boot_n = count(f0, 0x8F)
    check("6.5s 内 ≥3 个 BOOT", boot_n >= 3, f"got {boot_n}")
    for c, p in f0:
        if c == 0x8F:
            check("BOOT len=0", len(p) == 0, f"len={len(p)}")
            break

    # S2 HEARTBEAT → NET_STAT(0x83) 1B（当前实现仅回 0 或 3）
    print("[S2] HEARTBEAT → NET_STAT")
    ser.write(encode(0x02))
    cmd, p = expect_frame_after(ser, 3, lambda c, _: c == 0x83)
    check("回 NET_STAT len=1", len(p) == 1, f"{fmt_frame(cmd, p)}")
    check("值 ∈ {0,3}", p[0] in (0, 3), f"val={p[0]}")

    # S3 REQ_CFG 自动拉取：仅在 g_cfg_valid=false 时每 3s 拉（c 个因上次运行残留镜像则不自拉，均属稳态）
    print("[S3] REQ_CFG 自动拉取")
    f2 = collect(ser, 6.5)
    got_req = count(f2, 0x87) > 0
    if got_req:
        check("收到 REQ_CFG 空帧", True, "")
    else:
        print("  ..  设备已有有效镜像（无 REQ_CFG，稳态正确）")

    # S4 应答 SET_CFG(0x82,13B) 完整镜像 → g_cfg_valid=true → REQ_CFG 停止（刷新镜像同时验证无拉取）
    print("[S4] SET_CFG 应答 → REQ_CFG 停止")
    cfg = bytearray(CFG_LEN)
    cfg[4] = 8           # tz 偏移4 = 8（后续 SET_TIME 复用断言）
    cfg[10] = 1          # led_en
    cfg[11] = 0          # smg1_mode
    cfg[0] = 0           # display_mode 固定不动
    ser.write(encode(0x82, bytes(cfg)))
    f3 = collect(ser, 6.5)
    check("应答后 ≤1 个 REQ_CFG（镜像生效中断拉取）", count(f3, 0x87) <= 1,
          f"got {count(f3, 0x87)}")

    # S5 IDLE 态 CD_CTRL=1（取消）→ 回 mode0 释放（防 51 残留铁律）
    print("[S5] CD_CTRL(1) 取消 → mode0")
    ser.write(encode(0x05, bytes([1])))
    cmd, p = expect_frame_after(ser, 3,
                                lambda c, _: c == 0x89)
    check("回 DISP_OVERRIDE mode0", p == bytes([DO_MODE_FREE]),
          f"{fmt_frame(cmd, p)}")

    # S6 IDLE 态 CD_CTRL=0（暂停/恢复）→ 仍回 mode0（脱节放权）
    print("[S6] CD_CTRL(0) 暂停/恢复 → mode0")
    ser.write(encode(0x05, bytes([0])))
    cmd, p = expect_frame_after(ser, 3, lambda c, _: c == 0x89)
    check("回 DISP_OVERRIDE mode0", p == bytes([DO_MODE_FREE]),
          f"{fmt_frame(cmd, p)}")

    # S7 坏校验帧被吞、不毒化后续帧
    print("[S7] 坏校验健壮性")
    bad = bytearray(encode(0x05, bytes([1]))); bad[-1] ^= 0xFF
    ser.write(bytes(bad))
    t0 = time.time()
    time.sleep(1.0)
    ser.write(encode(0x05, bytes([1])))
    cmd, p = expect_frame_after(ser, 3, lambda c, _: c == 0x89, prewait=0)
    check("坏帧后正常 CD_CTRL 仍回 mode0", p == bytes([DO_MODE_FREE]),
          f"{fmt_frame(cmd, p)}")

    # S8 超长帧（LEN>64）拒收、随后正常帧仍解析
    print("[S8] 超长帧拒收")
    overlong = encode(0x83, bytes(70))
    ser.write(overlong)
    time.sleep(0.5)
    ser.write(encode(0x05, bytes([1])))
    cmd, p = expect_frame_after(ser, 3, lambda c, _: c == 0x89, prewait=0)
    check("超长后正常帧仍回 mode0", p == bytes([DO_MODE_FREE]),
          f"{fmt_frame(cmd, p)}")

    # S9 半帧（缺 CHK）：状态机停在等 CHK，仅消耗后续 1 字节即自愈复位；随后正常帧恢复响应
    print("[S9] 半帧自愈")
    ser.write(bytes([0xAA, 0x55, 0x02, 0x00]))
    time.sleep(0.3)
    ser.write(b"\x00")                       # 冲掉待 CHK 的一字节
    time.sleep(0.3)
    ser.write(encode(0x05, bytes([1])))
    cmd, p = expect_frame_after(ser, 3, lambda c, _: c == 0x89, prewait=0)
    check("半帧冲掉后正常帧仍回 mode0", p == bytes([DO_MODE_FREE]),
          f"{fmt_frame(cmd, p)}")

    # S10 REQ_TIME → do_sync（最长阻塞 ~35s）：连上外网则 SET_TIME(0x81) 8B 且 tz=g_cfg[4]；
    #    网络不可达时 STA 超时后不开网、也不回 SET_TIME（受控软失败，不判 FAIL）
    print("[S10] REQ_TIME → 触发对时 → SET_TIME")
    ser.write(encode(0x01))
    try:
        cmd, p = expect_frame_after(ser, 45, lambda c, _: c == 0x81, prewait=0)
        check("回 SET_TIME len=8", len(p) == 8, f"{fmt_frame(cmd, p)}")
        if len(p) == 8:
            check("tz 字节 = 镜像 g_cfg[4]=8", p[7] == 8, f"tz={p[7]}")
    except TimeoutError:
        note = "未回 SET_TIME（STA/NTP 不可达属受控软失败，非缺陷）"
        print(f"  ..  {note}")

    # 汇总
    print("-" * 40)
    if fails:
        print(f"FAILED: {len(fails)} -> {', '.join(fails)}")
        ser.close()
        sys.exit(1)
    print("ALL PASS")
    ser.close()


if __name__ == "__main__":
    main()