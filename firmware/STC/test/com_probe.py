import serial, serial.tools.list_ports as p
print("pyserial OK")
for x in p.comports():
    print(x.device, "|", x.description)
