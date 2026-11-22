#!/usr/bin/env python3
"""Reproduit le vrai enchainement : bouton -> prise -> texte -> reponse.
Et affiche TOUT ce que la carte dit pendant la reflexion, pour voir ou ca bloque.
A lancer cerveau arrete."""
import os
import sys
import time

sys.path.insert(0, "/home/taz/mino-app")
os.environ["PATH"] = os.environ.get("PATH", "") + ":/home/taz/.hermes-usb/node/bin"

import cerveau  # noqa: E402

link = cerveau.Link()
print("1) on appuie sur le bouton (wake) ...")
link.send("wake")
debut = time.time()
while time.time() - debut < 15:
    l = link.readline()
    if l and "MINO_VOIX" in l:
        print("   prise prete en %.1f s" % (time.time() - debut))
        break
else:
    print("   pas d'annonce de prise")
    sys.exit(1)

chemin = link.fetch_recording()
print("2) prise recuperee : %s" % chemin)

print("3) on envoie un texte comme le cerveau, on regarde tout ce qui revient :")
link.s.reset_input_buffer()
link.send("bonjour batman tu es la ?")
fin = time.time() + 40
repondu = False
while time.time() < fin:
    l = link.readline()
    if not l:
        continue
    if "mino:" in l:
        print("   %s" % l.split("mino:")[-1].strip()[:160])
    if "Batmino [" in l:
        repondu = True
        break
print("4) la carte a repondu : %s (en %.1f s)" % (repondu, time.time() - fin + 40))

print("5) on affiche la reponse a l'ecran (comme le cerveau) :")
link.send("repond COUCOU PETIT BATMAN, JE SUIS LA !")
time.sleep(1)
for l in [link.readline()]:
    pass
print("   commande envoyee")
