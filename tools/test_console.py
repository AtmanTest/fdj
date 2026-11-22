#!/usr/bin/env python3
"""Verifie que la console du robot repond encore : envoie une commande et
affiche ce qui revient. Usage : python test_console.py "state" [secondes]"""
import sys
import time

import serial


def attend_pret(s, limite=25.0):
    """la carte redemarre quand on ouvre le port : on attend qu'elle parle"""
    fin = time.time() + limite
    tampon = b""
    while time.time() < fin:
        s.write(b"state\n")
        s.flush()
        t = time.time() + 1.5
        while time.time() < t:
            n = s.in_waiting
            if n:
                tampon += s.read(n)
                if b"RAM libre" in tampon or b"emotion=" in tampon or b"stade=" in tampon:
                    return tampon
            time.sleep(0.05)
    return tampon


def main():
    commande = sys.argv[1] if len(sys.argv) > 1 else "state"
    attente = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
    s = serial.Serial("/dev/ttyACM0", 115200, timeout=0.3)
    s.dtr = False
    s.rts = False
    try:
        avant = attend_pret(s)
        print("--- la carte repond au demarrage :", b"oui" if avant else "NON")
        s.reset_input_buffer()
        print("--- envoi de :", commande)
        s.write((commande + "\n").encode())
        s.flush()
        fin = time.time() + attente
        vu = b""
        while time.time() < fin:
            n = s.in_waiting
            if n:
                vu += s.read(n)
            time.sleep(0.05)
        texte = vu.decode("utf-8", "replace")
        print("--- ce qui revient :")
        for ligne in texte.splitlines():
            if ligne.strip():
                print("   ", ligne[:200])
        if not texte.strip():
            print("    (RIEN : la console ne repond pas)")
    finally:
        s.close()


main()
