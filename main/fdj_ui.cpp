/*  fdj_ui.cpp — les 20 pages. Meme code sur la carte et dans l'apercu PNG.
 *
 *  Contraintes de la dalle, respectees partout :
 *    - 200x200, 1 bit, pas de gris : le contraste se fait au trait plein ;
 *    - police 3x5, avance 4*s px : 24 caracteres max en s=2, 48 en s=1 ;
 *    - le titre est TOUJOURS dans le bandeau noir du haut, avec "NN/20" ;
 *    - le pied rappelle les gestes REELLEMENT cables sur cette page.
 *
 *  Gabarit (v4.0.0) :
 *      0..17    bandeau noir : titre blanc + numero + etat reseau
 *      22..176  contenu
 *      179      filet
 *      184..190 aide de la page, a gauche / geste d'accueil, a droite
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
#define FTOP 24
#define FBOT 176

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
static int  largeur(int s, const char *t) { return t ? a_text_w(s, t) : 0; }

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

/* gros texte : agrandit la ligne si elle tient dans maxw.
   Reserve aux lignes qui ont la place verticalement (voir appels). */
static void txg(const fdj_vue_t *v, int x, int y, int s, int maxw, const char *t, int noir)
{
    if (!t) return;
    if (v && v->e && v->e->gros_texte && largeur(s, t) <= maxw) {
        int s2 = s + 1;
        if (largeur(s2, t) <= maxw) { a_text_col(x, y, s2, t, noir); return; }
    }
    a_text_col(x, y, s, t, noir);
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

/* "12,50 E" : la police n'a pas le signe euro, on ecrit E. */
static const char *euros(char *b, size_t cap, uint32_t ct, bool signe)
{
    uint32_t e = ct / 100, c = ct % 100;
    snprintf(b, cap, "%s%u,%02u E", signe && ct ? "+" : "", (unsigned)e, (unsigned)c);
    return b;
}

static const char *euros_net(char *b, size_t cap, int32_t ct)
{
    const char *sg = ct > 0 ? "+" : (ct < 0 ? "-" : "");
    uint32_t a = (uint32_t)(ct < 0 ? -ct : ct);
    snprintf(b, cap, "%s%u,%02u E", sg, (unsigned)(a / 100), (unsigned)(a % 100));
    return b;
}

/* mm:ss restant, ou "?" si l'echeance est inconnue */
static const char *restant(char *b, size_t cap, int64_t s)
{
    if (s < 0) { snprintf(b, cap, "DUREE INCONNUE"); return b; }
    snprintf(b, cap, "%dh%02d", (int)(s / 3600), (int)((s / 60) % 60));
    return b;
}

/* ---------------------------------------------------------------- */
/* bandeau de titre + pied                                           */
/* ---------------------------------------------------------------- */
static void entete(const fdj_vue_t *v, const char *titre)
{
    plein(0, 0, 199, 17, 1);
    txt_blanc(6, 3, 2, titre);
    char b[16];
    snprintf(b, sizeof(b), "%02d/%d", v->page + 1, FDJ_PAGES);
    txt_blanc(182 - largeur(1, b), 6, 1, b);
    /* etat reseau : carre plein = connecte, contour = hors ligne */
    if (v->wifi) plein(188, 5, 195, 12, 0);
    else         cadre(188, 5, 195, 12, 1);
}

static void pied(const fdj_vue_t *v)
{
    plein(FX0, 179, FX1, 179, 1);
    if (v && v->message) {
        plein(0, 181, 199, 199, 1);
        char b[52];
        elide(b, sizeof(b), 1, 190, v->message);
        txt_blanc(6, 186, 1, b);
        return;
    }
    txt(FX0, 184, 1, fdj_page_aide(v ? v->page : 0));
    txt_droite(FX1, 184, 1, "B2 3S:ACCUEIL");
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

/* petit bloc plein avec texte blanc, pour les valeurs fortes */
static void valeur_forte(int x0, int y0, int x1, int y1, int s, const char *t)
{
    plein(x0, y0, x1, y1, 1);
    int y = y0 + ((y1 - y0 + 1) - 5 * s) / 2;
    if (y < y0) y = y0;
    txt_blanc(x0 + 4, y, s, t);
}

/* ---------------------------------------------------------------- */
/* page 1 — ACCUEIL                                                 */
/* ---------------------------------------------------------------- */
static void p_accueil(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    const fdj_profil_t *pr = fdj_profil_actif_c(e);

    /* le logo FDJ : un bloc plein, trois lettres, lisible de loin */
    plein(6, 22, 70, 54, 1);
    txt_blanc(14, 24, 4, "FDJ");
    txt_blanc(10, 47, 1, "PARIS EN LIGNE");

    /* logo en 5x10 : le M du 3x5 se lit comme un H sur la dalle */
    a_text5(76, 20, "BATMINO");
    a_text5(76, 31, "MAQUETTE DE DEMO");
    a_text5(76, 42, "AUCUNE MISE REELLE");
    char quand[26];
    if (!v->heure_ok) {
        snprintf(quand, sizeof(quand), "HEURE NON SYNCHRO");
    } else {
        time_t t = (time_t)v->now;
        struct tm ti;
        localtime_r(&t, &ti);
        static const char *J[] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
        snprintf(quand, sizeof(quand), "%s %02d/%02d %02d:%02d",
                 J[ti.tm_wday % 7], ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
    }
    txt(76, 55, 1, quand);   /* sous le bloc, au-dessus des encadres (62) */

    /* solde du profil actif */
    cadre(6, 62, 122, 96, 1);
    txt(10, 64, 1, "SOLDE DE DEMONSTRATION");
    char b[32];
    txt(10, 72, 3, euros(b, sizeof(b), pr ? pr->solde_ct : 0, false));

    /* profil + plafond effectif (peut etre divise par la protection) */
    cadre(128, 62, 194, 96, 1);
    txt(132, 64, 1, "PROFIL ACTIF");
    char nom[14];
    elide(nom, sizeof(nom), 2, 56, pr ? pr->nom : "AUCUN");
    txt(132, 71, 2, nom);
    char d[32];
    if (pr) {
        uint32_t sem = 0, mois = 0;
        fdj_depenses(e, v->now, &sem, &mois);
        snprintf(d, sizeof(d), "%u/%u E", (unsigned)(fdj_plafond_sem_effectif(e) / 100),
                 (unsigned)(pr->plafond_mois_ct / 100));
    } else {
        snprintf(d, sizeof(d), "N.D.");
    }
    txt(132, 86, 1, d);

    /* etat du direct : une seule fois "hors ligne", jamais deux */
    plein(6, 100, 194, 100, 1);
    if (v->live && v->live->ok && v->live->n > 0) {
        snprintf(b, sizeof(b), "%d MATCHS EN DIRECT", v->live->n);
        txt(9, 104, 2, b);
        if (v->live->m[0].dom[0]) {
            char l[40];
            snprintf(l, sizeof(l), "%s %d-%d %s", v->live->m[0].dom,
                     v->live->m[0].buts_dom, v->live->m[0].buts_ext, v->live->m[0].ext);
            elide(b, sizeof(b), 1, 140, l);
            txt(9, 118, 1, b);
        }
        char m[20];
        snprintf(m, sizeof(m), "MAJ %02d:%02d", (int)((v->live->maj / 3600) % 24),
                 (int)((v->live->maj / 60) % 60));
        txt_droite(190, 118, 1, m);
    } else {
        txt(9, 104, 2, "HORS LIGNE");
        txt(9, 118, 1, "SCORES : N.D. - JAMAIS UN 0 INVENTE");
    }

    /* l'item unique : demarrer */
    curseur_barre(132, 18, v->curseur == 0);
    /* en gros texte, un seul mot : 18 caracteres en s=3 ne tiennent pas */
    const char *lbl = v->e->gros_texte ? "DEMARRER" : "DEMARRER LA VISITE";
    int sd = v->e->gros_texte ? 3 : 2;
    int xd = v->e->gros_texte ? 52 : 10;
    int yd = v->e->gros_texte ? 135 : 136;
    txt_blanc(xd, yd, sd, v->curseur == 0 ? lbl : "");
    txt(xd, yd, sd, v->curseur == 0 ? "" : lbl);

    char bas[44];
    snprintf(bas, sizeof(bas), "FDT %s - %u DEMARRAGES", FDJ_VERSION, (unsigned)e->boots);
    txt(FX0, 156, 1, bas);
    txt(FX0, 166, 1, "CODE INITIAL A MODIFIER (PAGE 16)");
}

/* ---------------------------------------------------------------- */
/* intertitres : une page de transition avant CHAQUE module          */
/* ---------------------------------------------------------------- */

/* jeux de caracteres par module : deux lignes courtes, une phrase */
typedef struct {
    const char *haut;      /* grande ligne 1 */
    const char *bas;       /* grande ligne 2 (peut etre vide) */
    const char *phrase;    /* une phrase, coupee en 2 ou 3 lignes */
    const char *note;      /* rappel court en bas */
} inter_t;

static const inter_t INTER[FDJ_M_NB] = {
    { "", "", "", "" },                                             /* accueil */
    { "ANALYSE", "ET DECISION", "DES SCORES AUX PROBABILITES DU MODELE",
      "PROBABILITE N'EST PAS UNE COTE" },
    { "LE HASARD", "SANS MENTIR", "TROIS GRILLES, UNE CHANCE SUR 19 MILLIONS",
      "GRILLE EQUILIBREE = MEME CHANCE" },
    { "LA TRACE", "DE CHAQUE PARI", "CODE, JEU, MISE, GAIN : TOUT EST GARDE",
      "LE DETAIL S'OUVRE VRAIMENT" },
    { "L'ARGENT", "SANS ARRONDIR", "MISES, GAINS CONFIRMES, NET PAR JOUR",
      "UN PARI EN COURS N'EST PAS UN GAIN" },
    { "LE FIL", "EN CONTINU", "TITRES REELS, HORODATES, JAMAIS INVENTES",
      "PAS DE RESEAU = N.D." },
    { "LE COUPON", "", "CHOISIR, VERIFIER, VALIDER",
      "REFUS MOTIVE, RIEN N'EST DEBITE" },
    { "GARDER", "LE CONTROLE", "DES LIMITES QUI S'APPLIQUENT VRAIMENT",
      "PAUSE 24 H AVEC ECHEANCE REELLE" },
    { "LES CHIFFRES", "EXACTS", "PROBABILITES, COTE IMPLICITE, HISTORIQUE",
      "AUCUN TRJ INVENTE" },
    { "LE CAHIER", "DES CHARGES", "SIX HISTOIRES ET LEURS CRITERES",
      "LA PAGE 12 MONTRE LES PREUVES" },
    { "LES PREUVES", "PAS LES PROMESSES", "VERIFICATIONS REELLES, DATES, RELANCEES",
      "AUCUNE PAGE LIVREE SANS TESTS" },
    { "SOUS", "LE CAPOT", "LE MEME CODE SUR LA CARTE ET SUR LE PC",
      "TAILLES MESUREES, PAS ESTIMEES" },
    { "VITESSE", "ET SECURITE", "MESURES LIVE, TLS VERIFIE",
      "AUCUN SECRET AFFICHE" },
    { "LA MEMOIRE", "COMPTEE", "HEAP, PSRAM, CAPACITES REELLES",
      "PAS DE FAUX SWAP" },
    { "MON COMPTE", "ET MES REGLAGES", "RESEAU, SON, CODE PROTEGE",
      "LE CODE N'EST JAMAIS AFFICHE" },
    { "QUATRE", "PROFILS", "PLAFONDS ET SOLDES SEPARES",
      "CHAQUE PROFIL A SON BUDGET" },
    { "POUR", "TOUS", "GROS TEXTE, CONTRASTE, CLIC",
      "AUCUNE INFO PAR LA COULEUR" },
    { "MES", "DONNEES", "CE QUI EST STOCKE, CE QUI SORT",
      "EFFACEMENT AVEC CONFIRMATION" },
    { "LES REGLES", "DU JEU", "FAMILLES, CONDITIONS, LIMITES",
      "MAQUETTE NON OFFICIELLE" },
};

/* plus grande taille qui fait tenir les deux lignes dans maxw */
static int fit_s(const char *a, const char *b, int maxw)
{
    for (int s = 6; s >= 3; s--) {
        int wa = largeur(s, a), wb = b ? largeur(s, b) : 0;
        if (wa <= maxw && wb <= maxw) return s;
    }
    return 3;
}

/* phrase coupee a l'espace, sur maxlignes, sans jamais trancher un mot */
static void phrase_wrap(int x, int y, int s, int maxw, const char *t, int maxlignes, int interligne)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", t ? t : "");
    char *p = buf;
    int maxc = maxw / (4 * s);
    for (int ligne = 0; ligne < maxlignes && *p; ligne++) {
        int n = (int)strlen(p);
        int cut = n;
        if (n > maxc) {
            cut = maxc;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (cut <= 0) cut = maxc;
        }
        char tmp[160];
        memcpy(tmp, p, (size_t)cut);
        tmp[cut] = 0;
        txt(x, y + ligne * interligne, s, tmp);
        p += cut;
        while (*p == ' ') p++;
    }
}

static void p_intertitre(const fdj_vue_t *v)
{
    int m = fdj_ecran_module(v->page);
    if (m <= 0 || m >= FDJ_M_NB) return;
    const inter_t *it = &INTER[m];

    char num[24];
    snprintf(num, sizeof(num), "MODULE %02d", m);
    txt(10, 22, 1, num);
    txt_droite(190, 22, 1, "MAQUETTE DE DEMONSTRATION");

    /* taille : la plus grande qui tient en largeur (fit_s) puis en hauteur,
       pour que la phrase et la note gardent leur place au-dessus du bouton */
    int sc = fit_s(it->haut, it->bas, 190);
    int lignes = 1 + (it->bas[0] ? 1 : 0);
    while (sc > 3 && 34 + lignes * 6 * sc + 12 + 2 * 12 + 6 + 15 > 148) sc--;
    int y = 34;
    txt(9, y, sc, it->haut);
    y += 6 * sc;
    if (it->bas[0]) { txt(9, y, sc, it->bas); y += 6 * sc; }

    plein(9, y + 4, 191, y + 4, 1);

    int py = y + 12;
    phrase_wrap(9, py, 2, 186, it->phrase, 2, 12);

    int by = py + 2 * 12 + 6;
    if (by > 142) by = 142;
    cadre(9, by, 191, by + 15, 1);
    char note[48];
    elide(note, sizeof(note), 1, 176, it->note);
    txt(13, by + 5, 1, note);

    curseur_barre(156, 17, true);
    txt_blanc(14, 160, 2, "B2 : CONTINUER");
}

/* ---------------------------------------------------------------- */
/* page 3 — PARIS SPORTIFS : le top 10 calcule                      */
/* ---------------------------------------------------------------- */
static void p_paris(const fdj_vue_t *v)
{
    const fdj_top_t *t = v->top;
    bool gros = v->e->gros_texte;
    int lignes = gros ? 5 : 10;
    int pas = gros ? 27 : 13;

    if (!t || t->n == 0) {
        txt_centre(100, 66, 2, "AUCUN MATCH EN DIRECT");
        txt_centre(100, 86, 1, "LE MODELE NE DEVINE PAS UN SCORE");
        if (!v->live || !v->live->ok)
            txt_centre(100, 98, 1, "HORS LIGNE : N.D.");
        else
            txt_centre(100, 98, 1, "FLUX VIDE : N.D.");
        txt_centre(100, 114, 1, "B2X2 : ACTUALISER LES SCORES");
        return;
    }

    /* age de la donnee, en clair */
    char h[44];
    if (v->live && v->live->ok && v->now > 0 && v->live->maj > 0) {
        int64_t age = (v->now - v->live->maj) / 60;
        if (age < 0) age = 0;
        snprintf(h, sizeof(h), "DONNEES MAJ IL Y A %d MIN - MODELE POISSON", (int)age);
    } else if (v->live && v->live->ok) {
        snprintf(h, sizeof(h), "DONNEES MAJ : HEURE NON SYNCHRONISEE");
    } else {
        snprintf(h, sizeof(h), "HORS LIGNE : N.D.");
    }
    txt(FX0, 21, 1, h);

    for (int i = 0; i < t->n && i < lignes; i++) {
        const fdj_sel_t *s = &t->s[i];
        int y = 30 + i * pas;
        bool sel = (v->curseur == i);
        if (sel) plein(0, y - 1, 178, y + pas - 3, 1);   /* la coche reste sur blanc */
        int noir = sel ? 0 : 1;

        /* code de coupon dans une pastille */
        plein(6, y + 1, 27, y + 9, sel ? 0 : 1);
        a_text_col(9, y + 2, 1, s->code, sel ? 1 : 0);

        char m[26];
        elide(m, sizeof(m), gros ? 2 : 1, gros ? 140 : 120, s->match);
        if (gros) a_text_col(31, y + 1, 2, m, noir);
        else      a_text_col(31, y + 1, 1, m, noir);

        char l2[44];
        const char *info = "";
        if (v->live && s->idx_match < v->live->n) info = v->live->m[s->idx_match].info;
        snprintf(l2, sizeof(l2), "%s %s %s", s->ligue, info, s->marche);
        char l2e[44];
        elide(l2e, sizeof(l2e), 1, gros ? 130 : 128, l2);
        a_text_col(31, y + (gros ? 15 : 8), 1, l2e, noir);

        char p[8];
        snprintf(p, sizeof(p), "%d%%", (int)(s->proba * 100.0f + 0.5f));
        a_text_col(176 - largeur(1, p), y + (gros ? 15 : 8), 1, p, noir);
        coche(184, y + 3, (v->e->sel_cochee & (1u << i)) != 0);
    }
    if (!gros) txt(FX0, 168, 1, "PROBA = MODELE, PAS UNE COTE FDJ");
}

/* ---------------------------------------------------------------- */
/* page 4 — LOTO : 3 grilles + des faits verifiables                */
/* ---------------------------------------------------------------- */
static void p_loto(const fdj_vue_t *v)
{
    for (int g = 0; g < v->nb_grilles && g < FDJ_GRILLE_MAX; g++) {
        int y = 24 + g * 32;
        bool sel = (v->curseur == g);
        if (sel) plein(0, y, 4, y + 29, 1);      /* barre, pas de fond plein */
        int noir = 1;

        char b[16];
        snprintf(b, sizeof(b), "GRILLE %d", g + 1);
        txg(v, 10, y + 2, 1, 62, b, noir);

        for (int k = 0; k < FDJ_GRILLE_NB; k++) {
            int bx = 10 + k * 26, by = y + 10;
            cadre(bx, by, bx + 21, by + 17, 1);
            char n[4];
            snprintf(n, sizeof(n), "%02u", (unsigned)v->grilles[g][k]);
            a_text_col(bx + 2, by + 4, 2, n, 1);
        }
        int cx = 10 + FDJ_GRILLE_NB * 26 + 4;
        cadre(cx, y + 10, cx + 26, y + 27, 1);
        char c[8];
        snprintf(c, sizeof(c), "C%u", (unsigned)v->chances[g]);
        a_text_col(cx + 3, y + 15, 1, c, 1);
        coche(184, y + 10, (v->e->grille_cochee & (1u << g)) != 0);
    }

    /* trois faits exacts, verifiables : pas d'astuce inventee */
    int ty = 122;
    cadre(FX0, ty, FX1, ty + 30, 1);
    txt(FX0 + 4, ty + 3, 1, "CE QUI EST VRAI");
    static const char *FAIT[] = {
        "1 GRILLE = 1 CHANCE SUR 19 068 840",
        "3 GRILLES = 3 CHANCES SUR 19 068 840",
        "GRILLE DITE EQUILIBREE : MEME CHANCE",
    };
    txt(FX0 + 4, ty + 12, 1, FAIT[(v->now / 86400) % 3]);
    txt(FX0 + 4, ty + 21, 1, "CHANCE : 1 SUR 10, INDEPENDANTE");

    int ny = 156;
    bool sel = (v->curseur == FDJ_GRILLE_MAX);
    curseur_barre(ny, 16, sel);
    txt_blanc(FX0 + 2, ny + 5, 1, sel ? "NOUVELLE GRILLE (B2X2)" : "");
    txt(FX0 + 2, ny + 5, 1, sel ? "" : "NOUVELLE GRILLE (B2X2)");
}

/* ---------------------------------------------------------------- */
/* page 5 — HISTORIQUE (+ detail reel)                              */
/* ---------------------------------------------------------------- */
static int hist_visibles(const fdj_etat_t *e)
{
    int reste = (int)e->hist_n - (int)e->hist_off;
    if (reste <= 0) return 0;
    return reste < FDJ_HIST_PAGE ? reste : FDJ_HIST_PAGE;
}

/* combien de paris tiennent, et reste-t-il une page suivante ?
   Quand il y a une suite, on n'affiche que 5 paris pour laisser la place
   a l'item "PLUS ANCIENS" au-dessus du pied. */
static int hist_affiche(const fdj_etat_t *e, bool *plus)
{
    int pas = e->gros_texte ? 34 : 25;
    int n = hist_visibles(e);
    int reste = (int)e->hist_n - (int)e->hist_off - n;
    bool p = reste > 0;
    /* 5 lignes max au-dessus du pied, et une de moins si l'item "PLUS
       ANCIENS" doit tenir a y=150 (cas du gros texte) */
    int maxn = p ? (150 - 24) / pas : (176 - 24) / pas;
    if (n > maxn) n = maxn;
    if (plus) *plus = p;
    return n;
}

static void p_historique(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    if (e->hist_n == 0) {
        txt_centre(100, 66, 2, "AUCUN PARI");
        txt_centre(100, 86, 1, "VALIDEZ LE COUPON (PAGE 8)");
        txt_centre(100, 98, 1, "TOUT EST VIDE, ET C'EST HONNETE");
        return;
    }
    bool gros = v->e->gros_texte;
    bool plus = false;
    int n = hist_affiche(e, &plus);
    int pas = gros ? 34 : 25;
    for (int i = 0; i < n; i++) {
        int idx = fdj_hist_index_recence(e, (int)e->hist_off + i);
        if (idx < 0) continue;
        const fdj_pari_t *p = &e->hist[idx];
        int y = 24 + i * pas;
        bool sel = (v->curseur == i);
        curseur_barre(y, pas - 1, sel);
        int noir = sel ? 0 : 1;

        char l[FDJ_TXT_L + 2];
        elide(l, sizeof(l), gros ? 2 : 1, gros ? 150 : 138, p->libelle);
        if (gros) a_text_col(8, y + 2, 2, l, noir);
        else      a_text_col(8, y + 1, 1, l, noir);

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
        a_text_col(8, y + (gros ? 15 : 8), 1, d, noir);

        char j[32];
        snprintf(j, sizeof(j), "CODE %s  %s", p->code, fdj_jeu_nom(p->jeu));
        a_text_col(8, y + (gros ? 25 : 15), 1, j, noir);
    }
    /* pagination : l'item supplementaire fait defiler l'anneau */
    if (plus) {
        int y = 24 + n * pas;
        bool sel = (v->curseur == n);
        curseur_barre(y, 18, sel);
        char b[40];
        snprintf(b, sizeof(b), "PLUS ANCIENS (%d) - B1", (int)e->hist_n - (int)e->hist_off - n);
        txt_blanc(8, y + 6, 1, sel ? b : "");
        txt(8, y + 6, 1, sel ? "" : b);
    }
    txt(FX0, 168, 1, "B1 : OUVRIR LE DETAIL DU PARI");
}

void fdj_ui_detail(uint8_t *fb, const fdj_pari_t *p, int idx)
{
    art_begin(fb, 0);
    art_clear();
    plein(0, 0, 199, 17, 1);
    txt_blanc(6, 3, 2, "DETAIL DU PARI");
    char b[20];
    snprintf(b, sizeof(b), "N%d", idx + 1);
    txt_blanc(172, 6, 1, b);

    if (!p) { txt_centre(100, 90, 2, "PARI INTROUVABLE"); return; }

    char l[FDJ_TXT_L + 2];
    elide(l, sizeof(l), 2, 188, p->libelle);
    txt(6, 28, 2, l);

    int y = 52;
    char d[64];
    time_t t = (time_t)p->t;
    struct tm ti;
    localtime_r(&t, &ti);
    snprintf(d, sizeof(d), "DATE   %02d/%02d/%04d %02d:%02d", ti.tm_mday, ti.tm_mon + 1,
             ti.tm_year + 1900, ti.tm_hour, ti.tm_min);
    txt(8, y, 1, d); y += 12;
    snprintf(d, sizeof(d), "JEU    %s", fdj_jeu_nom(p->jeu));
    txt(8, y, 1, d); y += 12;
    snprintf(d, sizeof(d), "CODE   %s", p->code);
    txt(8, y, 1, d); y += 12;

    plein(6, y + 2, 194, y + 2, 1); y += 8;

    snprintf(d, sizeof(d), "MISE ENGAGEE   %u,%02u E", (unsigned)(p->mise_ct / 100),
             (unsigned)(p->mise_ct % 100));
    txt(8, y, 1, d); y += 14;

    if (p->statut == FDJ_PARI_GAGNE) {
        snprintf(d, sizeof(d), "GAIN CONFIRME  %u,%02u E", (unsigned)(p->gain_ct / 100),
                 (unsigned)(p->gain_ct % 100));
        valeur_forte(6, y - 3, 194, y + 15, 2, d);
        y += 24;
        txt(8, y, 1, "STATUT : GAGNE, GAIN CREDITE");
    } else if (p->statut == FDJ_PARI_EN_COURS) {
        snprintf(d, sizeof(d), "GAIN POTENTIEL %u,%02u E", (unsigned)(p->gain_ct / 100),
                 (unsigned)(p->gain_ct % 100));
        cadre(6, y - 3, 194, y + 15, 1);
        txt(10, y + 1, 2, d);
        y += 24;
        txt(8, y, 1, "STATUT : EN COURS - PAS UN GAIN");
    } else {
        txt(8, y, 2, "PERDU");
        y += 20;
        txt(8, y, 1, "STATUT : PERDU, MISE ENGAGEE");
    }

    txt_centre(100, 186, 1, "B1 : FERMER");
}

/* ---------------------------------------------------------------- */
/* page 6 — GAINS / PERTES                                          */
/* ---------------------------------------------------------------- */
static void p_gains(const fdj_vue_t *v)
{
    static const int JOURS[3] = { 7, 30, 365 };
    static const char *NOM[3] = { "7 JOURS", "30 JOURS", "TOUT (32 MAX)" };
    int choix = v->curseur < 3 ? v->curseur : 0;

    for (int i = 0; i < 3; i++) {
        fdj_periode_t p;
        fdj_periode_calcule(v->e, v->now, JOURS[i], &p);
        int y = 24 + i * 28;
        bool sel = (v->curseur == i);
        curseur_barre(y, 27, sel);
        int noir = sel ? 0 : 1;

        a_text_col(8, y + 2, 2, NOM[i], noir);
        char b[24];
        if (p.nb == 0) snprintf(b, sizeof(b), "N.D.");
        else           euros_net(b, sizeof(b), p.net_ct);
        int sv = (v->e->gros_texte && largeur(3, b) <= 110) ? 3 : 2;
        int yv = (sv == 3) ? y + 1 : y + 2;
        a_text_col(190 - largeur(sv, b), yv, sv, b, noir);

        char d[64];
        snprintf(d, sizeof(d), "%d PARIS  MISE %u,%02u  GAIN %u,%02u",
                 p.nb, (unsigned)(p.mise_ct / 100), (unsigned)(p.mise_ct % 100),
                 (unsigned)(p.gain_ct / 100), (unsigned)(p.gain_ct % 100));
        elide(d, sizeof(d), 1, 182, d);
        a_text_col(8, y + 16, 1, d, noir);
    }

    /* le detail de la periode choisie : ce n'est plus un controle decoratif */
    fdj_periode_t p;
    fdj_periode_calcule(v->e, v->now, JOURS[choix], &p);
    plein(6, 110, 194, 110, 1);
    char net[20], b[64];
    if (p.nb == 0) snprintf(b, sizeof(b), "%s : AUCUN PARI, N.D.", NOM[choix]);
    else snprintf(b, sizeof(b), "%s : %d PARIS, NET %s", NOM[choix], p.nb,
                  euros_net(net, sizeof(net), p.net_ct));
    txt(8, 116, 1, b);
    if (v->gains_detail) {
        /* ventilation par jeu, calculee sur la periode choisie */
        uint32_t mise[FDJ_JEU_NB] = { 0 }, gain[FDJ_JEU_NB] = { 0 };
        int nbj[FDJ_JEU_NB] = { 0 };
        int64_t depuis = v->now - (int64_t)JOURS[choix] * 86400;
        for (int k = 0; k < v->e->hist_n; k++) {
            int idx = fdj_hist_index_recence(v->e, k);
            if (idx < 0) continue;
            const fdj_pari_t *pp = &v->e->hist[idx];
            if (pp->t < depuis) continue;
            if (pp->jeu >= FDJ_JEU_NB) continue;
            mise[pp->jeu] += pp->mise_ct;
            nbj[pp->jeu]++;
            if (pp->statut == FDJ_PARI_GAGNE) gain[pp->jeu] += pp->gain_ct;
        }
        int ligne = 0;
        for (int j = 0; j < FDJ_JEU_NB && ligne < 2; j++) {
            if (!nbj[j]) continue;
            char d[56];
            snprintf(d, sizeof(d), "%s : %d PARIS, M %u,%02u G %u,%02u",
                     fdj_jeu_nom((uint8_t)j), nbj[j], (unsigned)(mise[j] / 100),
                     (unsigned)(mise[j] % 100), (unsigned)(gain[j] / 100),
                     (unsigned)(gain[j] % 100));
            elide(d, sizeof(d), 1, 186, d);
            txt(8, 126 + ligne * 9, 1, d);
            ligne++;
        }
        if (!ligne) txt(8, 126, 1, "AUCUN PARI SUR CETTE PERIODE");
    } else {
        txt(8, 126, 1, "GAINS CONFIRMES SEULEMENT, PAS POTENTIELS");
        txt(8, 136, 1, "L'ANNEAU GARDE 32 PARIS : PAS 1 AN GARANTI");
    }

    /* 7 derniers jours : au-dessus du trait = gain, en dessous = perte */
    txt(FX0, 146, 1, "NET PAR JOUR (7 J)");
    int by = 160, hmax = 8;
    plein(6, by, 194, by, 1);
    txt_droite(194, 146, 1, "HAUT=GAIN BAS=PERTE");
    for (int i = 0; i < 7; i++) {
        int64_t jour_debut = (v->now / 86400) * 86400 + (int64_t)i * 86400 - 6 * 86400;
        int32_t net = 0;
        for (int k = 0; k < v->e->hist_n; k++) {
            int idx = fdj_hist_index_recence(v->e, k);
            if (idx < 0) continue;
            const fdj_pari_t *pp = &v->e->hist[idx];
            if (pp->t >= jour_debut && pp->t < jour_debut + 86400)
                net += (int32_t)pp->gain_ct - (int32_t)pp->mise_ct;
        }
        int h = (int)(net / 100);
        if (h > hmax) h = hmax;
        if (h < -hmax) h = -hmax;
        int x = 8 + i * 26;
        if (h > 0)      plein(x + 3, by - h, x + 14, by - 1, 1);
        else if (h < 0) plein(x + 3, by + 1, x + 14, by - h, 1);
        else            plein(x + 3, by, x + 14, by, 1);
        static const char *JR[] = { "D", "L", "M", "M", "J", "V", "S" };
        time_t tt = (time_t)(jour_debut + 43200);
        struct tm tj; localtime_r(&tt, &tj);
        txt_centre(x + 8, 171, 1, JR[tj.tm_wday % 7]);
    }
}

/* ---------------------------------------------------------------- */
/* page 7 — NEWS LIVE                                               */
/* ---------------------------------------------------------------- */
static void p_news(const fdj_vue_t *v)
{
    const fdj_flux_t *f = v->news;
    if (!f || !f->ok || f->nb == 0) {
        txt_centre(100, 64, 2, "FLUX INDISPONIBLE");
        txt_centre(100, 84, 1, "PAS DE TITRE INVENTE : N.D.");
        txt_centre(100, 96, 1, f && f->maj ? "DEJA CHARGE, PUIS VIDE" : "JAMAIS CHARGE");
        txt_centre(100, 110, 1, "B2X2 : FORCER UNE REQUETE");
        return;
    }
    bool gros = v->e->gros_texte;
    int n = f->nb < (gros ? 3 : 4) ? f->nb : (gros ? 3 : 4);
    for (int i = 0; i < n; i++) {
        const fdj_news_t *nn = &f->n[i];
        int y = 24 + i * (gros ? 46 : 36);
        bool sel = (v->curseur == i);
        curseur_barre(y, (gros ? 44 : 34), sel);
        int noir = sel ? 0 : 1;

        a_text_col(8, y + 2, 1, nn->quand, noir);
        a_text_col(8 + largeur(1, nn->quand) + 6, y + 2, 1, nn->source, noir);

        int maxc = gros ? 22 : 40;
        char a[56], b[56];
        int len = (int)strlen(nn->titre);
        int cut = len < maxc ? len : maxc;
        if (cut < len) {
            int k = cut;
            while (k > maxc - 14 && nn->titre[k] != ' ') k--;
            if (k > 0) cut = k;
        }
        memcpy(a, nn->titre, (size_t)cut);
        a[cut] = 0;
        elide(b, sizeof(b), 1, 160, nn->titre + cut + (nn->titre[cut] == ' ' ? 1 : 0));
        if (gros) a_text_col(8, y + 10, 2, a, noir);
        else      a_text_col(8, y + 9, 1, a, noir);
        if (b[0]) a_text_col(8, y + (gros ? 24 : 17), 1, b, noir);

        coche(184, y + 2, (v->e->news_lu & (1u << i)) != 0);
    }
}

/* ---------------------------------------------------------------- */
/* page 8 — COUPON (mise en page compacte : plus de chevauchement)  */
/* ---------------------------------------------------------------- */
static int coupon_nb(const fdj_vue_t *v)
{
    int n = 0;
    if (v->top) for (int i = 0; i < v->top->n; i++)
        if (v->e->sel_cochee & (1u << i)) n++;
    for (int g = 0; g < FDJ_GRILLE_MAX; g++)
        if (v->e->grille_cochee & (1u << g)) n++;
    return n;
}

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
    int total_nb = coches + loto;
    uint32_t total_ct = (uint32_t)coches * 200 + (uint32_t)loto * 220;

    char b[64];
    snprintf(b, sizeof(b), "MISE TOTALE  %u,%02u E", (unsigned)(total_ct / 100),
             (unsigned)(total_ct % 100));
    txt(8, 22, 2, b);
    if (total_nb == 0) {
        txt(8, 36, 1, "GAIN POTENTIEL : N.D. (AUCUN PARI)");
    } else {
        snprintf(b, sizeof(b), "GAIN POTENTIEL (MODELE) %u,%02u E",
                 (unsigned)(gain_pot / 100), (unsigned)(gain_pot % 100));
        elide(b, sizeof(b), 1, 186, b);
        txt(8, 36, 1, b);
    }
    plein(6, 44, 194, 44, 1);

    /* liste compacte : 5 lignes max + un compteur, jamais de debordement.
       Tableau dense : il garde son corps 1 meme en gros texte. */
    const int maxl = 5;
    int k = 0;
    if (t) {
        for (int i = 0; i < t->n && k < maxl; i++) {
            if (!(v->e->sel_cochee & (1u << i))) continue;
            const fdj_sel_t *s = &t->s[i];
            int y = 48 + k * 14;
            bool sel = (v->curseur == k);
            if (sel) plein(0, y, 178, y + 13, 1);
            int noir = sel ? 0 : 1;
            a_text_col(8, y + 4, 1, s->code, noir);
            char m[28];
            elide(m, sizeof(m), 1, 96, s->match);
            a_text_col(36, y + 4, 1, m, noir);
            a_text_col(136, y + 4, 1, s->marche, noir);
            a_text_col(164, y + 4, 1, "2,00", noir);
            coche(186, y + 2, true);
            k++;
        }
    }
    for (int g = 0; g < loto && k < maxl; g++) {
        if (!(v->e->grille_cochee & (1u << g))) continue;
        int y = 48 + k * 14;
        bool sel = (v->curseur == k);
        if (sel) plein(0, y, 178, y + 13, 1);
        int noir = sel ? 0 : 1;
        char m[44];
        snprintf(m, sizeof(m), "LOTO%d %02u %02u %02u %02u %02u C%u", g + 1,
                 v->grilles[g][0], v->grilles[g][1], v->grilles[g][2],
                 v->grilles[g][3], v->grilles[g][4], v->chances[g]);
        a_text_col(8, y + 4, 1, m, noir);
        a_text_col(164, y + 4, 1, "2,20", noir);
        k++;
    }
    if (total_nb > maxl) {
        snprintf(b, sizeof(b), "+ %d AUTRES SELECTIONS (TOTAL %d)", total_nb - maxl, total_nb);
        txt(8, 120, 1, b);
    } else if (total_nb == 0) {
        txt(8, 100, 1, "AUCUNE SELECTION : COCHEZ PAGE 3 OU 4");
    }

    /* actions : deux zones fixes, plus de chevauchement a 10 selections */
    int yv = 132;
    bool sv = (v->curseur == total_nb);
    curseur_barre(yv, 15, sv);
    txt_blanc(10, yv + 5, 1, sv ? "VALIDER LE COUPON (B2X2)" : "");
    txt(10, yv + 5, 1, sv ? "" : "VALIDER LE COUPON (B2X2)");

    int yc = 148;
    bool sc = (v->curseur == total_nb + 1);
    curseur_barre(yc, 15, sc);
    txt_blanc(10, yc + 5, 1, sc ? "VIDER LE COUPON" : "");
    txt(10, yc + 5, 1, sc ? "" : "VIDER LE COUPON");

    txt(FX0, 168, 1, "LOTO : GAIN SELON LE RANG DU TIRAGE");
}

/* ---------------------------------------------------------------- */
/* page 9 — JEU RESPONSABLE                                         */
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
    int score = fdj_protection_score(v->e);
    for (int i = 0; i < QN; i++) {
        int y = 24 + i * 21;
        bool sel = (v->curseur == i);
        curseur_barre(y, 20, sel);
        int noir = sel ? 0 : 1;

        char b[52];
        elide(b, sizeof(b), 1, 148, QUESTIONS[i]);
        txg(v, 8, y + 3, 1, 148, b, noir);
        bool oui = (v->e->sel_cochee >> (8 + i)) & 1;
        a_text_col(156, y + 3, 1, oui ? "OUI" : "NON", noir);
        coche(184, y + 6, oui);
    }

    /* jauge de protection, sans couleur : pleine = bon */
    int y = 132;
    txt(8, y, 1, "PROTECTION");
    for (int i = 0; i < QN; i++)
        plein(84 + i * 18, y, 84 + i * 18 + 14, y + 6, i < score ? 1 : 0);
    cadre(84, y, 84 + QN * 18 - 4, y + 6, 1);
    char b[32];
    snprintf(b, sizeof(b), "%d/%d", score, QN);
    txt(84 + QN * 18 + 2, y, 1, b);

    /* pause 24 h : une vraie echeance, affichee */
    int yp = 146;
    bool sel = (v->curseur == QN);
    curseur_barre(yp, 15, sel);
    int noir = sel ? 0 : 1;
    char p[52];
    if (fdj_pause_active(v->e, v->now)) {
        char r[20];
        restant(r, sizeof(r), fdj_pause_restant_s(v->e, v->now));
        snprintf(p, sizeof(p), "PAUSE ACTIVE - FIN DANS %s", r);
    } else {
        snprintf(p, sizeof(p), "PAUSE 24 H (AUTO-EXCLUSION)");
    }
    elide(p, sizeof(p), 1, 160, p);
    a_text_col(8, yp + 4, 1, p, noir);
    coche(184, yp + 3, fdj_pause_active(v->e, v->now));

    if (score > 0 && score < 3)
        txt(FX0, 166, 1, "PROTECTION FAIBLE : PLAFOND 7J DIVISE PAR 2");
    else if (score >= 3)
        txt(FX0, 166, 1, "PROTECTION OK : REPONSES PRISES EN COMPTE");
    else
        txt(FX0, 166, 1, "REPONDEZ OUI POUR RENFORCER VOTRE PROTECTION");
}

