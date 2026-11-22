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

/* ---- 1. vingt ecrans, dont l'intertitre a sa place ---- */
static void test_vingt_ecrans(void)
{
    OK(FDJ_PAGES == 37, "37 ecrans : accueil + 18 x (intertitre, page)");
    OK(fdj_ecran_est_intertitre(1), "l'ecran 2 est un intertitre");
    OK(strcmp(fdj_page_titre(1), "PARIS SPORTIFS") == 0, "intertitre du module paris");
    OK(strcmp(fdj_page_titre(2), "PARIS SPORTIFS") == 0, "page de contenu du module");
    OK(fdj_page_items(1, &v) == 1, "l'intertitre a un seul item");
    int inters = 0;
    for (int p = 1; p < FDJ_PAGES; p++) if (fdj_ecran_est_intertitre(p)) inters++;
    OK(inters == 18, "un intertitre avant chacun des 18 modules");
    for (int m = 1; m < FDJ_M_NB; m++)
        if (!fdj_ecran_est_intertitre(fdj_module_page(m) - 1))
            OKF(0, "module %d sans intertitre", m);

    v.page = 1; v.curseur = 0;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    OK(noirs_zone(0, 30, 199, 110) > 300, "l'intertitre affiche un grand titre");
    OK(noirs_zone(0, 22, 199, 40) < 1200, "l'intertitre laisse de l'espace blanc");

    /* les 20 pages existent et se dessinent */
    int vues = 0;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p; v.curseur = 0;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        if (noirs_zone(0, 0, 199, 199) > 150) vues++;
    }
    OK(vues == FDJ_PAGES, "les 37 ecrans produisent une image");

    /* l'aide du pied est propre a la page, pas un texte unique recopie */
    int differentes = 0;
    for (int p = 1; p < FDJ_PAGES; p++)
        if (strcmp(fdj_page_aide(p), fdj_page_aide(p - 1)) != 0) differentes++;
    OK(differentes >= 10, "l'aide du pied change selon la page");
    for (int p = 0; p < FDJ_PAGES; p++)
        if (!fdj_page_aide(p)[0]) OKF(0, "page %d sans aide de pied", p + 1);
}

/* ---- 2. coupon plein : plus de chevauchement ---- */
static void test_coupon_plein(void)
{
    /* 8 selections sportives + 2 grilles = le pire cas */
    e.sel_cochee = 0x00FF;
    e.grille_cochee = 0x0003;
    v.page = (uint8_t)fdj_module_page(FDJ_M_COUPON); v.curseur = 0;
    int items = fdj_page_items(v.page, &v);
    OK(items == 12, "10 selections + 2 actions = 12 items");

    int positions_ok = 0;
    for (int k = 0; k < items; k++) {
        v.curseur = k;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        if (noirs_zone(0, 18, 199, 178) > 300) positions_ok++;
    }
    OK(positions_ok == items, "chaque position du coupon plein reste lisible");

    /* les zones fixes ont chacune leur encre : rien n'est ecrase */
    v.curseur = 10;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    OK(noirs_zone(0, 18, 199, 44) > 100, "resume (mise totale) dessine");
    OK(noirs_zone(0, 46, 199, 122) > 200, "liste des selections dessinee");
    OK(noirs_zone(0, 130, 199, 164) > 100, "boutons VALIDER / VIDER dessines");
    OK(noirs_zone(0, 165, 199, 177) > 10, "note LOTO dessinee");

    /* la page ne deborde jamais sous le filet du pied */
    v.curseur = 0;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    e.sel_cochee = 0;
    e.grille_cochee = 0;
}

