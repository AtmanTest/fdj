/*  fdj_ui.cpp — les 19 pages. Meme code sur la carte et dans l'apercu PNG.
 *
 *  Contraintes de la dalle, respectees partout :
 *    - 200x200, 1 bit, pas de gris : le contraste se fait au trait plein ;
 *    - police 3x5, avance 4*s px : 24 caracteres max en s=2, 48 en s=1 ;
 *    - le titre de la page est TOUJOURS en haut, souligne, avec son numero ;
 *    - le pied rappelle les gestes, il ne bouge jamais.
 */
#include "fdj_ui.h"
#include "art.h"
#include "fdj_tests_stats.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ---------------------------------------------------------------- */
/* primitives de mise en page                                        */
/* ---------------------------------------------------------------- */
#define FX0 5
#define FX1 194
#define FTOP 18
#define FBOT 174

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

static void txt(int x, int y, int s, const char *t) { if (t) a_text(x, y, s, t); }
static void txt_blanc(int x, int y, int s, const char *t) { if (t) a_text_col(x, y, s, t, 0); }

static int largeur(int s, const char *t) { return t ? a_text_w(s, t) : 0; }

static void txt_droite(int xd, int y, int s, const char *t)
{
    if (!t) return;
    a_text(xd - largeur(s, t), y, s, t);
}

static void txt_centre(int cx, int y, int s, const char *t)
{
    if (!t) return;
    a_text(cx - largeur(s, t) / 2, y, s, t);
}

/* copie elidee : "SAINT-ETIENNE" -> "SAINT-ETI..." dans la largeur voulue */
static void elide(char *dst, size_t cap, int s, int maxw, const char *src)
{
    size_t maxc = (size_t)(maxw / (4 * s));
    if (maxc >= cap) maxc = cap - 1;
    size_t n = src ? strlen(src) : 0;
    if (n <= maxc) { snprintf(dst, cap, "%s", src ? src : ""); return; }
    if (maxc < 3) { dst[0] = 0; return; }
    memcpy(dst, src, maxc - 2);
    dst[maxc - 2] = '.';
    dst[maxc - 1] = '.';
    dst[maxc] = 0;
}

/* "12,50 EUR" : la police n'a pas le signe euro, on ecrit E. */
static const char *euros(char *b, size_t cap, uint32_t ct, bool signe)
{
    uint32_t e = ct / 100, c = ct % 100;
    snprintf(b, cap, "%s%u,%02u E", signe && ct ? "+" : "", (unsigned)e, (unsigned)c);
    return b;
}

/* ---------------------------------------------------------------- */
/* bandeau de titre + pied                                           */
/* ---------------------------------------------------------------- */
static void entete(const fdj_vue_t *v, const char *titre)
{
    txt(FX0, 3, 2, titre);
    char b[16];
    snprintf(b, sizeof(b), "%02d/19", v->page + 1);
    txt_droite(168, 4, 1, b);
    plein(FX0, 15, FX1, 16, 1);
    /* etat reseau, discret, a gauche du numero */
    ic_wifi(181, 9, v->wifi);
}

static void pied(const fdj_vue_t *v)
{
    plein(FX0, 178, FX1, 179, 1);
    if (v && v->message) {
        plein(0, 181, 199, 199, 1);
        char b[32];
        elide(b, sizeof(b), 1, 192, v->message);
        txt_blanc(6, 187, 1, b);
        return;
    }
    txt(FX0, 183, 1, "B2:SUIV  B2X2:OK  B2 3S:DEBUT");
    txt_droite(FX1, 183, 1, "B1:COCHE");
}

/* barre de selection : pleine largeur, texte inverse */
static void curseur_barre(int y, int h, bool sel)
{
    if (!sel) return;
    plein(0, y, 199, y + h - 1, 1);
}

static void coche(int x, int y, bool on)
{
    cadre(x, y, x + 8, y + 8, 1);
    if (on) {
        a_seg(x + 2, y + 4, x + 4, y + 7, 1);
        a_seg(x + 4, y + 7, x + 7, y + 1, 1);
    }
}

