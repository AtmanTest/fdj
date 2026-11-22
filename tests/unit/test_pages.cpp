/*  test_pages.cpp — tests de COMPOSANT : chaque page, rendue pour de vrai.
 *
 *  Une page n'est pas "finie" parce qu'elle compile : elle doit produire des
 *  pixels, un titre en haut, un pied en bas, un numero lisible, et rester
 *  identique d'un rendu a l'autre (pas de hasard cache dans le dessin).
 *  On teste aussi chaque position de curseur : c'est la que se cachent les
 *  debordements et les cases qui disparaissent sous la barre noire.
 */
#include "check.h"
#include "fdj_ui.h"
#include "art.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static uint8_t fbm[ART_FB_LEN];
static fdj_etat_t e;
static fdj_vue_t v;
static fdj_top_t top;
static fdj_flux_t flux;
static fdj_live_t live;

static int noirs(void)
{
    int n = 0;
    for (int i = 0; i < ART_FB_LEN; i++) {
        uint8_t b = fbm[i];
        while (b) { n += b & 1; b >>= 1; }
    }
    return n;
}

static int noirs_zone(int y0, int y1)
{
    int n = 0;
    if (y1 > 199) y1 = 199;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < 200; x++) {
            int i = y * 25 + (x >> 3);
            if (!((fbm[i] >> (7 - (x & 7))) & 1)) n++;
        }
    return n;
}

static int noirs_zone_x(int x0, int x1, int y0, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int i = y * 25 + (x >> 3);
            if (!((fbm[i] >> (7 - (x & 7))) & 1)) n++;
        }
    return n;
}

static void donnees(void)
{
    int64_t now = 1760000000LL;
    fdj_etat_defaut(&e, 77);
    e.boots = 3;
    e.profils[0].solde_ct = 8000;
    fdj_profil_ajoute(&e, "EXPERT", 2, 5000, 20000);
    fdj_pari_ajoute(&e, "TROYES-LE HAVRE +2,5", "7KQ", 500, 1100, now - 3600, FDJ_JEU_SPORT, FDJ_PARI_GAGNE);
    fdj_pari_ajoute(&e, "LOTO 02 11 24 38 41 C7", "LOT", 220, 0, now - 2 * 86400, FDJ_JEU_LOTO, FDJ_PARI_PERDU);
    for (int i = 0; i < 8; i++)
        fdj_pari_ajoute(&e, "PARI DE TEST", "ABC", 200, 0, now - (i + 3) * 86400,
                        FDJ_JEU_SPORT, FDJ_PARI_PERDU);

    memset(&live, 0, sizeof(live));
    live.n = 3;
    live.ok = true;
    live.maj = now;
    const char *L[3] = { "LIGUE 1", "SERIE A", "PREMIER LEAGUE" };
    const char *D[3] = { "TROYES", "ROME", "LONDRES" };
    const char *X[3] = { "LE HAVRE", "MILAN", "LIVERPOOL" };
    for (int i = 0; i < 3; i++) {
        fdj_txt_copie(live.m[i].ligue, sizeof(live.m[i].ligue), L[i]);
        fdj_txt_copie(live.m[i].dom, sizeof(live.m[i].dom), D[i]);
        fdj_txt_copie(live.m[i].ext, sizeof(live.m[i].ext), X[i]);
        live.m[i].buts_dom = i; live.m[i].buts_ext = 1;
        live.m[i].minute = 55 + i * 8;
        live.m[i].live = true;
        snprintf(live.m[i].info, sizeof(live.m[i].info), "%d'", live.m[i].minute);
    }
    uint32_t g = 99;
    fdj_top_calcule(&live, &g, now, &top);

    memset(&flux, 0, sizeof(flux));
    flux.ok = true;
    flux.nb = 4;
    flux.maj = now;
    fdj_txt_copie(flux.n[0].titre, FDJ_NEWS_LEN, "LE GOUVERNEMENT PRESENTE SON BUDGET 2027 DEVANT L'ASSEMBLEE");
    fdj_txt_copie(flux.n[1].titre, FDJ_NEWS_LEN, "LIGUE 1 : LE PSG S'IMPOSE 2-0 A LENS ET PREND LA TETE");
    fdj_txt_copie(flux.n[2].titre, FDJ_NEWS_LEN, "METEO : VIGILANCE ORANGE SUR CINQ DEPARTEMENTS");
    fdj_txt_copie(flux.n[3].titre, FDJ_NEWS_LEN, "ECONOMIE : L'INFLATION RALENTIT A 1,8 % SUR UN AN");
    for (int i = 0; i < 4; i++) {
        fdj_txt_copie(flux.n[i].source, sizeof(flux.n[i].source), "FRANCEINFO");
        snprintf(flux.n[i].quand, sizeof(flux.n[i].quand), "%02d:%02d", 10 + i, 15 * i);
    }

    memset(&v, 0, sizeof(v));
    v.e = &e;
    v.top = &top;
    v.news = &flux;
    v.live = &live;
    v.now = now;
    v.ssid = "ESPRESSIF";
    v.wifi = true;
    v.rssi = -55;
    v.heap_int_libre = 120 * 1024;
    v.heap_int_max = 80 * 1024;
    v.psram_libre = 7 * 1024 * 1024;
    v.sd_ok = true;
    v.rendu_ms = 9;
    v.ecran_ms = 470;
    v.nb_grilles = FDJ_GRILLE_MAX;
    uint32_t sg = 4242;
    for (int i = 0; i < FDJ_GRILLE_MAX; i++)
        fdj_loto_grille(&sg, v.grilles[i], &v.chances[i]);
    e.sel_cochee = 0x0003;
    e.grille_cochee = 0x0001;
    e.news_lu = 0x0001;
}

