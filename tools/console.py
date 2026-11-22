#!/usr/bin/env python3
"""Envoie une ligne au banc console du robot et affiche la reponse.

    python3 tools/console.py '!wifi' [secondes]
"""
import sys, time, serial

cmd = sys.argv[1] if len(sys.argv) > 1 else "!st"
duree = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0

ser = serial.Serial("/dev/ttyACM0", 115200, timeout=0.3)
time.sleep(0.4)
ser.reset_input_buffer()
ser.write((cmd + "\r\n").encode())
fin = time.time() + duree
buf = b""
while time.time() < fin:
    buf += ser.read(4096)
ser.close()
print(buf.decode(errors="replace"))
