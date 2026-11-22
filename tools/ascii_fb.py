#!/usr/bin/env python3
"""ascii_fb.py - affiche le framebuffer de la carte en ASCII dans le terminal.

Pratique pour verifier un ecran sans passer par une image : une ligne de texte
par 2 lignes de pixels, un caractere par pixel.
    /opt/mino-venv/bin/python ascii_fb.py            (ecran courant)
    /opt/mino-venv/bin/python ascii_fb.py menu 4     (avant de dumper)
"""
import sys
import time

import serial

FB_LEN = 5000


def lire_fb(port="/dev/ttyACM0", avant=()):
    s = serial.Serial(port, 115200, timeout=1)
    s.dtr = False
    s.rts = False
    time.sleep(0.5)
    # la carte redemarre a l'ouverture du port : on attend qu'elle reponde
    for _ in range(20):
        s.reset_input_buffer()
        s.write(b"state\n")
        t0 = time.time()
        txt = ""
        while time.time() - t0 < 2.0:
            txt += s.read(65536).decode("utf-8", "replace")
        if "RAM libre" in txt:
            break
    for c in avant:                       # commandes a passer avant le dump
        s.reset_input_buffer()
        s.write((c + "\n").encode())
        time.sleep(4)
    s.reset_input_buffer()
    s.write(b"fb\n")
    txt = ""
    t0 = time.time()
    while time.time() - t0 < 30:
        txt += s.read(65536).decode("utf-8", "replace")
        if "FB END" in txt:
            break
    s.close()
    fb = bytearray([0xFF] * FB_LEN)
    lu = 0
    for ligne in txt.splitlines():
        i = ligne.find("mino: ")
        if i >= 0:
            ligne = ligne[i + 6:]
        ligne = ligne.strip()
        if ligne.startswith("FB ") and "END" not in ligne:
            p = ligne.split()
            if len(p) >= 3:
                try:
                    d = bytes.fromhex(p[2])
                except ValueError:
                    continue
                off = int(p[1])
                fb[off:off + len(d)] = d
                lu += len(d)
    return bytes(fb), lu


def ascii_art(fb, w=200, h=200):
    out = []
    for y in range(0, h, 2):
        ligne = ""
        for x in range(w):
            b = fb[(y // 8) * (w // 8) + (x // 8)]
            bit = (b >> (7 - (x & 7))) & 1
            ligne += " " if bit else "#"      # 1 = blanc, 0 = noir
        out.append(ligne)
    return "\n".join(out)


if __name__ == "__main__":
    fb, lu = lire_fb(avant=sys.argv[1:])
    print("octets recus : %d/%d" % (lu, FB_LEN), file=sys.stderr)
    print(ascii_art(fb))