/* ---------------------------------------------------------------- */
/* page 10 — STATS & PROBAS                                         */
/* ---------------------------------------------------------------- */
static void p_stats(const fdj_vue_t *v)
{
    char b[40], d[52];
    int gagne = 0, total = 0;
    for (int i = 0; i < v->e->hist_n; i++) {
        int idx = fdj_hist_index_recence(v->e, i);
        if (idx < 0) continue;
        if (v->e->hist[idx].statut == FDJ_PARI_EN_COURS) continue;
        total++;
        if (v->e->hist[idx].statut == FDJ_PARI_GAGNE) gagne++;
    }
    struct { const char *t, *val, *n; } B[4];
    B[0].t = "LOTO 5/49 + CHANCE";
    B[0].val = "1 / 19 068 840";
    B[0].n = "5 GRILLES = 5 CHANCES, PAS PLUS";
    B[1].t = "EUROMILLIONS";
    B[1].val = "1 / 139 838 160";
    B[1].n = "2 ETOILES EN PLUS : 7,3X PIRE";
    B[2].t = "COTE 2,00";
    B[2].val = "50 %";
    B[2].n = "PROBA IMPLICITE, PAS UNE COTE FDJ";
    if (total > 0) {
        snprintf(b, sizeof(b), "%d / %d", gagne, total);
        B[3].val = b;
        B[3].n = "HISTORIQUE REEL, PAS UNE PREDICTION";
    } else {
        B[3].val = "N.D.";
        B[3].n = "AUCUN PARI REGLE POUR L'INSTANT";
    }
    B[3].t = "MES PARIS REGLES";

    for (int i = 0; i < 4; i++) {
        int y = 24 + i * 34;
        bool sel = (v->curseur == i);
        curseur_barre(y, 33, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 1, B[i].t, noir);
        /* valeur dominante : s=3 si elle tient, sinon s=2 */
        a_text_col(8, y + 10, 2, B[i].val, noir);
        elide(d, sizeof(d), 1, 182, B[i].n);
        a_text_col(8, y + 25, 1, d, noir);
    }
    txt(FX0, 166, 1, "AUCUN TRJ OFFICIEL N'EST AFFICHE ICI");
}