/* ---------------------------------------------------------------- */
/* page 1 — ACCUEIL                                                 */
/* ---------------------------------------------------------------- */
static void p_accueil(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    const fdj_profil_t *pr = fdj_profil_actif_c(e);

    /* le logo FDJ : un bloc plein, trois lettres, lisible de loin */
    plein(6, 20, 70, 56, 1);
    txt_blanc(15, 26, 4, "FDJ");
    txt_blanc(9, 47, 1, "PARIS EN LIGNE");

    txt(78, 21, 2, "BATMINO");
    txt(78, 33, 1, "MAQUETTE FDJ 2026");
    char quand[26];
    if (!v->heure_ok) {
        snprintf(quand, sizeof(quand), "HEURE NON SYNCHRONISEE");
    } else {
        time_t t = (time_t)v->now;
        struct tm ti;
        localtime_r(&t, &ti);
        static const char *J[] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
        snprintf(quand, sizeof(quand), "%s %02d/%02d %02d:%02d",
                 J[ti.tm_wday % 7], ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
    }
    txt(78, 42, 1, quand);

    /* solde du profil actif */
    cadre(6, 62, 122, 96, 1);
    txt(10, 66, 1, "SOLDE DISPONIBLE");
    char b[32];
    txt(10, 75, 3, euros(b, sizeof(b), pr ? pr->solde_ct : 0, false));

    /* profil */
    cadre(128, 62, 194, 96, 1);
    txt(132, 66, 1, "PROFIL");
    char nom[14];
    elide(nom, sizeof(nom), 1, 60, pr ? pr->nom : "AUCUN");
    txt(132, 74, 2, nom);
    snprintf(b, sizeof(b), "%u/%u E", (unsigned)(pr ? pr->plafond_sem_ct / 100 : 0),
             (unsigned)(pr ? pr->plafond_mois_ct / 100 : 0));
    txt(132, 86, 1, b);

    /* etat du direct */
    plein(6, 102, 194, 103, 1);
    if (v->live && v->live->ok && v->live->n > 0) {
        snprintf(b, sizeof(b), "%d MATCHS EN DIRECT", v->live->n);
        txt(10, 108, 2, b);
        if (v->live->m[0].dom[0]) {
            char l[40];
            snprintf(l, sizeof(l), "%s %d-%d %s", v->live->m[0].dom,
                     v->live->m[0].buts_dom, v->live->m[0].buts_ext, v->live->m[0].ext);
            elide(b, sizeof(b), 1, 180, l);
            txt(10, 121, 1, b);
        }
        char m[20];
        snprintf(m, sizeof(m), "MAJ %02d:%02d", (int)((v->live->maj / 3600) % 24),
                 (int)((v->live->maj / 60) % 60));
        txt_droite(190, 121, 1, m);
    } else {
        txt(10, 108, 2, "HORS LIGNE");
        txt(10, 121, 1, "PAS DE DONNEES : N.D. (JAMAIS 0 INVENTE)");
    }

    /* l'item unique : demarrer */
    curseur_barre(136, 17, v->curseur == 0);
    txt(10, 141, 2, v->curseur == 0 ? "" : "DEMARRER LA VISITE");
    txt_blanc(10, 141, 2, v->curseur == 0 ? "DEMARRER LA VISITE" : "");

    char bas[40];
    snprintf(bas, sizeof(bas), "FDT 3.0.0 - %u DEMARRAGES", (unsigned)e->boots);
    txt(FX0, 162, 1, bas);
    txt(FX0, 169, 1, "CODE DEMARRAGE : 0000 PAR DEFAUT");
}

/* ---------------------------------------------------------------- */
/* page 2 — PARIS SPORTIFS : le top 10 calcule                      */
/* ---------------------------------------------------------------- */
static void p_paris(const fdj_vue_t *v)
{
    const fdj_top_t *t = v->top;
    if (!t || t->n == 0) {
        txt_centre(100, 70, 2, "AUCUN MATCH EN DIRECT");
        txt_centre(100, 88, 1, "LE MODELE NE DEVINE PAS UN SCORE");
        txt_centre(100, 96, 1, v->wifi ? "B2X2 : RAFRAICHIR" : "HORS LIGNE : N.D.");
        return;
    }
    for (int i = 0; i < t->n && i < FDJ_SEL_MAX; i++) {
        const fdj_sel_t *s = &t->s[i];
        int y = FTOP + i * 15;
        bool sel = (v->curseur == i);
        if (sel) plein(0, y, 180, y + 14, 1);     /* la coche reste sur blanc */
        int noir = sel ? 0 : 1;

        /* code de coupon dans une pastille */
        plein(6, y + 1, 27, y + 8, sel ? 0 : 1);
        a_text_col(9, y + 2, 1, s->code, sel ? 1 : 0);

        char m[26];
        elide(m, sizeof(m), 1, 120, s->match);
        a_text_col(31, y + 1, 1, m, noir);

        char l2[44];
        const char *info = "";
        if (s->idx_match < v->live->n) info = v->live->m[s->idx_match].info;
        snprintf(l2, sizeof(l2), "%s %s %s", s->ligue, info, s->marche);
        char l2e[44];
        elide(l2e, sizeof(l2e), 1, 128, l2);
        a_text_col(31, y + 8, 1, l2e, noir);

        char p[8];
        snprintf(p, sizeof(p), "%d%%", (int)(s->proba * 100.0f + 0.5f));
        a_text_col(178 - largeur(1, p), y + 8, 1, p, noir);

        coche(184, y + 3, (v->e->sel_cochee & (1u << i)) != 0);
    }
    txt(FX0, 168, 1, "PROBA = MODELE POISSON, PAS UNE COTE FDJ");
}

/* ---------------------------------------------------------------- */
/* page 3 — LOTO : 3 grilles + l'astuce du jour                     */
/* ---------------------------------------------------------------- */
static const char *ASTUCES[] = {
    "2 PAIRS / 3 IMPAIRS : 1 GRILLE SUR 3",
    "SOMME ENTRE 100 ET 180 : 7 SUR 10",
    "EVITER 1-2-3-4-5 : TROP PARTAGEE",
    "JOUER 2 BAS + 3 HAUTS, PAS L'INVERSE",
    "LA CHANCE NE SE CALCULE PAS : 1/10",
    "MEME GRILLE 2 FOIS : AUCUN EFFET",
};
#define ASTUCE_N 6

static void p_loto(const fdj_vue_t *v)
{
    for (int g = 0; g < v->nb_grilles && g < FDJ_GRILLE_MAX; g++) {
        int y = FTOP + g * 33;
        bool sel = (v->curseur == g);
        if (sel) plein(0, y - 1, 5, y + 30, 1);     /* barre, pas de fond plein */
        int noir = 1;

        char b[16];
        snprintf(b, sizeof(b), "GRILLE %d", g + 1);
        a_text_col(10, y + 2, 1, b, noir);

        for (int k = 0; k < FDJ_GRILLE_NB; k++) {
            int bx = 10 + k * 26, by = y + 9;
            cadre(bx, by, bx + 21, by + 17, 1);
            char n[4];
            snprintf(n, sizeof(n), "%02u", (unsigned)v->grilles[g][k]);
            a_text_col(bx + 2, by + 4, 2, n, 1);
        }
        int cx = 10 + FDJ_GRILLE_NB * 26 + 6;
        cadre(cx, y + 9, cx + 22, y + 26, 1);
        char c[8];
        snprintf(c, sizeof(c), "C%u", (unsigned)v->chances[g]);
        a_text_col(cx + 2, y + 13, 1, c, 1);
        coche(184, y + 10, (v->e->grille_cochee & (1u << g)) != 0);
    }

    int ty = FTOP + FDJ_GRILLE_MAX * 33 + 2;
    cadre(FX0, ty, FX1, ty + 30, 1);
    txt(FX0 + 4, ty + 3, 1, "ASTUCE DU JOUR");
    txt(FX0 + 4, ty + 12, 1, ASTUCES[(v->now / 86400) % ASTUCE_N]);
    txt(FX0 + 4, ty + 21, 1, "5/49 + CHANCE : 1 SUR 19 068 840");

    /* le 4e item : nouvelle grille (validee par double clic B2) */
    int ny = ty + 34;
    bool sel = (v->curseur == FDJ_GRILLE_MAX);
    curseur_barre(ny - 1, 13, sel);
    txt_blanc(FX0 + 2, ny, 1, sel ? "NOUVELLE GRILLE (B2X2)" : "");
    txt(FX0 + 2, ny, 1, sel ? "" : "NOUVELLE GRILLE (B2X2)");
}

/* ---------------------------------------------------------------- */
/* page 4 — HISTORIQUE                                              */
/* ---------------------------------------------------------------- */
static void p_historique(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    if (e->hist_n == 0) {
        txt_centre(100, 72, 2, "AUCUN PARI");
        txt_centre(100, 92, 1, "VALIDEZ LE COUPON PAGE 7");
        txt_centre(100, 104, 1, "TOUT EST VIDE, ET C'EST HONNETE");
        return;
    }
    int n = e->hist_n < 6 ? e->hist_n : 6;
    for (int i = 0; i < n; i++) {
        int idx = fdj_hist_index_recence(e, i);
        if (idx < 0) continue;
        const fdj_pari_t *p = &e->hist[idx];
        int y = FTOP + i * 25;
        bool sel = (v->curseur == i);
        bool det = (v->e->detail_ouvert == (uint8_t)i);
        curseur_barre(y, det ? 25 : 24, sel);
        int noir = sel ? 0 : 1;

        char l[FDJ_TXT_L + 2];
        elide(l, sizeof(l), 1, 140, p->libelle);
        a_text_col(8, y + 1, 1, l, noir);

        const char *st = p->statut == FDJ_PARI_GAGNE ? "GAGNE" :
                         p->statut == FDJ_PARI_PERDU ? "PERDU" : "EN COURS";
        a_text_col(150, y + 1, 1, st, noir);

        char d[64];
        time_t t = (time_t)p->t;
        struct tm ti;
        localtime_r(&t, &ti);
        if (p->statut == FDJ_PARI_GAGNE)
            snprintf(d, sizeof(d), "%02d/%02d  M %u,%02u  G %u,%02u",
                     ti.tm_mday, ti.tm_mon + 1,
                     (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100),
                     (unsigned)(p->gain_ct / 100), (unsigned)(p->gain_ct % 100));
        else if (p->statut == FDJ_PARI_EN_COURS)
            snprintf(d, sizeof(d), "%02d/%02d  M %u,%02u  POT %u,%02u",
                     ti.tm_mday, ti.tm_mon + 1,
                     (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100),
                     (unsigned)(p->gain_ct / 100), (unsigned)(p->gain_ct % 100));
        else
            snprintf(d, sizeof(d), "%02d/%02d  M %u,%02u  PERDU",
                     ti.tm_mday, ti.tm_mon + 1,
                     (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100));
        a_text_col(8, y + 8, 1, d, noir);

        char j[32];
        snprintf(j, sizeof(j), "CODE %s  %s", p->code, fdj_jeu_nom(p->jeu));
        a_text_col(8, y + 15, 1, j, noir);
        (void)det;
    }
    txt(FX0, 168, 1, "B1 : OUVRIR / FERMER LE DETAIL");
}

/* ---------------------------------------------------------------- */
/* page 5 — GAINS / PERTES                                          */
/* ---------------------------------------------------------------- */
static void p_gains(const fdj_vue_t *v)
{
    static const int JOURS[3] = { 7, 30, 365 };
    static const char *NOM[3] = { "7 JOURS", "30 JOURS", "1 AN" };
    for (int i = 0; i < 3; i++) {
        fdj_periode_t p;
        fdj_periode_calcule(v->e, v->now, JOURS[i], &p);
        int y = FTOP + i * 33;
        bool sel = (v->curseur == i);
        curseur_barre(y, 31, sel);
        int noir = sel ? 0 : 1;

        a_text_col(8, y + 2, 2, NOM[i], noir);
        char b[24];
        if (p.nb == 0) snprintf(b, sizeof(b), "N.D.");
        else           euros(b, sizeof(b), (uint32_t)(p.net_ct < 0 ? -p.net_ct : p.net_ct), false);
        int w = largeur(2, b);
        a_text_col(190 - w, y + 2, 2, b, noir);
        if (p.nb > 0) {
            const char *sg = p.net_ct > 0 ? "+" : (p.net_ct < 0 ? "-" : "=");
            a_text_col(190 - w - 9, y + 2, 2, sg, noir);
        }
        char d[64];
        snprintf(d, sizeof(d), "%d PARIS  MISE %u,%02u  GAIN %u,%02u",
                 p.nb, (unsigned)(p.mise_ct / 100), (unsigned)(p.mise_ct % 100),
                 (unsigned)(p.gain_ct / 100), (unsigned)(p.gain_ct % 100));
        a_text_col(8, y + 20, 1, d, noir);
    }

    /* 7 derniers jours en barres : le net, signe vers le haut ou le bas */
    int bx = FX0, by = 132, hmax = 30;
    plein(FX0, by + hmax, FX1, by + hmax, 1);
    for (int i = 0; i < 7; i++) {
        int64_t jour_debut = (v->now / 86400) * 86400 + (int64_t)i * 86400 - 6 * 86400;
        int32_t net = 0;
        for (int k = 0; k < v->e->hist_n; k++) {
            int idx = fdj_hist_index_recence(v->e, k);
            if (idx < 0) continue;
            const fdj_pari_t *p = &v->e->hist[idx];
            if (p->t >= jour_debut && p->t < jour_debut + 86400)
                net += (int32_t)p->gain_ct - (int32_t)p->mise_ct;
        }
        int h = (int)(net / 100);
        if (h > hmax) h = hmax;
        if (h < -hmax / 2) h = -hmax / 2;
        int x = bx + i * 14;
        plein(x + 2, by + hmax - h, x + 9, by + hmax - 1, 1);
        if (h == 0) plein(x + 2, by + hmax - 1, x + 9, by + hmax, 1);
    }
    txt(FX0, 166, 1, "NET PAR JOUR (7 DERNIERS JOURS)");
    txt(FX0 + 150, 132, 1, "E");
}

/* ---------------------------------------------------------------- */
/* page 6 — NEWS LIVE                                               */
/* ---------------------------------------------------------------- */
static void p_news(const fdj_vue_t *v)
{
    const fdj_flux_t *f = v->news;
    if (!f || !f->ok || f->nb == 0) {
        txt_centre(100, 70, 2, "FLUX INDISPONIBLE");
        txt_centre(100, 90, 1, "PAS DE TITRE INVENTE : N.D.");
        txt_centre(100, 104, 1, "NOUVELLE TENTATIVE DANS 3 H");
        return;
    }
    for (int i = 0; i < f->nb && i < 4; i++) {
        const fdj_news_t *n = &f->n[i];
        int y = FTOP + i * 38;
        bool sel = (v->curseur == i);
        curseur_barre(y, 36, sel);
        int noir = sel ? 0 : 1;

        a_text_col(8, y + 2, 1, n->quand, noir);
        a_text_col(8 + largeur(1, n->quand) + 6, y + 2, 1, n->source, noir);

        /* coupe au dernier espace : pas de mot tranche en deux */
        char a[56], b[56];
        int maxc = 40;                       /* 160 px en s=1 */
        int len = (int)strlen(n->titre);
        int cut = len < maxc ? len : maxc;
        if (cut < len) {
            int k = cut;
            while (k > maxc - 14 && n->titre[k] != ' ') k--;
            if (k > 0) cut = k;
        }
        memcpy(a, n->titre, (size_t)cut);
        a[cut] = 0;
        elide(b, sizeof(b), 1, 160, n->titre + cut + (n->titre[cut] == ' ' ? 1 : 0));
        a_text_col(8, y + 9, 1, a, noir);
        if (b[0]) a_text_col(8, y + 17, 1, b, noir);

        coche(184, y + 2, (v->e->news_lu & (1u << i)) != 0);
    }
    txt(FX0, 168, 1, "MAJ TOUTES LES 3 H - B2X2 POUR FORCER");
}

/* ---------------------------------------------------------------- */
/* page 7 — COUPON                                                  */
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
    int loto = 0;
    for (int g = 0; g < FDJ_GRILLE_MAX; g++) if (v->e->grille_cochee & (1u << g)) loto++;

    uint32_t total_ct = (uint32_t)coches * 200 + (uint32_t)loto * 220;
    int k = 0;

    if (t) {
        for (int i = 0; i < t->n && k < 8; i++) {
            if (!(v->e->sel_cochee & (1u << i))) continue;
            const fdj_sel_t *s = &t->s[i];
            int y = FTOP + k * 14;
            bool sel = (v->curseur == k);
            if (sel) plein(0, y, 180, y + 13, 1);
            int noir = sel ? 0 : 1;
            a_text_col(8, y + 4, 1, s->code, noir);
            char b[28];
            elide(b, sizeof(b), 1, 104, s->match);
            a_text_col(36, y + 4, 1, b, noir);
            a_text_col(140, y + 4, 1, s->marche, noir);
            a_text_col(164, y + 4, 1, "2,00", noir);
            coche(186, y + 2, true);
            k++;
        }
    }
    for (int g = 0; g < loto && k < 10; g++) {
        int y = FTOP + k * 14;
        bool sel = (v->curseur == k);
        if (sel) plein(0, y, 180, y + 13, 1);
        int noir = sel ? 0 : 1;
        char b[44];
        snprintf(b, sizeof(b), "LOTO%d %02u %02u %02u %02u %02u C%u", g + 1,
                 v->grilles[g][0], v->grilles[g][1], v->grilles[g][2],
                 v->grilles[g][3], v->grilles[g][4], v->chances[g]);
        a_text_col(8, y + 4, 1, b, noir);
        a_text_col(164, y + 4, 1, "2,20", noir);
        k++;
    }

    int base = k;
    int yv = FTOP + base * 14 + 2;
    bool sv = (v->curseur == base);
    curseur_barre(yv, 13, sv);
    txt_blanc(10, yv + 3, 1, sv ? "VALIDER LE COUPON (B2X2)" : "");
    txt(10, yv + 3, 1, sv ? "" : "VALIDER LE COUPON (B2X2)");

    int yc = yv + 14;
    bool sc = (v->curseur == base + 1);
    curseur_barre(yc, 13, sc);
    txt_blanc(10, yc + 3, 1, sc ? "VIDER LE COUPON" : "");
    txt(10, yc + 3, 1, sc ? "" : "VIDER LE COUPON");

    /* totaux, en bas, toujours a la meme place */
    char b[48];
    snprintf(b, sizeof(b), "MISE TOTALE : %u,%02u E", (unsigned)(total_ct / 100),
             (unsigned)(total_ct % 100));
    txt(FX0, 152, 1, b);
    if (coches > 0) {
        char g2[48];
        snprintf(g2, sizeof(g2), "GAIN POSSIBLE (MODELE) : %u,%02u E",
                 (unsigned)(gain_pot / 100), (unsigned)(gain_pot % 100));
        txt(FX0, 161, 1, g2);
    } else {
        txt(FX0, 161, 1, "GAIN POSSIBLE : N.D. (AUCUN PARI)");
    }
    txt(FX0, 169, 1, "LOTO : GAIN SELON LE RANG DU TIRAGE");

    if (coches == 0 && loto == 0) {
        txt_centre(100, 110, 1, "AUCUNE SELECTION : COCHEZ PAGE 2 OU 3");
    }
}

