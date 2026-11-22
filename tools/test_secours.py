#!/usr/bin/env python3
"""Exercice du reseau de secours : on envoie un SSID bidon au robot pour voir
s'il bascule seul sur la borne de secours, et sur quelle adresse il arrive."""
import time, urllib.request, serial

ser = serial.Serial("/dev/ttyACM0", 115200, timeout=0.3)
buf = b""
t0 = time.time()
declenche = False
while time.time() - t0 < 75:
    buf += ser.read(4096)
    if not declenche and time.time() - t0 > 18:
        try:
            urllib.request.urlopen(
                "http://192.168.8.210/wifi?ssid=reseau-bidon&pass=12345678", timeout=8)
            print("--- bascule demandee vers un SSID bidon ---")
        except Exception as e:
            print("--- bascule : ", e)
        declenche = True
ser.close()
t = buf.decode(errors="replace")
for l in t.splitlines():
    if any(k in l for k in ("tentative sur", "IP :", "raison 15", "secours", "reseau(x)")):
        print(l)
