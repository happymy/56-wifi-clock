#!/usr/bin/env python3
"""8266 侧固件逻辑离线自检（不依赖真实硬件 / PlatformIO）。

覆盖：
  1. 协议帧编解码（AA 55 CMD LEN PAYLOAD CHK，CHK=XOR）  —— 对应 src/proto.cpp
  2. 倒计时状态机 tick/暂停/取消帧序列                    —— 对应 src/countdown.cpp
  3. BCD / 十进制换算（闹钟字段铁律）                     —— 对应 web.cpp 换算
  4. SET_CFG 完整性铁律：改单字节仍整帧 13B 下发          —— 对应 web.cpp h_sta_save
  5. proto_rx 收帧状态机逐字节镜像：空帧/坏校验/超长帧/连续帧 —— 对应 src/proto.cpp

用法：python test_proto.py
"""
import sys

# ---- 帧编码（复刻 proto.cpp） ----
SYNC0, SYNC1 = 0xAA, 0x55
CMD_HEARTBEAT = 0x02
CMD_SET_TIME, CMD_SET_CFG, CMD_NET_STAT, CMD_DISP_OVERRIDE, CMD_BOOT = 0x81, 0x82, 0x83, 0x89, 0x8F
CFG_LEN = 13
DO_MODE_FREE, DO_MODE_CD, DO_MODE_RING = 0x00, 0x01, 0x02


def encode(cmd, payload=b""):
    chk = cmd ^ len(payload)
    out = [SYNC0, SYNC1, cmd, len(payload)]
    for b in payload:
        out.append(b)
        chk ^= b
    out.append(chk)
    return bytes(out)


def decode_frame(frame):
    """返回 (cmd, payload) 或抛错。空帧为 5 字节（AA 55 CMD 00 CHK）。"""
    assert len(frame) >= 5 and frame[0] == SYNC0 and frame[1] == SYNC1
    cmd, ln = frame[2], frame[3]
    payload = frame[4:4 + ln]
    chk = cmd ^ ln
    for b in payload:
        chk ^= b
    assert frame[4 + ln] == chk, f"chk mismatch: got {frame[4+ln]:#x} want {chk:#x}"
    return cmd, payload


# ---- 倒计时（复刻 countdown.cpp） ----
class CD:
    IDLE, RUN, PAUSE = 0, 1, 2
    def __init__(self):
        self.preset = 5
        self.st = CD.IDLE
        self.remain = 0
        self.ring = False
        self.sent = []   # (cmd, payload) 列表，用于断言帧序列

    # 替身：向 self.sent 追加发帧
    def _send(self, cmds, payload):
        self.sent.append((cmds, payload))

    def set_preset(self, m):
        if 1 <= m <= 99:
            self.preset = m

    def start(self):
        # start 推首帧满时长 (MM:SS)，remain 整秒计；tick1(1s后) 减为 MM-1:59，帧序不重复、总时长满 preset 分钟
        self.remain = self.preset * 60
        self.st = CD.RUN
        self.ring = False
        self._send(CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, self.preset, 0]))

    def pause_resume(self):
        if self.st == CD.RUN:
            self.st = CD.PAUSE
        elif self.st == CD.PAUSE:
            self.st = CD.RUN
            self._send(CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, self.remain // 60, self.remain % 60]))
        else:  # IDLE：与 51 脱节（归零响铃后单击 SET / 8266 重启残留 cd_disp）→ mode0 放权释放
            self._send(CMD_DISP_OVERRIDE, bytes([DO_MODE_FREE]))

    def cancel(self):
        self.st = CD.IDLE
        self.ring = False
        self._send(CMD_DISP_OVERRIDE, bytes([DO_MODE_FREE]))   # 幂等：始终回发，防 51 残留 cd_disp 卡死

    def tick(self):
        if self.st == CD.RUN:
            if self.remain > 0:
                self.remain -= 1
                if self.remain == 0:
                    self._send(CMD_DISP_OVERRIDE, bytes([DO_MODE_RING]))
                    self.st = CD.IDLE
                    self.ring = True
                else:
                    self._send(CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, self.remain // 60, self.remain % 60]))


# ---- 断言工具 ----
fails = []
def check(name, cond):
    if cond:
        print(f"  ok  {name}")
    else:
        print(f" FAIL {name}")
        fails.append(name)


def same_frame(a, b):
    return a == b


def test_frame_io():
    print("[帧编解码]")
    for cmd, payload, name in [
        (CMD_SET_TIME, bytes([0x26, 0x08, 0x30, 0x07, 0x09, 0x15, 0x30, 0x08]), "SET_TIME 8B"),
        (CMD_SET_CFG, bytes(range(CFG_LEN)), "SET_CFG 13B"),
        (CMD_NET_STAT, bytes([3]), "NET_STAT"),
        (CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, 4, 59]), "DISPLAY mode1"),
        (CMD_DISP_OVERRIDE, bytes([DO_MODE_RING]), "DISPLAY mode2"),
        (CMD_DISP_OVERRIDE, bytes([DO_MODE_FREE]), "DISPLAY mode0 释放(单字节0)"),
        (CMD_BOOT, b"", "BOOT 空帧"),
    ]:
        frame = encode(cmd, payload)
        dcmd, dpayload = decode_frame(frame)
        check(f"roundtrip {name}", dcmd == cmd and dpayload == payload)

    # 校验位破坏
    bad = bytearray(encode(CMD_NET_STAT, bytes([3])))
    bad[-1] ^= 0xFF
    try:
        decode_frame(bytes(bad))
        check("坏校验被拒", False)
    except AssertionError:
        check("坏校验被拒", True)