/* page 8 — JEU RESPONSABLE                                         */
/* ---------------------------------------------------------------- */
static const char *QUESTIONS[] = {
    "JOUER EST UN PLAISIR, PAS UN BESOIN",
    "JE NE JOUE JAMAIS POUR ME REFAIRE",
    "MON BUDGET EST FIXE ET TENU",
    "JE SAIS DIRE STOP APRES UNE PERTE",
    "LE JEU NE REMPLACE PAS MES PROCHES",
};
#define QN 5

static void p_responsable(const fdj_vue_t *v)
{
    int score = 0;
    for (int i = 0; i < QN; i++) {
        int y = FTOP + i * 22;
        bool sel = (v->curseur == i);
        curseur_barre(y, 21, sel);
        int noir = sel ? 0 : 1;

        char src[52], b[52];
        snprintf(src, sizeof(src), "%s", QUESTIONS[i]);
        elide(b, sizeof(b), 1, 140, src);
        a_text_col(8, y + 3, 1, b, noir);
        bool oui = (v->e->sel_cochee >> (8 + i)) & 1;   /* bits 8..12 : reponses */
        if (oui) score++;
        a_text_col(164, y + 3, 1, oui ? "OUI" : "NON", noir);
        coche(184, y + 6, oui);
    }

    /* jauge de risque, sans couleur : pleine = bon */
    int y = 134;
    txt(8, y, 1, "PROTECTION");
    for (int i = 0; i < QN; i++)
        plein(84 + i * 18, y, 84 + i * 18 + 14, y + 6, i < score ? 1 : 0);
    cadre(84, y, 84 + QN * 18 - 4, y + 6, 1);
    char b[32];
    snprintf(b, sizeof(b), "%d/%d", score, QN);
    txt(84 + QN * 18 + 2, y, 1, b);

    /* pause 24 h : l'option de vraie protection */
    int yp = 148;
    bool sel = (v->curseur == QN);
    curseur_barre(yp, 14, sel);
    int noir = sel ? 0 : 1;
    a_text_col(8, yp + 3, 1, "PAUSE 24 H (AUTO-EXCLUSION)", noir);
    coche(184, yp + 3, v->e->pause_24h);
    txt(FX0, 166, 1, "L'ARGENT ENGAGE RESTE ENGAGE : RIEN N'EST CACHE");
}

