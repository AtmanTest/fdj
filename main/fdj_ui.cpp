/*  fdj_ui.cpp — les 37 écrans, en police 5×10 (lisible sur la dalle).
 *
 *  Pourquoi 5x10 et plus 3x5 : sur la vraie dalle 200x200, le 3x5 à l'échelle
 *  1 (5 px de haut) est illisible à 50 cm, et le 3x5 doublé est épais et
 *  grossier. La police 5x10 du projet (FONT5) donne 33 caractères par ligne
 *  pour 10 px de haut : moins de contenu, mais lisible.
 *
 *  Gabarit :
 *      0..13    bandeau noir : titre + NN/37 + état réseau
 *      18..176  contenu, lignes de 12 px
 *      182      aide de la page
 */
#include "fdj_ui.h"
#include "art.h"
#include "fdj_tests_stats.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

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

static void t5(int x, int y, const char *s) { if (s) a_text5c(x, y, 1, s, 1); }
static void t5w(int x, int y, const char *s) { if (s) a_text5c(x, y, 1, s, 0); }
static void t5b(int x, int y, const char *s) { if (s) a_text5c(x, y, 2, s, 1); }
static void t5bw(int x, int y, const char *s) { if (s) a_text5c(x, y, 2, s, 0); }
static int  largeur(const char *s) { return s ? a_text5_w(1, s) : 0; }
static int  largeur2(const char *s) { return s ? a_text5_w(2, s) : 0; }

static void t5d(int xd, int y, const char *s)
{
    if (!s) return;
    t5(xd - largeur(s), y, s);
}

static void t5centre(int cx, int y, const char *s)
{
    if (!s) return;
    t5(cx - largeur(s) / 2, y, s);
}

