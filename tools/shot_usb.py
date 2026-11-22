#!/usr/bin/env python3
"""Capture l'ecran du BATMino par l'USB (commande console 'fb') et ecrit un PNG.

    python3 tools/shot_usb.py sortie.png [/dev/ttyACM1] ["ui 3"]

Ouvre le port sans toucher DTR/RTS, envoie la commande eventuelle puis 'fb',
lit les lignes "FB <offset> <hex>" et reconstruit l'image 200x200 (x4).
Aucun reseau necessaire : c'est le seul moyen de voir la dalle quand le PC
n'est pas sur le meme wifi que la carte.
"""
import re
import struct
import sys
import time
import zlib

import serial

PORT = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM1"
OUT = sys.argv[1] if len(sys.argv) > 1 else "ecran.png"
CMD = sys.argv[3] if len(sys.argv) > 3 else ""

FB_LEN = 200 * 25


def png_gray(path, rows, w, h, scale=4):
    raw = bytearray()
    for y in range(h):
        for _ in range(scale):
            raw.append(0)                      # filtre 0
            for x in range(w):
                v = rows[y][x]
                raw.extend(bytes((v,)) * scale)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    time.sleep(0.4)
    ser.reset_input_buffer()
    if CMD:
        ser.write((CMD + "\n").encode())
        time.sleep(1.5)
        ser.reset_input_buffer()
    ser.write(b"fb\n")
    fb = bytearray(FB_LEN)
    vu = 0
    fin = time.time() + 25
    buf = b""
    while time.time() < fin:
        buf += ser.read(4096)
        while b"\n" in buf:
            ligne, buf = buf.split(b"\n", 1)
            m = re.search(rb"FB (\d+) ([0-9a-f]+)", ligne)
            if m:
                off = int(m.group(1))
                data = bytes.fromhex(m.group(2).decode())
                fb[off:off + len(data)] = data
                vu += len(data)
            elif b"FB END" in ligne:
                fin = 0
                break
    ser.close()
    print(f"{vu} octets de framebuffer lus sur {PORT}")
    if vu < FB_LEN:
        print("ATTENTION : framebuffer incomplet")
    # 1 = blanc, 0 = noir (bit 1 = blanc dans le BSP)
    rows = []
    for y in range(200):
        row = []
        for x in range(200):
            octet = fb[y * 25 + (x >> 3)]
            bit = (octet >> (7 - (x & 7))) & 1
            row.append(255 if bit else 0)
        rows.append(row)
    png_gray(OUT, rows, 200, 200)
    print("ecrit :", OUT)


if __name__ == "__main__":
    main()
