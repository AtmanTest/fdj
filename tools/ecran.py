#!/usr/bin/env python3
"""ecran.py - photo de l'ecran du robot : attend qu'il soit pret, envoie des
commandes si besoin, puis ecrit le framebuffer en PNG.

    /opt/mino-venv/bin/python ecran.py sortie.png
    /opt/mino-venv/bin/python ecran.py sortie.png "ui 0" "menu 4"
"""
import sys

import ascii_fb
import talk

if __name__ == "__main__":
    sortie = sys.argv[1] if len(sys.argv) > 1 else "ecran.png"
    fb, lu = ascii_fb.lire_fb(avant=tuple(sys.argv[2:]))
    if lu < 5000:
        print("ATTENTION : %d/5000 octets recus" % lu, file=sys.stderr)
    w, h = talk.write_png(sortie, fb, scale=3)
    print("%s ecrit (%dx%d), %d octets" % (sortie, w, h, lu))