/* coupe dans la largeur, sans jamais trancher au milieu d'un mot */
static void elide(char *dst, size_t cap, int maxw, const char *src)
{
    /* copie d'abord : l'appelant passe souvent le meme tampon en entree */
    char tmp[160];
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

/* ---------------------------------------------------------------- */
/* bandeau + pied                                                   */
/* ---------------------------------------------------------------- */
static void entete(const fdj_vue_t *v, const char *titre)
{
    plein(0, 0, 199, 13, 1);
    t5w(5, 2, titre);
    char b[16];
    snprintf(b, sizeof(b), "%02d/%d", v->page + 1, FDJ_PAGES);
    t5w(194 - largeur(b), 2, b);
    if (v->wifi) plein(185, 4, 193, 10, 0);
    else         cadre(185, 4, 193, 10, 1);
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
    char a[32];
    elide(a, sizeof(a), 128, fdj_page_aide(v ? v->page : 0));
    t5(FX0, FOOT, a);
    t5d(198, FOOT, "3S=ACCUEIL");
}

/* sélection : barre pleine, texte blanc */
static void sel_bar(int y, int h, bool sel)
{
    if (sel) plein(0, y, 199, y + h - 1, 1);
}

/* ---------------------------------------------------------------- */
/* accueil                                                          */
/* ---------------------------------------------------------------- */
static void p_accueil(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    const fdj_profil_t *pr = fdj_profil_actif_c(e);

    t5b(6, 18, "BATMINO");
    t5(6, 42, "MAQUETTE DE DEMO");
    t5(6, 54, "AUCUNE MISE REELLE");

    cadre(4, 68, 196, 108, 1);
    t5(9, 71, "SOLDE DE DEMONSTRATION");
    char b[32];
    t5b(9, 84, euros(b, sizeof(b), pr ? pr->solde_ct : 0, false));

    if (pr) {
        char d[48];
        snprintf(d, sizeof(d), "PROFIL %s   %u/%u E", pr->nom,
                 (unsigned)(fdj_plafond_sem_effectif(e) / 100),
                 (unsigned)(pr->plafond_mois_ct / 100));
        elide(d, sizeof(d), 186, d);
        t5(6, 114, d);
    } else {
        t5(6, 114, "AUCUN PROFIL ACTIF");
    }

    if (v->live && v->live->ok && v->live->n > 0) {
        snprintf(b, sizeof(b), "%d MATCHS EN DIRECT", v->live->n);
        t5(6, 126, b);
        if (v->live->m[0].dom[0]) {
            char l[44];
            snprintf(l, sizeof(l), "%s %d-%d %s", v->live->m[0].dom,
                     v->live->m[0].buts_dom, v->live->m[0].buts_ext, v->live->m[0].ext);
            elide(b, sizeof(b), 150, l);
            t5(6, 136, b);
        }
    } else {
        t5(6, 126, "HORS LIGNE");
        t5(6, 136, "SCORES : N.D.");
    }

    sel_bar(148, 15, v->curseur == 0);
    if (v->curseur == 0) t5w(6, 150, "DEMARRER LA VISITE");
    else                 t5(6, 150, "DEMARRER LA VISITE");

    char bas[40];
    snprintf(bas, sizeof(bas), "FDT %s - %u DEMARRAGES", FDJ_VERSION, (unsigned)e->boots);
    t5(6, 166, bas);
}

/* ---------------------------------------------------------------- */
/* intertitres                                                      */
/* ---------------------------------------------------------------- */
typedef struct { const char *haut, *bas, *phrase, *note; } inter_t;
static const inter_t INTER[FDJ_M_NB] = {
    { "", "", "", "" },
    { "ANALYSE", "DECISION", "DES SCORES AUX PROBABILITES DU MODELE", "PROBABILITE N'EST PAS UNE COTE" },
    { "LE HASARD", "SANS MENTIR", "TROIS GRILLES, UNE CHANCE SUR 19 MILLIONS", "EQUILIBREE = MEME CHANCE" },
    { "LA TRACE", "DE CHAQUE PARI", "CODE, JEU, MISE, GAIN : TOUT EST GARDE", "LE DETAIL S'OUVRE VRAIMENT" },
    { "L'ARGENT", "SANS ARRONDIR", "MISES, GAINS CONFIRMES, NET PAR JOUR", "EN COURS N'EST PAS GAGNE" },
    { "LE FIL", "EN CONTINU", "DES TITRES REELS ET HORODATES", "PAS DE RESEAU = N.D." },
    { "LE COUPON", "", "CHOISIR, VERIFIER, VALIDER", "REFUS MOTIVE, RIEN DE DEBITE" },
    { "GARDER", "LE CONTROLE", "DES LIMITES QUI S'APPLIQUENT VRAIMENT", "PAUSE 24 H DATEE" },
    { "LES CHIFFRES", "EXACTS", "PROBABILITES, COTE IMPLICITE, HISTORIQUE", "AUCUN TRJ INVENTE" },
    { "LE CAHIER", "DES CHARGES", "SIX HISTOIRES ET LEURS CRITERES", "LA PAGE 12 MONTRE LES PREUVES" },
    { "LES PREUVES", "PAS LES PROMESSES", "VERIFICATIONS REELLES ET DATEES", "AUCUNE PAGE SANS TESTS" },
    { "SOUS", "LE CAPOT", "LE MEME CODE SUR LA CARTE ET SUR LE PC", "TAILLES MESUREES" },
    { "VITESSE", "ET SECURITE", "MESURES LIVE, TLS VERIFIE", "AUCUN SECRET AFFICHE" },
    { "LA MEMOIRE", "COMPTEE", "HEAP, PSRAM, CAPACITES REELLES", "PAS DE FAUX SWAP" },
    { "MON COMPTE", "ET MES REGLAGES", "RESEAU, SON, CODE PROTEGE", "LE CODE N'EST JAMAIS AFFICHE" },
    { "QUATRE", "PROFILS", "PLAFONDS ET SOLDES SEPARES", "CHAQUE PROFIL A SON BUDGET" },
    { "POUR", "TOUS", "GROS TEXTE, CONTRASTE, CLIC", "AUCUNE INFO PAR LA COULEUR" },
    { "MES", "DONNEES", "CE QUI EST STOCKE, CE QUI SORT", "EFFACEMENT AVEC CONFIRMATION" },
    { "LES REGLES", "DU JEU", "FAMILLES, CONDITIONS, LIMITES", "MAQUETTE NON OFFICIELLE" },
};

/* phrase en 5x10, coupee a l'espace sur maxlignes */
static void phrase(int x, int y, int maxw, const char *t, int maxlignes, int interligne)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", t ? t : "");
    char *p = buf;
    int maxc = maxw / 6;
    for (int l = 0; l < maxlignes && *p; l++) {
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
        t5(x, y + l * interligne, tmp);
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
    t5(6, 20, num);
    t5d(194, 20, "MAQUETTE DE DEMO");

    int sc = 2;
    while (sc > 1 && (largeur2(it->haut) > 190 ||
                      (it->bas[0] && largeur2(it->bas) > 190))) sc--;
    int h = 12 * sc;
    int y = 36;
    a_text5c(6, y, sc, it->haut, 1); y += h;
    if (it->bas[0]) { a_text5c(6, y, sc, it->bas, 1); y += h; }

    plein(6, y + 4, 194, y + 4, 1);
    phrase(6, y + 10, 188, it->phrase, 2, 12);

    cadre(6, 132, 194, 150, 1);
    char note[44];
    elide(note, sizeof(note), 180, it->note);
    t5(10, 136, note);

    sel_bar(158, 15, true);
    t5w(8, 160, "B2 : CONTINUER");
}

/* ---------------------------------------------------------------- */
/* paris sportifs                                                   */
/* ---------------------------------------------------------------- */
static void p_paris(const fdj_vue_t *v)
{
    const fdj_top_t *t = v->top;
    if (!t || t->n == 0) {
        t5centre(100, 60, "AUCUN MATCH EN DIRECT");
        t5centre(100, 76, "LE MODELE NE DEVINE PAS");
        t5centre(100, 88, v->live && v->live->ok ? "FLUX VIDE : N.D." : "HORS LIGNE : N.D.");
        t5centre(100, 108, "B2X2 : ACTUALISER");
        return;
    }
    char h[44];
    if (v->live && v->live->ok && v->now > 0 && v->live->maj > 0) {
        int64_t age = (v->now - v->live->maj) / 60;
        snprintf(h, sizeof(h), "DONNEES MAJ IL Y A %d MIN", (int)(age < 0 ? 0 : age));
    } else {
        snprintf(h, sizeof(h), "HORS LIGNE : N.D.");
    }
    t5(6, 18, h);

    int n = t->n < 6 ? t->n : 6;
    for (int i = 0; i < n; i++) {
        const fdj_sel_t *s = &t->s[i];
        int y = 34 + i * 24;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 22, sel);
        int col = sel ? 0 : 1;

        char m[28];
        elide(m, sizeof(m), 130, s->match);
        a_text5c(6, y, 1, m, col);
        char p[8];
        snprintf(p, sizeof(p), "%d%%", (int)(s->proba * 100.0f + 0.5f));
        a_text5c(178 - largeur(p), y, 1, p, col);

        char l2[44];
        const char *info = "";
        if (v->live && s->idx_match < v->live->n) info = v->live->m[s->idx_match].info;
        snprintf(l2, sizeof(l2), "%s %s  %s  %s", s->code, s->ligue, info, s->marche);
        elide(l2, sizeof(l2), 184, l2);
        a_text5c(6, y + 11, 1, l2, col);

        cadre(184, y + 1, 193, y + 9, 1);
        if (v->e->sel_cochee & (1u << i)) {
            a_seg(186, y + 5, 188, y + 8, col ? 1 : 0);
            a_seg(188, y + 8, 192, y + 2, col ? 1 : 0);
        }
    }
}

/* ---------------------------------------------------------------- */
/* loto                                                             */
/* ---------------------------------------------------------------- */
static void p_loto(const fdj_vue_t *v)
{
    for (int g = 0; g < v->nb_grilles && g < FDJ_GRILLE_MAX; g++) {
        int y = 20 + g * 14;
        bool sel = (v->curseur == g);
        sel_bar(y - 1, 13, sel);
        char b[48];
        snprintf(b, sizeof(b), "GRILLE %d   %02u %02u %02u %02u %02u  C%u", g + 1,
                 v->grilles[g][0], v->grilles[g][1], v->grilles[g][2],
                 v->grilles[g][3], v->grilles[g][4], v->chances[g]);
        a_text5c(6, y, 1, b, sel ? 0 : 1);
        if (v->e->grille_cochee & (1u << g)) a_text5c(188, y, 1, "X", sel ? 0 : 1);
    }

    plein(FX0, 66, FX1, 66, 1);
    t5(6, 72, "CE QUI EST VRAI");
    t5(6, 86, "1 GRILLE = 1 CHANCE / 19 068 840");
    t5(6, 98, "3 GRILLES = 3 CHANCES, PAS PLUS");
    t5(6, 110, "EQUILIBREE : MEME CHANCE");
    t5(6, 122, "CHANCE : 1 SUR 10");

    int ny = 150;
    bool sel = (v->curseur == FDJ_GRILLE_MAX);
    sel_bar(ny, 15, sel);
    a_text5c(8, ny + 2, 1, "NOUVELLE GRILLE (B2X2)", sel ? 0 : 1);
}

