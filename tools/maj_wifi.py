#!/usr/bin/env python3
"""Mise a jour du robot par wifi : POST /maj avec build/mino.bin.

    python3 tools/maj_wifi.py            (192.168.8.210 par defaut)
    python3 tools/maj_wifi.py 192.168.8.212

Le robot ecrit l'image dans l'emplacement OTA libre, redemarre dessus et
revient en ligne. Si l'image est refusee, l'ancienne version continue de
demarrer : le cable USB n'est plus necessaire pour les mises a jour.
"""
import json
import os
import sys
import time
import urllib.request

ROBOT = sys.argv[1] if len(sys.argv) > 1 else "192.168.8.210"
BIN = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "build", "mino.bin")


def etat(ip, timeout=4):
    with urllib.request.urlopen(f"http://{ip}/state", timeout=timeout) as r:
        return json.load(r)


def main():
    if not os.path.exists(BIN):
        print("binaire absent :", BIN, "- lance idf.py build")
        return 1
    taille = os.path.getsize(BIN)
    print("envoi de %s (%.0f ko) vers %s" % (BIN, taille / 1024.0, ROBOT))
    with open(BIN, "rb") as f:
        data = f.read()
    req = urllib.request.Request(f"http://{ROBOT}/maj", data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            rep = json.load(r)
    except Exception as e:
        print("echec de l'envoi :", e)
        return 1
    print("reponse :", rep, "(%.1f s)" % (time.time() - t0))
    if not rep.get("ok"):
        return 1

    print("attente du redemarrage...")
    for _ in range(60):
        time.sleep(2)
        try:
            s = etat(ROBOT, timeout=3)
        except Exception:
            continue
        if s.get("uptime_s", 9999) < 120:
            print("robot revenu en ligne, uptime %s s, wifi %s, ip %s" %
                  (s.get("uptime_s"), s.get("wifi_ssid"), ROBOT))
            return 0
    print("robot pas revenu en 2 min : verifie l'alimentation ou rebranche l'USB")
    return 1


if __name__ == "__main__":
    sys.exit(main())