/* ---------------------------------------------------------------- */
/* page 11 — USER STORIES                                           */
/* ---------------------------------------------------------------- */
typedef struct { const char *id, *titre, *detail; int crit; } us_t;
static const us_t US[] = {
    { "US-01", "PARIER EN DIRECT",    "MODELE POISSON + CODES", 5 },
    { "US-02", "LOTO EQUILIBRE",      "5/49 + CHANCE, 3 GRILLES", 4 },
    { "US-03", "HISTORIQUE ET GAINS", "DETAIL, PAGINATION, NET", 4 },
    { "US-04", "PROFILS ET PLAFONDS", "4 MAX, SUPPRESSION REELLE", 6 },
    { "US-05", "CODE DE DEMARRAGE",   "4 CHIFFRES, JAMAIS AFFICHE", 4 },
    { "US-06", "NEWS ET HORS LIGNE",  "3 H, ET N.D. SI PANNE", 4 },
};
#define USN 6

static void p_stories(const fdj_vue_t *v)
{
    for (int i = 0; i < USN; i++) {
        int y = 24 + i * 23;
        bool sel = (v->curseur == i);
        curseur_barre(y, 22, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 1, US[i].id, noir);
        if (v->e->gros_texte) {
            a_text_col(40, y + 2, 2, US[i].titre, noir);
        } else {
            a_text_col(40, y + 2, 1, US[i].titre, noir);
            txg(v, 40, y + 10, 1, 120, US[i].detail, noir);
        }
        char t[12];
        snprintf(t, sizeof(t), "%d AC", US[i].crit);
        a_text_col(166, y + 10, 1, t, noir);
    }
    char b[56];
    snprintf(b, sizeof(b), "AC = CRITERES - DETAIL DANS docs/features");
    txt(FX0, 166, 1, b);
}

