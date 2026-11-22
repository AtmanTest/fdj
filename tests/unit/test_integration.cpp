/*  test_integration.cpp — nav + argent + persistance ENSEMBLE.
 *
 *  Les suites unitaires verifient chaque piece ; celle-ci verifie qu'elles
 *  s'accordent : un geste change le modele, le modele change l'ecran, l'ecran
 *  relu apres sauvegarde redonne le meme etat.
 */
#include "check.h"
#include "fdj_model.h"
#include "fdj_nav.h"
#include "fdj_ui.h"
#include "art.h"

#include <stdio.h>
#include <string.h>

static const int64_t T0 = 1760000000LL;
static uint8_t fbm[ART_FB_LEN];

static fdj_live_t live;
static fdj_top_t  top;

static void prepare(fdj_etat_t *e, fdj_nav_t *n)
{
    fdj_etat_defaut(e, 1234);
    memset(&live, 0, sizeof(live));
    live.n = 2;
    live.ok = true;
    fdj_txt_copie(live.m[0].ligue, sizeof(live.m[0].ligue), "LIGUE 1");
    fdj_txt_copie(live.m[0].dom, sizeof(live.m[0].dom), "TROYES");
    fdj_txt_copie(live.m[0].ext, sizeof(live.m[0].ext), "LE HAVRE");
    live.m[0].buts_dom = 1; live.m[0].buts_ext = 0; live.m[0].minute = 60;
    fdj_txt_copie(live.m[1].ligue, sizeof(live.m[1].ligue), "SERIE A");
    fdj_txt_copie(live.m[1].dom, sizeof(live.m[1].dom), "ROME");
    fdj_txt_copie(live.m[1].ext, sizeof(live.m[1].ext), "MILAN");
    live.m[1].buts_dom = 0; live.m[1].buts_ext = 0; live.m[1].minute = 30;
    uint32_t g = e->graine;
    fdj_top_calcule(&live, &g, T0, &top);
    fdj_nav_init(n, 0, 0);
}

static void test_cycle_pari(void)
{
    fdj_etat_t e;
    fdj_nav_t n;
    prepare(&e, &n);

    /* page 2 : trois selections cochees */
    n.page = 1;
    for (int i = 0; i < 3 && i < top.n; i++) {
        n.curseur = (uint8_t)i;
        fdj_act_t a = fdj_nav_geste(&n, top.n, 0, GESTE_COURT, 80);
        OK(a == FDJ_ACT_COCHER, "B1 coche la selection");
        if (e.sel_cochee & (1u << i)) continue;
        e.sel_cochee |= (uint16_t)(1u << i);
    }
    int coches = 0;
    for (int i = 0; i < top.n; i++) if (e.sel_cochee & (1u << i)) coches++;
    OK(coches == 3, "trois selections au coupon");

    /* le modele refuse une mise trop grosse avant d'accepter */
    OK(fdj_mise_verifie(&e, T0, 2000) == 0, "20 EUR passent (plafond 25)");

    uint32_t solde_avant = e.profils[0].solde_ct;
    char msg[48];
    int r = fdj_coupon_valide(&e, &top, NULL, NULL, 0, T0, msg, sizeof(msg));
    OK(r == 0, "coupon valide");
    OK(strcmp(msg, "COUPON VALIDE") == 0, "message de succes");
    OK(e.sel_cochee == 0, "le coupon se vide apres validation");
    OK(e.hist_n == 3, "trois paris ecrits dans l'historique");
    OK(e.profils[0].solde_ct == solde_avant - 600, "6,00 EUR engages");

    /* l'historique se lit dans le bon ordre et alimente les stats */
    fdj_periode_t p;
    fdj_periode_calcule(&e, T0, 7, &p);
    OK(p.nb == 3 && p.mise_ct == 600, "les gains/pertes voient les 3 paris");

    /* on regle un pari : le solde remonte du gain, pas de la mise */
    int idx = fdj_hist_index_recence(&e, 0);
    uint32_t gain = e.hist[idx].gain_ct;
    fdj_pari_regle(&e, (uint8_t)idx, true);
    OK(e.profils[0].solde_ct == solde_avant - 600 + gain, "le gain est credite exactement");

    /* sauvegarde / relecture : l'etat doit etre identique */
    uint8_t blob[2048];
    size_t nb = fdj_etat_serialise(&e, blob, sizeof(blob));
    fdj_etat_t relu;
    PRES(fdj_etat_deserialise(&relu, blob, nb));
    OK(memcmp(&e, &relu, sizeof(e)) == 0, "l'etat survit a un redemarrage");
    OK(relu.hist_n == 3, "l'historique est bien dans le blob");
}

static void test_refus_plafond(void)
{
    fdj_etat_t e;
    fdj_nav_t n;
    prepare(&e, &n);
    e.profils[0].solde_ct = 10000;
    /* plafond serre pour la demonstration : 4 EUR / semaine */
    e.profils[0].plafond_sem_ct = 400;
    e.profils[0].plafond_mois_ct = 1000;

    for (int i = 0; i < 2; i++) {
        n.page = 1; n.curseur = (uint8_t)i;
        e.sel_cochee = (uint16_t)(1u << i);
        char msg[48];
        int r = fdj_coupon_valide(&e, &top, NULL, NULL, 0, T0, msg, sizeof(msg));
        OKF(r == 0, "coupon %d accepte", i + 1);
    }
    OK(fdj_mise_verifie(&e, T0, 100) == 2, "le plafond de 4 EUR est atteint");
    n.page = 1; n.curseur = 0;
    e.sel_cochee = (uint16_t)(1u << 2);
    char msg[48];
    int r = fdj_coupon_valide(&e, &top, NULL, NULL, 0, T0, msg, sizeof(msg));
    OK(r == 2, "le 3e coupon est refuse par le modele");
    OK(strcmp(msg, "PLAFOND SEMAINE ATTEINT") == 0, "le refus est explique");
    OK(e.hist_n == 2, "aucun pari n'est ecrit quand c'est refuse");

    /* la pause de jeu responsable bloque tout, meme avec du solde */
    e.pause_24h = true;
    OK(fdj_mise_verifie(&e, T0, 100) == 5, "pause active : refus");
    e.pause_24h = false;
}

