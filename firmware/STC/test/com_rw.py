import serial, time, sys

PORT = "COM3"
BAUD = 9600

def chk(body):  # cmd,len,payload... 全部字节异或（与 51 固件一致）
    c = 0
    for x in body:
        c ^= x
    return c

def frame(cmd, payload=b""):
    body = bytes([cmd, len(payload)]) + payload
    return b"\xAA\x55" + body + bytes([chk(body)])

ser = serial.Serial(PORT, BAUD, timeout=1.0)
print(f"opened {PORT} @ {BAUD}")
ser.reset_input_buffer()
t0 = time.time()

def reader(dur):
    end = time.time() + dur
    buf = b""
    while time.time() < end:
        b = ser.read(1)
        if b:
            buf += b
            # 尝试按 AA 55 cmd len 切帧
            if len(buf) >= 64:
                buf = buf[-64:]
        else:
            if buf:
                dt = time.time() - t0
                print(f"[{dt:6.2f}s] RX {buf.hex(' ')}")
                buf = b""
    if buf:
        dt = time.time() - t0
        print(f"[{dt:6.2f}s] RX {buf.hex(' ')}")

print("--- read 8s (expect heartbeat AA 55 02 00 02 ~1.3s) ---")
reader(8)

print("--- send BOOT (AA55 8F 00 8F) ---")
ser.write(frame(0x8F))
reader(4)

print("--- send SET_TIME 2026-01-01 12:00:00 ---")
# 51 解析序: 年,月,日,星期,时,分,秒,时区 (BCD; 2026-01-01=周四→5)
t = bytes([0x26, 0x01, 0x01, 0x05, 0x12, 0x00, 0x00, 0x08])
ser.write(frame(0x81, t))
reader(4)

print("--- send NET_STATUS online ---")
ser.write(frame(0x83, bytes([0x01])))
reader(3)

ser.close()
print("done")
