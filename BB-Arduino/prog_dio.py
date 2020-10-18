#!/usr/bin/env python
# DEPENDS: python3-serial

import serial, sys, os
from time import sleep

serialPort = sys.argv[1] if len(sys.argv) > 1 else "/dev/serial/by-id/usb-Arduino_LLC_Arduino_Micro-if00"
hexfile    = sys.argv[2] if len(sys.argv) > 2 else "bb400r2_dio.ino.hex"
bl_file    = sys.argv[3] if len(sys.argv) > 3 else "caterina_2341_0037_noblink.hex"

print("Programming bootloader:")
cmd = "sudo avrdude -C/usr/share/brainboxes/bb-arduino/avrdude.bb400.conf -cbb400dio -patmega32u4 -u -Uflash:w:" + bl_file + ":i -Uefuse:w:0xcb:m -Uhfuse:w:0xd8:m -Ulfuse:w:0xff:m"
print("$ "+cmd)
e = (os.system(cmd) >> 8) & 0xff
print("Exit code="+str(e))
if e != 0:
	sys.exit(e)

sleep(1.0) # allow time for the Arduino to start up

# Find the name of the Arduino serial port interface
try:
    # assume serialPort is a symlink, read where it points to
    t = os.readlink(serialPort)
    # if we got here without an exception, it was indeed a link: make it an absolute path
    serialPort = os.path.join(os.path.dirname(serialPort), t)
except OSError:
    # we get this exception if the path is not a link
    pass
except FileNotFoundError:
    print(repr(serialPort) + " does not exist!")
    sys.exit(1)

print("Resetting Arduino on "+serialPort)
ser = serial.Serial(
    port=serialPort,
    baudrate=1200,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS
)
ser.setRTS(True)
#ser.setDTR(False)
ser.isOpen()
ser.close()             # close port

sleep(1.0) # allow time for the Arduino to go into the bootloader
cmd = "sudo avrdude -C/usr/share/brainboxes/bb-arduino/avrdude.bb400.conf -patmega32u4 -cavr109 -P%s -b57600 -D -Uflash:w:%s:i" % (serialPort, hexfile)
print("$ "+cmd)
e = (os.system(cmd) >> 8) & 0xff
print("Exit code="+str(e))
sys.exit(e)