/* ---------------------------------------------------------------- */
/* page 12 — TESTS                                                  */
/* ---------------------------------------------------------------- */
static void p_tests(const fdj_vue_t *v)
{
    struct { const char *t, *d; int n; } N[4] = {
        { "1. UNITAIRES",   "MODELE, PARSEURS, HASARD", 0 },
        { "2. COMPOSANTS",  "CHAQUE PAGE RENDUE EN PNG", 0 },
        { "3. INTEGRATION", "NAV + ARGENT + PERSISTANCE", 0 },
        { "4. FONCTIONNEL", "SCENARIO COMPLET, 20 PAGES", 0 },
    };
#ifdef FDJ_TESTS_UNIT
    N[0].n = FDJ_TESTS_UNIT;
    N[1].n = FDJ_TESTS_COMP;
    N[2].n = FDJ_TESTS_INTEG;
    N[3].n = FDJ_TESTS_FONC;
#endif
    int total = 0;
    for (int i = 0; i < 4; i++) total += N[i].n;

    for (int i = 0; i < 4; i++) {
        int y = 24 + i * 25;
        bool sel = (v->curseur == i);
        curseur_barre(y, 24, sel);
        int noir = sel ? 0 : 1;
        if (v->e->gros_texte && largeur(3, N[i].t) <= 150)
            a_text_col(8, y + 2, 3, N[i].t, noir);
        else
            a_text_col(8, y + 2, 2, N[i].t, noir);
        if (!v->e->gros_texte) txg(v, 8, y + 14, 1, 160, N[i].d, noir);
        char b[10];
        snprintf(b, sizeof(b), "%d", N[i].n);
        a_text_col(172, y + 2, 2, b, noir);
    }
    char b[72];
    snprintf(b, sizeof(b), "%d VERIFS, %d ECHEC - RECETTE DU %s",
             total, FDJ_TESTS_FAIL, FDJ_TESTS_DATE);
    txt(FX0, 128, 1, b);
    txt(FX0, 138, 1, "CHAQUE BLOC CONTRE UN TEST QUI ECHOUE AVANT");
    txt(FX0, 148, 1, "TESTS CARTE (FLASH, WIFI 2,4 GHZ) : A PART");
    txt(FX0, 158, 1, "NON EXECUTES ICI = DITS NON EXECUTES");
    txt(FX0, 168, 1, "AUCUNE PAGE N'EST LIVREE SANS SES TESTS");
}

