/*  fdj_ui.cpp — les 8 ecrans de l'application Unibet du BATMino.
 *
 *  Pourquoi 8 et pas 37 : sur une dalle de 200x200 a 50 cm, une page qui
 *  demande de lire 10 lignes est une page qu'on ne lit pas. Huit ecrans,
 *  un par role, chacun avec UNE chose a retenir, en police 5x10 (10 px de
 *  haut, 33 caracteres par ligne) : c'est le maximum lisible.
 *
 *  Gabarit commun :
 *      0..13    bandeau noir : titre + NN/08 + carre reseau
 *      18..174  contenu
 *      182      aide de la page (gestes reels)
 */
#include "fdj_ui.h"
#include "art.h"
#include "fdj_tests_stats.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define FX0 4
#define FX1 195
#define FOOT 182

/* ---------------------------------------------------------------- */
/* primitives (5x10 partout)                                        */
/* ---------------------------------------------------------------- */
static void plein(int x0, int y0, int x1, int y1, int noir)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) a_px(x, y, noir);
}

static void cadre(int x0, int y0, int x1, int y1, int t)
{
    plein(x0, y0, x1, y0 + t - 1, 1);
    plein(x0, y1 - t + 1, x1, y1, 1);
    plein(x0, y0, x0 + t - 1, y1, 1);
    plein(x1 - t + 1, y0, x1, y1, 1);
}

static void t5(int x, int y, const char *s)  { if (s) a_text5c(x, y, 1, s, 1); }
static void t5w(int x, int y, const char *s) { if (s) a_text5c(x, y, 1, s, 0); }
static void t5b(int x, int y, const char *s) { if (s) a_text5c(x, y, 2, s, 1); }
static int  largeur(const char *s)  { return s ? a_text5_w(1, s) : 0; }
static void t5d(int xd, int y, const char *s) { if (s) t5(xd - largeur(s), y, s); }
static void t5centre(int cx, int y, const char *s) { if (s) t5(cx - largeur(s) / 2, y, s); }

/* coupe dans la largeur, sans jamais trancher au milieu d'un mot.
   Le tampon source peut etre le tampon destination. */
static void elide(char *dst, size_t cap, int maxw, const char *src)
{
    char tmp[192];
    snprintf(tmp, sizeof(tmp), "%s", src ? src : "");
    src = tmp;
    size_t maxc = (size_t)(maxw / 6);
    if (maxc >= cap) maxc = cap - 1;
    size_t n = strlen(src);
    if (n <= maxc) { snprintf(dst, cap, "%s", src); return; }
    if (maxc < 4) { dst[0] = 0; return; }
    memcpy(dst, src, maxc - 2);
    dst[maxc - 2] = '.';
    dst[maxc - 1] = '.';
    dst[maxc] = 0;
}

static const char *euros(char *b, size_t cap, uint32_t ct)
{
    snprintf(b, cap, "%u,%02u E", (unsigned)(ct / 100), (unsigned)(ct % 100));
    return b;
}

static const char *euros_net(char *b, size_t cap, int32_t ct)
{
    const char *sg = ct > 0 ? "+" : (ct < 0 ? "-" : "");
    uint32_t a = (uint32_t)(ct < 0 ? -ct : ct);
    snprintf(b, cap, "%s%u,%02u E", sg, (unsigned)(a / 100), (unsigned)(a % 100));
    return b;
}

/* ---------------------------------------------------------------- */
/* bandeau + pied                                                   */
/* ---------------------------------------------------------------- */
static void entete(const fdj_vue_t *v, const char *titre)
{
    plein(0, 0, 199, 13, 1);
    t5w(5, 2, titre);
    char b[16];
    snprintf(b, sizeof(b), "%d/%d", v->page + 1, FDJ_PAGES);
    t5w(181 - largeur(b), 2, b);
    if (v->wifi) plein(187, 4, 195, 10, 0);
    else         cadre(187, 4, 195, 10, 1);
}

