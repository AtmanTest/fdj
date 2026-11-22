/*  test_defauts.cpp — les defauts constates de la 3.0.0, un test par defaut.
 *
 *  Chaque test decrit le SYMPTOME visible : coupon plein qui se chevauche,
 *  detail jamais dessine, mesures non cablees, pause sans echeance, profil
 *  annonce supprimable mais jamais supprime, gros texte cosmetique, etc.
 *  Ces tests doivent echouer sur la 3.0.0 et passer sur la 4.0.0.
 */
#include "check.h"
#include "fdj_model.h"
#include "fdj_nav.h"
#include "fdj_ui.h"
#include "art.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const int64_t T0 = 1760000000LL;
static uint8_t fbm[ART_FB_LEN];
static fdj_etat_t e;
static fdj_vue_t v;
static fdj_top_t top;
static fdj_flux_t flux;
static fdj_live_t live;

static int noirs_zone(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int i = y * 25 + (x >> 3);
            if (!((fbm[i] >> (7 - (x & 7))) & 1)) n++;
        }
    return n;
}

static void decor(void)
{
    fdj_etat_defaut(&e, 2026);
    e.boots = 4;
    e.profils[0].solde_ct = 9000;
    memset(&live, 0, sizeof(live));
    live.n = 3; live.ok = true; live.maj = T0;
    const char *D[3] = { "TROYES", "ROME", "LENS" };
    const char *X[3] = { "LE HAVRE", "MILAN", "NICE" };
    for (int i = 0; i < 3; i++) {
        fdj_txt_copie(live.m[i].ligue, sizeof(live.m[i].ligue), "LIGUE 1");
        fdj_txt_copie(live.m[i].dom, sizeof(live.m[i].dom), D[i]);
        fdj_txt_copie(live.m[i].ext, sizeof(live.m[i].ext), X[i]);
        live.m[i].buts_dom = 1; live.m[i].buts_ext = 0; live.m[i].minute = 60;
        snprintf(live.m[i].info, sizeof(live.m[i].info), "%d'", live.m[i].minute);
    }
    uint32_t g = 77;
    fdj_top_calcule(&live, &g, T0, &top);

    memset(&flux, 0, sizeof(flux));
    flux.ok = true; flux.nb = 3; flux.maj = T0;
    fdj_txt_copie(flux.n[0].titre, FDJ_NEWS_LEN, "LE GOUVERNEMENT PRESENTE SON BUDGET 2027");
    fdj_txt_copie(flux.n[1].titre, FDJ_NEWS_LEN, "LIGUE 1 : LE PSG S'IMPOSE 2-0 A LENS");
    fdj_txt_copie(flux.n[2].titre, FDJ_NEWS_LEN, "METEO : VIGILANCE ORANGE SUR CINQ DEPARTEMENTS");
    for (int i = 0; i < 3; i++) {
        fdj_txt_copie(flux.n[i].source, sizeof(flux.n[i].source), "FRANCEINFO");
        snprintf(flux.n[i].quand, sizeof(flux.n[i].quand), "10:%02d", i * 15);
    }

    memset(&v, 0, sizeof(v));
    v.e = &e; v.top = &top; v.news = &flux; v.live = &live;
    v.now = T0; v.wifi = true; v.ssid = "ESPRESSIF"; v.rssi = -55;
    v.heap_int_libre = 120 * 1024; v.heap_int_max = 80 * 1024;
    v.heap_int_total = 320 * 1024;
    v.psram_libre = 7 * 1024 * 1024; v.psram_total = 8 * 1024 * 1024;
    v.sd_ok = true; v.rendu_ms = 9; v.ecran_ms = 470;
    v.nb_grilles = FDJ_GRILLE_MAX;
    uint32_t sg = 4242;
    for (int i = 0; i < FDJ_GRILLE_MAX; i++)
        fdj_loto_grille(&sg, v.grilles[i], &v.chances[i]);
}

