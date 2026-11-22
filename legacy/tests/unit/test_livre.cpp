/* Tests unitaires du lecteur de livres (main/livre.h).
   Tourne sur le PC :  bash tests/run_unit.sh  */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../main/livre.h"
#include "../../main/font5.h"

static int ok = 0, ko = 0;

#define VERIF(cond, nom) do {                       \
    if (cond) { ok++; printf("  ok   %s\n", nom); } \
    else      { ko++; printf("  KO   %s\n", nom); } \
} while (0)

static const char *CHEMIN = "build/tests/livre_test.txt";

static void ecrit(const char *txt)
{
    FILE *f = fopen(CHEMIN, "wb");
    if (!f) { perror(CHEMIN); exit(1); }
    fputs(txt, f);
    fclose(f);
}

int main(void)
{
    printf("tests unitaires : livre\n");

    /* ---- encodage ---- */
    {
        uint8_t buf[64];
        const char *src = "éàçùê";
        int n = livre_encode(src, buf, (int)sizeof(buf));
        VERIF(n == 5, "cinq accents = cinq caracteres affiches");
        int distincts = 1;
        for (int i = 1; i < n; i++) if (buf[i] == buf[0]) distincts = 0;
        VERIF(distincts, "les accents ne retombent pas tous sur le meme glyphe");
        for (int i = 0; i < n; i++) VERIF(buf[i] != 0, "glyphe connu");
    }
    {
        uint8_t buf[64];
        int n = livre_encode("a  \t b", buf, (int)sizeof(buf));
        VERIF(n == 3, "espaces multiples compresses");
        n = livre_encode("fin\n\n\ndebut", buf, (int)sizeof(buf));
        VERIF(n == 9, "trois sauts de ligne = un seul paragraphe");
        n = livre_encode("œuf", buf, (int)sizeof(buf));
        VERIF(n == 4, "o-e-ligature rendue en deux lettres");
        n = livre_encode("«oui»", buf, (int)sizeof(buf));
        VERIF(n == 5 && buf[0] != font5_code('?'), "guillemets francais connus");
    }

    /* ---- pagination ---- */
    {
        ecrit("alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima");
        livre_t l;
        VERIF(livre_charge(&l, CHEMIN, 10, 2), "livre charge");
        VERIF(l.nlignes > 0 && l.nlignes <= 12, "nombre de lignes plausible");
        int trop_long = 0;
        for (int i = 0; i < l.nlignes; i++) {
            int n = 0;
            livre_ligne(&l, i, &n);
            if (n > 10) trop_long++;
        }
        VERIF(trop_long == 0, "aucune ligne plus large que la colonne");
        VERIF(livre_pages(&l) == (l.nlignes + 1) / 2, "pages = lignes / 2");
        VERIF(livre_pct(&l) == 0, "premiere page = 0 %");
        VERIF(livre_avance(&l, 1), "on peut tourner");
        VERIF(l.page == 1, "page 2 apres un tour");
        livre_va(&l, 99999);
        VERIF(l.page == livre_pages(&l) - 1, "la derniere page borne la fin");
        VERIF(!livre_avance(&l, 1), "tourner apres la fin ne fait rien");
        VERIF(livre_pct(&l) == 100, "derniere page = 100 %");
        livre_va(&l, -5);
        VERIF(l.page == 0, "page 1 borne le debut");

        /* le texte se retrouve en entier : autant de mots qu'au depart */
        int mots = 0;
        for (int p = 0; p < livre_pages(&l); p++) {
            for (int k = 0; k < l.rows; k++) {
                int n = 0;
                const uint8_t *s = livre_ligne(&l, p * l.rows + k, &n);
                if (!s) continue;
                int dans_mot = 0;
                for (int i = 0; i < n; i++) {
                    if (s[i] != font5_code(' ')) { if (!dans_mot) { mots++; dans_mot = 1; } }
                    else dans_mot = 0;
                }
            }
        }
        VERIF(mots == 12, "les 12 mots du fichier sont tous affiches");
        livre_ferme(&l);
    }

    /* ---- mot plus long que la ligne ---- */
    {
        ecrit("abcdefghijklmnop fin");
        livre_t l;
        VERIF(livre_charge(&l, CHEMIN, 8, 3), "livre charge (mot long)");
        int n0 = 0;
        livre_ligne(&l, 0, &n0);
        VERIF(n0 == 8, "un mot trop long est coupe net a la colonne");
        livre_ferme(&l);
    }

    /* ---- saut de paragraphe : une ligne vide, pas une ligne collee ---- */
    {
        ecrit("un\n\ndeux\n\ntrois");
        livre_t l;
        VERIF(livre_charge(&l, CHEMIN, 10, 4), "livre charge (paragraphes)");
        VERIF(l.nlignes == 5, "cinq lignes : texte, vide, texte, vide, texte");
        int n1 = 0, n2 = 0;
        livre_ligne(&l, 1, &n1);
        livre_ligne(&l, 3, &n2);
        VERIF(n1 == 0 && n2 == 0, "les lignes 2 et 4 sont vides");
        livre_ferme(&l);
    }

    /* ---- fichiers limites ---- */
    {
        livre_t l;
        VERIF(!livre_charge(&l, "build/tests/inexistant.txt", 10, 2), "fichier absent refuse");
        ecrit("");
        VERIF(!livre_charge(&l, CHEMIN, 10, 2), "fichier vide refuse");
        ecrit("mot");
        VERIF(livre_charge(&l, CHEMIN, 10, 2), "un seul mot suffit");
        VERIF(livre_pages(&l) == 1, "une page");
        livre_ferme(&l);
    }

    remove(CHEMIN);
    printf("%d ok, %d ko\n", ok, ko);
    return ko ? 1 : 0;
}
