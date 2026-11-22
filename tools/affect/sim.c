/* sim.c - banc d'essai du moteur affectif sur l'ordinateur, avant la carte.
 * Compile avec affect.c ; aucune dependance ESP-IDF.
 *
 *   gcc -O2 -o sim sim.c affect.c -lm && ./sim
 */
#include <stdio.h>
#include <string.h>
#include "affect.h"

typedef struct { float t; appraisal_t a; const char *quoi; } evt_t;

static void entete(const char *titre)
{
    printf("\n=== %s ===\n", titre);
    printf("   t(s)   val    act    dom   emotion            int   cort  oxy   fat  trust\n");
}

static void ligne(affect_t *e, float t)
{
    printf(" %6.0f  %+.2f  %+.2f  %+.2f  %-18s %3.0f%%  %.2f  %.2f  %.2f  %.2f\n",
           (double)t, (double)e->v, (double)e->a, (double)e->d,
           affect_label_name(e), (double)(e->intensity * 100.0f),
           (double)e->cort, (double)e->oxy, (double)e->fatigue, (double)e->trust);
}

/* duree totale, pas d'affichage, liste d'evenements tries */
static void scenario(const char *titre, affect_t *e, float duree, const evt_t *ev, int nev,
                     float pas_aff, float dt)
{
    entete(titre);
    int k = 0;
    float prochain = 0.0f;
    for (float t = 0.0f; t <= duree; t += dt) {
        while (k < nev && ev[k].t <= t) {
            affect_tick(e, &ev[k].a, dt);
            printf("   -> t=%4.0f  EVENEMENT : %s\n", (double)ev[k].t, ev[k].quoi);
            k++;
        }
        affect_tick(e, NULL, dt);
        if (t >= prochain) { ligne(e, t); prochain += pas_aff; }
    }
}