/* ---------------------------------------------------------------- */
/* page 13 — ARCHITECTURE                                           */
/* ---------------------------------------------------------------- */
static void p_archi(const fdj_vue_t *v)
{
    struct { int x, y, w, h; const char *t, *d; } M[4] = {
        { 8,  24, 84, 26, "MODELE",  "ARGENT / POISSON" },
        { 108, 24, 84, 26, "PARSEURS", "JSON / RSS" },
        { 8,  56, 84, 26, "NAV + UI", "20 PAGES, 1 BIT" },
        { 108, 56, 84, 26, "MATERIEL", "EPD / AUDIO / NVS" },
    };
    for (int i = 0; i < 4; i++) {
        bool sel = (v->curseur == i);
        cadre(M[i].x, M[i].y, M[i].x + M[i].w, M[i].y + M[i].h, sel ? 2 : 1);
        if (v->e->gros_texte && largeur(3, M[i].t) <= M[i].w - 8) {
            txt(M[i].x + 6, M[i].y + 3, 3, M[i].t);
        } else {
            txt(M[i].x + 6, M[i].y + 3, 2, M[i].t);
            txt(M[i].x + 6, M[i].y + 16, 1, M[i].d);
        }
    }
    a_seg(84, 37, 106, 37, 1);
    a_tri(112, 37, 106, 33, 106, 41, 1);
    a_seg(150, 50, 150, 54, 1);
    a_tri(150, 60, 146, 54, 154, 54, 1);
    a_seg(106, 69, 86, 69, 1);
    a_tri(80, 69, 86, 65, 86, 73, 1);

    txt(FX0, 90, 1, "MEME CODE SUR LA CARTE ET SUR LE PC :");
    txt(FX0, 99, 1, "L'APERCU PNG EST LE VRAI DESSIN.");
    char b[52];
    snprintf(b, sizeof(b), "ETAT : %u OCTETS", (unsigned)sizeof(fdj_etat_t));
    txt(FX0, 112, 2, b);
    snprintf(b, sizeof(b), "IMAGE E-PAPER : %d OCTETS (200x25)", ART_FB_LEN);
    txt(FX0, 126, 1, b);
    txt(FX0, 136, 1, "16 MATCHS, 32 PARIS, 4 PROFILS : BORNES FIXES");
    txt(FX0, 146, 1, "1 BLOB NVS + CRC32, AUCUNE ALLOCATION PAR PAGE");
    snprintf(b, sizeof(b), "VUE ECRAN : %u OCTETS (sizeof)", (unsigned)sizeof(fdj_vue_t));
    txt(FX0, 156, 1, b);
    txt(FX0, 166, 1, "MODULES PURS : AUCUN INCLUDE ESP-IDF");
}

