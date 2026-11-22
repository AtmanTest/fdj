/*  tableau.cpp - la page unique du BATMino (200x200, 1 bit).
 *
 *  Version "gros caracteres" (11/11) : plus rien en corps 1. La plus petite
 *  taille de texte de la page est le corps 2 (glyphes de 6x10 px) : en dessous,
 *  c'est illisible sur une dalle de 200 px de cote.
 *
 *     0    heure (corps 7), date, visage du compagnon, etat du wifi
 *    38    filet
 *    42    ciel : temperature du moment (corps 4), etat, demain, interieur
 *    90    filet
 *    94    batterie : pourcentage (corps 4), jauge, consommation
 *   116    filet
 *   120    les quatre jauges du compagnon, en deux colonnes
 *   156    filet
 *   161    agenda du jour
 *   178    dernier mot (deux lignes de corps 2)
 *
 *  Regle : une valeur indisponible affiche N.D. ou --, jamais zero ni un nom
 *  suppose. Aucune dependance ESP-IDF : le meme code tourne sur la carte et
 *  dans l'apercu du PC (tools/apercu_tableau.cpp).
 */
#include "tableau.h"
#include "art.h"
#include <stdio.h>
#include <string.h>

int tab_text_w(int s, const char *str) { return a_text_w(s, str); }

/* ---------------------------------------------------------------- */
/* petits outils                                                     */
/* ---------------------------------------------------------------- */

static void txt(int x, int y, int s, const char *str) { a_text(x, y, s, str); }

static void txt_r(int xr, int y, int s, const char *str)
{
    a_text(xr - a_text_w(s, str), y, s, str);
}

/* un nombre a une decimale (13.6), sans passer par %f de newlib */
static const char *un_chiffre(char *b, size_t n, float v)
{
    bool neg = v < 0;
    float a = neg ? -v : v;
    int i = (int)a;
    int d = (int)((a - (float)i) * 10.0f + 0.5f);
    if (d > 9) d = 9;
    snprintf(b, n, "%s%d.%d", neg ? "-" : "", i, d);
    return b;
}

/* le signe pourcent : deux points et une barre. Le glyphe 3x5 ressemble a un Z
   des le corps 2, on le dessine donc a la main. */
static int pct_signe(int x, int y, int s)
{
    int sz = 4 * s, dot = (s >= 4) ? s : (s >= 3) ? 2 : 1;
    for (int dy = 0; dy < dot; dy++)
        for (int dx = 0; dx < dot; dx++) {
            a_px(x + dx, y + dy, 1);                        /* le point d'en haut */
            a_px(x + sz - dot + dx, y + sz - dot + dy, 1);   /* celui d'en bas */
        }
    /* la barre s'arrete a 2 px des coins : collee aux points, elle se lit
       comme une simple barre oblique */
    for (int k = 2; k <= sz - 2; k++)
        a_px(x + sz - k, y + k, 1);
    return sz + 1;
}

/* "36%" : le nombre, puis le signe (renvoie la largeur) */
static int nombre_pct(int x, int y, int s, int v)
{
    char b[12];
    snprintf(b, sizeof(b), "%d", v);
    int w = a_text_w(s, b);
    a_text(x, y, s, b);
    return w + 2 + pct_signe(x + w + 2, y, s);
}

/* copie bornee, coupee au dernier espace pour ne pas finir sur un demi-mot */
static void coupe(char *dst, size_t cap, const char *src, int max_chars)
{
    if (!src || !src[0]) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if ((int)n <= max_chars) {
        snprintf(dst, cap, "%s", src);
    } else {
        int k = max_chars;
        while (k > 8 && src[k] != ' ') k--;
        if (k <= 8) k = max_chars;
        snprintf(dst, cap, "%.*s", k, src);
    }
}

