import serial, time

PORT, BAUD = "COM3", 9600

def chk(body):  # body = cmd,len,payload...
    c = 0
    for x in body: c ^= x
    return c

def frame(cmd, payload=b""):
    body = bytes([cmd, len(payload)]) + payload
    return b"\xAA\x55" + body + bytes([chk(body)])

def read_frames(ser, dur):
    buf = b""
    end = time.time() + dur
    out = []
    while time.time() < end:
        b = ser.read(1)
        if not b: continue
        buf += b
        # 扫描帧
        i = buf.find(b"\xAA\x55")
        while i >= 0 and len(buf) >= i + 4:
            cmd = buf[i+2]; ln = buf[i+3]
            if len(buf) >= i + 4 + ln + 1:
                payload = buf[i+4:i+4+ln]
                c = buf[i+4+ln]
                if c == chk(bytes([cmd, ln]) + payload):
                    out.append((cmd, payload))
                    buf = buf[i+4+ln+1:]
                    i = buf.find(b"\xAA\x55")
                else:
                    buf = buf[i+1:]; i = buf.find(b"\xAA\x55")
            else:
                break
    return out, buf

ser = serial.Serial(PORT, BAUD, timeout=1.0)
ser.reset_input_buffer()
print("open", PORT)
ser.write(frame(0x8F))            # BOOT
time.sleep(0.5)
ser.write(frame(0x87))            # REQ_CFG -> 51 回发 cfg
frames, _ = read_frames(ser, 4)
ser.close()

for cmd, pl in frames:
    print(f"RX cmd=0x{cmd:02X} len={len(pl)} : {pl.hex(' ')}")

# 解码 cfg (13B) 按 config.h 布局
for cmd, pl in frames:
    if cmd == 0x82 and len(pl) == 13:
        d = pl
        def u(o): return d[o]
        def s(o): return d[o] - 256 if d[o] & 0x80 else d[o]   # 有符号 (tz/temp_offset)
        print("--- decoded cfg ---")
        print("display_mode =", u(0))
        print("bright_mode  =", u(1), "(0=auto/1=manual)")
        print("bright_lvl   =", u(2), "(1-8)")
        print("temp_offset  =", s(3))
        print("tz           =", s(4))
        print("off_start    =", u(5), u(6))
        print("off_end      =", u(7), u(8))
        print("snooze       =", u(9))
        print("led_en       =", u(10), "(1=开红灯 0=关)")
        print("smg1_mode     =", "温度" if u(11)==0 else "日期")
        print("temp_unit    =", u(12), "(0=C/1=F)")
        print("(闹钟/报时/倒计时预设已迁 8266 store，不在此 cfg 内)")
print("done")