/* ---------------------------------------------------------------- */
/* page 14 — PERF / SECURITE                                        */
/* ---------------------------------------------------------------- */
static void p_perf(const fdj_vue_t *v)
{
    struct { const char *t, *val; } L[3];
    char b1[24], b2[24], b3[24];
    snprintf(b1, sizeof(b1), "%d MS", v->rendu_ms);
    snprintf(b2, sizeof(b2), "%d MS", v->ecran_ms);
    snprintf(b3, sizeof(b3), "%u KO", (unsigned)(v->heap_int_libre / 1024));
    L[0].t = "DESSIN D'UNE PAGE";      L[0].val = b1;
    L[1].t = "RAFRAICHISSEMENT EPD";   L[1].val = b2;
    L[2].t = "HEAP INTERNE LIBRE";     L[2].val = b3;

    for (int i = 0; i < 3; i++) {
        int y = 24 + i * 18;
        bool sel = (v->curseur == i);
        curseur_barre(y, 17, sel);
        int noir = sel ? 0 : 1;
        txg(v, 8, y + 3, 1, 118, L[i].t, noir);
        a_text_col(160, y + 3, 1, L[i].val, noir);
    }

    int y = 82;
    plein(FX0, y, FX1, y, 1);
    txt(FX0, y + 5, 1, "SECURITE");
    txt(FX0, y + 14, 1, "TLS VERIFIE (BUNDLE IDF), PAS DE MODE INSECURE");
    txt(FX0, y + 23, 1, "AUCUN SECRET DANS /etat, LOGS NI ECRAN");
    txt(FX0, y + 32, 1, "API /bouton /page : RESEAU LOCAL SEULEMENT");

    plein(FX0, 124, FX1, 124, 1);
    txt(FX0, 130, 1, "TUNNEL VPN : NON IMPLEMENTE");
    txt(FX0, 140, 1, "L'OPTION A ETE RETIREE DE L'INTERFACE :");
    txt(FX0, 150, 1, "UN MENU 'VPN ON' SANS TUNNEL SERAIT UN MENSONGE.");
    txt(FX0, 164, 1, "MESURES PRISES A L'INSTANT, PAS SIMULEES");
}

/* ---------------------------------------------------------------- */
/* page 15 — MEMOIRE (plus de faux "swap")                          */
/* ---------------------------------------------------------------- */
static void jauge(int y, const char *nom, uint32_t libre, uint32_t total,
                  const char *unite, int s)
{
    txt(8, y, s, nom);
    char b[24];
    if (total == 0) {
        snprintf(b, sizeof(b), "N.D.");
    } else {
        snprintf(b, sizeof(b), "%u / %u %s", (unsigned)(libre / 1024),
                 (unsigned)(total / 1024), unite);
    }
    txt_droite(186, y, 1, b);
    int pct = total ? (int)((uint64_t)libre * 100 / total) : 0;
    if (pct > 100) pct = 100;
    plein(8, y + 8, 186, y + 8, 0);
    cadre(8, y + 8, 186, y + 12, 1);
    if (total) plein(9, y + 9, 9 + (int)((176 * pct) / 100), y + 11, 1);
}

static void p_ram(const fdj_vue_t *v)
{
    bool g = v->e->gros_texte;
    int  s = g ? 2 : 1;
    jauge(24, g ? "RAM" : "RAM INTERNE LIBRE", v->heap_int_libre, v->heap_int_total, "KO", s);
    jauge(46, g ? "BLOC" : "PLUS GROS BLOC",   v->heap_int_max,   v->heap_int_total, "KO", s);
    jauge(68, "PSRAM",                         v->psram_libre,    v->psram_total, "KO", s);

    plein(FX0, 90, FX1, 90, 1);
    txt(FX0, 96, 1, "PAGING / SWAP : AUCUN, NON IMPLEMENTE");
    txt(FX0, 106, 1, "RIEN N'EST ECRIT SUR LA CARTE SD POUR LA RAM");
    if (v->sd_ok) txt(FX0, 116, 1, "CARTE SD PRESENTE : STOCKAGE DU CSV SEULEMENT");
    else          txt(FX0, 116, 1, "AUCUNE CARTE SD : EXPORT CSV INDISPONIBLE");
    txt(FX0, 130, 1, "CAPACITES LUES PAR heap_caps_get_total_size");
    txt(FX0, 140, 1, "TAMPONS RESEAU EN PSRAM, PAS DANS LE HEAP INTERNE");
    txt(FX0, 150, 1, "IMAGE E-PAPER : 5000 OCTETS SUR 200x25");
    txt(FX0, 166, 1, "B2X2 : REFAIRE LES MESURES (CABLE)");
}