def test_countdown():
    print("[倒计时 tick/暂停/取消]")
    cd = CD()
    cd.set_preset(2)
    cd.start()
    # 首帧应为满时长 2:00，tick1 减为 1:59（C 侧 start 推 preset,0；tick 自减）
    check("start 推 mode1(2,0)", cd.sent[-1] == (CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, 2, 0])))

    cd.tick()
    check("tick1 推 mode1(1,59)", cd.sent[-1] == (CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, 1, 59])))

    cd.pause_resume()
    check("暂停不长推", len(cd.sent) == 2)
    n_sent = len(cd.sent)
    cd.tick()
    check("暂停 tick 不发帧", len(cd.sent) == n_sent)

    cd.pause_resume()
    check("恢复推当前 mode1", cd.sent[-1] == (CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, 1, 59])))

    # 快进到归零：start 推首帧(2,0) remain=120；tick1→119 推(1,59)；
    # 该行每次发 (remain//60, remain%60)；remain 到 0 时推 mode2。
    # 已 tick 1 次 remain=119；再 119 拍后 remain=0 → 归零响铃
    for _ in range(119):
        cd.tick()
    check("归零推 mode2 响铃", cd.sent[-1] == (CMD_DISP_OVERRIDE, bytes([DO_MODE_RING])))
    check("归零后 IDLE", cd.st == CD.IDLE and cd.ring)

    # 取消应回 mode0
    cd._send  # noqa
    # C 侧 cancel 条件: IDLE 且 !ring 直接 return
    cd2 = CD()
    cd2.start()
    cd2.pause_resume()          # PAUSE
    cd2.cancel()
    check("取消回 mode0", cd2.sent[-1] == (CMD_DISP_OVERRIDE, bytes([DO_MODE_FREE])))

    cd3 = CD()
    cd3.cancel()
    check("IDLE 取消回 mode0（防 51 残留）", cd3.sent[-1] == (CMD_DISP_OVERRIDE, bytes([DO_MODE_FREE])))