/* ---------------------------------------------------------------- */
/* historique                                                       */
/* ---------------------------------------------------------------- */
static int hist_visibles(const fdj_etat_t *e)
{
    int reste = (int)e->hist_n - (int)e->hist_off;
    if (reste <= 0) return 0;
    return reste < 4 ? reste : 4;
}

static int hist_affiche(const fdj_etat_t *e, bool *plus)
{
    int n = hist_visibles(e);
    int reste = (int)e->hist_n - (int)e->hist_off - n;
    if (plus) *plus = reste > 0;
    return n;
}

static void p_historique(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    if (e->hist_n == 0) {
        t5centre(100, 58, "AUCUN PARI");
        t5centre(100, 74, "VALIDEZ LE COUPON");
        t5centre(100, 90, "TOUT EST VIDE, ET C'EST HONNETE");
        return;
    }
    bool plus = false;
    int n = hist_affiche(e, &plus);
    for (int i = 0; i < n; i++) {
        int idx = fdj_hist_index_recence(e, (int)e->hist_off + i);
        if (idx < 0) continue;
        const fdj_pari_t *p = &e->hist[idx];
        int y = 20 + i * 35;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 35, sel);   /* les trois lignes du pari */
        int col = sel ? 0 : 1;

        char l[FDJ_TXT_L + 2];
        elide(l, sizeof(l), 130, p->libelle);
        a_text5c(6, y, 1, l, col);
        const char *st = p->statut == FDJ_PARI_GAGNE ? "GAGNE" :
                         p->statut == FDJ_PARI_PERDU ? "PERDU" : "EN COURS";
        a_text5c(190 - largeur(st), y, 1, st, col);

        char d[64];
        time_t t = (time_t)p->t;
        struct tm ti;
        localtime_r(&t, &ti);
        if (p->statut == FDJ_PARI_GAGNE)
            snprintf(d, sizeof(d), "%02d/%02d  M %u,%02u  G %u,%02u", ti.tm_mday, ti.tm_mon + 1,
                     (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100),
                     (unsigned)(p->gain_ct / 100), (unsigned)(p->gain_ct % 100));
        else if (p->statut == FDJ_PARI_EN_COURS)
            snprintf(d, sizeof(d), "%02d/%02d  M %u,%02u  POT %u,%02u", ti.tm_mday, ti.tm_mon + 1,
                     (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100),
                     (unsigned)(p->gain_ct / 100), (unsigned)(p->gain_ct % 100));
        else
            snprintf(d, sizeof(d), "%02d/%02d  M %u,%02u  PERDU", ti.tm_mday, ti.tm_mon + 1,
                     (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100));
        a_text5c(6, y + 12, 1, d, col);

        char j[32];
        snprintf(j, sizeof(j), "CODE %s  %s", p->code, fdj_jeu_nom(p->jeu));
        a_text5c(6, y + 23, 1, j, col);
    }
    if (plus) {
        int y = 20 + n * 35;
        bool sel = (v->curseur == n);
        sel_bar(y, 13, sel);
        a_text5c(6, y, 1, "PLUS ANCIENS (B1)", sel ? 0 : 1);
    }
}

void fdj_ui_detail(uint8_t *fb, const fdj_pari_t *p, int idx)
{
    art_begin(fb, 0);
    art_clear();
    plein(0, 0, 199, 13, 1);
    t5w(5, 2, "DETAIL DU PARI");
    char b[20];
    snprintf(b, sizeof(b), "N%d", idx + 1);
    t5w(194 - largeur(b), 2, b);
    if (!p) { t5centre(100, 90, "PARI INTROUVABLE"); return; }

    char l[FDJ_TXT_L + 2];
    elide(l, sizeof(l), 186, p->libelle);
    t5(6, 22, l);

    int y = 40;
    char d[64];
    time_t t = (time_t)p->t;
    struct tm ti;
    localtime_r(&t, &ti);
    snprintf(d, sizeof(d), "LE %02d/%02d/%04d A %02d:%02d", ti.tm_mday, ti.tm_mon + 1,
             ti.tm_year + 1900, ti.tm_hour, ti.tm_min);
    t5(6, y, d); y += 13;
    snprintf(d, sizeof(d), "JEU : %s", fdj_jeu_nom(p->jeu));
    t5(6, y, d); y += 13;
    snprintf(d, sizeof(d), "CODE : %s", p->code);
    t5(6, y, d); y += 16;

    snprintf(d, sizeof(d), "MISE : %u,%02u E", (unsigned)(p->mise_ct / 100),
             (unsigned)(p->mise_ct % 100));
    t5(6, y, d); y += 16;

    if (p->statut == FDJ_PARI_GAGNE) {
        snprintf(d, sizeof(d), "GAIN : %u,%02u E", (unsigned)(p->gain_ct / 100),
                 (unsigned)(p->gain_ct % 100));
        t5b(6, y, d); y += 26;
        t5(6, y, "STATUT : GAGNE, CREDITE");
    } else if (p->statut == FDJ_PARI_EN_COURS) {
        snprintf(d, sizeof(d), "POTENTIEL : %u,%02u E", (unsigned)(p->gain_ct / 100),
                 (unsigned)(p->gain_ct % 100));
        t5(6, y, d); y += 14;
        t5(6, y, "STATUT : EN COURS, PAS UN GAIN");
    } else {
        t5(6, y, "STATUT : PERDU");
    }
    t5centre(100, 186, "B1 : FERMER");
}