/* ---------------------------------------------------------------- */
/* page 9 — STATS & PROBAS                                          */
/* ---------------------------------------------------------------- */
static void p_stats(const fdj_vue_t *v)
{
    char b[40];
    int gagne = 0, total = 0;
    for (int i = 0; i < v->e->hist_n; i++) {
        int idx = fdj_hist_index_recence(v->e, i);
        if (idx < 0) continue;
        if (v->e->hist[idx].statut == FDJ_PARI_EN_COURS) continue;
        total++;
        if (v->e->hist[idx].statut == FDJ_PARI_GAGNE) gagne++;
    }
    struct { const char *t, *v, *n; } B[4];
    B[0].t = "LOTO 5/49 + CHANCE";
    B[0].v = "1 / 19 068 840";
    B[0].n = "5 PARIS = 5 CHANCES DE PLUS";
    B[1].t = "EUROMILLIONS";
    B[1].v = "1 / 139 838 160";
    B[1].n = "2 ETOILES EN PLUS, 7,3X PIRE";
    B[2].t = "COTE 2,00";
    B[2].v = "50 %";
    B[2].n = "ESP. 1,90 : LA MAISON GARDE 5 %";
    if (total > 0) {
        snprintf(b, sizeof(b), "%d / %d", gagne, total);
        B[3].v = b;
    } else {
        B[3].v = "N.D.";
    }
    B[3].t = "MES PARIS REGLES";
    B[3].n = total ? "WIN RATE SUR L'HISTORIQUE REEL" : "AUCUN PARI REGLE";

    for (int i = 0; i < 4; i++) {
        int y = FTOP + i * 35;
        bool sel = (v->curseur == i);
        curseur_barre(y, 33, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 1, B[i].t, noir);
        a_text_col(8, y + 10, 2, B[i].v, noir);
        char n[52];
        elide(n, sizeof(n), 1, 180, B[i].n);
        a_text_col(8, y + 24, 1, n, noir);
    }
    txt(FX0, 166, 1, "LE MODELE N'EST PAS UNE PROMESSE DE GAIN");
}

