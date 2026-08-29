import serial, time

PORT, BAUD = "COM3", 9600

def chk(body):
    c = 0
    for x in body: c ^= x
    return c

def frame(cmd, payload=b""):
    body = bytes([cmd, len(payload)]) + payload
    return b"\xAA\x55" + body + bytes([chk(body)])

def bcd(v):
    return ((v // 10) << 4) | (v % 10)

now = time.localtime()
payload = bytes([
    bcd(now.tm_year % 100), bcd(now.tm_mon), bcd(now.tm_mday),
    bcd(((now.tm_wday + 1) % 7) + 1),  # DS1302 约定: 1=周日..7=周六
    bcd(now.tm_hour), bcd(now.tm_min), bcd(now.tm_sec), 0x08,
])
print("set:", time.strftime("%Y-%m-%d %H:%M:%S", now), "weekday", now.tm_wday + 1)

ser = serial.Serial(PORT, BAUD, timeout=1.0)
ser.reset_input_buffer()
ser.write(frame(0x8F))            # BOOT -> esp_online=1
time.sleep(0.4)
ser.write(frame(0x81, payload))  # SET_TIME 协议序 年/月/日/星期/时/分/秒/时区
print("sent SET_TIME")

buf = b""; end = time.time() + 5
while time.time() < end:
    b = ser.read(1)
    if b:
        buf += b
        if len(buf) > 64: buf = buf[-64:]
print("RX:", buf.hex(' '))
ser.close()
print("done")