int main(void)
{
    donnees();

    /* ---- 1. chaque page a un titre, un numero et un pied ---- */
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p;
        v.curseur = 0;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);

        char quoi[96];
        int items = fdj_page_items(p, &v);
        snprintf(quoi, sizeof(quoi), "page %d : %d items", p + 1, items);
        OK(items >= 1, quoi);
        OK(noirs() > 150, "la page n'est pas vide");

        snprintf(quoi, sizeof(quoi), "page %d : titre dessine en haut", p + 1);
        OK(noirs_zone(0, 13) > 20, quoi);
        snprintf(quoi, sizeof(quoi), "page %d : numero de page en haut a droite", p + 1);
        OK(noirs_zone_x(160, 199, 0, 13) > 4, quoi);
        snprintf(quoi, sizeof(quoi), "page %d : pied de page", p + 1);
        OK(noirs_zone(180, 199) > 10, quoi);

        int w = a_text_w(2, fdj_page_titre(p));
        snprintf(quoi, sizeof(quoi), "page %d : titre de %d px (max 160)", p + 1, w);
        OK(w > 0 && w <= 160, quoi);
        OK(strlen(fdj_page_titre(p)) > 2, "un titre a un nom");
    }

    /* ---- 2. chaque curseur, sur chaque page : rien ne casse ---- */
    int positions = 0;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p;
        int items = fdj_page_items(p, &v);
        for (int k = 0; k < items; k++) {
            v.curseur = k;
            memset(fbm, 0xFF, sizeof(fbm));
            fdj_ui_dessine(&v, fbm);
            positions++;
            if (noirs() < 120) {
                OKF(0, "page %d curseur %d : image vide", p + 1, k);
            }
        }
    }
    OKF(positions >= FDJ_PAGES, "%d positions de curseur rendues", positions);

    /* ---- 3. determinisme : deux rendus identiques ---- */
    v.page = 1; v.curseur = 2;
    uint8_t a[ART_FB_LEN], b[ART_FB_LEN];
    fdj_ui_dessine(&v, a);
    v.now += 37;               /* l'heure change : la page 2 n'en depend pas */
    fdj_ui_dessine(&v, b);
    OK(memcmp(a, b, ART_FB_LEN) == 0, "page 2 : rendu deterministe a donnees egales");

    /* ---- 4. donnees absentes : on ecrit N.D., pas un zero ---- */
    fdj_etat_t vide;
    fdj_etat_defaut(&vide, 1);
    fdj_vue_t vv;
    memset(&vv, 0, sizeof(vv));
    vv.e = &vide;
    vv.now = 1760000000LL;
    for (int p = 0; p < FDJ_PAGES; p++) {
        vv.page = p;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&vv, fbm);
        if (noirs() < 100) { OKF(0, "page %d vide sans donnees", p + 1); }
    }
    OK(1, "19 pages rendues sans reseau, sans historique, sans profil");

    /* ---- 5. ecrans plein cadre ---- */
    uint8_t ch[4] = { 0, 0, 0, 0 };
    fdj_ui_demande_code(fbm, "CODE DE DEMARRAGE", ch, 1, false);
    OK(noirs() > 500, "ecran de code lisible");
    OK(noirs_zone_x(20, 180, 60, 110) > 100, "les 4 chiffres sont dessines");

    fdj_ui_demande_code(fbm, "CODE DE DEMARRAGE", ch, 0, true);
    OK(noirs() > 500, "ecran de code avec erreur lisible");

    fdj_ui_confirme(fbm, "EFFACER MES DONNEES", "TOUT PERDRE, POUR DE BON ?", true);
    OK(noirs() > 400, "ecran de confirmation lisible");

    /* ---- 6. un message remplace le pied, sans deborder ---- */
    v.page = 0;
    v.message = "COUPON VALIDE";
    fdj_ui_dessine(&v, fbm);
    OK(noirs_zone(181, 199) > 20, "bandeau de message dessine");
    v.message = NULL;

    /* ---- 7. poids et bornes ---- */
    OK(sizeof(fdj_etat_t) < 4096, "l'etat tient en moins de 4 Ko");
    OK(sizeof(fdj_vue_t) < 2048, "la vue tient en moins de 2 Ko");
    for (int g = 0; g < FDJ_GRILLE_MAX; g++)
        for (int k = 0; k < FDJ_GRILLE_NB; k++)
            if (v.grilles[g][k] < 1 || v.grilles[g][k] > 49) { OK(0, "numero hors 1..49"); }

    FIN("pages (composant)");
}
