#!/bin/bash
# Recette de test du firmware FDJ — sur le PC, sans carte.
#
#   tests/run_unit.sh            toutes les suites
#   tests/run_unit.sh modele     une seule suite
#
# Chaque suite affiche "<nom>  N verifications, M echec(s)" ; le script rend le
# nombre de suites en echec. tools/recette.sh relit ces chiffres pour remplir
# la page 11 du firmware : ce qui est affiche sur la carte a vraiment tourne.
set -u
cd "$(dirname "$0")/.." || exit 2
mkdir -p build/tests
rc=0
filtre="${1:-}"

CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I main -I tests/unit -I /usr/include/cjson"
LIBS="-lcjson -lm"

suite() {   # suite <nom> <binaire> <sources...>
    local nom="$1" bin="$2"; shift 2
    if [ -n "$filtre" ] && [ "$filtre" != "$nom" ]; then return 0; fi
    echo "— $nom"
    if ! g++ $CXXFLAGS -o "build/tests/$bin" "$@" $LIBS; then
        echo "  COMPILATION ECHOUEE"
        rc=$((rc+1))
        return 0
    fi
    "./build/tests/$bin" || rc=$((rc+1))
}

# 1. modele metier : argent, profils, hasard, Poisson, loto, persistance
suite modele test_modele \
    tests/unit/test_modele.cpp main/fdj_model.cpp

# 2. navigation : la table des gestes, sans boutons ni carte
suite nav test_nav \
    tests/unit/test_nav.cpp main/fdj_nav.cpp

# 3. parseurs : de vraies reponses serveur, sans reseau
suite parse test_parse \
    tests/unit/test_parse.cpp main/fdj_parse.cpp main/fdj_model.cpp

# 4. pages : chaque page rendue pour de vrai (composant)
suite pages test_pages \
    tests/unit/test_pages.cpp main/fdj_ui.cpp main/fdj_model.cpp \
    main/art.cpp main/font5.cpp

# 5. integration : nav + argent + persistance ensemble
suite integration test_integration \
    tests/unit/test_integration.cpp main/fdj_model.cpp main/fdj_nav.cpp \
    main/fdj_ui.cpp main/art.cpp main/font5.cpp

# 6. fonctionnel : la demo de bout en bout, sur les fixtures serveur
suite fonctionnel test_fonctionnel \
    tests/unit/test_fonctionnel.cpp main/fdj_model.cpp main/fdj_nav.cpp \
    main/fdj_parse.cpp main/fdj_ui.cpp main/art.cpp main/font5.cpp

echo
if [ $rc -eq 0 ]; then echo "unitaires : tout bon"; else echo "unitaires : $rc suite(s) en echec"; fi
exit $rc