/* ---------------------------------------------------------------- */
/* page 10 — USER STORIES                                           */
/* ---------------------------------------------------------------- */
typedef struct { const char *id, *titre, *detail; int crit; } us_t;
static const us_t US[] = {
    { "US-01", "PARIER EN DIRECT",    "MODELE POISSON + CODES", 5 },
    { "US-02", "LOTO EQUILIBRE",      "5/49 + CHANCE, 3 GRILLES", 4 },
    { "US-03", "HISTORIQUE ET GAINS", "7 J / 30 J / 1 AN", 4 },
    { "US-04", "PROFILS ET PLAFONDS", "4 MAX, 2 PLAFONDS CHACUN", 6 },
    { "US-05", "CODE DE DEMARRAGE",   "4 CHIFFRES, 0000 D'USINE", 4 },
    { "US-06", "NEWS ET HORS LIGNE",  "3 H, ET N.D. SI PANNE", 4 },
};
#define USN 6

static void p_stories(const fdj_vue_t *v)
{
    for (int i = 0; i < USN; i++) {
        int y = FTOP + i * 24;
        bool sel = (v->curseur == i);
        curseur_barre(y, 23, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 1, US[i].id, noir);
        a_text_col(40, y + 2, 1, US[i].titre, noir);
        a_text_col(40, y + 10, 1, US[i].detail, noir);
        char t[12];
        snprintf(t, sizeof(t), "%d AC", US[i].crit);
        a_text_col(166, y + 10, 1, t, noir);
    }
    char b[52];
    snprintf(b, sizeof(b), "%d VERIFICATIONS AU TOTAL - PAGE 11", FDJ_TESTS_TOTAL);
    txt(FX0, 166, 1, b);
}

/* ---------------------------------------------------------------- */
/* page 11 — WORKFLOW DES TESTS                                     */
/* ---------------------------------------------------------------- */
static void p_tests(const fdj_vue_t *v)
{
    (void)v;
    struct { const char *t, *d; int n; } N[4] = {
        { "1. UNITAIRES",   "MODELE, PARSEURS, HASARD", 0 },
        { "2. COMPOSANTS",  "CHAQUE PAGE RENDUE EN PNG", 0 },
        { "3. INTEGRATION", "NAV + ARGENT + PERSISTANCE", 0 },
        { "4. FONCTIONNEL", "SCENARIO COMPLET, 19 PAGES", 0 },
    };
    /* les compteurs reels sont injectes par le generateur de recette */
#ifdef FDJ_TESTS_UNIT
    N[0].n = FDJ_TESTS_UNIT;
    N[1].n = FDJ_TESTS_COMP;
    N[2].n = FDJ_TESTS_INTEG;
    N[3].n = FDJ_TESTS_FONC;
#endif
    int total = 0;
    for (int i = 0; i < 4; i++) total += N[i].n;

    for (int i = 0; i < 4; i++) {
        int y = FTOP + i * 26;
        bool sel = (v->curseur == i);
        curseur_barre(y, 25, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 2, N[i].t, noir);
        a_text_col(8, y + 14, 1, N[i].d, noir);
        char b[10];
        snprintf(b, sizeof(b), "%d", N[i].n);
        a_text_col(172, y + 2, 2, b, noir);
    }
    char b[80];
    snprintf(b, sizeof(b), "%d VERIFS, %d ECHEC - RECETTE DU %s",
             total, FDJ_TESTS_FAIL, FDJ_TESTS_DATE);
    txt(FX0, 132, 1, b);
    txt(FX0, 142, 1, "UNIT -> COMPOSANT -> INTEG -> FONCTION");
    txt(FX0, 152, 1, "CORRECTION APRES CHAQUE PAGE, PUIS RETEST");
    txt(FX0, 166, 1, "AUCUNE PAGE N'EST LIVREE SANS SES TESTS");
}

/* ---------------------------------------------------------------- */
/* page 12 — ARCHITECTURE (choix de la maison)                      */
/* ---------------------------------------------------------------- */
static void p_archi(const fdj_vue_t *v)
{
    (void)v;
    struct { int x, y, w, h; const char *t, *d; } M[4] = {
        { 8,  20, 84, 26, "MODELE",  "ARGENT / POISSON" },
        { 108, 20, 84, 26, "PARSEURS", "JSON / RSS" },
        { 8,  60, 84, 26, "NAV + UI", "19 PAGES, 1 BIT" },
        { 108, 60, 84, 26, "MATERIEL", "EPD / AUDIO / NVS" },
    };
    for (int i = 0; i < 4; i++) {
        bool sel = (v->curseur == i);
        cadre(M[i].x, M[i].y, M[i].x + M[i].w, M[i].y + M[i].h, sel ? 2 : 1);
        txt(M[i].x + 6, M[i].y + 4, 2, M[i].t);
        txt(M[i].x + 6, M[i].y + 17, 1, M[i].d);
    }
    /* les fleches du flux */
    a_seg(84, 33, 106, 33, 1);
    a_tri(112, 33, 106, 29, 106, 37, 1);
    a_seg(150, 47, 150, 58, 1);
    a_tri(150, 64, 146, 58, 154, 58, 1);
    a_seg(106, 73, 86, 73, 1);
    a_tri(80, 73, 86, 69, 86, 77, 1);

    txt(FX0, 96, 1, "MEME CODE SUR LA CARTE ET SUR LE PC :");
    txt(FX0, 105, 1, "L'APERCU PNG EST LE VRAI DESSIN.");
    txt(FX0, 118, 2, "4 000 OCTETS DE RAM");
    txt(FX0, 132, 1, "PAS DE JSON EN MEMOIRE LONGUE : 16 MATCHS MAX");
    txt(FX0, 142, 1, "UNE IMAGE = 5 000 OCTETS, ENVOYEE EN PARTIEL");
    txt(FX0, 152, 1, "ETAT PERSISTANT = 1 BLOB NVS + CRC32");
    txt(FX0, 166, 1, "AUCUNE ALLOCATION DANS UNE PAGE");
}