/* ---------------------------------------------------------------- */
/* gains / pertes                                                   */
/* ---------------------------------------------------------------- */
static void p_gains(const fdj_vue_t *v)
{
    static const int JOURS[3] = { 7, 30, 365 };
    static const char *NOM[3] = { "7 JOURS", "30 JOURS", "TOUT" };
    for (int i = 0; i < 3; i++) {
        fdj_periode_t p;
        fdj_periode_calcule(v->e, v->now, JOURS[i], &p);
        int y = 18 + i * 20;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 19, sel);
        int col = sel ? 0 : 1;
        a_text5c(6, y, 1, NOM[i], col);
        char b[24];
        if (p.nb == 0) snprintf(b, sizeof(b), "N.D.");
        else           euros_net(b, sizeof(b), p.net_ct);
        a_text5c(190 - largeur(b), y, 1, b, col);
        /* le nombre de paris est rappele dans la ligne de detail : pas de
           colonne au milieu, qui touchait le montant a droite */
        (void)0;
    }

    int choix = v->curseur < 3 ? v->curseur : 0;
    fdj_periode_t p;
    fdj_periode_calcule(v->e, v->now, JOURS[choix], &p);
    plein(FX0, 82, FX1, 82, 1);
    char b[56];
    if (p.nb == 0) snprintf(b, sizeof(b), "%s : AUCUN PARI, N.D.", NOM[choix]);
    else snprintf(b, sizeof(b), "%s : %d PARIS", NOM[choix], p.nb);
    t5(6, 88, b);
    t5(6, 102, "MISES ET GAINS CONFIRMES");
    t5(6, 114, "EN COURS N'EST PAS GAGNE");
    t5(6, 126, "L'ANNEAU GARDE 32 PARIS");

    if (v->gains_detail) {
        uint32_t mise[FDJ_JEU_NB] = { 0 }, gain[FDJ_JEU_NB] = { 0 };
        int nbj[FDJ_JEU_NB] = { 0 };
        int64_t depuis = v->now - (int64_t)JOURS[choix] * 86400;
        for (int k = 0; k < v->e->hist_n; k++) {
            int idx = fdj_hist_index_recence(v->e, k);
            if (idx < 0) continue;
            const fdj_pari_t *pp = &v->e->hist[idx];
            if (pp->t < depuis || pp->jeu >= FDJ_JEU_NB) continue;
            mise[pp->jeu] += pp->mise_ct; nbj[pp->jeu]++;
            if (pp->statut == FDJ_PARI_GAGNE) gain[pp->jeu] += pp->gain_ct;
        }
        int ligne = 0;
        for (int j = 0; j < FDJ_JEU_NB && ligne < 2; j++) {
            if (!nbj[j]) continue;
            char d[52];
            snprintf(d, sizeof(d), "%s : %d PARIS", fdj_jeu_nom((uint8_t)j), nbj[j]);
            t5(6, 144 + ligne * 12, d);
            snprintf(d, sizeof(d), "M %u,%02u  G %u,%02u", (unsigned)(mise[j] / 100),
                     (unsigned)(mise[j] % 100), (unsigned)(gain[j] / 100),
                     (unsigned)(gain[j] % 100));
            t5(120, 144 + ligne * 12, d);
            ligne++;
        }
        if (!ligne) t5(6, 144, "AUCUN PARI SUR CETTE PERIODE");
    } else {
        t5(6, 144, "B1 : DETAIL PAR JEU");
    }
}

/* ---------------------------------------------------------------- */
/* news                                                             */
/* ---------------------------------------------------------------- */
static void p_news(const fdj_vue_t *v)
{
    const fdj_flux_t *f = v->news;
    if (!f || !f->ok || f->nb == 0) {
        t5centre(100, 58, "FLUX INDISPONIBLE");
        t5centre(100, 74, "PAS DE TITRE INVENTE");
        t5centre(100, 86, "N.D.");
        t5centre(100, 106, "B2X2 : FORCER");
        return;
    }
    int n = f->nb < 3 ? f->nb : 3;
    for (int i = 0; i < n; i++) {
        const fdj_news_t *nn = &f->n[i];
        int y = 18 + i * 46;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 44, sel);
        int col = sel ? 0 : 1;
        char h[28];
        snprintf(h, sizeof(h), "%s  %s", nn->quand, nn->source);
        a_text5c(6, y, 1, h, col);
        char a[64], b[64];
        int maxc = 30;
        int len = (int)strlen(nn->titre);
        int cut = len < maxc ? len : maxc;
        if (cut < len) {
            int k = cut;
            while (k > maxc - 14 && nn->titre[k] != ' ') k--;
            if (k > 0) cut = k;
        }
        memcpy(a, nn->titre, (size_t)cut);
        a[cut] = 0;
        elide(b, sizeof(b), 178, nn->titre + cut + (nn->titre[cut] == ' ' ? 1 : 0));
        a_text5c(6, y + 12, 1, a, col);
        if (b[0]) a_text5c(6, y + 24, 1, b, col);
    }
}

/* ---------------------------------------------------------------- */
/* coupon                                                           */
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

    char b[48];
    snprintf(b, sizeof(b), "MISE %u,%02u E", (unsigned)(total_ct / 100),
             (unsigned)(total_ct % 100));
    t5b(6, 16, b);

    if (total_nb == 0) t5(6, 42, "GAIN POTENTIEL : N.D.");
    else {
        snprintf(b, sizeof(b), "GAIN POTENTIEL %u,%02u E",
                 (unsigned)(gain_pot / 100), (unsigned)(gain_pot % 100));
        elide(b, sizeof(b), 188, b);
        t5(6, 42, b);
    }
    plein(FX0, 56, FX1, 56, 1);

    const int maxl = 4;
    int k = 0;
    int y = 62;
    if (t) {
        for (int i = 0; i < t->n && k < maxl; i++) {
            if (!(v->e->sel_cochee & (1u << i))) continue;
            const fdj_sel_t *s = &t->s[i];
            bool sel = (v->curseur == k);
            sel_bar(y - 1, 12, sel);
            int col = sel ? 0 : 1;
            char m[24];
            elide(m, sizeof(m), 96, s->match);
            char ligne[52];
            snprintf(ligne, sizeof(ligne), "%s %s", s->code, m);
            a_text5c(6, y, 1, ligne, col);
            a_text5c(190 - largeur("2,00"), y, 1, "2,00", col);
            k++; y += 13;
        }
    }
    for (int g = 0; g < loto && k < maxl; g++) {
        if (!(v->e->grille_cochee & (1u << g))) continue;
        bool sel = (v->curseur == k);
        sel_bar(y - 1, 12, sel);
        int col = sel ? 0 : 1;
        char m[44];
        snprintf(m, sizeof(m), "LOTO%d %02u %02u %02u %02u %02u", g + 1,
                 v->grilles[g][0], v->grilles[g][1], v->grilles[g][2],
                 v->grilles[g][3], v->grilles[g][4]);
        a_text5c(6, y, 1, m, col);
        a_text5c(190 - largeur("2,20"), y, 1, "2,20", col);
        k++; y += 13;
    }
    if (total_nb > maxl) {
        snprintf(b, sizeof(b), "+ %d AUTRES (TOTAL %d)", total_nb - maxl, total_nb);
        t5(6, 114, b);
    } else if (total_nb == 0) {
        t5(6, 76, "AUCUNE SELECTION");
        t5(6, 88, "COCHEZ PAGE 3 OU 4");
    }

    int yv = 128;
    bool sv = (v->curseur == total_nb);
    sel_bar(yv, 15, sv);
    a_text5c(8, yv + 2, 1, "VALIDER LE COUPON", sv ? 0 : 1);
    int yc = 146;
    bool sc = (v->curseur == total_nb + 1);
    sel_bar(yc, 15, sc);
    a_text5c(8, yc + 2, 1, "VIDER LE COUPON", sc ? 0 : 1);

    t5(6, 166, "LOTO : GAIN SELON LE RANG");
}

