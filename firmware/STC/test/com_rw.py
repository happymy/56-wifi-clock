import serial, time, sys

PORT = "COM3"
BAUD = 9600

def chk(frame):  # cmd,len,payload...
    return frame[0] ^ frame[1] ^ (sum(frame[2:]) & 0xFF)

def frame(cmd, payload=b""):
    body = bytes([cmd, len(payload)]) + payload
    body += bytes([body[0] ^ body[1] ^ (sum(payload) & 0xFF)])
    return b"\xAA\x55" + body

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
# ds1302: sec,min,hr,week,day,mon,year
t = bytes([0x00, 0x00, 0x12, 0x04, 0x01, 0x01, 0x26, 0x00])
ser.write(frame(0x81, t))
reader(4)

print("--- send NET_STATUS online ---")
ser.write(frame(0x83, bytes([0x01])))
reader(3)

ser.close()
print("done")