static void test_navigation_complete(void)
{
    fdj_etat_t e;
    fdj_nav_t n;
    prepare(&e, &n);

    /* on fait le tour des 19 pages avec le seul bouton 2 */
    int pages_vues[FDJ_PAGES] = { 0 };
    pages_vues[n.page] = 1;
    int garde = 0;
    while (garde++ < 400) {
        int items = 5;
        fdj_nav_geste(&n, items, 1, GESTE_COURT, 80);
        pages_vues[n.page] = 1;
        if (n.page == 0 && n.curseur == 0 && garde > 3) break;
    }
    int vues = 0;
    for (int p = 0; p < FDJ_PAGES; p++) vues += pages_vues[p];
    OK(vues == FDJ_PAGES, "les 19 pages sont atteignables avec B2 seul");

    /* le retour d'urgence marche depuis n'importe ou */
    n.page = 13;
    OK(fdj_nav_geste(&n, 4, 1, GESTE_REPEAT, 3100) == FDJ_ACT_ACCUEIL, "B2 3 s : accueil");
    OK(n.page == 0, "on est revenu page 1");

    /* et l'ecran suit la navigation, sans etat incoherent */
    fdj_vue_t v;
    memset(&v, 0, sizeof(v));
    v.e = &e; v.top = &top; v.live = &live; v.now = T0;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p;
        v.curseur = 0;
        fdj_ui_dessine(&v, fbm);
        if (fdj_page_items(p, &v) < 1) OKF(0, "page %d sans item", p + 1);
    }
    OK(1, "chaque page de la navigation a au moins un item");
}

static void test_profil_et_code(void)
{
    fdj_etat_t e;
    fdj_nav_t n;
    prepare(&e, &n);

    OK(fdj_profil_ajoute(&e, "DEBUTANT", 0, 1000, 3000) == 1, "2e profil");
    fdj_profil_selectionne(&e, 1);
    e.profils[1].solde_ct = 1000;
    OK(fdj_mise_verifie(&e, T0, 1500) == 4, "le solde du profil actif compte");
    OK(fdj_mise_verifie(&e, T0, 900) == 0, "900 centimes passent");
    fdj_profil_selectionne(&e, 0);
    OK(fdj_mise_verifie(&e, T0, 900) == 0, "le profil precedent garde son solde");

    /* changement de code, puis redemarrage */
    PRES(fdj_code_change(&e, 3141));
    uint8_t blob[2048];
    size_t nb = fdj_etat_serialise(&e, blob, sizeof(blob));
    fdj_etat_t relu;
    PRES(fdj_etat_deserialise(&relu, blob, nb));
    OK(fdj_code_verifie(&relu, 3141), "le nouveau code fonctionne apres relecture");
    OK(!fdj_code_verifie(&relu, 0), "l'ancien code ne fonctionne plus");

    /* saisie du code au clavier de la carte */
    fdj_nav_mode(&n, FDJ_MODE_CODE);
    uint8_t chiffres[4] = { 3, 1, 4, 1 };
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < chiffres[i]; k++)
            fdj_nav_geste(&n, 6, 0, GESTE_COURT, 80);
        if (i < 3) fdj_nav_geste(&n, 6, 1, GESTE_COURT, 80);
    }
    OK(fdj_nav_code(&n) == 3141, "le code se saisit avec deux boutons");
    OK(fdj_code_verifie(&relu, fdj_nav_code(&n)), "et il ouvre la maquette");
}

static void test_orphelins(void)
{
    /* etat corrompu : on repart sur l'usine, jamais sur du hasard */
    fdj_etat_t e;
    fdj_etat_defaut(&e, 1);
    uint8_t blob[2048];
    size_t nb = fdj_etat_serialise(&e, blob, sizeof(blob));
    blob[nb / 2] ^= 0x40;
    fdj_etat_t d;
    OK(!fdj_etat_deserialise(&d, blob, nb), "blob corrompu : refuse");
    memset(&d, 0xAA, sizeof(d));
    OK(!fdj_etat_valide(&d), "etat aleatoire : invalide");

    /* coupon vide : refuse proprement */
    char msg[48];
    fdj_etat_defaut(&e, 1);
    top.n = 0;
    OK(fdj_coupon_valide(&e, &top, NULL, NULL, 0, T0, msg, sizeof(msg)) == 1, "coupon vide refuse");
    OK(strcmp(msg, "AUCUNE SELECTION") == 0, "et le dit");
    OK(e.hist_n == 0, "rien n'est ecrit");
}

int main(void)
{
    test_cycle_pari();
    test_refus_plafond();
    test_navigation_complete();
    test_profil_et_code();
    test_orphelins();
    FIN("integration");
}
