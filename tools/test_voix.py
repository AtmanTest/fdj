#!/usr/bin/env python3
"""Reproduit une vraie demande, sans micro : on declenche une prise sur la carte,
on la transcrit, on passe par le serveur du robot (comme le cerveau) et on lui
fait dire la reponse. Chrono de chaque etape.
A lancer cerveau arrete (le script prend le port serie)."""
import os
import sys
import time

sys.path.insert(0, "/home/taz/mino-app")
os.environ["PATH"] = os.environ.get("PATH", "") + ":/home/taz/.hermes-usb/node/bin"

import cerveau  # noqa: E402

link = cerveau.Link()
debut = time.time()

link.send("wake")
while True:
    ligne = link.readline()
    if ligne and "MINO_VOIX" in ligne:
        break
    if time.time() - debut > 15:
        print("la carte n'a pas annonce sa prise")
        sys.exit(1)
t_prise = time.time()
print("1) prise prete (comme le bouton)        : %.1f s" % (t_prise - debut))

chemin = link.fetch_recording()
print("2) prise recuperee                      : %.2f s" % (time.time() - t_prise))

t = time.time()
txt = cerveau.transcribe(chemin)
print("3) texte entendu : %-38r : %.2f s" % (txt, time.time() - t))

t = time.time()
emo, dit = link.ask_robot(txt or "bonjour, tu es la ?")
print("4) le robot reflechit et repond          : %.2f s" % (time.time() - t))
print("   humeur=%s  dit=%r" % (emo, dit))

if dit:
    t = time.time()
    pcm = cerveau.tts_pcm(dit)
    print("5) voix fabriquee                        : %.2f s (%.1f s de parole)"
          % (time.time() - t, len(pcm) / 32000.0))
    link.play(pcm)
    print("6) il a parle                            : total %.1f s" % (time.time() - debut))
