#!/usr/bin/env python3
"""Envoie le jeton de rafraichissement Microsoft au robot (POST /caltoken).

    python3 tools/cal_installe.py [ip]

Lit ~/.mino-outlook.json (ecrit par outils/outlook_token.py) et pousse le
refresh_token dans le robot, puis verifie /state.
"""
import json
import os
import sys
import urllib.request

ROBOT = sys.argv[1] if len(sys.argv) > 1 else "192.168.8.210"
SRC = os.path.expanduser("~/.mino-outlook.json")


def main():
    if not os.path.exists(SRC):
        print("pas de jeton dans", SRC, "- lance d'abord tools/outlook_token.py")
        return 1
    with open(SRC) as f:
        j = json.load(f)
    rt = j.get("refresh_token")
    if not rt:
        print("le fichier ne contient pas de refresh_token")
        return 1
    req = urllib.request.Request(f"http://{ROBOT}/caltoken", data=rt.encode(),
                                 method="POST",
                                 headers={"Content-Type": "text/plain"})
    with urllib.request.urlopen(req, timeout=30) as r:
        print("robot :", r.read().decode(errors="replace").strip())
    with urllib.request.urlopen(f"http://{ROBOT}/state", timeout=20) as r:
        s = json.load(r)
    print("cal_titre=%r cal_heure=%r cal_dans=%s" %
          (s.get("cal_titre"), s.get("cal_heure"), s.get("cal_dans")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