def test_bcd():
    print("[BCD/十进制（闹钟字段铁律）]")
    def dec2bcd(v): return ((v // 10) << 4) | (v % 10)
    def bcd2dec(v): return (v >> 4) * 10 + (v & 0x0F)
    for dec in [0, 9, 15, 23, 59]:
        b = dec2bcd(dec)
        check(f"dec2bcd({dec})={b:#x}", bcd2dec(b) == dec)
    check("15→0x15", dec2bcd(15) == 0x15)
    check("0x59→59", bcd2dec(0x59) == 59)


def test_cfg_mirror():
    print("[SET_CFG 完整性（整帧 13B 下发）]")
    cfg = bytearray(range(CFG_LEN))
    # 改一个字段（web save 模拟：关屏 start 时 in cfg[5]）
    cfg[5] = 22
    out = encode(CMD_SET_CFG, bytes(cfg))
    cmd, payload = decode_frame(out)
    check("整帧 13B", len(payload) == CFG_LEN)
    check("改后帧仍携全量", payload[5] == 22 and payload[4] == 4)
    check("未改字节保留", payload[0] == 0 and payload[12] == 12)


def test_proto_rx_sm():
    print("[收帧状态机（镜像 C: 空帧/坏帧/超长/连续帧）]")
    def new_rx():
        """返回 (feed, frames)：逐字节喂入，完整帧回调进 frames=(cmd,payload)。
        逻辑与 C proto_rx 一致：空帧 len==0 直接跳 state5 验 CHK，不吞字节。"""
        st = cmd = ln = chk = idx = 0
        buf = bytearray()
        frames = []
        c = [st, cmd, ln, chk, idx, buf]
        def feed(b):
            s = c[0]
            if s == 0:
                if b == 0xAA: c[0] = 1
            elif s == 1:
                c[0] = 2 if b == 0x55 else 0
            elif s == 2:
                c[1] = b; c[0] = 3
            elif s == 3:
                c[2] = b; c[3] = c[1] ^ c[2]; c[4] = 0; c[5].clear()
                c[0] = 5 if c[2] == 0 else (4 if c[2] <= 64 else 0)   # 空帧直接验 CHK
            elif s == 4:
                if c[4] < c[2]:
                    c[5].append(b); c[4] += 1; c[3] ^= b
                if c[4] == c[2]: c[0] = 5
            elif s == 5:
                if b == c[3]: frames.append((c[1], bytes(c[5])))
                c[0] = 0
        return feed, frames

    feed, frames = new_rx()
    for b in encode(CMD_HEARTBEAT): feed(b)
    check("空帧 HEARTBEAT 解出 (cmd, 空payload)", frames == [(CMD_HEARTBEAT, b"")])

    feed, frames = new_rx()
    for b in encode(CMD_DISP_OVERRIDE, bytes([DO_MODE_FREE])): feed(b)
    check("mode0 单字节0 解出", frames == [(CMD_DISP_OVERRIDE, b"\x00")])

    feed, frames = new_rx()
    for b in encode(CMD_BOOT) + encode(CMD_NET_STAT, bytes([3])) + \
            encode(CMD_DISP_OVERRIDE, bytes([DO_MODE_CD, 1, 59])):
        feed(b)
    check("连续帧不失步", frames == [
        (CMD_BOOT, b""), (CMD_NET_STAT, b"\x03"),
        (CMD_DISP_OVERRIDE, b"\x01\x01\x3b")])

    bad = bytearray(encode(CMD_NET_STAT, bytes([3]))); bad[-1] ^= 0xFF
    feed, frames = new_rx()
    for b in bytes(bad) + encode(CMD_BOOT): feed(b)
    check("坏校验被吞且不毒化后续帧", frames == [(CMD_BOOT, b"")])

    feed, frames = new_rx()
    for b in encode(CMD_SET_CFG, bytes(80)) + encode(CMD_BOOT): feed(b)   # LEN=80>64
    check("超长 LEN>64 拒收", frames == [(CMD_BOOT, b"")])


def main():
    rc = 0
    for fn in [test_frame_io, test_countdown, test_bcd, test_cfg_mirror, test_proto_rx_sm]:
        try:
            fn()
        except Exception as e:  # noqa: BLE001
            print(f" EXC {fn.__name__}: {e}")
            fails.append(fn.__name__)
    print("-" * 40)
    if fails:
        print(f"FAILED: {len(fails)} -> {', '.join(fails)}")
        sys.exit(1)
    print("ALL PASS")


if __name__ == "__main__":
    main()