/* ---- 1. huit ecrans, pas un de plus ---- */
static void test_huit_ecrans(void)
{
    OK(FDJ_PAGES == 8, "8 ecrans : un par role");
    static const char *T[8] = {
        "ACCUEIL", "CONNEXION", "PARIS", "COUPON",
        "COMPTE", "RESPONSABLE", "PREUVES", "REGLAGES"
    };
    for (int p = 0; p < FDJ_PAGES; p++)
        if (strcmp(fdj_page_titre(p), T[p]) != 0) OKF(0, "titre de l'ecran %d", p + 1);

    int inters = 0;
    for (int p = 0; p < FDJ_PAGES; p++) if (fdj_ecran_est_intertitre(p)) inters++;
    OK(inters == 0, "aucun intertitre : 8 ecrans directs");

    int vues = 0;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p; v.curseur = 0;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        if (noirs_zone(0, 0, 199, 199) > 150) vues++;
    }
    OK(vues == FDJ_PAGES, "les 8 ecrans produisent une image");

    /* l'aide du pied est propre a la page */
    int differentes = 0;
    for (int p = 1; p < FDJ_PAGES; p++)
        if (strcmp(fdj_page_aide(p), fdj_page_aide(p - 1)) != 0) differentes++;
    OK(differentes >= 7, "l'aide du pied change a chaque ecran");
    for (int p = 0; p < FDJ_PAGES; p++)
        if (!fdj_page_aide(p)[0]) OKF(0, "page %d sans aide de pied", p + 1);
}

/* ---- 2. coupon plein : plus de chevauchement ---- */
static void test_coupon_plein(void)
{
    /* 4 selections : le maximum affichable sur le coupon */
    e.sel_cochee = 0x000F;
    e.grille_cochee = 0;
    v.page = FDJ_M_COUPON; v.curseur = 0;
    int items = fdj_page_items(v.page, &v);
    OK(items == 2, "deux actions : valider et vider");

    int positions_ok = 0;
    for (int k = 0; k < items; k++) {
        v.curseur = k;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        if (noirs_zone(0, 18, 199, 178) > 300) positions_ok++;
    }
    OK(positions_ok == items, "chaque position du coupon plein reste lisible");

    /* les zones fixes ont chacune leur encre : rien n'est ecrase */
    v.curseur = 0;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    OK(noirs_zone(0, 16, 199, 52) > 100, "resume (mise et gain) dessine");
    OK(noirs_zone(0, 60, 199, 120) > 200, "liste des selections dessinee");
    OK(noirs_zone(0, 126, 199, 164) > 100, "boutons VALIDER / VIDER dessines");

    /* la page ne deborde jamais sous le filet du pied */
    v.curseur = 0;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    e.sel_cochee = 0;
    e.grille_cochee = 0;
}

/* ---- 3. detail d'un pari : vrai ecran, depend du pari ---- */
static void test_detail_pari(void)
{
    for (int i = 0; i < 12; i++)
        fdj_pari_ajoute(&e, "PARI DE TEST POUR LE DETAIL", "ABC", 200, 450,
                        T0 - (int64_t)i * 86400, FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);

    int idx0 = fdj_hist_index_recence(&e, 0);
    fdj_ui_detail(fbm, &e.hist[idx0], idx0);
    OK(noirs_zone(0, 0, 199, 199) > 400, "l'ecran de detail dessine quelque chose");

    uint8_t autre[ART_FB_LEN];
    int idx1 = fdj_hist_index_recence(&e, 7);
    fdj_ui_detail(autre, &e.hist[idx1], idx1);
    OK(memcmp(fbm, autre, ART_FB_LEN) != 0, "le detail change selon le pari");

    v.page = FDJ_M_COMPTE;
    v.curseur = 0;
    OK(fdj_page_items(v.page, &v) == 1, "le compte tient en un ecran");
}

/* ---- 4. preuves : les mesures affichees sont les mesures lues ---- */
static void test_preuves(void)
{
    v.page = FDJ_M_PREUVES;
    v.curseur = 0;
    v.rendu_ms = 4;
    v.ecran_ms = 580;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    uint8_t a[ART_FB_LEN];
    memcpy(a, fbm, sizeof(a));

    v.rendu_ms = 999;
    v.ecran_ms = 999;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    OK(memcmp(a, fbm, ART_FB_LEN) != 0, "la page preuves affiche les vraies mesures");
    OK(noirs_zone(0, 20, 199, 140) > 300, "la page preuves est remplie");

    v.page = FDJ_M_REGLAGES;
    OK(fdj_page_items(v.page, &v) == 2, "reglages : deux actions");
}

