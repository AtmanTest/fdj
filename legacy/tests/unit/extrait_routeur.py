#!/usr/bin/env python3
"""Extrait le routeur des boutons de main/main.cpp pour le compiler sur le PC.

Le bloc est delimite dans le firmware par les marqueurs @@ROUTEUR_DEBUT@@ et
@@ROUTEUR_FIN@@ : ce qui est teste ici est le VRAI code du firmware, pas une
copie. tests/unit/test_routeur.cpp fournit les stubs (ecran, livre, reglages)
et verifie l'ordre des priorites.

    python3 tests/unit/extrait_routeur.py build/tests/routeur_extrait.inc
"""
import sys

SRC = "main/main.cpp"


def main() -> int:
    sortie = sys.argv[1] if len(sys.argv) > 1 else "build/tests/routeur_extrait.inc"
    src = open(SRC, encoding="utf8").read()
    if "@@ROUTEUR_DEBUT@@" not in src or "@@ROUTEUR_FIN@@" not in src:
        print("marqueurs @@ROUTEUR_DEBUT@@ / @@ROUTEUR_FIN@@ absents de " + SRC)
        return 1
    bloc = src.split("/* @@ROUTEUR_DEBUT@@ */", 1)[1].split("/* @@ROUTEUR_FIN@@ */", 1)[0]
    if "bouton_court" not in bloc or "bouton_long" not in bloc:
        print("le bloc extrait ne ressemble pas au routeur")
        return 1
    open(sortie, "w", encoding="utf8").write(bloc)
    print("routeur extrait : %d lignes -> %s" % (bloc.count("\n"), sortie))
    return 0


if __name__ == "__main__":
    sys.exit(main())