int main(void)
{
    printf("empreinte du moteur : %d octets\n", affect_bytes());

    /* ---- A : trois echecs puis une insulte ---- */
    {
        affect_t e; affect_init(&e);
        evt_t ev[] = {
            {  60.0f, {0.85f, -0.65f, -0.55f, 0.00f,  0.00f, 0.30f, 0.0f}, "echec 1 (ca ne marche pas)"},
            { 180.0f, {0.90f, -0.70f, -0.60f, 0.00f,  0.00f, 0.10f, 0.0f}, "echec 2 (ca ne marche toujours pas)"},
            { 300.0f, {0.95f, -0.75f, -0.55f, 0.00f,  0.00f, 0.05f, 0.0f}, "echec 3 (encore)"},
            { 380.0f, {0.90f, -0.35f, 0.45f, -0.95f, -0.90f, 0.30f, 0.0f}, "insulte ('putain ta gueule')"},
        };
        scenario("A. galere repetee puis insulte, puis silence", &e, 1500.0f, ev, 4, 120.0f, 1.0f);
        printf("  etat final : %s, humeur %+.2f, attachement %.2f\n",
               affect_label_name(&e), (double)e.mv, (double)e.trust);
    }

    /* ---- B : nuit blanche, puis une remarque banale ---- */
    {
        affect_t e; affect_init(&e);
        e.energy = 0.12f; e.satiety = 0.25f;          /* il n'a pas dormi ni mange */
        e.fatigue = 0.55f;                            /* dette de sommeil deja la */
        for (float t = 0; t < 2400; t += 1.0f) affect_tick(&e, NULL, 1.0f);
        entete("B. apres une nuit blanche (aucun evenement)");
        ligne(&e, 2400.0f);
        evt_t ev[] = {
            { 2410.0f, {0.55f, -0.35f, 0.05f, 0.00f, 0.00f, 0.0f, 0.0f}, "remarque banale ('c'est pas ouf')"},
        };
        scenario("   puis une remarque banale", &e, 3300.0f, ev, 1, 120.0f, 1.0f);
    }

    /* ---- C : reussite, merci, puis echec (effet tampon du lien) ---- */
    {
        affect_t e; affect_init(&e);
        evt_t ev[] = {
            {  60.0f, {0.85f,  0.80f,  0.65f, 0.30f,  0.20f, 0.40f, 0.0f}, "succes (le flash passe)"},
            { 150.0f, {0.80f,  0.55f,  0.20f, 0.40f,  0.90f, 0.10f, 0.0f}, "merci ('tu m'as bien aide')"},
            { 600.0f, {0.90f, -0.75f, -0.55f, 0.00f,  0.00f, 0.10f, 0.0f}, "echec (meme gravite que A)"},
        };
        scenario("C. succes, remerciement, puis un echec", &e, 1500.0f, ev, 3, 120.0f, 1.0f);
        printf("  etat final : %s, humeur %+.2f, attachement %.2f\n",
               affect_label_name(&e), (double)e.mv, (double)e.trust);
    }

    /* ---- D : l'humain annonce une mauvaise nouvelle (empathie) ---- */
    {
        affect_t e; affect_init(&e);
        e.trust = 0.75f; e.oxy = 0.55f;               /* lien deja solide */
        evt_t ev[] = {
            { 120.0f, {0.90f, -0.55f, -0.70f, 0.10f, -0.30f, 0.20f, 0.95f},
              "mauvaise nouvelle (deuil, sante)" },
        };
        scenario("D. l'autre souffre : compassion ou chagrin ?", &e, 900.0f, ev, 1, 90.0f, 1.0f);
    }

    /* ---- H : la meme remarque, repose ou epuise ---- */
    {
        printf("\n=== H. meme remarque ('c'est pas ouf') selon la fatigue ===\n");
        appraisal_t rem = {0.55f, -0.35f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 2; k++) {
            affect_t e; affect_init(&e);
            e.fatigue = k ? 0.70f : 0.05f;
            e.energy  = k ? 0.20f : 0.90f;
            affect_tick(&e, &rem, 1.0f);
            for (int i = 0; i < 60; i++) affect_tick(&e, NULL, 1.0f);
            printf("  fatigue %.2f -> %-14s intensite %3.0f%%  valence %+.2f  cort %.2f\n",
                   (double)e.fatigue, affect_label_name(&e),
                   (double)(e.intensity * 100.0f), (double)e.v, (double)e.cort);
        }
    }

    /* ---- G : injustice nette (le robot a raison, on l'accuse) ---- */
    {
        affect_t e; affect_init(&e);
        evt_t ev[] = {
            { 60.0f, {1.00f, -0.60f, 0.80f, -1.00f, -0.70f, 0.40f, 0.0f},
              "injustice : on l'accuse d'une panne qu'elle n'a pas causee" },
        };
        scenario("G. injustice avec pouvoir d'agir (colere vs peur)", &e, 400.0f, ev, 1, 60.0f, 1.0f);
    }

    /* ---- F : le meme echec, selon le lien deja construit ---- */
    {
        printf("\n=== F. le meme echec (c=-0.75) selon l'attachement ===\n");
        appraisal_t ech = {0.90f, -0.75f, -0.55f, 0.0f, 0.0f, 0.10f, 0.0f};
        for (int k = 0; k < 2; k++) {
            affect_t e; affect_init(&e);
            e.trust = k ? 0.88f : 0.18f;
            e.oxy   = k ? 0.65f : 0.10f;
            affect_tick(&e, &ech, 1.0f);
            for (int i = 0; i < 30; i++) affect_tick(&e, NULL, 1.0f);
            printf("  attachement %.2f -> %-16s intensite %3.0f%%  valence %+.2f\n",
                   (double)(k ? 0.88f : 0.18f), affect_label_name(&e),
                   (double)(e.intensity * 100.0f), (double)e.v);
        }
    }

    /* ---- E : vingt minutes sans rien (le robot vit quand meme) ---- */
    {
        affect_t e; affect_init(&e);
        scenario("E. aucun evenement pendant 20 min (bruit + humeur)", &e, 1200.0f, NULL, 0, 180.0f, 1.0f);
    }
    return 0;
}
