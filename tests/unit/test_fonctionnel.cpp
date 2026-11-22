/*  test_fonctionnel.cpp — la demo, de bout en bout, avec de VRAIES donnees.
 *
 *  Scenario joue exactement comme au doigt :
 *    boot -> code 0000 -> page 2 (scores reels) -> deux paris -> coupon ->
 *    historique -> gains -> les 20 pages -> sauvegarde -> redemarrage.
 *  Aucune donnee n'est inventee : les scores viennent des fixtures serveur.
 */
#include "check.h"
#include "fdj_model.h"
#include "fdj_parse.h"
#include "fdj_ui.h"
#include "fdj_nav.h"
#include "art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t fbm[ART_FB_LEN];
static fdj_etat_t e;
static fdj_live_t live;
static fdj_flux_t flux;
static fdj_top_t top;
static fdj_nav_t nav;
static fdj_vue_t v;

static char *lire(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)l + 1);
    if (!b) { fclose(f); return NULL; }
    size_t lu = fread(b, 1, (size_t)l, f);
    b[lu] = 0;
    fclose(f);
    if (n) *n = (long)lu;
    return b;
}

static int noirs(void)
{
    int n = 0;
    for (int i = 0; i < ART_FB_LEN; i++) {
        uint8_t b = fbm[i];
        while (b) { n += b & 1; b >>= 1; }
    }
    return n;
}

