#!/usr/bin/env python3
"""Valide la transcription en vitesse (modele minuscule) sur des phrases connues :
on fabrique l'audio par synthese, donc on sait ce qui a ete dit."""
import os
import subprocess
import sys
import time

sys.path.insert(0, "/home/taz/mino-app")
os.environ["PATH"] = os.environ.get("PATH", "") + ":/home/taz/.hermes-usb/node/bin"

import cerveau  # noqa: E402

PHRASES = [
    "Hello, est-ce que tu m'entends ?",
    "Batman, tu es content de me voir ? On joue ensemble ?",
    "Quelle heure il est ?",
]

for i, phrase in enumerate(PHRASES):
    mp3 = "/tmp/vt_%d.mp3" % i
    wav = "/tmp/vt_%d.wav" % i
    subprocess.run([cerveau.TTS, "--voice", cerveau.VOICE, "--text", phrase,
                    "--write-media", mp3], capture_output=True)
    subprocess.run(["ffmpeg", "-v", "quiet", "-y", "-i", mp3, "-ar", "16000",
                    "-ac", "1", "-c:a", "pcm_s16le", wav], capture_output=True)
    t = time.time()
    txt = cerveau.transcribe(wav)
    print("%.2f s | dit : %r" % (time.time() - t, phrase))
    print("       | lu  : %r  (retenu : %s)" % (txt, cerveau._utilisable(txt)))