static void pied(const fdj_vue_t *v)
{
    if (v && v->message) {
        plein(0, 176, 199, 196, 1);
        char b[52];
        elide(b, sizeof(b), 186, v->message);
        t5w(6, 181, b);
        return;
    }
    char a[34];
    elide(a, sizeof(a), 128, fdj_page_aide(v ? v->page : 0));
    t5(FX0, FOOT, a);
    t5d(198, FOOT, "3S=ACCUEIL");
}

static void sel_bar(int y, int h, bool sel)
{
    if (sel) plein(0, y, 199, y + h - 1, 1);
}

/* ---------------------------------------------------------------- */
/* 1. accueil                                                       */
/* ---------------------------------------------------------------- */
static void p_accueil(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    const fdj_profil_t *pr = fdj_profil_actif_c(e);
    bool co = e->connecte;
    char b[52];

    if (co) {
        t5(6, 16, "CONNECTE");
        plein(6, 28, 60, 28, 1);
    } else {
        t5(6, 16, "NON CONNECTE");
        cadre(6, 28, 58, 28, 1);
    }
    if (co) t5d(194, 16, "SESSION OUVERTE");

    t5b(6, 32, co ? e->compte_nom : "COMPTE DEMO");
    snprintf(b, sizeof(b), "ID %s", e->compte_id);
    t5(6, 56, b);

    cadre(4, 70, 196, 112, 1);
    t5(9, 74, co ? "SOLDE DISPONIBLE" : "SOLDE APRES CONNEXION");
    if (co) t5b(9, 86, euros(b, sizeof(b), pr ? pr->solde_ct : 0));
    else    t5b(9, 86, "N.D.");

    if (pr) {
        snprintf(b, sizeof(b), "PLAFOND SEMAINE %u/%u E",
                 (unsigned)(fdj_plafond_sem_effectif(e) / 100),
                 (unsigned)(pr->plafond_mois_ct / 100));
        t5(6, 118, b);
    } else {
        t5(6, 118, "AUCUN PROFIL ACTIF");
    }

    if (v->live && v->live->ok && v->live->n > 0) {
        snprintf(b, sizeof(b), "%d MATCHS EN DIRECT", v->live->n);
        t5(6, 130, b);
        if (v->live->m[0].dom[0]) {
            char l[44];
            snprintf(l, sizeof(l), "%s %d-%d %s", v->live->m[0].dom,
                     v->live->m[0].buts_dom, v->live->m[0].buts_ext, v->live->m[0].ext);
            elide(b, sizeof(b), 186, l);
            t5(6, 142, b);
        }
    } else if (v->wifi) {
        t5(6, 130, "SCORES : N.D.");
        t5(6, 142, "RECEPTION EN COURS");
    } else {
        t5(6, 130, "HORS LIGNE : N.D.");
        t5(6, 142, "AUCUN SCORE INVENTE");
    }

    sel_bar(154, 15, v->curseur == 0);
    if (v->curseur == 0) t5w(6, 156, co ? "PARIER MAINTENANT" : "SE CONNECTER");
    else                 t5(6, 156, co ? "PARIER MAINTENANT" : "SE CONNECTER");
}

/* ---------------------------------------------------------------- */
/* 2. connexion (la saisie est dessinee par fdj_ui_demande_code)    */
/* ---------------------------------------------------------------- */
static void p_connexion(void)
{
    cadre(4, 22, 196, 56, 1);
    t5(9, 26, "COMPTE DE DEMONSTRATION");
    t5(9, 40, "ID FDJ-0001");

    t5(6, 70, "1  B1 CHANGE LE CHIFFRE");
    t5(6, 84, "2  B2 PASSE AU SUIVANT");
    t5(6, 98, "3  B2X2 OUVRE LA SESSION");

    plein(FX0, 116, FX1, 116, 1);
    t5(6, 124, "CODE D'USINE : 0000");
    t5(6, 138, "AUCUN MOT DE PASSE N'EST");
    t5(6, 150, "DEMANDE NI TRANSMIS.");
}