/* ---------------------------------------------------------------- */
/* jeu responsable                                                  */
/* ---------------------------------------------------------------- */
static const char *QUESTIONS[] = {
    "JOUER EST UN PLAISIR",
    "PAS JOUER POUR ME REFAIRE",
    "BUDGET FIXE ET TENU",
    "SAVOIR DIRE STOP",
    "LE JEU N'EST PAS MA VIE",
};
#define QN 5

static void p_responsable(const fdj_vue_t *v)
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
        cadre(120 + i * 14, y, 131 + i * 14, y + 8, 1);
        if (i < score) plein(121 + i * 14, y + 1, 130 + i * 14, y + 7, 1);
    }

    int yp = 110;
    bool sel = (v->curseur == QN);
    sel_bar(yp - 1, 13, sel);
    int col = sel ? 0 : 1;
    char p[48];
    if (fdj_pause_active(v->e, v->now)) {
        int64_t r = fdj_pause_restant_s(v->e, v->now);
        if (r < 0) snprintf(p, sizeof(p), "PAUSE ACTIVE, FIN INCONNUE");
        else snprintf(p, sizeof(p), "PAUSE ENCORE %dh%02d", (int)(r / 3600), (int)((r / 60) % 60));
    } else {
        snprintf(p, sizeof(p), "PAUSE 24 H (AUTO-EXCL.)");
    }
    a_text5c(6, yp, 1, p, col);
    a_text5c(194 - largeur("OFF"), yp, 1, fdj_pause_active(v->e, v->now) ? "ON" : "OFF", col);

    if (score > 0 && score < 3) t5(6, 132, "PROTECTION FAIBLE : PLAFOND / 2");
    else if (score >= 3)        t5(6, 132, "PROTECTION OK");
    else                        t5(6, 132, "REPONDEZ OUI : PLUS SUR");
    t5(6, 148, "L'ARGENT ENGAGE RESTE ENGAGE");
}

/* ---------------------------------------------------------------- */
/* stats                                                            */
/* ---------------------------------------------------------------- */
static void p_stats(const fdj_vue_t *v)
{
    int gagne = 0, total = 0;
    for (int i = 0; i < v->e->hist_n; i++) {
        int idx = fdj_hist_index_recence(v->e, i);
        if (idx < 0) continue;
        if (v->e->hist[idx].statut == FDJ_PARI_EN_COURS) continue;
        total++;
        if (v->e->hist[idx].statut == FDJ_PARI_GAGNE) gagne++;
    }
    char b[48];
    struct { const char *t, *val; } B[3];
    B[0].t = "LOTO 5/49"; B[0].val = "1 / 19 068 840";
    B[1].t = "EUROMILLIONS";       B[1].val = "1 / 139 838 160";
    B[2].t = "COTE 2,00";          B[2].val = "50 %";
    for (int i = 0; i < 3; i++) {
        int y = 18 + i * 26;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 25, sel);
        int col = sel ? 0 : 1;
        a_text5c(6, y, 1, B[i].t, col);
        a_text5c(190 - largeur(B[i].val), y, 1, B[i].val, col);
    }
    plein(FX0, 96, FX1, 96, 1);
    if (total > 0) snprintf(b, sizeof(b), "MES PARIS REGLES : %d / %d", gagne, total);
    else           snprintf(b, sizeof(b), "MES PARIS REGLES : N.D.");
    t5(6, 104, b);
    t5(6, 120, "PROBA IMPLICITE, PAS UNE COTE");
    t5(6, 132, "AUCUN TRJ OFFICIEL AFFICHE");
    t5(6, 144, "L'HISTORIQUE N'EST PAS UNE PREDICTION");
}

/* ---------------------------------------------------------------- */
/* user stories                                                     */
/* ---------------------------------------------------------------- */
typedef struct { const char *id, *titre; int crit; } us_t;
static const us_t US[] = {
    { "US-01", "PARIER EN DIRECT", 5 },
    { "US-02", "LOTO EQUILIBRE", 4 },
    { "US-03", "HISTORIQUE ET GAINS", 4 },
    { "US-04", "PROFILS ET PLAFONDS", 6 },
    { "US-05", "CODE DE DEMARRAGE", 4 },
    { "US-06", "NEWS ET HORS LIGNE", 4 },
};
#define USN 6

static void p_stories(const fdj_vue_t *v)
{
    for (int i = 0; i < USN; i++) {
        int y = 18 + i * 13;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 12, sel);
        int col = sel ? 0 : 1;
        char b[40];
        snprintf(b, sizeof(b), "%s %s", US[i].id, US[i].titre);
        a_text5c(6, y, 1, b, col);
        char c[8];
        snprintf(c, sizeof(c), "%d AC", US[i].crit);
        a_text5c(190 - largeur(c), y, 1, c, col);
    }
    t5(6, 100, "AC = CRITERES D'ACCEPTATION");
    t5(6, 114, "DETAIL : docs/features/US-FDJ.md");
    t5(6, 130, "CHAQUE HISTOIRE A SES TESTS");
}