/* ---------------------------------------------------------------- */
/* page 13 — PERF / SECURITE / VPN                                  */
/* ---------------------------------------------------------------- */
static void p_perf(const fdj_vue_t *v)
{
    struct { const char *t, *v; } L[3];
    char b1[24], b2[24], b3[24];
    snprintf(b1, sizeof(b1), "%d MS", v->rendu_ms);
    snprintf(b2, sizeof(b2), "%d MS", v->ecran_ms);
    snprintf(b3, sizeof(b3), "%u KO", (unsigned)(v->heap_int_libre / 1024));
    L[0].t = "DESSIN D'UNE PAGE";      L[0].v = b1;
    L[1].t = "RAFRAICHISSEMENT EPD";   L[1].v = b2;
    L[2].t = "HEAP INTERNE LIBRE";     L[2].v = b3;

    for (int i = 0; i < 3; i++) {
        int y = FTOP + i * 17;
        bool sel = (v->curseur == i);
        curseur_barre(y, 16, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 3, 1, L[i].t, noir);
        a_text_col(160, y + 3, 1, L[i].v, noir);
    }

    int y = FTOP + 3 * 17 + 4;
    plein(FX0, y, FX1, y, 1);
    txt(FX0, y + 4, 1, "SECURITE");
    txt(FX0, y + 13, 1, "TLS VERIFIE (BUNDLE IDF), PAS DE MODE INSECURE");
    txt(FX0, y + 21, 1, "CODE DE DEMARRAGE LOCAL, JAMAIS ENVOYE");
    txt(FX0, y + 29, 1, "DONNEES EN NVS, EFFACABLES PAGE 18");

    int yv = y + 38;
    bool sel = (v->curseur == 3);
    curseur_barre(yv, 14, sel);
    int noir = sel ? 0 : 1;
    a_text_col(8, yv + 3, 1, "VPN OPTIONNEL (RELais LOCAL)", noir);
    a_text_col(168, yv + 3, 1, v->e->vpn ? "ON" : "OFF", noir);
    coche(184, yv + 3, v->e->vpn);
}

/* ---------------------------------------------------------------- */
/* page 14 — RAM ET SWAP                                            */
/* ---------------------------------------------------------------- */
static void jauge(int y, const char *nom, uint32_t libre, uint32_t total, const char *unite)
{
    txt(8, y, 1, nom);
    char b[24];
    snprintf(b, sizeof(b), "%u %s", (unsigned)(libre / (strcmp(unite, "KO") == 0 ? 1024 : 1)), unite);
    txt_droite(186, y, 1, b);
    int pct = total ? (int)((uint64_t)libre * 100 / total) : 0;
    if (pct > 100) pct = 100;
    plein(8, y + 7, 186, y + 11, 0);
    cadre(8, y + 7, 186, y + 11, 1);
    plein(9, y + 8, 9 + (int)((176 * pct) / 100), y + 10, 1);
}

static void p_ram(const fdj_vue_t *v)
{
    jauge(FTOP + 2,  "RAM INTERNE LIBRE", v->heap_int_libre, 512 * 1024, "KO");
    jauge(FTOP + 22, "PLUS GROS BLOC",    v->heap_int_max,   128 * 1024, "KO");
    jauge(FTOP + 42, "PSRAM LIBRE",       v->psram_libre,    8 * 1024 * 1024, "KO");

    plein(FX0, 82, FX1, 83, 1);
    txt(FX0, 88, 1, "SWAP SUR MICRO SD (OPTIONNEL)");
    if (v->sd_ok) {
        txt(FX0, 98, 1, "CARTE PRESENTE : ETAT SAIN");
        txt(FX0, 108, 1, "AUCUN PAGING EN COURS : TOUT TIENT EN RAM");
        txt(FX0, 118, 1, "LE SWAP EST UN FILET, PAS UN MOTEUR");
    } else {
        txt(FX0, 98, 1, "PAS DE CARTE SD : SWAP INDISPONIBLE");
        txt(FX0, 108, 1, "RIEN N'EST INVENTE, LA PAGE LE DIT");
    }
    txt(FX0, 132, 1, "MESURES PRISES A L'INSTANT, PAS DE SIMULATION");
    txt(FX0, 142, 1, "L'IMAGE E-PAPER N'OCCUPE QUE 5 000 OCTETS");
    txt(FX0, 152, 1, "LES TAMPONS RESEAU SONT EN PSRAM");
    txt(FX0, 166, 1, "B2X2 : REFAIRE LES MESURES");
}

/* ---------------------------------------------------------------- */
/* page 15 — COMPTE ET REGLAGES                                     */
/* ---------------------------------------------------------------- */
static void p_compte(const fdj_vue_t *v)
{
    char ligne[6][56];
    char mdp[24];
    if (v->code_affiche) snprintf(mdp, sizeof(mdp), "REVELE 3 SECONDES");
    else                 snprintf(mdp, sizeof(mdp), "******** (MASQUE)");

    snprintf(ligne[0], sizeof(ligne[0]), "%s", v->e->compte_nom);
    snprintf(ligne[1], sizeof(ligne[1]), "%s", v->e->compte_id);
    if (v->ssid && v->ssid[0])
        snprintf(ligne[2], sizeof(ligne[2]), "%s  %d DBM", v->ssid, v->rssi);
    else
        snprintf(ligne[2], sizeof(ligne[2]), "HORS LIGNE");
    snprintf(ligne[3], sizeof(ligne[3]), "%s", mdp);
    snprintf(ligne[4], sizeof(ligne[4]), "%04u - %s",
             (unsigned)v->e->code_boot, v->e->code_perso ? "CODE PERSO" : "CODE D'USINE");
    snprintf(ligne[5], sizeof(ligne[5]), "JINGLE 5 S AU DEMARRAGE (%s)", v->e->musique ? "ON" : "OFF");

    static const char *TITRE[6] = {
        "TITULAIRE", "COMPTE FDJ", "RESEAU", "SECRET", "CODE 4 CHIFFRES", "AMBIANCE"
    };
    for (int i = 0; i < 6; i++) {
        int y = FTOP + i * 22;
        bool sel = (v->curseur == i);
        curseur_barre(y, 21, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 1, TITRE[i], noir);
        char b[52];
        elide(b, sizeof(b), 1, 160, ligne[i]);
        a_text_col(8, y + 10, 1, b, noir);
        if (i == 4) {   /* l'item qui se modifie : un chevron */
            a_text_col(186, y + 10, 1, ">", noir);
        }
        if (i == 5) coche(184, y + 8, v->e->musique);
    }
    txt(FX0, 156, 1, "B1 SUR SECRET : AFFICHER 3 SECONDES");
    txt(FX0, 166, 1, "B1 SUR CODE : CHANGER (4 CHIFFRES, DEFAUT 0000)");
}

