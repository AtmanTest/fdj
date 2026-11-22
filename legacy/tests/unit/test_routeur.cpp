/* Tests du routeur des boutons, sans carte et sans ESP-IDF.

   Le bloc teste n'est pas une reecriture : tests/unit/extrait_routeur.py tire
   le VRAI code de main/main.cpp (entre les marqueurs @@ROUTEUR_DEBUT@@ et
   @@ROUTEUR_FIN@@) et l'inclut ici. Tout ce qui vient du firmware est remplace
   par un stub qui compte les appels.

   Depuis la page unique (11/11) il n'y a plus de table par page : le routeur
   tient en quatre fonctions, et l'ordre de priorite est explicite.

   Tourne sur le PC :  bash tests/run_unit.sh  */
#include <cstdio>
#include <cstdint>
#include <cstdbool>
#include <cstring>

static int ok = 0, ko = 0;

#define VERIF(cond, nom) do {                       \
    if (cond) { ok++; printf("  ok   %s\n", nom); } \
    else      { ko++; printf("  KO   %s\n", nom); } \
} while (0)

/* ---------- ce que le firmware fournit d'habitude ---------- */
#define PAGE_MAITRE 0
#define PAGE_ASTUCE 1

#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

/* horloge simulee */
static int64_t faux_us = 1000000000LL;
static int64_t esp_timer_get_time(void) { return faux_us; }

/* etat du monde */
static int  ui_page;
static int64_t geste_ms;
static bool alarme_on, wake_busy, radio_etat, voix_etat, rep_ouvert;
static bool meteo_force;
static bool astuce_demande;

/* compteurs d'appels */
static int n_show, n_beep, n_tache, n_assist, n_capteurs;
static int n_rep_court, n_rep_long, n_rep_ferme;

static void show_ui(void)         { n_show++; }
static void ui_beep(void)         { n_beep++; }
static void ui_read_sensors(void) { n_capteurs++; }
static void tache_arrete(void)    { n_tache++; wake_busy = radio_etat = voix_etat = false; }
static void assistant_wake(void)  { n_assist++; }
static void rep_ferme(void)       { n_rep_ferme++; rep_ouvert = false; }

static bool rep_bouton_court(int n)
{
    (void)n;
    if (!rep_ouvert) return false;
    n_rep_court++;
    return true;
}

static bool rep_bouton_long(int n)
{
    (void)n;
    if (!rep_ouvert) return false;
    n_rep_long++;
    rep_ouvert = false;
    return true;
}

/* ---------- LE VRAI CODE DU FIRMWARE ---------- */
#include "routeur_extrait.inc"

/* ---------- outillage de test ---------- */
static void remise(void)
{
    faux_us += 5000000;              /* les gardes de cadence sont loin derriere */
    ui_page = PAGE_MAITRE;
    alarme_on = wake_busy = radio_etat = voix_etat = rep_ouvert = false;
    meteo_force = astuce_demande = false;
    n_show = n_beep = n_tache = n_assist = n_capteurs = 0;
    n_rep_court = n_rep_long = n_rep_ferme = 0;
    geste_ms = 0;
}

int main(void)
{
    printf("tests du routeur : une page, trois gestes par bouton\n");

    /* ---- bouton 1 court : relire les donnees ---- */
    remise();
    bouton_court(0);
    VERIF(meteo_force, "court 1 : la meteo est redemandee");
    VERIF(n_capteurs == 1, "court 1 : les capteurs sont relus");
    VERIF(n_beep == 1, "court 1 : un bip");
    VERIF(n_show == 1 && ui_page == PAGE_MAITRE, "court 1 : le tableau est redessine");

    remise();
    bouton_court(0);
    bouton_court(0);
    VERIF(n_show == 2 && n_capteurs == 2, "deux appuis rapproches : deux relectures, aucun perdu");

    /* ---- bouton 2 court : une page, puis l'autre ---- */
    remise();
    bouton_court(1);
    VERIF(ui_page == PAGE_ASTUCE && n_show == 1, "court 2 : passe a la page astuce");
    VERIF(n_capteurs == 0 && !meteo_force, "court 2 : pas de relecture meteo");
    remise();
    ui_page = PAGE_ASTUCE;
    bouton_court(1);
    VERIF(ui_page == PAGE_MAITRE && n_show == 1, "court 2 depuis l'astuce : retour au tableau");
    remise();
    ui_page = PAGE_ASTUCE;
    bouton_court(0);
    VERIF(astuce_demande && n_show == 0, "astuce + court 1 : nouvelle astuce demandee");
    VERIF(ui_page == PAGE_ASTUCE, "astuce + court 1 : on reste sur la page");

    /* ---- la sonnerie passe avant tout ---- */
    remise();
    alarme_on = true;
    bouton_court(0);
    VERIF(!alarme_on && n_show == 1, "sonnerie : le bouton 1 la fait taire");
    VERIF(!meteo_force, "sonnerie : le court n'a pas relu la meteo");
    remise();
    alarme_on = true;
    bouton_court(1);
    VERIF(alarme_on, "sonnerie : le bouton 2 ne la coupe pas");

    /* ---- une tache en cours s'arrete ---- */
    remise();
    wake_busy = true;
    bouton_court(0);
    VERIF(n_tache == 1 && !wake_busy, "tache en cours + court : elle s'arrete");
    VERIF(n_show == 1 && ui_page == PAGE_MAITRE, "tache en cours + court : retour au tableau");

    /* ---- la reponse plein ecran capture tout ---- */
    remise();
    rep_ouvert = true;
    bouton_court(0);
    VERIF(n_rep_court == 1, "reponse ouverte : le court lui appartient");
    VERIF(n_show == 0 && !meteo_force, "reponse ouverte : la meteo n'est pas relue");
    bouton_long(0, 0);
    VERIF(n_rep_long == 1 && n_tache == 0, "reponse ouverte : le long la ferme");

    /* ---- double appui ---- */
    remise();
    bouton_double(0);
    VERIF(n_assist == 1, "double 1 : lance l'assistant");
    remise();
    rep_ouvert = true;
    bouton_double(0);
    VERIF(n_rep_ferme == 1 && n_assist == 1,
          "double 1 sur une reponse : elle se ferme, puis l'assistant");
    remise();
    bouton_double(1);
    VERIF(n_assist == 0 && ui_page == PAGE_MAITRE,
          "double 2 : pas d'assistant, on reste sur le tableau");

    /* ---- appui long : retour au tableau, arret de ce qui tourne ---- */
    remise();
    radio_etat = true;
    bouton_long(1, 0);
    VERIF(n_tache == 1 && !radio_etat, "long 2 : la radio s'arrete");
    VERIF(ui_page == PAGE_MAITRE && n_show == 1, "long 2 : retour au tableau");
    remise();
    bouton_long(0, 1);
    VERIF(ui_page == PAGE_MAITRE && n_tache == 1, "long 1 (tres long) : retour au tableau");

    /* ---- maintien prolonge : plus rien a faire defiler ---- */
    remise();
    bouton_repeat(0);
    VERIF(n_show == 0 && n_capteurs == 0 && !meteo_force, "maintien : aucune action");

    printf("resultat : %d ok, %d ko\n", ok, ko);
    return ko ? 1 : 0;
}