/* ---------------------------------------------------------------- */
/* tests                                                            */
/* ---------------------------------------------------------------- */
static void p_tests(const fdj_vue_t *v)
{
    struct { const char *t; int n; } N[4] = {
        { "1. UNITAIRES", 0 }, { "2. COMPOSANTS", 0 },
        { "3. INTEGRATION", 0 }, { "4. FONCTIONNEL", 0 },
    };
#ifdef FDJ_TESTS_UNIT
    N[0].n = FDJ_TESTS_UNIT; N[1].n = FDJ_TESTS_COMP;
    N[2].n = FDJ_TESTS_INTEG; N[3].n = FDJ_TESTS_FONC;
#endif
    int total = 0;
    for (int i = 0; i < 4; i++) total += N[i].n;
    for (int i = 0; i < 4; i++) {
        int y = 18 + i * 14;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 13, sel);
        int col = sel ? 0 : 1;
        a_text5c(6, y, 1, N[i].t, col);
        char b[10];
        snprintf(b, sizeof(b), "%d", N[i].n);
        a_text5c(190 - largeur(b), y, 1, b, col);
    }
    char b[64];
    snprintf(b, sizeof(b), "%d VERIFS, %d ECHEC", total, FDJ_TESTS_FAIL);
    t5(6, 82, b);
    snprintf(b, sizeof(b), "RECETTE DU %s", FDJ_TESTS_DATE);
    t5(6, 94, b);
    t5(6, 110, "CHAQUE BLOC A SON TEST");
    t5(6, 122, "TESTS CARTE NON EXECUTES ICI");
    t5(6, 134, "AUCUNE PAGE SANS SES TESTS");
}

/* ---------------------------------------------------------------- */
/* architecture                                                     */
/* ---------------------------------------------------------------- */
static void p_archi(const fdj_vue_t *v)
{
    static const char *M[4] = {
        "MODELE   ARGENT / POISSON",
        "PARSEURS JSON / RSS",
        "NAV + UI  37 ECRANS 1 BIT",
        "MATERIEL EPD / NVS / WIFI",
    };
    for (int i = 0; i < 4; i++) {
        int y = 18 + i * 14;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 13, sel);
        a_text5c(6, y, 1, M[i], sel ? 0 : 1);
    }
    plein(FX0, 78, FX1, 78, 1);
    char b[56];
    snprintf(b, sizeof(b), "ETAT : %u OCTETS (sizeof)", (unsigned)sizeof(fdj_etat_t));
    t5(6, 84, b);
    snprintf(b, sizeof(b), "IMAGE : %d OCTETS (200x25)", ART_FB_LEN);
    t5(6, 96, b);
    snprintf(b, sizeof(b), "VUE : %u OCTETS (sizeof)", (unsigned)sizeof(fdj_vue_t));
    t5(6, 108, b);
    t5(6, 124, "MEME CODE CARTE ET PC");
    t5(6, 136, "1 BLOB NVS + CRC32");
    t5(6, 148, "16 MATCHS, 32 PARIS, 4 PROFILS");
}

/* ---------------------------------------------------------------- */
/* perf / securite                                                  */
/* ---------------------------------------------------------------- */
static void p_perf(const fdj_vue_t *v)
{
    char b1[16], b2[16], b3[16];
    snprintf(b1, sizeof(b1), "%d MS", v->rendu_ms);
    snprintf(b2, sizeof(b2), "%d MS", v->ecran_ms);
    snprintf(b3, sizeof(b3), "%u KO", (unsigned)(v->heap_int_libre / 1024));
    struct { const char *t, *val; } L[3] = {
        { "DESSIN PAGE", b1 }, { "RAFRAICHISSEMENT", b2 }, { "HEAP INTERNE", b3 },
    };
    for (int i = 0; i < 3; i++) {
        int y = 18 + i * 14;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 13, sel);
        a_text5c(6, y, 1, L[i].t, sel ? 0 : 1);
        a_text5c(190 - largeur(L[i].val), y, 1, L[i].val, sel ? 0 : 1);
    }
    plein(FX0, 62, FX1, 62, 1);
    t5(6, 68, "TLS VERIFIE, PAS D'INSECURE");
    t5(6, 80, "AUCUN SECRET DANS /etat");
    t5(6, 92, "API LOCALE SEULEMENT");
    t5(6, 110, "TUNNEL VPN : NON IMPLEMENTE");
    t5(6, 122, "OPTION RETIREE, PAS UN MENSONGE");
    t5(6, 140, "MESURES A L'INSTANT");
}

/* ---------------------------------------------------------------- */
/* memoire                                                          */
/* ---------------------------------------------------------------- */
static void jauge(int y, const char *nom, uint32_t libre, uint32_t total)
{
    t5(6, y, nom);
    char b[24];
    if (total == 0) snprintf(b, sizeof(b), "N.D.");
    else snprintf(b, sizeof(b), "%u/%u KO", (unsigned)(libre / 1024), (unsigned)(total / 1024));
    t5d(190, y, b);
    int pct = total ? (int)((uint64_t)libre * 100 / total) : 0;
    if (pct > 100) pct = 100;
    cadre(6, y + 13, 194, y + 18, 1);
    if (total) plein(7, y + 14, 7 + (int)(186 * pct / 100), y + 17, 1);
}

static void p_ram(const fdj_vue_t *v)
{
    jauge(18, "RAM INTERNE", v->heap_int_libre, v->heap_int_total);
    jauge(46, "PLUS GROS BLOC", v->heap_int_max, v->heap_int_total);
    jauge(74, "PSRAM", v->psram_libre, v->psram_total);
    t5(6, 108, "PAGING / SWAP : AUCUN");
    t5(6, 120, "RIEN N'EST ECRIT SUR LA CARTE");
    if (v->sd_ok) t5(6, 132, "CARTE SD : CSV SEULEMENT");
    else          t5(6, 132, "PAS DE CARTE SD");
    t5(6, 148, "B2X2 : REFAIRE LES MESURES");
}

