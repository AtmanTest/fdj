#!/usr/bin/env python3
"""dump_ecran.py - dump du framebuffer REEL de la carte, en ASCII.

    python3 dump_ecran.py 3          (force la page 3 puis affiche l'ecran)
    python3 dump_ecran.py 3 /tmp/x.pgm

Ouvre le port serie (ce qui redemarre la carte), attend qu'elle reponde,
force la page demandee, puis demande le framebuffer.
"""
import sys, time

import serial

FB_LEN = 5000
PORT = "/dev/ttyACM0"


def dump(page=None):
    s = serial.Serial(PORT, 115200, timeout=1)
    s.dtr = False
    s.rts = False
    time.sleep(0.5)
    pret = False
    for _ in range(40):
        s.reset_input_buffer()
        s.write(b"state\n")
        t0 = time.time()
        txt = ""
        while time.time() - t0 < 2.0:
            txt += s.read(65536).decode("utf-8", "replace")
        if "RAM libre" in txt:
            pret = True
            break
    if not pret:
        print("la carte n'a pas repondu")
        s.close()
        return None
    if page is not None:
        s.reset_input_buffer()
        s.write(("ui %d\n" % page).encode())
        time.sleep(6)
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
    print("octets recus : %d / %d" % (lu, FB_LEN))
    return bytes(fb)


def ascii_fb(fb):
    lignes = []
    for y in range(200):
        l = ""
        for x in range(200):
            bit = (fb[y * 25 + (x >> 3)] >> (7 - (x & 7))) & 1
            l += "." if bit else "#"      # 1 = blanc, 0 = noir
        lignes.append(l)
    return lignes


if __name__ == "__main__":
    page = int(sys.argv[1]) if len(sys.argv) > 1 else None
    fb = dump(page)
    if fb:
        lignes = ascii_fb(fb)
        for i, l in enumerate(lignes):
            print("%3d %s" % (i, l))
        if len(sys.argv) > 2:
            with open(sys.argv[2], "wb") as o:
                o.write(b"P5\n200 200\n255\n")
                for y in range(200):
                    for x in range(200):
                        bit = (fb[y * 25 + (x >> 3)] >> (7 - (x & 7))) & 1
                        o.write(bytes([255 if bit else 0]))
            print("ecrit " + sys.argv[2])
