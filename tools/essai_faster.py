#!/usr/bin/env python3
"""faster-whisper : le modele reste en memoire entre deux questions.
On lui passe directement le tableau audio (pas de decodeur externe, il est
incompatible avec cette version de Python)."""
import time
import wave

import numpy as np
from faster_whisper import WhisperModel

AMORCE = "Bonjour Mino, comment ca va ? Tu es la ?"


def lit_wav(chemin):
    with wave.open(chemin, "rb") as w:
        sr = w.getframerate()
        brut = w.readframes(w.getnframes())
    return np.frombuffer(brut, dtype=np.int16).astype(np.float32) / 32768.0, sr


for nom in ("tiny", "small"):
    t0 = time.time()
    modele = WhisperModel(nom, device="cpu", compute_type="int8",
                          download_root="/home/taz/mino-app/models")
    print("=== %s : chargement %.1f s" % (nom, time.time() - t0))
    audio, sr = lit_wav("/tmp/vt_0.wav")
    for i in range(3):
        for amorce in (None, AMORCE):
            audio, sr = lit_wav("/tmp/vt_%d.wav" % i)
            t = time.time()
            segs, _ = modele.transcribe(audio, language="fr", beam_size=1,
                                        initial_prompt=amorce,
                                        condition_on_previous_text=False,
                                        vad_filter=False)
            txt = " ".join(s.text.strip() for s in segs).strip()
            print("    %5.2f s  %-11s %r" % (time.time() - t,
                                             "amorce" if amorce else "sans", txt))
    del modele