/* ---- 5. profils : suppression reelle, refus du profil actif ---- */
static void test_suppression_profil(void)
{
    fdj_etat_t s;
    fdj_etat_defaut(&s, 1);
    fdj_profil_ajoute(&s, "DEUX", 0, 1000, 3000);
    fdj_profil_ajoute(&s, "TROIS", 1, 2000, 6000);
    OK(s.nb_profils == 3, "trois profils");

    OK(fdj_profil_supprime(&s, 0) == 1, "le profil actif est refuse");
    OK(s.nb_profils == 3, "rien n'est supprime dans ce cas");

    OK(fdj_profil_supprime(&s, 1) == 0, "un profil non actif est supprime");
    OK(s.nb_profils == 2, "il en reste deux");
    OK(strcmp(s.profils[1].nom, "TROIS") == 0, "le tableau est tasse");

    /* actif apres la case supprimee : l'index suit */
    fdj_etat_t t;
    fdj_etat_defaut(&t, 1);
    fdj_profil_ajoute(&t, "DEUX", 0, 1000, 3000);
    fdj_profil_ajoute(&t, "TROIS", 1, 2000, 6000);
    fdj_profil_selectionne(&t, 2);
    OK(fdj_profil_supprime(&t, 0) == 0, "case avant l'actif supprimee");
    OK(t.profil_actif == 1, "l'index actif est corrige");
    OK(strcmp(t.profils[t.profil_actif].nom, "TROIS") == 0, "c'est toujours le bon profil");

    OK(fdj_profil_supprime(&t, 9) == 2, "index hors bornes refuse");
}

/* ---- 6. plus de petit texte : chaque ecran tient avec la police 5x10 ---- */
static void test_police_lisible(void)
{
    int pleins = 0;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p; v.curseur = 0;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        /* une page 5x10 a des glyphes de 10 px : au moins 8 lignes de texte */
        if (noirs_zone(0, 0, 199, 199) > 400) pleins++;
    }
    OKF(pleins == FDJ_PAGES, "%d/%d ecrans rendus en 5x10", pleins, FDJ_PAGES);
}

/* ---- 7. pause 24 h : une echeance, pas un boolean ---- */
static void test_pause(void)
{
    fdj_etat_t s;
    fdj_etat_defaut(&s, 1);
    OK(!fdj_pause_active(&s, T0), "pas de pause au depart");

    s.pause_24h = true;
    s.pause_debut = T0;
    OK(fdj_pause_active(&s, T0 + 3600), "pause active une heure apres");
    int64_t r = fdj_pause_restant_s(&s, T0 + 3600);
    OK(r == 86400 - 3600, "le temps restant est exact");

    fdj_pause_active_maj(&s, T0 + 86401);
    OK(!s.pause_24h && s.pause_debut == 0, "la pause expire toute seule");
    OK(!fdj_pause_active(&s, T0 + 86401), "elle ne bloque plus rien");

    /* sans heure synchronisee : active, echeance inconnue, jamais inventee */
    fdj_etat_t n;
    fdj_etat_defaut(&n, 1);
    n.pause_24h = true;
    n.pause_debut = 0;
    OK(fdj_pause_active(&n, 0), "sans horloge, la pause reste active");
    OK(fdj_pause_restant_s(&n, 0) == -1, "echeance inconnue : -1, pas une valeur inventee");
    OK(fdj_mise_verifie(&n, 0, 100) == 5, "et elle bloque reellement une mise");
}