/* ---------------------------------------------------------------- */
/* page 16 — PROFILS                                               */
/* ---------------------------------------------------------------- */
static void p_profils(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    int n = e->nb_profils;
    for (int i = 0; i < n; i++) {
        const fdj_profil_t *p = &e->profils[i];
        int y = FTOP + i * 30;
        bool sel = (v->curseur == i);
        curseur_barre(y, 29, sel);
        int noir = sel ? 0 : 1;

        if (i == e->profil_actif) a_text_col(8, y + 3, 2, ">", noir);
        a_text_col(18, y + 3, 2, p->nom, noir);
        a_text_col(18, y + 15, 1, fdj_niveau_nom(p->niveau), noir);

        char b[32];
        snprintf(b, sizeof(b), "%u/%u E PAR 7/30 J",
                 (unsigned)(p->plafond_sem_ct / 100), (unsigned)(p->plafond_mois_ct / 100));
        a_text_col(18, y + 22, 1, b, noir);

        char s[16];
        if (i == e->profil_actif) snprintf(s, sizeof(s), "%u,%02u", (unsigned)(p->solde_ct / 100),
                                           (unsigned)(p->solde_ct % 100));
        else                      snprintf(s, sizeof(s), "----");
        a_text_col(160, y + 15, 1, s, noir);
    }

    int y = FTOP + n * 30;
    bool sel = (v->curseur == n);
    curseur_barre(y, 22, sel);
    int noir = sel ? 0 : 1;
    if (n < FDJ_PROFILS_MAX) {
        a_text_col(8, y + 3, 1, "NOUVEAU PROFIL (B2X2)", noir);
        a_text_col(8, y + 11, 1, "DEBUTANT / REGULIER / EXPERT", noir);
    } else {
        a_text_col(8, y + 3, 1, "4 PROFILS MAXIMUM : SUPPRIMER D'ABORD", noir);
        a_text_col(8, y + 11, 1, "B1 : SUPPRIMER LE PROFIL (SAUF ACTIF)", noir);
    }
    /* l'argent reel du profil actif : depense et reste a jouer */
    uint32_t sem = 0, mois = 0;
    fdj_depenses(e, v->now, &sem, &mois);
    const fdj_profil_t *pa = fdj_profil_actif_c(e);
    char d[80];
    if (pa) {
        snprintf(d, sizeof(d), "ACTIF %s : DEPENSE 7J %u,%02u / %u,%02u E",
                 pa->nom, (unsigned)(sem / 100), (unsigned)(sem % 100),
                 (unsigned)(pa->plafond_sem_ct / 100), (unsigned)(pa->plafond_sem_ct % 100));
    } else {
        snprintf(d, sizeof(d), "AUCUN PROFIL ACTIF");
    }
    txt(FX0, 158, 1, d);
    txt(FX0, 168, 1, "CHAQUE PROFIL A SON PLAFOND 7 J / 30 J");
}

/* ---------------------------------------------------------------- */
/* page 17 — ACCESSIBILITE                                          */
/* ---------------------------------------------------------------- */
static void p_acces(const fdj_vue_t *v)
{
    struct { const char *t, *d; bool on; } O[4] = {
        { "GROS TEXTE",     "CORPS 2X PLUS GRAND SUR TOUT", v->e->gros_texte },
        { "CONTRASTE FORT", "NOIR SUR BLANC, AUCUN GRIS",    true },
        { "SON DES ACTIONS", "POSITIF MONTE, NEGATIF DESCEND", true },
        { "LANGUE",         "FRANCAIS (SEULE LANGUE EMBARQUEE)", true },
    };
    for (int i = 0; i < 4; i++) {
        int y = FTOP + i * 27;
        bool sel = (v->curseur == i);
        curseur_barre(y, 26, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 3, 2, O[i].t, noir);
        a_text_col(8, y + 16, 1, O[i].d, noir);
        coche(184, y + 8, O[i].on);
    }
    txt(FX0, 138, 1, "AUCUNE INFORMATION PAR LA COULEUR SEULE");
    txt(FX0, 148, 1, "COCHE = CARRE + TIRET, LISIBLE DE LOIN");
    if (v->e->gros_texte) txt(FX0, 160, 2, "EXEMPLE GROS");
    else                  txt(FX0, 160, 1, "EXEMPLE NORMAL");
}

/* ---------------------------------------------------------------- */
/* page 18 — DONNEES ET RGPD                                        */
/* ---------------------------------------------------------------- */
static void p_rgpd(const fdj_vue_t *v)
{
    struct { const char *t, *d; } L[3] = {
        { "EFFACER MES DONNEES",   "PROFILS, HISTORIQUE, CODE : TOUT" },
        { "EXPORTER MON HISTORIQUE", "FICHIER CSV SUR LA CARTE SD" },
        { "A PROPOS",              "FDT 3.0.0 - BATMino x FDJ" },
    };
    for (int i = 0; i < 3; i++) {
        int y = FTOP + i * 27;
        bool sel = (v->curseur == i);
        curseur_barre(y, 26, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 3, 2, L[i].t, noir);
        a_text_col(8, y + 16, 1, L[i].d, noir);
    }
    plein(FX0, 102, FX1, 103, 1);
    txt(FX0, 108, 1, "OU VIVENT LES DONNEES ?");
    txt(FX0, 118, 1, "NVS INTERNE : COMPTE, CODE, PROFILS, PARIS");
    txt(FX0, 128, 1, "AUCUN ENVOI VERS UN SERVEUR : TOUT RESTE ICI");
    txt(FX0, 138, 1, "RESEAU : SCORES ET TITRES, SANS IDENTITE");
    txt(FX0, 148, 1, "SUPPRESSION = EFFACEMENT NVS + REDEMARRAGE");
    txt(FX0, 162, 1, "@PARISTZOU - JUSTE POUR LES FILLES");
}

