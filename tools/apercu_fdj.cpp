/*  apercu_fdj.cpp — rend les 19 pages sur le PC, avec des donnees REELLES
 *  (fixtures serveur) et ecrit un .fb puis un PNG par page.
 *
 *      g++ -std=c++17 -I main -I /usr/include/cjson -o build/tests/apercu_fdj \
 *          tools/apercu_fdj.cpp main/fdj_ui.cpp main/fdj_parse.cpp \
 *          main/fdj_model.cpp main/art.cpp main/font5.cpp -lcjson -lm
 *      ./build/tests/apercu_fdj build/apercu
 *      puis fb2png.py sur chaque .fb du dossier
 */
#include "fdj_model.h"
#include "fdj_parse.h"
#include "fdj_ui.h"
#include "art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t fbuf[ART_FB_LEN];

static char *lire(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t l = fread(b, 1, (size_t)n, f);
    b[l] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    const char *dossier = argc > 1 ? argv[1] : "build/apercu";
    int64_t now = (int64_t)time(NULL);

    fdj_etat_t e;
    fdj_etat_defaut(&e, 20261120u);
    e.boots = 7;
    fdj_profil_ajoute(&e, "DEBUTANT", 0, 1000, 3000);
    fdj_profil_ajoute(&e, "EXPERT", 2, 5000, 20000);
    fdj_profil_ajoute(&e, "INVITE", 1, 2000, 4000);
    e.profils[0].solde_ct = 8720;

    /* historique realiste : 14 paris etales sur l'annee */
    struct { const char *l; const char *c; uint32_t mise, gain; int jours; uint8_t j, st; } H[] = {
        { "TROYES-LE HAVRE  +2,5", "7KQ",  500, 1100, 1,  FDJ_JEU_SPORT, FDJ_PARI_GAGNE },
        { "ROME-MILAN  N",         "PZ4",  200, 0,    1,  FDJ_JEU_SPORT, FDJ_PARI_PERDU  },
        { "LOTO GRILLE 1",         "M3T",  220, 0,    2,  FDJ_JEU_LOTO,  FDJ_PARI_PERDU  },
        { "DORTMUND-BAYERN  2",    "Q9W",  500, 950,  3,  FDJ_JEU_SPORT, FDJ_PARI_GAGNE },
        { "KENO 6 NUMEROS",        "K7P",  200, 0,    5,  FDJ_JEU_KENO,  FDJ_PARI_PERDU  },
        { "PSG-OM  1",             "A2B",  1000, 1800, 8,  FDJ_JEU_SPORT, FDJ_PARI_GAGNE },
        { "EUROMILLIONS",          "E5R",  250, 0,    12, FDJ_JEU_EUROMILLIONS, FDJ_PARI_PERDU },
        { "LENS-NICE  BTTS O",     "T4Y",  300, 540,  16, FDJ_JEU_SPORT, FDJ_PARI_GAGNE },
        { "LOTO GRILLE 2",         "M8C",  220, 0,    21, FDJ_JEU_LOTO,  FDJ_PARI_PERDU  },
        { "MILAN-ROME  -2,5",      "H6N",  400, 0,    40, FDJ_JEU_SPORT, FDJ_PARI_PERDU  },
        { "BREST-LYON  2",         "B1V",  200, 460,  70, FDJ_JEU_SPORT, FDJ_PARI_GAGNE },
        { "ILLIKO 5 E",            "I3D",  500, 0,    95, FDJ_JEU_ILLIKO, FDJ_PARI_PERDU },
        { "LOTO GRILLE 3",         "L9S",  220, 1200, 150, FDJ_JEU_LOTO, FDJ_PARI_GAGNE },
        { "REIMS-METZ  +1,5",      "R2F",  300, 0,    200, FDJ_JEU_SPORT, FDJ_PARI_PERDU },
    };
    for (size_t i = 0; i < sizeof(H) / sizeof(H[0]); i++)
        fdj_pari_ajoute(&e, H[i].l, H[i].c, H[i].mise, H[i].gain, now - (int64_t)H[i].jours * 86400,
                        H[i].j, H[i].st);
    e.hist[fdj_hist_index_recence(&e, 0)].t = now;      /* un pari du jour */

    /* donnees vivantes : les fixtures sont de vraies reponses serveur */
    static fdj_live_t live;
    static fdj_flux_t flux;
    static fdj_top_t top;
    char *js = lire("tests/fixtures/thesportsdb_livescore.json");
    if (js) { fdj_parse_events(js, false, &live); free(js); }
    live.maj = now;
    char *rss = lire("tests/fixtures/franceinfo.rss");
    if (rss) { fdj_parse_news(rss, &flux); free(rss); }
    flux.maj = now;
    uint32_t g = e.graine;
    fdj_top_calcule(&live, &g, now, &top);

    /* etat de demonstration : quelques cases cochees */
    e.sel_cochee = 0x0005;
    e.grille_cochee = 0x0001;
    e.news_lu = 0x0002;
    e.vpn = true;

    fdj_vue_t v;
    memset(&v, 0, sizeof(v));
    v.e = &e;
    v.top = &top;
    v.news = &flux;
    v.live = &live;
    v.now = now;
    v.ssid = "ESPRESSIF";
    v.wifi = true;
    v.rssi = -52;
    v.bat_pct = 87;
    v.heap_int_libre = 138 * 1024;
    v.heap_int_max = 96 * 1024;
    v.psram_libre = 7730 * 1024;
    v.sd_ok = true;
    v.rendu_ms = 11;
    v.ecran_ms = 480;

    /* trois grilles de loto, tirees une fois */
    uint32_t sg = e.graine ^ 0xBEEF;
    v.nb_grilles = FDJ_GRILLE_MAX;
    for (int i = 0; i < FDJ_GRILLE_MAX; i++)
        fdj_loto_grille(&sg, v.grilles[i], &v.chances[i]);

    char chemin[256];
    for (int p = 0; p < FDJ_PAGES; p++) {
        v.page = p;
        v.curseur = p == 1 ? 2 : (p == 6 ? 0 : 0);
        fdj_ui_dessine(&v, fbuf);
        snprintf(chemin, sizeof(chemin), "%s/p%02d.fb", dossier, p + 1);
        FILE *f = fopen(chemin, "wb");
        if (!f) { perror(chemin); return 1; }
        fwrite(fbuf, 1, ART_FB_LEN, f);
        fclose(f);
        printf("%s  %-18s items=%d\n", chemin, fdj_page_titre(p), fdj_page_items(p, &v));
    }

    /* l'ecran de code et la confirmation */
    uint8_t ch[4] = { 0, 0, 0, 0 };
    fdj_ui_demande_code(fbuf, "CODE DE DEMARRAGE", ch, 1, false);
    snprintf(chemin, sizeof(chemin), "%s/pin.fb", dossier);
    FILE *f = fopen(chemin, "wb");
    if (f) { fwrite(fbuf, 1, ART_FB_LEN, f); fclose(f); printf("%s  CODE\n", chemin); }

    fdj_ui_confirme(fbuf, "EFFACER MES DONNEES", "TOUT PERDRE, POUR DE BON ?", false);
    snprintf(chemin, sizeof(chemin), "%s/confirme.fb", dossier);
    f = fopen(chemin, "wb");
    if (f) { fwrite(fbuf, 1, ART_FB_LEN, f); fclose(f); printf("%s  CONFIRMATION\n", chemin); }
    return 0;
}