/* ---- 8. jeu responsable : les reponses changent le plafond ---- */
static void test_protection(void)
{
    fdj_etat_t s;
    fdj_etat_defaut(&s, 1);
    s.profils[0].solde_ct = 10000;
    OK(fdj_protection_score(&s) == 0, "aucune reponse au depart");
    OK(fdj_plafond_sem_effectif(&s) == 2500, "sans reponse, plafond inchange");

    s.sel_cochee |= (1u << 8);                 /* une seule reponse OUI */
    OK(fdj_protection_score(&s) == 1, "une reponse");
    OK(fdj_plafond_sem_effectif(&s) == 1250, "protection faible : plafond divise par 2");
    OK(fdj_mise_verifie(&s, T0, 2100) == 1, "la mise unitaire reste bornee a 20 EUR");
    fdj_pari_ajoute(&s, "A", "AAA", 1000, 2000, T0 - 86400, FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);
    OK(fdj_mise_verifie(&s, T0, 300) == 2, "le plafond reduit est applique");

    fdj_etat_t t;
    fdj_etat_defaut(&t, 1);
    t.profils[0].solde_ct = 10000;
    t.sel_cochee |= (1u << 8) | (1u << 9) | (1u << 10);
    OK(fdj_protection_score(&t) == 3, "trois reponses");
    OK(fdj_plafond_sem_effectif(&t) == 2500, "trois OUI : plafond normal");
}

/* ---- 9. honnetete : version unique, aucun champ musical ---- */
static void test_version_et_options(void)
{
    /* VERSION (fichier) et FDJ_VERSION (firmware) ne doivent pas diverger */
    FILE *f = fopen("VERSION", "r");
    char version[32] = { 0 };
    if (f) {
        if (fgets(version, sizeof(version), f)) {
            size_t n = strlen(version);
            while (n && (version[n - 1] == '\n' || version[n - 1] == '\r')) version[--n] = 0;
        }
        fclose(f);
    }
    OK(version[0] != 0, "fichier VERSION lisible");
    OK(strcmp(version, FDJ_VERSION) == 0, "VERSION et FDJ_VERSION concordent");

    fdj_etat_t s;
    fdj_etat_defaut(&s, 1);
    OK(s.pause_debut == 0, "aucune pause lancee au depart");
    OK(s.pause_debut == 0, "pause sans echeance au depart");
    OK(FDJ_ETAT_VERSION == 6, "le blob NVS est versionne (v6, avec session)");
}

/* ---- 10. reglages : deux items, un curseur visible ---- */
static void test_reglages_curseur(void)
{
    v.page = FDJ_M_REGLAGES;
    OK(fdj_page_items(v.page, &v) == 2, "deux items declares");
    uint8_t a[ART_FB_LEN], b[ART_FB_LEN];
    v.curseur = 0; fdj_ui_dessine(&v, a);
    v.curseur = 1; fdj_ui_dessine(&v, b);
    OK(memcmp(a, b, ART_FB_LEN) != 0, "le curseur se deplace vraiment");
    OK(noirs_zone(0, 20, 199, 140) > 200, "les blocs sont dessines");
}

/* ---- 11. validation atomique : un refus n'ecrit rien ---- */
static void test_coupon_atomique(void)
{
    fdj_etat_t s;
    fdj_etat_defaut(&s, 1);
    s.profils[0].plafond_sem_ct = 300;
    s.profils[0].solde_ct = 5000;
    s.sel_cochee = 0x0003;                     /* 2 paris = 4,00 EUR > 3,00 */
    char msg[48];
    int r = fdj_coupon_valide(&s, &top, NULL, NULL, 0, T0, msg, sizeof(msg));
    OK(r == 2, "le plafond refuse le coupon entier");
    OK(s.hist_n == 0, "aucune ligne d'historique ecrite");
    OK(s.profils[0].solde_ct == 5000, "aucun debit partiel");
    OK(s.sel_cochee == 0x0003, "le coupon reste intact pour correction");
}

int main(void)
{
    decor();
    test_huit_ecrans();
    test_coupon_plein();
    test_detail_pari();
    test_preuves();
    test_suppression_profil();
    test_police_lisible();
    test_pause();
    test_protection();
    test_version_et_options();
    test_reglages_curseur();
    test_coupon_atomique();
    FIN("defauts 3.0.0 corriges");
}