/* ---------------------------------------------------------------- */
/* compte                                                           */
/* ---------------------------------------------------------------- */
static void p_compte(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    char ligne[5][56];
    snprintf(ligne[0], sizeof(ligne[0]), "%s", e->compte_nom);
    snprintf(ligne[1], sizeof(ligne[1]), "%s", e->compte_id);
    if (v->ssid && v->ssid[0])
        snprintf(ligne[2], sizeof(ligne[2]), "%s %d DBM", v->ssid, v->rssi);
    else
        snprintf(ligne[2], sizeof(ligne[2]), "HORS LIGNE");
    snprintf(ligne[3], sizeof(ligne[3]), "%s", e->son_clic ? "ON" : "OFF");
    snprintf(ligne[4], sizeof(ligne[4]), "%s",
             e->code_perso ? "CODE PERSONNALISE" : "CODE D'USINE A MODIFIER");
    static const char *TITRE[5] = { "TITULAIRE", "COMPTE", "RESEAU", "SON", "CODE" };
    for (int i = 0; i < 5; i++) {
        int y = 18 + i * 26;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 25, sel);
        int col = sel ? 0 : 1;
        a_text5c(6, y, 1, TITRE[i], col);
        char b[52];
        elide(b, sizeof(b), 172, ligne[i]);
        a_text5c(78, y, 1, b, col);
        if (i == 4) a_text5c(190, y, 1, ">", col);
    }
    t5(6, 158, "MOT DE PASSE WIFI : JAMAIS AFFICHE");
    t5(6, 170, "SECRETS : main/secrets.h");
}

/* ---------------------------------------------------------------- */
/* profils                                                          */
/* ---------------------------------------------------------------- */
static void p_profils(const fdj_vue_t *v)
{
    const fdj_etat_t *e = v->e;
    int n = e->nb_profils;
    for (int i = 0; i < n; i++) {
        const fdj_profil_t *p = &e->profils[i];
        int y = 18 + i * 26;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 25, sel);
        int col = sel ? 0 : 1;
        char b[44];
        if (i == e->profil_actif) a_text5c(6, y, 1, ">", col);
        elide(b, sizeof(b), 90, p->nom);
        a_text5c(16, y, 1, b, col);
        snprintf(b, sizeof(b), "%u/%u E", (unsigned)(p->plafond_sem_ct / 100),
                 (unsigned)(p->plafond_mois_ct / 100));
        a_text5c(120, y, 1, b, col);
        if (i == e->profil_actif) {
            snprintf(b, sizeof(b), "%u,%02u", (unsigned)(p->solde_ct / 100),
                     (unsigned)(p->solde_ct % 100));
            a_text5c(190 - largeur(b), y, 1, b, col);
        }
    }
    int y = 18 + n * 26;
    bool sel = (v->curseur == n);
    sel_bar(y - 1, 22, sel);
    int col = sel ? 0 : 1;
    a_text5c(6, y, 1, n < FDJ_PROFILS_MAX ? "NOUVEAU PROFIL (B2X2)" : "4 PROFILS MAXIMUM", col);

    uint32_t sem = 0, mois = 0;
    fdj_depenses(e, v->now, &sem, &mois);
    const fdj_profil_t *pa = fdj_profil_actif_c(e);
    char d[64];
    if (pa) snprintf(d, sizeof(d), "ACTIF %s : %u/%u E", pa->nom,
                     (unsigned)(sem / 100), (unsigned)(fdj_plafond_sem_effectif(e) / 100));
    else snprintf(d, sizeof(d), "AUCUN PROFIL ACTIF");
    elide(d, sizeof(d), 186, d);
    t5(6, 150, d);
    t5(6, 164, "B1 ACTIVER   B1X2 SUPPRIMER");
}

/* ---------------------------------------------------------------- */
/* accessibilite                                                    */
/* ---------------------------------------------------------------- */
static void p_acces(const fdj_vue_t *v)
{
    char l2[8];
    snprintf(l2, sizeof(l2), "%s", v->e->son_clic ? "ON" : "OFF");
    struct { const char *t, *val; } O[4] = {
        { "SON DES TOUCHES", l2 },
        { "CONTRASTE FORT", "TOUJOURS" },
        { "LANGUE", "FRANCAIS" },
        { "TEXTE", "5x10 PARTOUT" },
    };
    for (int i = 0; i < 4; i++) {
        int y = 18 + i * 26;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 25, sel);
        int col = sel ? 0 : 1;
        a_text5c(6, y, 1, O[i].t, col);
        a_text5c(190 - largeur(O[i].val), y, 1, O[i].val, col);
    }
    t5(6, 134, "AUCUNE INFO PAR LA COULEUR");
    t5(6, 146, "COCHE = CARRE + TIRET");
    t5(6, 162, "PLUS DE PETITS CARACTERES");
}

/* ---------------------------------------------------------------- */
/* rgpd                                                             */
/* ---------------------------------------------------------------- */
static void p_rgpd(const fdj_vue_t *v)
{
    static const char *L[3] = { "EFFACER MES DONNEES", "EXPORTER L'HISTORIQUE", "A PROPOS" };
    for (int i = 0; i < 3; i++) {
        int y = 18 + i * 20;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 19, sel);
        a_text5c(6, y, 1, L[i], sel ? 0 : 1);
    }
    plein(FX0, 86, FX1, 86, 1);
    t5(6, 92, "NVS : COMPTE, CODE, PROFILS, PARIS");
    t5(6, 106, "SORTIES : SCORES + TITRES");
    t5(6, 118, "AUCUN COMPTE CLIENT, AUCUN PAIEMENT");
    t5(6, 130, "EFFACER = TOUT EFFACER");
    t5(6, 142, "CONSERVATION : JUSQU'A EFFACEMENT");
    t5(6, 160, "@PARISTZOU - JUSTE POUR LES FILLES");
}

/* ---------------------------------------------------------------- */
/* regles                                                           */
/* ---------------------------------------------------------------- */
static void p_regles(const fdj_vue_t *v)
{
    static const char *B[3] = {
        "3 FAMILLES : PARIS, LOTERIE, CASINO",
        "INTERDIT AUX -18 ANS",
        "MAQUETTE NON OFFICIELLE",
    };
    for (int i = 0; i < 3; i++) {
        int y = 18 + i * 14;
        bool sel = (v->curseur == i);
        sel_bar(y - 1, 13, sel);
        a_text5c(6, y, 1, B[i], sel ? 0 : 1);
    }
    plein(FX0, 64, FX1, 64, 1);
    t5(6, 70, "JEUX DECLARES");
    for (int i = 0; i < FDJ_JEU_NB; i++) {
        int col = i % 2, row = i / 2;
        int x = 6 + col * 96, y = 84 + row * 12;
        t5(x, y, fdj_jeu_nom((uint8_t)i));
    }
    t5(6, 140, "TRJ ET REGLES : FDJ.FR");
    t5(6, 156, "AUCUNE MISE REELLE");
}

