#!/usr/bin/env python3
"""gen_stats.py — relit le journal de recette et ecrit main/fdj_tests_stats.h.

La page 11 du firmware n'affiche pas des chiffres ecrits a la main : elle
affiche le nombre de verifications qui ont VRAIMENT tourne lors de la
derniere recette, avec sa date. Si le fichier n'existe pas, les compteurs
valent 0 et la page le dit.

    python3 tools/gen_stats.py build/tests/recette.log main/fdj_tests_stats.h
"""
import re
import sys
import datetime

SUITES = {
    "modele": "unit",
    "nav": "unit",
    "parse": "unit",
    "pages": "comp",
    "integration": "integ",
    "fonctionnel": "fonc",
    "defauts": "comp",
}

LIGNE = re.compile(r"^(\S[^\n]*?)\s+(\d+) verifications,\s+(\d+) echec", re.M)


def lit(chemin):
    tot = {"unit": 0, "comp": 0, "integ": 0, "fonc": 0}
    echecs = 0
    vus = set()
    try:
        txt = open(chemin, encoding="utf-8", errors="replace").read()
    except FileNotFoundError:
        return tot, 0, vus
    for nom, n, e in LIGNE.findall(txt):
        cle = nom.strip().lower()
        n, e = int(n), int(e)
        echecs += e
        for motif, famille in SUITES.items():
            if motif in cle:
                tot[famille] += n
                vus.add(motif)
                break
    return tot, echecs, vus


def main():
    log = sys.argv[1] if len(sys.argv) > 1 else "build/tests/recette.log"
    sortie = sys.argv[2] if len(sys.argv) > 2 else "main/fdj_tests_stats.h"
    tot, echecs, vus = lit(log)
    total = sum(tot.values())
    date = datetime.date.today().isoformat()
    contenu = f"""/*  fdj_tests_stats.h — chiffres REELS de la derniere recette.
 *  Regenere par tools/gen_stats.py a chaque lancement de tools/recette.sh.
 *  Ne pas editer a la main : la page 11 affiche exactement ce qui a tourne.
 *  Derniere recette : {date} — {total} verifications, {echecs} echec(s).
 */
#ifndef FDJ_TESTS_STATS_H
#define FDJ_TESTS_STATS_H

#define FDJ_TESTS_UNIT   {tot['unit']}
#define FDJ_TESTS_COMP   {tot['comp']}
#define FDJ_TESTS_INTEG  {tot['integ']}
#define FDJ_TESTS_FONC   {tot['fonc']}
#define FDJ_TESTS_TOTAL  {total}
#define FDJ_TESTS_FAIL   {echecs}
#define FDJ_TESTS_DATE   "{date}"

#endif
"""
    with open(sortie, "w", encoding="utf-8") as f:
        f.write(contenu)
    print(f"{sortie} : unit={tot['unit']} comp={tot['comp']} "
          f"integ={tot['integ']} fonc={tot['fonc']} total={total} "
          f"echecs={echecs} (suites vues : {', '.join(sorted(vus)) or 'aucune'})")
    return 0 if echecs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