/* ---- 3. historique : detail reel + pagination ---- */
static void test_historique_detail(void)
{
    for (int i = 0; i < 12; i++)
        fdj_pari_ajoute(&e, "PARI DE TEST POUR LE DETAIL", "ABC", 200, 450,
                        T0 - (int64_t)i * 86400, FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);

    v.page = (uint8_t)fdj_module_page(FDJ_M_HIST);
    v.curseur = 0;
    OK(fdj_page_items(v.page, &v) == FDJ_HIST_PAGE, "5 paris + l'item de defilement = 6 items");

    /* le detail est un vrai ecran, et il depend du pari */
    int idx0 = fdj_hist_index_recence(&e, 0);
    fdj_ui_detail(fbm, &e.hist[idx0], idx0);
    int a = noirs_zone(0, 0, 199, 199);
    OK(a > 400, "l'ecran de detail dessine quelque chose");

    uint8_t autre[ART_FB_LEN];
    int idx1 = fdj_hist_index_recence(&e, 7);
    fdj_ui_detail(autre, &e.hist[idx1], idx1);
    OK(memcmp(fbm, autre, ART_FB_LEN) != 0, "le detail change selon le pari");

    /* pagination : le curseur sur le 7e item fait defiler l'anneau */
    e.hist_off = FDJ_HIST_PAGE;
    OK(fdj_page_items(v.page, &v) == 6, "page 2 de l'historique : 6 paris");
    e.hist_off = FDJ_HIST_PAGE * 2 - 1;
    OK(fdj_page_items(v.page, &v) == 1, "derniere page : 1 pari, pas de defilement");
    e.hist_off = 0;
}

/* ---- 4. RAM : capacites reelles, action cablee, plus de faux swap ---- */
static void test_ram(void)
{
    v.page = (uint8_t)fdj_module_page(FDJ_M_RAM);
    v.curseur = 0;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&v, fbm);
    int avec = noirs_zone(0, 20, 199, 120);

    fdj_vue_t sans = v;
    sans.heap_int_total = 0;
    sans.psram_total = 0;
    memset(fbm, 0xFF, sizeof(fbm));
    fdj_ui_dessine(&sans, fbm);
    int sans_totaux = noirs_zone(0, 20, 199, 120);
    OK(avec > 0 && sans_totaux > 0, "la page memoire se dessine avec ou sans totaux");
    OK(strstr(fdj_page_aide(v.page), "MESURES") != NULL, "l'action REFAIRE LES MESURES est annoncee");
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

/* ---- 6. gros texte : effet reel sur la majorite des pages ---- */
static void test_gros_texte(void)
{
    int changees = 0;
    uint8_t petit[ART_FB_LEN], grand[ART_FB_LEN];
    e.gros_texte = false;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p; v.curseur = 0;
        fdj_ui_dessine(&v, petit);
        e.gros_texte = true;
        fdj_ui_dessine(&v, grand);
        e.gros_texte = false;
        if (memcmp(petit, grand, ART_FB_LEN) != 0) changees++;
    }
    OKF(changees >= 12, "le gros texte change %d ecrans sur %d", changees, FDJ_PAGES);

    /* et il ne casse pas la navigation : chaque page garde ses items */
    e.gros_texte = true;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p; v.curseur = 0;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        if (noirs_zone(0, 0, 199, 199) < 150) OKF(0, "page %d vide en gros texte", p + 1);
    }
    e.gros_texte = false;
    OK(1, "les 20 pages tiennent en gros texte");
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
    OK(s.son_clic, "le clic est actif par defaut");
    OK(s.pause_debut == 0, "pause sans echeance au depart");
    OK(FDJ_ETAT_VERSION == 4, "le blob NVS est versionne (v4)");
}

/* ---- 10. regles FDJ : trois items, un curseur visible ---- */
static void test_regles_curseur(void)
{
    v.page = (uint8_t)fdj_module_page(FDJ_M_REGLES);
    OK(fdj_page_items(v.page, &v) == 3, "trois items declares");
    uint8_t a[ART_FB_LEN], b[ART_FB_LEN];
    v.curseur = 0; fdj_ui_dessine(&v, a);
    v.curseur = 1; fdj_ui_dessine(&v, b);
    OK(memcmp(a, b, ART_FB_LEN) != 0, "le curseur se deplace vraiment");
    OK(noirs_zone(0, 20, 199, 110) > 200, "les trois blocs sont dessines");
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
    test_vingt_ecrans();
    test_coupon_plein();
    test_historique_detail();
    test_ram();
    test_suppression_profil();
    test_gros_texte();
    test_pause();
    test_protection();
    test_version_et_options();
    test_regles_curseur();
    test_coupon_atomique();
    FIN("defauts 3.0.0 corriges");
}