/* ---------------------------------------------------------------- */
/* page 16 — COMPTE & REGLAGES                                      */
/* ---------------------------------------------------------------- */
static void p_compte(const fdj_vue_t *v)
{
    char ligne[5][56];
    const fdj_etat_t *e = v->e;
    snprintf(ligne[0], sizeof(ligne[0]), "%s", e->compte_nom);
    snprintf(ligne[1], sizeof(ligne[1]), "%s", e->compte_id);
    if (v->ssid && v->ssid[0])
        snprintf(ligne[2], sizeof(ligne[2]), "%s  %d DBM  %s", v->ssid, v->rssi,
                 v->wifi ? "CONNECTE" : "NON CONNECTE");
    else
        snprintf(ligne[2], sizeof(ligne[2]), "HORS LIGNE");
    snprintf(ligne[3], sizeof(ligne[3]), "SON DES TOUCHES : %s", e->son_clic ? "ON" : "OFF");
    /* le code n'est JAMAIS affiche en clair */
    snprintf(ligne[4], sizeof(ligne[4]), "%s",
             e->code_perso ? "CODE PERSONNALISE" : "CODE D'USINE A MODIFIER");

    static const char *TITRE[5] = {
        "TITULAIRE", "COMPTE FDJ", "RESEAU", "SON", "CODE 4 CHIFFRES"
    };
    for (int i = 0; i < 5; i++) {
        int y = 24 + i * 26;
        bool sel = (v->curseur == i);
        curseur_barre(y, 25, sel);
        int noir = sel ? 0 : 1;
        a_text_col(8, y + 2, 1, TITRE[i], noir);
        char b[52];
        elide(b, sizeof(b), 1, 160, ligne[i]);
        txg(v, 8, y + 12, 1, 160, b, noir);
        if (i == 4) a_text_col(186, y + 12, 1, ">", noir);
        if (i == 3) coche(184, y + 10, e->son_clic);
    }
    txt(FX0, 160, 1, "MOT DE PASSE WIFI : JAMAIS AFFICHE, JAMAIS STOCKE ICI");
    txt(FX0, 169, 1, "SECRETS DANS main/secrets.h (NON VERSIONNE)");
}

/* ---------------------------------------------------------------- */
/* page 17 — PROFILS                                                */
/* ---------------------------------------------------------------- */
static void p_profils(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    int n = e->nb_profils;
    for (int i = 0; i < n; i++) {
        const fdj_profil_t *p = &e->profils[i];
        int y = 24 + i * 26;
        bool sel = (v->curseur == i);
        curseur_barre(y, 25, sel);
        int noir = sel ? 0 : 1;

        if (i == e->profil_actif) a_text_col(6, y + 2, 2, ">", noir);
        char b[32];
        int ybas = y + 14;
        if (v->e->gros_texte) {
            elide(b, sizeof(b), 3, 110, p->nom);
            a_text_col(18, y + 1, 3, b, noir);
            ybas = y + 19;
        } else {
            elide(b, sizeof(b), 2, 96, p->nom);
            a_text_col(18, y + 2, 2, b, noir);
        }
        a_text_col(18, ybas, 1, fdj_niveau_nom(p->niveau), noir);
        snprintf(b, sizeof(b), "%u/%u E PAR 7/30 J",
                 (unsigned)(p->plafond_sem_ct / 100), (unsigned)(p->plafond_mois_ct / 100));
        a_text_col(70, ybas, 1, b, noir);

        char s[16];
        if (i == e->profil_actif) snprintf(s, sizeof(s), "%u,%02u E", (unsigned)(p->solde_ct / 100),
                                           (unsigned)(p->solde_ct % 100));
        else                      snprintf(s, sizeof(s), "----");
        a_text_col(160, y + 2, 1, s, noir);
    }

    int y = 24 + n * 26;
    bool sel = (v->curseur == n);
    curseur_barre(y, 22, sel);
    int noir = sel ? 0 : 1;
    if (n < FDJ_PROFILS_MAX) {
        a_text_col(8, y + 3, 1, "NOUVEAU PROFIL (B2X2)", noir);
        a_text_col(8, y + 11, 1, "DEBUTANT / REGULIER / EXPERT", noir);
    } else {
        a_text_col(8, y + 3, 1, "4 PROFILS MAXIMUM", noir);
        a_text_col(8, y + 11, 1, "B1X2 : SUPPRIMER (SAUF ACTIF)", noir);
    }
    uint32_t sem = 0, mois = 0;
    fdj_depenses(e, v->now, &sem, &mois);
    const fdj_profil_t *pa = fdj_profil_actif_c(e);
    char d[80];
    if (pa) {
        snprintf(d, sizeof(d), "ACTIF %s : DEPENSE 7J %u,%02u / %u,%02u E",
                 pa->nom, (unsigned)(sem / 100), (unsigned)(sem % 100),
                 (unsigned)(fdj_plafond_sem_effectif(e) / 100),
                 (unsigned)(fdj_plafond_sem_effectif(e) % 100));
    } else {
        snprintf(d, sizeof(d), "AUCUN PROFIL ACTIF");
    }
    elide(d, sizeof(d), 1, 186, d);
    txt(FX0, 158, 1, d);
    txt(FX0, 168, 1, "B1 : ACTIVER   B1X2 : SUPPRIMER   B2X2 : CREER");
}

/* ---------------------------------------------------------------- */
/* page 18 — ACCESSIBILITE                                          */
/* ---------------------------------------------------------------- */
static void p_acces(const fdj_vue_t *v)
{
    struct { const char *t, *d; bool on; } O[4] = {
        { "GROS TEXTE",     "LIGNES PRINCIPALES AGRANDIES", v->e->gros_texte },
        { "CONTRASTE FORT", "NOIR SUR BLANC, TOUJOURS ACTIF", true },
        { "SON DES TOUCHES", "UN CLIC COURT, DESACTIVABLE", v->e->son_clic },
        { "LANGUE",         "FRANCAIS, SEULE LANGUE EMBARQUEE", true },
    };
    for (int i = 0; i < 4; i++) {
        int y = 24 + i * 27;
        bool sel = (v->curseur == i);
        curseur_barre(y, 26, sel);
        int noir = sel ? 0 : 1;
        if (v->e->gros_texte && largeur(3, O[i].t) <= 188) {
            a_text_col(8, y + 1, 3, O[i].t, noir);
            a_text_col(8, y + 19, 1, O[i].d, noir);
            coche(188, y + 18, O[i].on);      /* la coche descend avec la ligne */
        } else {
            a_text_col(8, y + 2, 2, O[i].t, noir);
            a_text_col(8, y + 16, 1, O[i].d, noir);
            coche(184, y + 8, O[i].on);
        }
    }
    txt(FX0, 136, 1, "AUCUNE INFORMATION PAR LA COULEUR SEULE");
    txt(FX0, 146, 1, "COCHE = CARRE + TIRET, LISIBLE DE LOIN");
    txt(FX0, 156, 1, "GROS TEXTE : TOUTES LES PAGES SAUF LES TABLEAUX");
    if (v->e->gros_texte) txt(FX0, 168, 2, "EXEMPLE GROS");
    else                  txt(FX0, 168, 1, "EXEMPLE NORMAL");
}

/* ---------------------------------------------------------------- */
/* page 19 — DONNEES ET RGPD                                        */
/* ---------------------------------------------------------------- */
static void p_rgpd(const fdj_vue_t *v)
{
    struct { const char *t, *d; } L[3] = {
        { "EFFACER MES DONNEES",    "PROFILS, PARIS, CODE : TOUT" },
        { "EXPORTER MON HISTORIQUE", "CSV SUR LA CARTE SD" },
        { "A PROPOS",               "FDT 4.0.0 - BATMino x FDJ" },
    };
    for (int i = 0; i < 3; i++) {
        int y = 24 + i * 27;
        bool sel = (v->curseur == i);
        curseur_barre(y, 26, sel);
        int noir = sel ? 0 : 1;
        if (v->e->gros_texte && largeur(3, L[i].t) <= 182) {
            a_text_col(8, y + 1, 3, L[i].t, noir);
            a_text_col(8, y + 19, 1, L[i].d, noir);
        } else {
            a_text_col(8, y + 2, 2, L[i].t, noir);
            a_text_col(8, y + 16, 1, L[i].d, noir);
        }
    }
    plein(FX0, 106, FX1, 106, 1);
    txt(FX0, 112, 1, "OU VIVENT LES DONNEES ?");
    txt(FX0, 122, 1, "NVS INTERNE : COMPTE, CODE, PROFILS, PARIS");
    txt(FX0, 132, 1, "SORTIES : SCORES THESPORTSDB + TITRES FRANCEINFO");
    txt(FX0, 142, 1, "AUCUN COMPTE CLIENT, AUCUN PAIEMENT, AUCUNE MISE");
    txt(FX0, 152, 1, "EFFACEMENT = TOUT EFFACER + REDEMARRAGE");
    txt(FX0, 162, 1, "CONSERVATION : JUSQU'A EFFACEMENT MANUEL");
    txt(FX0, 172, 1, "@PARISTZOU - JUSTE POUR LES FILLES");
}

/* ---------------------------------------------------------------- */
/* page 20 — REGLES FDJ                                             */
/* ---------------------------------------------------------------- */
static void p_regles(const fdj_vue_t *v)
{
    struct { const char *t, *d; } B[3] = {
        { "3 FAMILLES DE JEUX", "TROIS FAMILLES : PARIS, LOTERIE, CASINO" },
        { "CONDITIONS", "INTERDIT AUX -18 ANS, MODERATION" },
        { "MAQUETTE NON OFFICIELLE", "AUCUNE MISE REELLE, AUCUN COMPTE FDJ" },
    };
    for (int i = 0; i < 3; i++) {
        int y = 24 + i * 26;
        bool sel = (v->curseur == i);
        curseur_barre(y, 25, sel);
        int noir = sel ? 0 : 1;
        char d[60];
        elide(d, sizeof(d), 1, 180, B[i].d);
        if (v->e->gros_texte && largeur(3, B[i].t) <= 180) {
            a_text_col(8, y + 1, 3, B[i].t, noir);
            a_text_col(8, y + 19, 1, d, noir);
        } else {
            a_text_col(8, y + 2, 1, B[i].t, noir);
            a_text_col(8, y + 12, 1, d, noir);
        }
    }

    txt(8, 108, 1, "JEUX DECLARES DANS LE MODELE :");
    for (int i = 0; i < FDJ_JEU_NB; i++) {
        int col = i % 2, row = i / 2;
        int x = 8 + col * 96, y = 120 + row * 11;
        plein(x, y + 1, x + 3, y + 3, 1);
        char b[24];
        elide(b, sizeof(b), 1, 88, fdj_jeu_nom((uint8_t)i));
        txt(x + 7, y, 1, b);
    }
    txt(FX0, 166, 1, "TRJ ET REGLES OFFICIELS : FDJ.FR");
    (void)v;
}