/* une jauge encadree : contour + remplissage (x, y = coin haut-gauche) */
static void barre(int x, int y, int w, int h, int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    a_hline(x, x + w - 1, y, 1);
    a_hline(x, x + w - 1, y + h - 1, 1);
    a_vline(x, y, y + h - 1, 1);
    a_vline(x + w - 1, y, y + h - 1, 1);
    int f = (w - 2) * pct / 100;
    for (int dy = 1; dy < h - 1; dy++)
        for (int dx = 0; dx < f; dx++)
            a_px(x + 1 + dx, y + dy, 1);
}

/* un texte en deux lignes de "cols" signes au plus (le reste est perdu) */
static void deux_lignes(char *l1, char *l2, const char *src, int cols)
{
    l1[0] = l2[0] = 0;
    if (!src || !src[0]) return;
    const char *p = src;
    for (int i = 0; i < 2 && *p; i++) {
        char *dst = (i == 0) ? l1 : l2;
        while (*p == ' ') p++;
        if (!*p) break;
        int n = (int)strlen(p);
        if (n <= cols) { snprintf(dst, 48, "%s", p); return; }
        int k = cols;
        while (k > 8 && p[k] != ' ') k--;
        if (k <= 8) k = cols;
        snprintf(dst, 48, "%.*s", k, p);
        p += k;
    }
}

/* l'icone wifi : trois arcs et le point, comme sur un telephone. Le nombre
   d'arcs donne le niveau ; hors ligne une barre diagonale barre le tout.
   Dessinee assez grande (34 x 22 px) pour se voir du premier coup d'oeil :
   la version precedente, 9 x 6 px dans un coin, etait invisible. */
static void icone_wifi(int cx, int cy, bool on, int rssi)
{
    int niv = 0;
    if (on) niv = (rssi >= -60) ? 3 : (rssi >= -75) ? 2 : 1;
    static const int R[3] = { 7, 13, 19 };
    a_disc(cx, cy, 2, 1);
    /* hors ligne : les trois arcs restent dessines, c'est la barre qui dit
       "pas de wifi" — sans eux il ne resterait qu'un point barre */
    for (int i = 0; i < 3; i++)
        if (!on || i < niv) a_arc(cx, cy, R[i], 40, 140, 2);
    if (!on) a_seg(cx - 15, cy - 22, cx + 15, cy + 4, 2);
}

/* ---------------------------------------------------------------- */
/* la page                                                          */
/* ---------------------------------------------------------------- */