/* ---------------------------------------------------------------- */
/* table des ecrans                                                 */
/* ---------------------------------------------------------------- */
static const char *TITRE_MODULE[FDJ_M_NB] = {
    "ACCUEIL",          "PARIS SPORTIFS",  "LOTO",             "HISTORIQUE",
    "GAINS / PERTES",   "NEWS",            "MON COUPON",       "JEU RESPONSABLE",
    "STATS",            "USER STORIES",    "TESTS",            "ARCHITECTURE",
    "PERF / SECU",      "MEMOIRE",         "COMPTE",           "PROFILS",
    "ACCESSIBILITE",    "DONNEES",         "REGLES",
};

int fdj_ecran_module(int page)
{
    if (page <= 0 || page >= FDJ_PAGES) return 0;
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
    if (fdj_ecran_est_intertitre(page)) return "B2 : SUIVANT";
    switch (fdj_ecran_module(page)) {
    case FDJ_M_ACCUEIL:  return "B1 OU B2X2 : DEMARRER";
    case FDJ_M_PARIS:    return "B1 COCHER  B2X2 MAJ";
    case FDJ_M_LOTO:     return "B1 GRILLE  B2X2 NEUVE";
    case FDJ_M_HIST:     return "B1 DETAIL  B2X2 SUITE";
    case FDJ_M_GAINS:    return "B1 PAR JEU";
    case FDJ_M_NEWS:     return "B1 LU  B2X2 FORCER";
    case FDJ_M_COUPON:   return "B1 VALIDER B1X2 VIDER";
    case FDJ_M_RESP:     return "B1 OUI  B2X2 PAUSE";
    case FDJ_M_STATS:    return "INFOS";
    case FDJ_M_STORIES:  return "INFOS";
    case FDJ_M_TESTS:    return "INFOS";
    case FDJ_M_ARCHI:    return "INFOS";
    case FDJ_M_PERF:     return "B2X2 MESURES";
    case FDJ_M_RAM:      return "B2X2 MESURES";
    case FDJ_M_COMPTE:   return "B1 REGLER";
    case FDJ_M_PROFILS:  return "B1 ACTIVER  B1X2 SUPPR";
    case FDJ_M_ACCES:    return "B1 REGLER";
    case FDJ_M_RGPD:     return "B1 ACTION";
    case FDJ_M_REGLES:   return "B1 FDJ.FR";
    default:             return "";
    }
}

int fdj_page_items(int page, const fdj_vue_t *v)
{
    if (page < 0 || page >= FDJ_PAGES) return 1;
    if (fdj_ecran_est_intertitre(page)) return 1;
    switch (fdj_ecran_module(page)) {
    case FDJ_M_ACCUEIL:  return 1;
    case FDJ_M_PARIS:    return v->top && v->top->n > 0 ? (v->top->n < 6 ? v->top->n : 6) : 1;
    case FDJ_M_LOTO:     return FDJ_GRILLE_MAX + 1;
    case FDJ_M_HIST: {
        if (v->e->hist_n == 0) return 1;
        bool plus = false;
        int n = hist_affiche(v->e, &plus);
        if (n < 1) return 1;
        return plus ? n + 1 : n;
    }
    case FDJ_M_GAINS:    return 3;
    case FDJ_M_NEWS:     return v->news && v->news->nb > 0 ? (v->news->nb < 3 ? v->news->nb : 3) : 1;
    case FDJ_M_COUPON:   return (coupon_nb(v) < 4 ? coupon_nb(v) : 4) + 2;
    case FDJ_M_RESP:     return QN + 1;
    case FDJ_M_STATS:    return 3;
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
        default:             t5centre(100, 90, "ECRAN INCONNU"); break;
        }
    }
    pied(v);
}

/* ---------------------------------------------------------------- */
/* saisie du code et confirmation                                   */
/* ---------------------------------------------------------------- */
void fdj_ui_demande_code(uint8_t *fb, const char *titre, const uint8_t chiffres[4],
                         int position, bool erreur)
{
    art_begin(fb, 0);
    art_clear();
    plein(0, 0, 199, 15, 1);
    t5w(6, 3, "UNIBET - FDT");
    t5w(190 - largeur("DEMO"), 3, "DEMO");

    t5centre(100, 30, titre);
    t5centre(100, 46, "MAQUETTE DE DEMONSTRATION");

    for (int i = 0; i < 4; i++) {
        int x = 18 + i * 44;
        cadre(x, 62, x + 36, 104, 2);
        if (i == position) plein(x - 3, 59, x + 39, 107, 1);
        char b[2] = { (char)('0' + chiffres[i]), 0 };
        a_text5c(x + 11, 72, 3, b, i == position ? 0 : 1);
    }
    t5centre(100, 118, "B2 CHIFFRE SUIVANT  B1 CHIFFRE +1");
    t5centre(100, 130, "B2X2 VALIDER");
    if (erreur) t5centre(100, 150, "CODE REFUSE");
    else        t5centre(100, 150, "CODE D'USINE : 0000");
    t5centre(100, 168, "4 CHIFFRES, RIEN D'AUTRE");
}

void fdj_ui_confirme(uint8_t *fb, const char *titre, const char *question,
                     bool oui_selectionne)
{
    art_begin(fb, 0);
    art_clear();
    t5centre(100, 30, titre);
    phrase(6, 50, 188, question, 2, 12);
    t5centre(100, 80, "ACTION DEFINITIVE");
    cadre(20, 96, 94, 130, oui_selectionne ? 3 : 1);
    t5centre(57, 108, "OUI");
    cadre(106, 96, 180, 130, oui_selectionne ? 1 : 3);
    t5centre(143, 108, "NON");
    t5centre(100, 146, "B2X2 CONFIRMER  B1 INVERSER");
    t5centre(100, 160, "B2 TENU 3 S : ANNULER");
    t5centre(100, 180, "@PARISTZOU");
}