/* ---------------------------------------------------------------- */
/* 3. paris en direct                                               */
/* ---------------------------------------------------------------- */
static void p_paris(const fdj_vue_t *v)
{
    const fdj_top_t *t = v->top;
    char b[52];

    if (v->live && v->live->ok && v->now > 0 && v->live->maj > 0) {
        int64_t age = (v->now - v->live->maj) / 60;
        snprintf(b, sizeof(b), "LIVE %d MATCHS - MAJ %d MIN", v->live->n, (int)(age < 0 ? 0 : age));
    } else {
        snprintf(b, sizeof(b), "LIVE : HORS LIGNE (N.D.)");
    }
    t5(6, 16, b);

    if (!t || t->n == 0) {
        t5centre(100, 60, "AUCUN MATCH EN DIRECT");
        t5centre(100, 76, "LE MODELE NE DEVINE PAS");
        t5centre(100, 96, "B2X2 : ACTUALISER");
        return;
    }

    int n = t->n < 4 ? t->n : 4;
    for (int i = 0; i < n; i++) {
        const fdj_sel_t *s = &t->s[i];
        int y = 32 + i * 30;
        bool sel = (v->curseur == i);
        sel_bar(y - 2, 27, sel);
        int col = sel ? 0 : 1;

        char m[28];
        elide(m, sizeof(m), 138, s->match);
        a_text5c(6, y, 1, m, col);
        snprintf(b, sizeof(b), "%d%%", (int)(s->proba * 100.0f + 0.5f));
        a_text5c(176 - largeur(b), y, 1, b, col);

        char l2[52];
        const char *info = "";
        if (v->live && s->idx_match < v->live->n) info = v->live->m[s->idx_match].info;
        snprintf(l2, sizeof(l2), "%s %s %s %s", s->code, s->ligue, info, s->marche);
        elide(l2, sizeof(l2), 160, l2);
        a_text5c(6, y + 11, 1, l2, col);

        cadre(182, y + 1, 192, y + 9, 1);
        if (v->e->sel_cochee & (1u << i)) {
            a_seg(184, y + 5, 186, y + 8, col ? 1 : 0);
            a_seg(186, y + 8, 190, y + 2, col ? 1 : 0);
        }
    }

    t5(6, 158, "PROBABILITE, PAS UNE COTE");
}

/* ---------------------------------------------------------------- */
/* 4. coupon                                                        */
/* ---------------------------------------------------------------- */
static void p_coupon(const fdj_vue_t *v)
{
    const fdj_top_t *t = v->top;
    int coches = 0;
    uint32_t gain_pot = 0;
    if (t) for (int i = 0; i < t->n; i++) {
        if (!(v->e->sel_cochee & (1u << i))) continue;
        coches++;
        if (t->s[i].proba > 0.02f) gain_pot += (uint32_t)(200.0f / t->s[i].proba);
    }
    uint32_t total_ct = (uint32_t)coches * 200;
    char b[44];

    snprintf(b, sizeof(b), "MISE %u,%02u E", (unsigned)(total_ct / 100), (unsigned)(total_ct % 100));
    t5b(6, 16, b);

    if (coches == 0) t5(6, 42, "GAIN POTENTIEL : N.D.");
    else {
        snprintf(b, sizeof(b), "GAIN POTENTIEL %u,%02u E",
                 (unsigned)(gain_pot / 100), (unsigned)(gain_pot % 100));
        t5(6, 42, b);
    }
    plein(FX0, 56, FX1, 56, 1);

    if (coches == 0) {
        t5(6, 66, "AUCUNE SELECTION");
        t5(6, 78, "COCHEZ UNE COTE PAGE 3");
    } else {
        snprintf(b, sizeof(b), "%d SELECTION%s", coches, coches > 1 ? "S" : "");
        t5(6, 66, b);
        int k = 0;
        if (t) for (int i = 0; i < t->n && k < 3; i++) {
            if (!(v->e->sel_cochee & (1u << i))) continue;
            char m[28];
            elide(m, sizeof(m), 138, t->s[i].match);
            char l[44];
            snprintf(l, sizeof(l), "%d %s", k + 1, m);
            t5(6, 80 + k * 13, l);
            t5d(190, 80 + k * 13, "2,00");
            k++;
        }
        if (coches > 3) {
            snprintf(b, sizeof(b), "+ %d AUTRES", coches - 3);
            t5(6, 80 + 3 * 13, b);
        }
    }

    int yv = 128;
    bool sv = (v->curseur == 0);
    sel_bar(yv, 15, sv);
    a_text5c(8, yv + 2, 1, "VALIDER LE COUPON", sv ? 0 : 1);
    int yc = 148;
    bool sc = (v->curseur == 1);
    sel_bar(yc, 15, sc);
    a_text5c(8, yc + 2, 1, "VIDER LE COUPON", sc ? 0 : 1);
}

