/* Tests unitaires du capteur de gestes (main/geste.h).
   Tourne sur le PC :  bash tests/run_unit.sh

   Le temps est simule : on avance l'horloge par pas de 10 ms, comme le fera
   l'echantillonneur sur la carte. Rien n'est suppose sur la duree reelle. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../main/geste.h"

static int ok = 0, ko = 0;

#define VERIF(cond, nom) do {                       \
    if (cond) { ok++; printf("  ok   %s\n", nom); } \
    else      { ko++; printf("  KO   %s\n", nom); } \
} while (0)

static geste_machine_t m;
static geste_file_t    file;
static int64_t         now_ms;
static uint8_t         niv[2];
static geste_t         ev[64];
static int             nev;

static void remise(void)
{
    now_ms = 1000;
    niv[0] = 1; niv[1] = 1;
    geste_init(&m, NULL, now_ms);
    geste_file_vide(&file);
    nev = 0;
}

/* avance l'horloge, echantillonne, garde les gestes */
static void avance(int64_t ms)
{
    for (int64_t t = 0; t < ms; t += 10) {
        now_ms += 10;
        int n = geste_maj(&m, niv, now_ms, ev + nev, 64 - nev);
        for (int i = 0; i < n; i++) geste_file_pousse(&file, &ev[nev + i]);
        nev += n;
    }
}

/* appui complet : enfonce, tient `tenu_ms`, relache, laisse passer 60 ms */
static void appuie(int b, int64_t tenu_ms)
{
    niv[b] = 0;
    avance(tenu_ms);
    niv[b] = 1;
    avance(60);
}

static int compte(int b, int type)
{
    int c = 0;
    for (int i = 0; i < nev; i++)
        if (ev[i].btn == b && ev[i].type == type) c++;
    return c;
}

static int total(void) { return nev; }

int main(void)
{
    printf("tests unitaires : gestes des deux boutons\n");

    /* ---- 1. appui court simple ------------------------------------- */
    remise();
    appuie(0, 100);
    avance(400);                        /* la fenetre de double est passee */
    VERIF(compte(0, GESTE_COURT) == 1, "un appui court donne un COURT, une seule fois");
    VERIF(compte(0, GESTE_DOUBLE) == 0, "un appui court ne donne pas de DOUBLE");
    VERIF(ev[0].duree_ms >= 90 && ev[0].duree_ms <= 120, "la duree de l'appui est mesuree");

    /* ---- 2. le COURT est retenu le temps de voir un DOUBLE ---------- */
    remise();
    appuie(0, 100);                     /* relache, +60 ms */
    avance(200);                        /* 260 ms : fenetre encore ouverte */
    VERIF(compte(0, GESTE_COURT) == 0, "le COURT n'est pas emis avant la fin de la fenetre");
    avance(100);                        /* 360 ms : fenetre passee */
    VERIF(compte(0, GESTE_COURT) == 1, "le COURT part a la fin de la fenetre");

    /* ---- 3. double appui ------------------------------------------- */
    remise();
    appuie(0, 80);                      /* +60 ms */
    appuie(0, 80);                      /* 2e appui dans les 320 ms */
    avance(400);
    VERIF(compte(0, GESTE_DOUBLE) == 1, "deux appuis rapproches donnent un DOUBLE");
    VERIF(compte(0, GESTE_COURT) == 0, "un DOUBLE ne laisse pas de COURT derriere lui");

    /* ---- 4. appui long --------------------------------------------- */
    remise();
    niv[0] = 0;
    avance(700);
    VERIF(compte(0, GESTE_LONG) == 1, "l'appui long part a 600 ms, sans attendre le relachement");
    niv[0] = 1;
    avance(400);
    VERIF(compte(0, GESTE_COURT) == 0, "un appui long ne produit jamais de COURT");

    /* ---- 5. repetition pendant un maintien -------------------------- */
    remise();
    niv[1] = 0;
    avance(2000);                       /* LONG a 600, puis REPEAT toutes les 400 */
    niv[1] = 1;
    avance(400);
    VERIF(compte(1, GESTE_LONG) == 1, "un seul LONG par maintien");
    VERIF(compte(1, GESTE_REPEAT) == 3, "le maintien repete toutes les 400 ms (3 fois en 2 s)");

    /* ---- 6. anti-rebond -------------------------------------------- */
    remise();
    niv[0] = 0; avance(20); niv[0] = 1; avance(100);
    VERIF(total() == 0, "un parasite de 20 ms ne produit rien");

    /* ---- 7. bouton sans double : il part tout de suite -------------- */
    remise();
    {
        geste_cfg_t c = geste_cfg_defaut();
        c.double_autorise[1] = false;
        geste_init(&m, &c, now_ms);
    }
    niv[1] = 0; avance(100); niv[1] = 1; avance(20);
    VERIF(compte(1, GESTE_COURT) == 1, "rien a attendre sur un bouton sans double");

    /* ---- 8. aucun appui perdu quand l'action bloque ----------------- */
    /* l'echantillonneur n'est pas la tache d'action : les gestes s'empilent
       dans la file pendant que l'ecran se rafraichit (~1,2 s). */
    remise();
    for (int i = 0; i < 10; i++) { appuie(1, 60); avance(400); }   /* espaces de 460 ms */
    VERIF(file.n == 10, "dix appuis avec un consommateur absent : dix gestes en file");
    VERIF(file.perdus == 0, "aucun geste perdu (file non pleine)");
    {
        geste_t g;
        int sortis = 0;
        while (geste_file_sort(&file, &g)) sortis++;
        VERIF(sortis == 10, "la file rend les dix appuis");
    }

    /* ---- 9. debordement compte, jamais silencieux ------------------- */
    remise();
    {
        geste_t g = {0};
        int pousses = 0;
        for (int i = 0; i < GESTE_FILE_NB + 5; i++)
            if (geste_file_pousse(&file, &g)) pousses++;
        VERIF(pousses == GESTE_FILE_NB, "la file s'arrete a GESTE_FILE_NB");
        VERIF(file.perdus == 5, "les gestes en trop sont comptes, pas ignores en silence");
    }

    /* ---- 10. les deux boutons sont independants -------------------- */
    remise();
    niv[0] = 0; niv[1] = 0;
    avance(700);
    niv[0] = 1; niv[1] = 1;
    avance(400);
    VERIF(compte(0, GESTE_LONG) == 1 && compte(1, GESTE_LONG) == 1,
          "les deux boutons tenus ensemble donnent deux LONG, pas un DOUBLE");

    printf("resultat : %d ok, %d ko\n", ok, ko);
    return ko ? 1 : 0;
}
