#!/usr/bin/env python3
"""Cherche le meilleur reglage vitesse/precision pour la transcription courte.
Trois phrases connues, quatre reglages, on compare texte et duree."""
import subprocess
import time

W = "/opt/whisper.cpp/build/bin/whisper-cli"
TINY = "/home/taz/mino-app/models/ggml-tiny.bin"
SMALL = "/opt/whisper.cpp/models/ggml-small.bin"
AMORCE = "Bonjour Mino, comment ca va ? Tu es la ?"

REGLAGES = [
    ("tiny defaut", TINY, []),
    ("tiny -bo 5", TINY, ["-bo", "5"]),
    ("tiny -bs 5 -bo 5", TINY, ["-bs", "5", "-bo", "5"]),
    ("tiny -bo 8 -bs 8", TINY, ["-bo", "8", "-bs", "8"]),
    ("small (ref)", SMALL, ["--prompt", AMORCE]),
]

for i in range(3):
    wav = "/tmp/vt_%d.wav" % i
    print("--- phrase %d" % i)
    for nom, modele, extra in REGLAGES:
        s = time.time()
        subprocess.run([W, "-m", modele, "-f", wav, "-l", "fr", "-nt", "-otxt",
                        "-tp", "0", "-tpi", "0", "-nf", "-t", "4",
                        "-of", "/tmp/rt", "--no-prints"] + extra,
                       capture_output=True)
        dt = time.time() - s
        try:
            txt = open("/tmp/rt.txt").read().strip()
        except OSError:
            txt = ""
        print("    %-18s %5.2f s  %r" % (nom, dt, txt))