int main(void)
{
    int64_t now = 1760000000LL;

    /* ---- 1. les donnees vivantes, telles qu'elles arrivent ---- */
    long n = 0;
    char *js = lire("tests/fixtures/thesportsdb_livescore.json", &n);
    OK(js != NULL, "fixture des scores presente");
    if (!js) { FIN("fonctionnel"); }

    memset(&live, 0, sizeof(live));
    int nb = fdj_parse_events(js, false, &live);
    free(js);
    OK(nb > 0, "les scores en direct se lisent");
    OK(live.n == nb && live.ok, "le flux est marque disponible");
    live.maj = now;

    char *rss = lire("tests/fixtures/franceinfo.rss", &n);
    OK(rss != NULL, "fixture des titres presente");
    if (rss) {
        memset(&flux, 0, sizeof(flux));
        int nn = fdj_parse_news(rss, &flux);
        free(rss);
        OK(nn > 0, "les titres se lisent");
        flux.maj = now;
    }

    /* ---- 2. le top 10, calcule sur ces scores ---- */
    fdj_etat_defaut(&e, 20261120);
    uint32_t g = e.graine;
    int k = fdj_top_calcule(&live, &g, now, &top);
    OK(k > 0, "le modele sort des selections");
    OK(k <= FDJ_SEL_MAX, "au plus 10");
    char msg[48];
    for (int i = 0; i < top.n; i++) {
        OK(top.s[i].proba > 0.0f && top.s[i].proba < 1.0f, "proba strictement entre 0 et 1");
        OK(strlen(top.s[i].code) == 3, "code de coupon a 3 caracteres");
        OK(top.s[i].match[0] != 0, "un match nomme");
    }

    /* ---- 3. le boot : le code 0000 ouvre ---- */
    fdj_nav_init(&nav, 0, 0);
    fdj_nav_mode(&nav, FDJ_MODE_CODE);
    OK(fdj_code_verifie(&e, fdj_nav_code(&nav)), "0000 ouvre la maquette sans toucher a rien");
    fdj_nav_mode(&nav, FDJ_MODE_NAV);

    /* ---- 4. le parcours au doigt ---- */
    fdj_nav_mode(&nav, FDJ_MODE_NAV);
    nav.page = 2;                  /* page paris (3/20) */
    nav.curseur = 0;
    for (int i = 0; i < 2 && i < top.n; i++) {
        nav.curseur = (uint8_t)i;
        OK(fdj_nav_geste(&nav, top.n, 0, GESTE_COURT, 80) == FDJ_ACT_COCHER, "coche #1");
        e.sel_cochee |= (uint16_t)(1u << i);
    }
    uint32_t solde_avant = e.profils[0].solde_ct;
    OK(fdj_coupon_valide(&e, &top, NULL, NULL, 0, now, msg, sizeof(msg)) == 0, "coupon accepte");
    OK(e.hist_n == 2, "deux lignes d'historique");
    OK(e.profils[0].solde_ct == solde_avant - 400, "4,00 EUR engages");
    OK(e.sel_cochee == 0, "cases remises a zero");

    /* ---- 5. l'ecran suit : les pages cles sont rendues ---- */
    memset(&v, 0, sizeof(v));
    v.e = &e; v.top = &top; v.news = &flux; v.live = &live;
    v.now = now; v.wifi = true; v.ssid = "ESPRESSIF";
    v.heap_int_libre = 100 * 1024; v.heap_int_max = 60 * 1024;
    v.psram_libre = 7 * 1024 * 1024; v.sd_ok = true;
    v.rendu_ms = 9; v.ecran_ms = 460;
    v.nb_grilles = FDJ_GRILLE_MAX;
    uint32_t sg = e.graine;
    for (int i = 0; i < FDJ_GRILLE_MAX; i++) fdj_loto_grille(&sg, v.grilles[i], &v.chances[i]);

    int pages_ok = 0;
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p;
        v.curseur = 0;
        memset(fbm, 0xFF, sizeof(fbm));
        fdj_ui_dessine(&v, fbm);
        if (noirs() > 150) pages_ok++;
    }
    OK(pages_ok == FDJ_PAGES, "les 8 ecrans s'affichent avec les vraies donnees");

    /* la page paris doit montrer des selections, pas un message d'attente */
    v.page = 2;
    fdj_ui_dessine(&v, fbm);
    OK(fdj_page_items(2, &v) == (top.n < 4 ? top.n : 4), "la page paris liste 4 selections max");
    OK(noirs() > 800, "la page paris est dense (10 lignes + titres)");

    /* ---- 6. persistance : on eteint et on rallume ---- */
    e.page = nav.page;
    e.curseur = nav.curseur;
    uint8_t blob[2048];
    size_t nb_blob = fdj_etat_serialise(&e, blob, sizeof(blob));
    fdj_etat_t relu;
    PRES(fdj_etat_deserialise(&relu, blob, nb_blob));
    OK(relu.hist_n == 2, "les paris sont toujours la apres redemarrage");
    OK(relu.profils[0].solde_ct == e.profils[0].solde_ct, "le solde aussi");
    OK(fdj_etat_valide(&relu), "l'etat relu est valide");

    /* ---- 7. la semaine affichee correspond a l'historique ---- */
    fdj_periode_t p7, p30;
    fdj_periode_calcule(&relu, now, 7, &p7);
    fdj_periode_calcule(&relu, now, 30, &p30);
    OK(p7.nb == 2 && p7.mise_ct == 400, "7 jours : 2 paris, 4,00 EUR");
    OK(p30.mise_ct == 400, "30 jours coherent");
    OK(p7.net_ct == -400, "net negatif : les paris sont en cours, rien n'est gagne");

    /* ---- 8. performance de rendu (sur PC ; la carte est ~1,5x plus lente) ---- */
    int64_t t0 = (int64_t)clock();
    for (int tour = 0; tour < 100; tour++) {
        v.page = tour % FDJ_PAGES;
        fdj_ui_dessine(&v, fbm);
    }
    int64_t ms = ((int64_t)clock() - t0) * 1000 / CLOCKS_PER_SEC;
    OKF(ms < 3000, "100 rendus complets en %lld ms", (long long)ms);

    /* ---- 9. memoire du modele ---- */
    OK(sizeof(fdj_etat_t) < 4096, "etat < 4 Ko");
    OK(sizeof(fdj_live_t) < 2048, "matchs < 2 Ko");
    OK(sizeof(fdj_top_t) < 2048, "top 10 < 2 Ko");

    FIN("fonctionnel");
}