void tableau_dessine(const tableau_t *t)
{
    static const char *const JOURS[7] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
    static const char *const MOIS[12] = { "JAN", "FEV", "MAR", "AVR", "MAI", "JUN",
                                          "JUL", "AOU", "SEP", "OCT", "NOV", "DEC" };
    char b[128], c[64], d[64];

    /* ============ l'heure, en corps 7 (21 x 35 px par signe) ========== */
    snprintf(b, sizeof(b), "%02d:%02d", t->heure % 24, t->minute % 60);
    txt(2, 0, 7, b);                                  /* "18:54" : 133 x 35 px */
    icone_wifi(180, 22, t->wifi, t->rssi);            /* l'etat du wifi, en clair */

    /* la date, sur une ligne, en corps 3 */
    snprintf(b, sizeof(b), "%s %d %s",
             JOURS[(t->sem < 0 || t->sem > 6) ? 0 : t->sem],
             t->jour, MOIS[(t->mois < 0 || t->mois > 11) ? 0 : t->mois]);
    txt(3, 40, 3, b);
    a_hline(2, 197, 58, 1);

    /* ============ le ciel =========================================== */
    /* quatre lignes, une par information : a ce corps, deux informations sur
       une meme ligne ne respirent plus (l'etat du ciel fait 11 signes). */
    if (t->meteo) {
        snprintf(b, sizeof(b), "%sC", un_chiffre(c, sizeof(c), t->meteo_c));
        txt(3, 62, 4, b);                             /* dehors */
    } else {
        txt(3, 62, 4, "--");
    }
    if (t->meteo) {
        coupe(d, sizeof(d), t->meteo_txt, 16);
        txt(3, 86, 3, d);                             /* l'etat du ciel, en clair */
    } else {
        txt(3, 86, 3, "CIEL N.D.");
    }
    if (t->capteur) snprintf(b, sizeof(b), "INT %dC", (int)(t->temp_int + 0.5f));
    else            snprintf(b, sizeof(b), "INT --");
    txt(3, 106, 3, b);                               /* dedans */
    if (t->dem_min > -900) {
        snprintf(b, sizeof(b), "DEM %d-%dC", t->dem_min, t->dem_max);
        txt(3, 126, 3, b);                           /* demain */
    } else {
        txt(3, 126, 3, "DEM --");
    }
    a_hline(2, 197, 144, 1);

    /* ============ la batterie ======================================= */
    if (t->bat_pct >= 0) {
        int w = nombre_pct(3, 148, 4, t->bat_pct);
        barre(3 + w + 10, 149, 196 - (3 + w + 10), 18, t->bat_pct);
    } else {
        txt(3, 148, 4, "USB");                        /* pas de batterie : pas de jauge */
    }
    a_hline(2, 197, 172, 1);

    /* ============ l'agenda du jour, en corps 4 ====================== */
    if (t->alarme) {
        txt(3, 176, 4, "ALARME");
        txt_r(197, 176, 4, "1=STOP");
    } else if (t->rdv_quand[0]) {
        coupe(d, sizeof(d), t->rdv_quand, 12);        /* 12 signes = 188 px en corps 4 */
        txt(3, 176, 4, d);
    } else if (t->rappels > 0) {
        snprintf(b, sizeof(b), "%d RAPPEL%s", t->rappels, t->rappels > 1 ? "S" : "");
        txt(3, 176, 4, b);
    } else {
        txt(3, 176, 4, "AGENDA LIBRE");
    }
}

/* ---------------------------------------------------------------- */
/* page 2 : l'astuce de streaming du jour                            */
/* ---------------------------------------------------------------- */

/* remplissage mot a mot : renvoie le nombre de lignes posees */
static int remplis(char lignes[][18], int max, int cols, const char *src)
{
    int n = 0;
    const char *p = src;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        int len = (int)strlen(p);
        int k;
        if (len <= cols) {
            k = len;
        } else {
            k = cols;
            while (k > 6 && p[k] != ' ') k--;
            if (k <= 6) k = cols;
        }
        snprintf(lignes[n], 18, "%.*s", k, p);
        n++;
        p += k;
    }
    return n;
}

void astuce_dessine(const char *texte, const char *source, const char *quand, bool dispo)
{
    static char lignes[9][18];

    /* titre plus court qu'en corps 5 : sinon la source, a droite, passait
       par-dessus le E de ASTUCE */
    txt(3, 0, 4, "ASTUCE");                       /* 92 x 20 px */
    if (source && source[0]) txt_r(197, 2, 3, source);   /* R/TWITCH, R/KICK */
    txt(3, 26, 3, "STREAMING");
    if (quand && quand[0]) {
        char d[16];
        snprintf(d, sizeof(d), "%.6s", quand);   /* "12 NOV" : la date suffit ici */
        txt_r(197, 30, 3, d);
    }
    a_hline(2, 197, 50, 1);

    if (!dispo || !texte || !texte[0]) {
        txt(3, 62, 3, "PAS ENCORE");
        txt(3, 84, 3, "D'ASTUCE LUE");
        txt(3, 106, 3, "SUR LE WEB.");
    } else {
        int n = remplis(lignes, 7, 16, texte);    /* 16 signes par ligne, 7 lignes
                                                     (la 8e touchait le pied) */
        for (int i = 0; i < n; i++)
            txt(3, 56 + i * 16, 3, lignes[i]);
    }

    /* le geste pour revenir : la page 2 ne se devine pas */
    txt(3, 184, 3, "2=TABLEAU");
}
