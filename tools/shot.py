#!/usr/bin/env python3
"""Capture l'ecran du robot en PNG (vraie resolution 200x200, x4).

    python3 tools/shot.py sortie.png [hote]
"""
import sys, urllib.request, subprocess, tempfile, os

hote = sys.argv[2] if len(sys.argv) > 2 else "192.168.8.210"
sortie = sys.argv[1] if len(sys.argv) > 1 else "ecran.png"

hexa = urllib.request.urlopen(f"http://{hote}/ecran?raw=1", timeout=10).read().decode()
fb = bytes.fromhex(hexa)
assert len(fb) == 5000, len(fb)

lignes = []
for y in range(200):
    ligne = bytearray()
    for x0 in range(0, 200, 8):
        o = fb[y * 25 + (x0 >> 3)]
        ligne.append(~o & 0xFF)          # PBM : 1 = noir, le fb est inverse
    lignes.append(bytes(ligne))

pbm = b"P4\n200 200\n" + b"".join(lignes)
tmp = tempfile.NamedTemporaryFile(suffix=".pbm", delete=False)
tmp.write(pbm)
tmp.close()
subprocess.run(["convert", tmp.name, "-scale", "400x400", sortie], check=True)
os.unlink(tmp.name)
print(sortie)