/* ---------------------------------------------------------------- */
/* table des ecrans : accueil + 18 x (intertitre, page)              */
/* ---------------------------------------------------------------- */
static const char *TITRE_MODULE[FDJ_M_NB] = {
    "ACCUEIL FDJ",        "PARIS SPORTIFS",  "LOTO",            "HISTORIQUE",
    "GAINS / PERTES",     "NEWS LIVE",       "MON COUPON",      "JEU RESPONSABLE",
    "STATS / PROBAS",     "USER STORIES",    "TESTS",           "ARCHITECTURE",
    "PERF / SECURITE",    "MEMOIRE RAM",     "COMPTE & REGLAGES","PROFILS",
    "ACCESSIBILITE",      "DONNEES & RGPD",  "REGLES FDJ",
};

int fdj_ecran_module(int page)
{
    if (page <= 0) return 0;
    if (page >= FDJ_PAGES) return 0;
    int m = 1 + (page - 1) / 2;
    return m < FDJ_M_NB ? m : 0;
}

bool fdj_ecran_est_intertitre(int page)
{
    return page > 0 && page < FDJ_PAGES && ((page % 2) == 1);
}

int fdj_module_page(int module)
{
    if (module <= 0) return 0;
    return 1 + (module - 1) * 2 + 1;
}

const char *fdj_module_nom(int module)
{
    if (module < 0 || module >= FDJ_M_NB) return "?";
    return TITRE_MODULE[module];
}

const char *fdj_page_titre(int page)
{
    if (page < 0 || page >= FDJ_PAGES) return "?";
    return TITRE_MODULE[fdj_ecran_module(page)];
}

const char *fdj_page_aide(int page)
{
    if (page < 0 || page >= FDJ_PAGES) return "";
    if (fdj_ecran_est_intertitre(page)) return "B2:CONTINUER";
    switch (fdj_ecran_module(page)) {
    case FDJ_M_ACCUEIL:  return "B1:B2 DEMARRER";
    case FDJ_M_PARIS:    return "B1:COCHER B2X2:SCORES";
    case FDJ_M_LOTO:     return "B1:GRILLE B2X2:NOUVELLE";
    case FDJ_M_HIST:     return "B1:DETAIL B2X2:SCROLL";
    case FDJ_M_GAINS:    return "B1:PAR JEU";
    case FDJ_M_NEWS:     return "B1:LU B2X2:FORCER";
    case FDJ_M_COUPON:   return "B1:VALIDER B1X2:VIDER";
    case FDJ_M_RESP:     return "B1:OUI B2X2:PAUSE";
    case FDJ_M_STATS:    return "INFOS";
    case FDJ_M_STORIES:  return "INFOS";
    case FDJ_M_TESTS:    return "INFOS";
    case FDJ_M_ARCHI:    return "INFOS";
    case FDJ_M_PERF:     return "B2X2:REFAIRE MESURES";
    case FDJ_M_RAM:      return "B2X2:REFAIRE MESURES";
    case FDJ_M_COMPTE:   return "B1:REGLER";
    case FDJ_M_PROFILS:  return "B1:ACTIVER B1X2:SUPPR";
    case FDJ_M_ACCES:    return "B1:REGLER";
    case FDJ_M_RGPD:     return "B1:ACTION";
    case FDJ_M_REGLES:   return "B1:B2 SUR FDJ.FR";
    default:             return "";
    }
}

int fdj_page_items(int page, const fdj_vue_t *v)
{
    if (page < 0 || page >= FDJ_PAGES) return 1;
    if (fdj_ecran_est_intertitre(page)) return 1;
    switch (fdj_ecran_module(page)) {
    case FDJ_M_ACCUEIL:  return 1;
    case FDJ_M_PARIS:    return v->top && v->top->n > 0 ? v->top->n : 1;
    case FDJ_M_LOTO:     return FDJ_GRILLE_MAX + 1;
    case FDJ_M_HIST: {
        if (v->e->hist_n == 0) return 1;
        bool plus = false;
        int n = hist_affiche(v->e, &plus);
        if (n < 1) return 1;
        return plus ? n + 1 : n;
    }
    case FDJ_M_GAINS:    return 3;
    case FDJ_M_NEWS:     return v->news && v->news->nb > 0 ? (v->news->nb < 4 ? v->news->nb : 4) : 1;
    case FDJ_M_COUPON:   return coupon_nb(v) + 2;
    case FDJ_M_RESP:     return QN + 1;
    case FDJ_M_STATS:    return 4;
    case FDJ_M_STORIES:  return USN;
    case FDJ_M_TESTS:    return 4;
    case FDJ_M_ARCHI:    return 4;
    case FDJ_M_PERF:     return 3;
    case FDJ_M_RAM:      return 3;
    case FDJ_M_COMPTE:   return 5;
    case FDJ_M_PROFILS:  return v->e->nb_profils + 1;
    case FDJ_M_ACCES:    return 4;
    case FDJ_M_RGPD:     return 3;
    case FDJ_M_REGLES:   return 3;
    default:             return 1;
    }
}

void fdj_ui_dessine(const fdj_vue_t *v, uint8_t *fb)
{
    art_begin(fb, 0);
    art_clear();
    if (!v || !v->e) return;

    entete(v, fdj_page_titre(v->page));
    if (fdj_ecran_est_intertitre(v->page)) {
        p_intertitre(v);
    } else {
        switch (fdj_ecran_module(v->page)) {
        case FDJ_M_ACCUEIL:  p_accueil(v); break;
        case FDJ_M_PARIS:    p_paris(v); break;
        case FDJ_M_LOTO:     p_loto(v); break;
        case FDJ_M_HIST:     p_historique(v); break;
        case FDJ_M_GAINS:    p_gains(v); break;
        case FDJ_M_NEWS:     p_news(v); break;
        case FDJ_M_COUPON:   p_coupon(v); break;
        case FDJ_M_RESP:     p_responsable(v); break;
        case FDJ_M_STATS:    p_stats(v); break;
        case FDJ_M_STORIES:  p_stories(v); break;
        case FDJ_M_TESTS:    p_tests(v); break;
        case FDJ_M_ARCHI:    p_archi(v); break;
        case FDJ_M_PERF:     p_perf(v); break;
        case FDJ_M_RAM:      p_ram(v); break;
        case FDJ_M_COMPTE:   p_compte(v); break;
        case FDJ_M_PROFILS:  p_profils(v); break;
        case FDJ_M_ACCES:    p_acces(v); break;
        case FDJ_M_RGPD:     p_rgpd(v); break;
        case FDJ_M_REGLES:   p_regles(v); break;
        default:             txt_centre(100, 90, 2, "ECRAN INCONNU"); break;
        }
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
    txt_centre(100, 50, 1, "MAQUETTE DE DEMONSTRATION");

    for (int i = 0; i < 4; i++) {
        int x = 22 + i * 40;
        cadre(x, 66, x + 32, 108, 2);
        if (i == position) plein(x - 3, 63, x + 35, 111, 1);
        char b[2] = { (char)('0' + chiffres[i]), 0 };
        a_text_col(x + 10, 78, 4, b, i == position ? 0 : 1);
    }
    txt_centre(100, 120, 1, "B2 : CHIFFRE SUIVANT   B1 : CHIFFRE +1");
    txt_centre(100, 130, 1, "B2X2 : VALIDER LE CODE");
    if (erreur) txt_centre(100, 146, 2, "CODE REFUSE");
    else        txt_centre(100, 146, 1, "CODE D'USINE : 0000 (A MODIFIER)");
    txt_centre(100, 160, 1, "4 CHIFFRES, RIEN D'AUTRE");
    txt_centre(100, 172, 1, "@PARISTZOU");
}

void fdj_ui_confirme(uint8_t *fb, const char *titre, const char *question,
                     bool oui_selectionne)
{
    art_begin(fb, 0);
    art_clear();
    txt_centre(100, 34, 2, titre);
    txt_centre(100, 58, 1, question);
    txt_centre(100, 70, 1, "CETTE ACTION EST DEFINITIVE");
    cadre(24, 88, 96, 122, oui_selectionne ? 3 : 1);
    txt_centre(60, 100, 2, "OUI");
    cadre(104, 88, 176, 122, oui_selectionne ? 1 : 3);
    txt_centre(140, 100, 2, "NON");
    txt_centre(100, 140, 1, "B2X2 : CONFIRMER   B1 : INVERSER");
    txt_centre(100, 152, 1, "B2 TENU 3 S : ANNULER");
    txt_centre(100, 170, 1, "@PARISTZOU - JUSTE POUR LES FILLES");
}