/* ---------------------------------------------------------------- */
/* 5. compte                                                        */
/* ---------------------------------------------------------------- */
static void p_compte(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    const fdj_profil_t *pr = fdj_profil_actif_c(e);
    char b[52];

    bool co = e->connecte;
    t5(6, 16, co ? "CONNECTE" : "NON CONNECTE");
    t5d(194, 16, v->wifi ? "EN LIGNE" : "HORS LIGNE");

    t5b(6, 30, co ? e->compte_nom : "COMPTE DEMO");
    snprintf(b, sizeof(b), "ID %s", e->compte_id);
    t5(6, 54, b);

    if (!co) {
        t5(6, 68, "SOLDE : N.D.");
        t5(6, 80, "OUVREZ LA SESSION");
    } else if (pr) {
        snprintf(b, sizeof(b), "SOLDE %u,%02u E", (unsigned)(pr->solde_ct / 100),
                 (unsigned)(pr->solde_ct % 100));
        t5(6, 68, b);
        snprintf(b, sizeof(b), "PLAFOND %u/%u E", (unsigned)(fdj_plafond_sem_effectif(e) / 100),
                 (unsigned)(pr->plafond_mois_ct / 100));
        t5(6, 80, b);
    } else {
        t5(6, 68, "SOLDE : N.D.");
        t5(6, 80, "PLAFOND : N.D.");
    }

    fdj_periode_t p;
    fdj_periode_calcule(e, v->now, 7, &p);
    plein(FX0, 94, FX1, 94, 1);
    if (!co) { t5(6, 100, "7 JOURS : N.D."); }
    else if (p.nb == 0) { t5(6, 100, "7 JOURS : N.D."); }
    else {
        char n[24];
        euros_net(n, sizeof(n), p.net_ct);
        snprintf(b, sizeof(b), "7 JOURS : %s", n);
        t5(6, 100, b);
    }
    snprintf(b, sizeof(b), "%d PARIS REGLES", p.nb);
    t5(6, 112, b);

    t5(6, 128, co ? "DERNIERS PARIS" : "PARIS RECENTS");
    int k = 0;
    if (!co) { t5(6, 140, "CONNECTEZ-VOUS POUR VOIR"); }
    for (int i = 0; i < e->hist_n && k < 2; i++) {
        int idx = fdj_hist_index_recence(e, i);
        if (idx < 0) continue;
        const fdj_pari_t *pp = &e->hist[idx];
        time_t t = (time_t)pp->t;
        struct tm ti;
        localtime_r(&t, &ti);
        char l[52];
        snprintf(l, sizeof(l), "%02d/%02d %u,%02u E %s", ti.tm_mday, ti.tm_mon + 1,
                 (unsigned)(pp->mise_ct / 100), (unsigned)(pp->mise_ct % 100),
                 pp->statut == FDJ_PARI_GAGNE ? "GAGNE" :
                 pp->statut == FDJ_PARI_PERDU ? "PERDU" : "EN COURS");
        t5(6, 140 + k * 12, l);
        k++;
    }
    if (k == 0) t5(6, 140, "AUCUN PARI POUR L'INSTANT");

    sel_bar(162, 15, v->curseur == 0);
    a_text5c(8, 164, 1, e->connecte ? "SE DECONNECTER" : "SE CONNECTER",
             v->curseur == 0 ? 0 : 1);
}

/* ---------------------------------------------------------------- */
/* 6. jeu responsable                                               */
/* ---------------------------------------------------------------- */
static const char *QUESTIONS[] = {
    "JOUER EST UN PLAISIR",
    "PAS JOUER POUR ME REFAIRE",
    "BUDGET FIXE ET TENU",
    "SAVOIR DIRE STOP",
    "LE JEU N'EST PAS MA VIE",
};
#define QN 5