/* ---------------------------------------------------------------- */
/* page 19 — REGLES ET CONDITIONS                                   */
/* ---------------------------------------------------------------- */
static void p_regles(const fdj_vue_t *v)
{
    int n = FDJ_JEU_NB;
    for (int i = 0; i < n; i++) {
        int col = i % 2, row = i / 2;
        int x = 8 + col * 96, y = FTOP + row * 13;
        bool sel = false;
        (void)sel;
        plein(x, y, x + 4, y + 4, 1);
        char b[24];
        elide(b, sizeof(b), 1, 88, fdj_jeu_nom((uint8_t)i));
        txt(x + 8, y, 1, b);
    }
    int y = FTOP + 4 * 13 + 6;
    plein(FX0, y, FX1, y, 1);
    txt(FX0, y + 5, 1, "CONDITIONS : JEU INTERDIT AUX -18 ANS");
    txt(FX0, y + 13, 1, "JOUEZ AVEC MODERATION (PLAFONDS PAR PROFIL)");
    txt(FX0, y + 21, 1, "LES GAINS SONT SOUMIS A LA FISCALITE");
    txt(FX0, y + 29, 1, "MAQUETTE DE DEMONSTRATION : PAS DE MISE REELLE");
    txt(FX0, y + 37, 1, "REGLES COMPLETES SUR FDJ.FR");
    plein(FX0, y + 46, FX1, y + 46, 1);
    txt(FX0, y + 51, 1, "3 FAMILLES : PARIS = PRONOSTIC");
    txt(FX0, y + 59, 1, "LOTERIE = TIRAGE - CASINO = HASARD");
    txt(FX0, y + 67, 1, "TOUTES ONT UN TRJ INFERIEUR A 100 %");
    txt(FX0, y + 75, 1, "PROTOTYPE TECHNIQUE - DEMONSTRATION");
    (void)v;
}

/* ---------------------------------------------------------------- */
/* dispatch                                                         */
/* ---------------------------------------------------------------- */
const char *fdj_page_titre(int page)
{
    static const char *T[FDJ_PAGES] = {
        "ACCUEIL FDJ",
        "PARIS SPORTIFS",
        "LOTO",
        "HISTORIQUE",
        "GAINS / PERTES",
        "NEWS LIVE",
        "MON COUPON",
        "JEU RESPONSABLE",
        "STATS / PROBAS",
        "USER STORIES",
        "TESTS",
        "ARCHITECTURE",
        "PERF / SECU / VPN",
        "RAM & SWAP",
        "COMPTE & REGLAGES",
        "PROFILS",
        "ACCESSIBILITE",
        "DONNEES & RGPD",
        "REGLES FDJ",
    };
    if (page < 0 || page >= FDJ_PAGES) return "?";
    return T[page];
}

int fdj_page_items(int page, const fdj_vue_t *v)
{
    switch (page) {
    case 0:  return 1;
    case 1:  return v->top && v->top->n > 0 ? v->top->n : 1;
    case 2:  return FDJ_GRILLE_MAX + 1;
    case 3:  return v->e->hist_n == 0 ? 1 : (v->e->hist_n < 6 ? v->e->hist_n : 6);
    case 4:  return 3;
    case 5:  return v->news && v->news->nb > 0 ? (v->news->nb < 4 ? v->news->nb : 4) : 1;
    case 6: {
        int n = 0;
        if (v->top) for (int i = 0; i < v->top->n; i++) if (v->e->sel_cochee & (1u << i)) n++;
        for (int g = 0; g < FDJ_GRILLE_MAX; g++) if (v->e->grille_cochee & (1u << g)) n++;
        return n + 2;                     /* + VALIDER + VIDER */
    }
    case 7:  return QN + 1;
    case 8:  return 4;
    case 9:  return USN;
    case 10: return 4;
    case 11: return 4;
    case 12: return 4;
    case 13: return 3;
    case 14: return 6;
    case 15: return v->e->nb_profils + 1;
    case 16: return 4;
    case 17: return 3;
    case 18: return 3;
    default: return 1;
    }
}

void fdj_ui_dessine(const fdj_vue_t *v, uint8_t *fb)
{
    art_begin(fb, 0);
    art_clear();
    if (!v || !v->e) return;

    entete(v, fdj_page_titre(v->page));
    switch (v->page) {
    case 0:  p_accueil(v); break;
    case 1:  p_paris(v); break;
    case 2:  p_loto(v); break;
    case 3:  p_historique(v); break;
    case 4:  p_gains(v); break;
    case 5:  p_news(v); break;
    case 6:  p_coupon(v); break;
    case 7:  p_responsable(v); break;
    case 8:  p_stats(v); break;
    case 9:  p_stories(v); break;
    case 10: p_tests(v); break;
    case 11: p_archi(v); break;
    case 12: p_perf(v); break;
    case 13: p_ram(v); break;
    case 14: p_compte(v); break;
    case 15: p_profils(v); break;
    case 16: p_acces(v); break;
    case 17: p_rgpd(v); break;
    case 18: p_regles(v); break;
    default: txt_centre(100, 90, 2, "PAGE INCONNUE"); break;
    }
    pied(v);
}

/* ---------------------------------------------------------------- */
/* saisie du code : un ecran dedie, lisible a 50 cm                 */
/* ---------------------------------------------------------------- */
void fdj_ui_demande_code(uint8_t *fb, const char *titre, const uint8_t chiffres[4],
                         int position, bool erreur)
{
    art_begin(fb, 0);
    art_clear();
    plein(0, 0, 199, 20, 1);
    txt_blanc(8, 5, 2, "FDT - FDJ");
    txt_centre(100, 34, 2, titre);

    for (int i = 0; i < 4; i++) {
        int x = 22 + i * 40;
        cadre(x, 62, x + 32, 104, 2);
        if (i == position) plein(x - 3, 59, x + 35, 107, 1);
        char b[2] = { (char)('0' + chiffres[i]), 0 };
        a_text_col(x + 10, 74, 4, b, i == position ? 0 : 1);
    }
    txt_centre(100, 116, 1, "B2 : CHIFFRE SUIVANT   B1 : CHIFFRE +1");
    txt_centre(100, 126, 1, "B2X2 : VALIDER LE CODE");
    if (erreur) txt_centre(100, 142, 2, "CODE REFUSE");
    else        txt_centre(100, 142, 1, "CODE D'USINE : 0000");
    txt_centre(100, 160, 1, "4 CHIFFRES, RIEN D'AUTRE");
    txt_centre(100, 170, 1, "@PARISTZOU");
}

void fdj_ui_confirme(uint8_t *fb, const char *titre, const char *question,
                     bool oui_selectionne)
{
    art_begin(fb, 0);
    art_clear();
    txt_centre(100, 40, 2, titre);
    txt_centre(100, 70, 1, question);
    cadre(30, 96, 96, 126, oui_selectionne ? 3 : 1);
    txt_centre(63, 108, 2, "OUI");
    cadre(104, 96, 170, 126, oui_selectionne ? 1 : 3);
    txt_centre(137, 108, 2, "NON");
    txt_centre(100, 148, 1, "B2X2 : CONFIRMER   B1 : INVERSER");
}
