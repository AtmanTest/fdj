/* Tests unitaires de l'historique de conversation (main/historique.h).
   Tourne sur le PC :  bash tests/run_unit.sh  */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../main/historique.h"

static int ok = 0, ko = 0;

#define VERIF(cond, nom) do {                       \
    if (cond) { ok++; printf("  ok   %s\n", nom); } \
    else      { ko++; printf("  KO   %s\n", nom); } \
} while (0)

int main(void)
{
    printf("tests unitaires : historique\n");

    hist_t h;
    hist_vide(&h);
    VERIF(h.n == 0, "vide au depart");

    hist_ajoute(&h, "MOI", "bonjour");
    hist_ajoute(&h, "MINO", "salut Taz");
    VERIF(h.n == 2, "deux tours ajoutes");
    VERIF(!hist_est_mino(&h, 0), "le tour 0 est celui de Taz");
    VERIF(hist_est_mino(&h, 1), "le tour 1 est celui de Mino");
    VERIF(strcmp(h.txt[0], "bonjour") == 0, "texte conserve");

    /* entrees vides ignorees */
    hist_ajoute(&h, "MOI", "");
    hist_ajoute(&h, "MOI", NULL);
    VERIF(h.n == 2, "texte vide ou NULL ignore");

    /* debordement : le plus ancien sort, l'ordre est conserve */
    char b[16];
    for (int i = 0; i < HIST_MAX + 3; i++) {
        snprintf(b, sizeof(b), "m%02d", i);
        hist_ajoute(&h, i % 2 ? "MINO" : "MOI", b);
    }
    VERIF(h.n == HIST_MAX, "l'historique plafonne a HIST_MAX");
    VERIF(strcmp(h.txt[h.n - 1], "m34") == 0, "le dernier message est bien le dernier ajoute (35 ajouts, 32 gardes)");
    VERIF(strcmp(h.txt[0], "m03") == 0, "les plus anciens ont ete pousses dehors");
    VERIF(hist_est_mino(&h, h.n - 1) == 0, "role du dernier tour correct");

    /* troncature : jamais plus de HIST_TXT_SZ-1 octets */
    char long_txt[900];
    memset(long_txt, 'a', sizeof(long_txt) - 1);
    long_txt[sizeof(long_txt) - 1] = 0;
    hist_vide(&h);
    hist_ajoute(&h, "MOI", long_txt);
    VERIF(strlen(h.txt[0]) == HIST_TXT_SZ - 1, "un texte trop long est tronque");

    /* indices hors bornes : pas de plantage */
    VERIF(!hist_est_mino(&h, -1) && !hist_est_mino(&h, 99), "indices hors bornes surs");

    printf("resultat : %d ok, %d ko\n", ok, ko);
    return ko ? 1 : 0;
}