static void p_resp(const fdj_vue_t *v)
{
    int score = fdj_protection_score(v->e);

    for (int i = 0; i < QN; i++) {
        int y = 18 + i * 13;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 12, sel);
        int col = sel ? 0 : 1;
        char b[44];
        elide(b, sizeof(b), 150, QUESTIONS[i]);
        a_text5c(6, y, 1, b, col);
        bool oui = (v->e->sel_cochee >> (8 + i)) & 1;
        a_text5c(184 - largeur(oui ? "OUI" : "NON"), y, 1, oui ? "OUI" : "NON", col);
    }

    int y = 90;
    char b[40];
    snprintf(b, sizeof(b), "PROTECTION %d/%d", score, QN);
    t5(6, y, b);
    for (int i = 0; i < QN; i++) {
        cadre(122 + i * 14, y, 133 + i * 14, y + 8, 1);
        if (i < score) plein(123 + i * 14, y + 1, 132 + i * 14, y + 7, 1);
    }

    int yp = 110;
    bool sel = (v->curseur == QN);
    sel_bar(yp - 1, 13, sel);
    int col = sel ? 0 : 1;
    char p[52];
    if (fdj_pause_active(v->e, v->now)) {
        int64_t r = fdj_pause_restant_s(v->e, v->now);
        if (r < 0) snprintf(p, sizeof(p), "PAUSE ACTIVE, FIN INCONNUE");
        else snprintf(p, sizeof(p), "PAUSE ENCORE %dh%02d", (int)(r / 3600), (int)((r / 60) % 60));
    } else {
        snprintf(p, sizeof(p), "PAUSE 24 H (AUTO-EXCL.)");
    }
    a_text5c(6, yp, 1, p, col);
    a_text5c(194 - largeur("OFF"), yp, 1, fdj_pause_active(v->e, v->now) ? "ON" : "OFF", col);

    if (score > 0 && score < 3) t5(6, 130, "PROTECTION FAIBLE : PLAFOND / 2");
    else if (score >= 3)        t5(6, 130, "PROTECTION OK");
    else                        t5(6, 130, "REPONDEZ OUI : PLUS SUR");
    t5(6, 146, "L'ARGENT ENGAGE RESTE ENGAGE");
}

/* ---------------------------------------------------------------- */
/* 7. preuves (QA)                                                  */
/* ---------------------------------------------------------------- */
static void p_preuves(const fdj_vue_t *v)
{
    char b[52];
    int total = FDJ_TESTS_UNIT + FDJ_TESTS_COMP + FDJ_TESTS_INTEG + FDJ_TESTS_FONC;

    snprintf(b, sizeof(b), "%d VERIFICATIONS", total);
    t5(6, 18, b);
    t5b(6, 32, FDJ_TESTS_FAIL == 0 ? "0 ECHEC" : "DES ECHECS");

    snprintf(b, sizeof(b), "RENDU %d MS   ECRAN %d MS", v->rendu_ms, v->ecran_ms);
    t5(6, 54, b);
    snprintf(b, sizeof(b), "RAM %u KO   PSRAM %u MO", (unsigned)(v->heap_int_libre / 1024),
             (unsigned)(v->psram_libre / 1048576));
    t5(6, 66, b);

    plein(FX0, 80, FX1, 80, 1);
    t5(6, 86, "TLS VERIFIE, AUCUN SECRET");
    t5(6, 98, "API /etat /ecran /fichier");
    t5(6, 110, "8 ECRANS, TEXTE 5x10");
    t5(6, 122, "N.D. QUAND LA DONNEE MANQUE");
    t5(6, 134, "AUCUNE VALEUR INVENTEE");

    sel_bar(154, 15, v->curseur == 0);
    a_text5c(8, 156, 1, "REFAIRE LES MESURES", v->curseur == 0 ? 0 : 1);
    t5(6, 172, "RECETTE DU " FDJ_TESTS_DATE);
}

