#!/usr/bin/env python3
"""Chronometre chaque etape de la voix, sans micro et sans rien casser.
A lancer cerveau arrete (le script prend le port serie)."""
import os
import subprocess
import sys
import time

sys.path.insert(0, "/home/taz/mino-app")
os.environ["PATH"] = os.environ.get("PATH", "") + ":/home/taz/.hermes-usb/node/bin"

import cerveau  # noqa: E402

W = "/opt/whisper.cpp/build/bin/whisper-cli"
AMORCE = "Bonjour Mino, comment ca va ? Tu es la ?"

link = cerveau.Link()

print("1) declenchement d'une prise (comme un appui long) ...")
t0 = time.time()
link.send("wake")
vu = ""
fin = time.time() + 12
while time.time() < fin:
    l = link.readline()
    if l and "MINO_VOIX" in l:
        vu = l.strip()
        break
print("   la carte annonce sa prise apres %.2f s : %s" % (time.time() - t0, vu[-60:]))

t0 = time.time()
wav = link.fetch_recording()
dt_fetch = time.time() - t0
taille = os.path.getsize(wav) if wav and os.path.exists(wav) else 0
print("2) recuperation de la prise : %.2f s pour %d octets (%.2f s d'audio)"
      % (dt_fetch, taille, taille / 32000.0))

if not wav:
    print("   pas de prise, j'arrete")
    sys.exit(1)

for nom, modele, amorce in (("tiny", "/tmp/ggml-tiny.bin", []),
                            ("small", "/opt/whisper.cpp/models/ggml-small.bin", ["--prompt", AMORCE])):
    t0 = time.time()
    subprocess.run([W, "-m", modele, "-f", wav, "-l", "fr", "-nt", "-otxt",
                    "-tp", "0", "-tpi", "0", "-nf", "-t", "4",
                    "-of", "/tmp/mes_" + nom, "--no-prints"] + amorce,
                   capture_output=True)
    dt = time.time() - t0
    try:
        txt = open("/tmp/mes_" + nom + ".txt").read().strip()
    except OSError:
        txt = ""
    print("3) transcription %-5s : %.2f s -> %r" % (nom, dt, txt))

t0 = time.time()
pcm = cerveau.tts_pcm("Coucou, je t'entends tres bien, tu veux qu'on joue ?")
print("4) voix synthetisee : %.2f s pour %.1f s d'audio" % (time.time() - t0, len(pcm) / 32000.0))
