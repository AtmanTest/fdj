#!/bin/bash
# recette.sh — la recette complete, dans l'ordre, et elle s'arrete au premier
# echec. C'est ce script que cite la page 11 du firmware.
#
#   1. tests unitaires / composant / integration / fonctionnel (sur le PC)
#   2. ecriture des chiffres reels dans main/fdj_tests_stats.h
#   3. recompilation du firmware avec ces chiffres
#   4. rendu des 19 pages en PNG (verification a l'oeil)
#   5. journal docs/RECETTE.md
#
#   tools/recette.sh            tout
#   tools/recette.sh --sans-build   tests + PNG seulement
set -u
cd "$(dirname "$0")/.." || exit 2
mkdir -p build/tests build/apercu docs
log=build/tests/recette.log
date_humaine=$(date '+%Y-%m-%d %H:%M')
rc=0

echo "== 1/4 tests =="
./tests/run_unit.sh 2>&1 | tee "$log"
[ "${PIPESTATUS[0]}" -eq 0 ] || rc=1

echo "== 2/4 chiffres reels -> main/fdj_tests_stats.h =="
python3 tools/gen_stats.py "$log" main/fdj_tests_stats.h || rc=1

echo "== 3/4 firmware =="
if [ "${1:-}" != "--sans-build" ]; then
    if env -i HOME="$HOME" PATH=/usr/local/bin:/usr/bin:/bin IDF_TOOLS_PATH="$HOME/esp/tools" \
        bash -c '. '"$HOME"'/esp/esp-idf/export.sh >/dev/null 2>&1; idf.py build' > build/tests/build.log 2>&1; then
        echo "firmware : $(ls -l build/mino.bin | awk '{print $5}') octets"
    else
        echo "FIRMWARE : ECHEC (voir build/tests/build.log)"
        rc=1
    fi
else
    echo "(build saute)"
fi

echo "== 4/4 apercu des 19 pages =="
g++ -std=c++17 -O2 -w -I main -I /usr/include/cjson -o build/tests/apercu_fdj \
    tools/apercu_fdj.cpp main/fdj_ui.cpp main/fdj_parse.cpp main/fdj_model.cpp \
    main/art.cpp main/font5.cpp -lcjson -lm || rc=1
if [ -x build/tests/apercu_fdj ]; then
    ./build/tests/apercu_fdj build/apercu > /dev/null || rc=1
    for f in build/apercu/*.fb; do
        python3 tools/fb2png.py "$f" "${f%.fb}.png" 3 > /dev/null
    done
    echo "PNG : build/apercu/p01.png ... p19.png"
fi

# journal
{
    echo "# Recette — BATMino x FDJ"
    echo
    echo "Lancee le $date_humaine, commit $(git rev-parse --short HEAD 2>/dev/null || echo 'hors git')."
    echo
    echo "## Chiffres reels"
    echo
    grep -E "verifications" "$log" | sed 's/^/    /'
    echo
    echo "Total : $(grep -oE 'FDJ_TESTS_TOTAL  [0-9]+' main/fdj_tests_stats.h | awk '{print $2}') verifications."
    echo
    echo "## Resultat"
    echo
    if [ $rc -eq 0 ]; then echo "TOUT PASSE."; else echo "ECHEC — voir $log"; fi
} > docs/RECETTE.md

exit $rc