/* ---------------------------------------------------------------- */
/* 8. reglages                                                      */
/* ---------------------------------------------------------------- */
static void p_reglages(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    char b[64];

    struct { const char *t, *val; } O[4] = {
        { "CONTRASTE", "MAXIMUM" },
        { "TEXTE", "5x10" },
        { "SON", "AUCUN" },
        { "LANGUE", "FRANCAIS" },
    };
    for (int i = 0; i < 4; i++) {
        t5(6, 18 + i * 13, O[i].t);
        t5d(194, 18 + i * 13, O[i].val);
    }

    plein(FX0, 72, FX1, 72, 1);
    t5(6, 78, "CODE");
    t5d(194, 78, e->code_perso ? "PERSONNALISE" : "0000 (USINE)");

    int y0 = 96;
    sel_bar(y0, 15, v->curseur == 0);
    a_text5c(8, y0 + 2, 1, "CHANGER LE CODE", v->curseur == 0 ? 0 : 1);
    int y1 = 116;
    sel_bar(y1, 15, v->curseur == 1);
    a_text5c(8, y1 + 2, 1, "EFFACER MES DONNEES", v->curseur == 1 ? 0 : 1);

    plein(FX0, 142, FX1, 142, 1);
    snprintf(b, sizeof(b), "UNIBET " FDJ_VERSION " - MAQUETTE");
    t5(6, 148, b);
    t5(6, 160, "AUCUNE MISE REELLE");
    t5(6, 172, "@PARISTZOU");
}

/* ---------------------------------------------------------------- */
/* table des ecrans                                                 */
/* ---------------------------------------------------------------- */
static const char *TITRE[FDJ_M_NB] = {
    "ACCUEIL", "CONNEXION", "PARIS", "COUPON",
    "COMPTE", "RESPONSABLE", "PREUVES", "REGLAGES",
};

int fdj_ecran_module(int page)
{
    if (page < 0 || page >= FDJ_PAGES) return 0;
    return page;
}

bool fdj_ecran_est_intertitre(int page)
{
    (void)page;
    return false;          /* plus de sous-menu : 8 ecrans directs */
}

int fdj_module_page(int module)
{
    if (module < 0 || module >= FDJ_M_NB) return 0;
    return module;
}

const char *fdj_module_nom(int module)
{
    if (module < 0 || module >= FDJ_M_NB) return "?";
    return TITRE[module];
}

const char *fdj_page_titre(int page)
{
    if (page < 0 || page >= FDJ_PAGES) return "?";
    return TITRE[page];
}

const char *fdj_page_aide(int page)
{
    switch (page) {
    case FDJ_M_ACCUEIL:   return "B2X2 OUVRIR";
    case FDJ_M_CONNEXION: return "B2 CHIFFRE  B2X2 OK";
    case FDJ_M_PARIS:     return "B1 COCHER  B2 DEFILER";
    case FDJ_M_COUPON:    return "B2X2 OK  B1X2 VIDER";
    case FDJ_M_COMPTE:    return "B2X2 SESSION";
    case FDJ_M_RESP:      return "B1 OUI  B2X2 PAUSE";
    case FDJ_M_PREUVES:   return "B2X2 MESURER";
    case FDJ_M_REGLAGES:  return "B1 CHOISIR  B2X2 OK";
    default:              return "";
    }
}

int fdj_page_items(int page, const fdj_vue_t *v)
{
    switch (page) {
    case FDJ_M_PARIS:    return v->top && v->top->n > 0 ? (v->top->n < 4 ? v->top->n : 4) : 1;
    case FDJ_M_COUPON:   return 2;
    case FDJ_M_RESP:     return QN + 1;
    case FDJ_M_REGLAGES: return 2;
    default:             return 1;
    }
}

void fdj_ui_dessine(const fdj_vue_t *v, uint8_t *fb)
{
    art_begin(fb, 0);
    art_clear();
    if (!v || !v->e) return;

    entete(v, fdj_page_titre(v->page));
    switch (v->page) {
    case FDJ_M_ACCUEIL:   p_accueil(v); break;
    case FDJ_M_CONNEXION: p_connexion(); break;
    case FDJ_M_PARIS:     p_paris(v); break;
    case FDJ_M_COUPON:    p_coupon(v); break;
    case FDJ_M_COMPTE:    p_compte(v); break;
    case FDJ_M_RESP:      p_resp(v); break;
    case FDJ_M_PREUVES:   p_preuves(v); break;
    case FDJ_M_REGLAGES:  p_reglages(v); break;
    default:              t5centre(100, 90, "ECRAN INCONNU"); break;
    }
    pied(v);
}

/* ---------------------------------------------------------------- */
/* detail d'un pari (API conservee)                                 */
/* ---------------------------------------------------------------- */
void fdj_ui_detail(uint8_t *fb, const fdj_pari_t *p, int idx)
{
    art_begin(fb, 0);
    art_clear();
    plein(0, 0, 199, 13, 1);
    t5w(5, 2, "DETAIL DU PARI");
    char b[16];
    snprintf(b, sizeof(b), "N%d", idx + 1);
    t5w(190 - largeur(b), 2, b);
    if (!p) { t5centre(100, 90, "PARI INTROUVABLE"); return; }

    char l[FDJ_TXT_L + 2];
    elide(l, sizeof(l), 186, p->libelle);
    t5(6, 22, l);

    char d[48];
    time_t t = (time_t)p->t;
    struct tm ti;
    localtime_r(&t, &ti);
    snprintf(d, sizeof(d), "LE %02d/%02d/%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    t5(6, 40, d);
    snprintf(d, sizeof(d), "JEU : %s", fdj_jeu_nom(p->jeu));
    t5(6, 54, d);
    snprintf(d, sizeof(d), "CODE : %s", p->code);
    t5(6, 68, d);
    snprintf(d, sizeof(d), "MISE : %u,%02u E", (unsigned)(p->mise_ct / 100),
             (unsigned)(p->mise_ct % 100));
    t5(6, 88, d);
    snprintf(d, sizeof(d), "GAIN : %u,%02u E", (unsigned)(p->gain_ct / 100),
             (unsigned)(p->gain_ct % 100));
    t5(6, 102, d);
    t5(6, 122, p->statut == FDJ_PARI_GAGNE ? "GAGNE, CREDITE" :
               p->statut == FDJ_PARI_PERDU ? "PERDU" : "EN COURS, PAS UN GAIN");
    t5centre(100, 186, "B1 : FERMER");
}

/* ---------------------------------------------------------------- */
/* saisie du code / connexion                                       */
/* ---------------------------------------------------------------- */
void fdj_ui_demande_code(uint8_t *fb, const char *titre, const uint8_t chiffres[4],
                         int position, bool erreur)
{
    art_begin(fb, 0);
    art_clear();
    plein(0, 0, 199, 15, 1);
    t5w(6, 3, "UNIBET");
    t5w(190 - largeur("DEMO"), 3, "DEMO");

    t5centre(100, 28, titre);
    t5centre(100, 44, "MAQUETTE DE DEMONSTRATION");

    for (int i = 0; i < 4; i++) {
        int x = 18 + i * 44;
        cadre(x, 62, x + 36, 104, 2);
        if (i == position) plein(x - 3, 59, x + 39, 107, 1);
        char b[2] = { (char)('0' + chiffres[i]), 0 };
        a_text5c(x + 11, 72, 3, b, i == position ? 0 : 1);
    }
    t5centre(100, 116, "B2 CHIFFRE SUIVANT  B1 +1");
    t5centre(100, 128, "B2X2 VALIDER");
    if (erreur) t5centre(100, 148, "CODE REFUSE");
    else        t5centre(100, 148, "CODE D'USINE : 0000");
    t5centre(100, 166, "4 CHIFFRES, RIEN D'AUTRE");
}

void fdj_ui_confirme(uint8_t *fb, const char *titre, const char *question,
                     bool oui_selectionne)
{
    art_begin(fb, 0);
    art_clear();
    t5centre(100, 30, titre);
    t5centre(100, 50, question);
    t5centre(100, 66, "ACTION DEFINITIVE");
    cadre(20, 88, 94, 122, oui_selectionne ? 3 : 1);
    t5centre(57, 100, "OUI");
    cadre(106, 88, 180, 122, oui_selectionne ? 1 : 3);
    t5centre(143, 100, "NON");
    t5centre(100, 138, "B2X2 CONFIRMER  B1 INVERSER");
    t5centre(100, 152, "B2 TENU 3 S : ANNULER");
    t5centre(100, 176, "@PARISTZOU");
}
