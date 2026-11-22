/*  petart.cpp - le monde Gotham, le sprite BATMino qui evolue, le bandeau. */
#include "petart.h"
#include "art.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* decor                                                              */
/* ------------------------------------------------------------------ */
#define CIEL_H     137          /* hauteur du sol (pieds du personnage) */
#define BANDEAU_Y  150

static void etoiles(void)
{
    static const uint8_t st[][2] = {
        {14,40},{32,32},{52,46},{78,34},{96,44},{122,30},{140,52},{168,36},
        {24,64},{66,70},{110,62},{150,74},{186,60},{8,58},{178,88}
    };
    for (unsigned i = 0; i < sizeof(st) / sizeof(st[0]); i++)
        a_px(st[i][0], st[i][1], 1);
}

/* projecteur geant : lune noire + logo chauve-souris blanc */
static void projecteur(void)
{
    a_disc(162, 52, 17, 1);
    a_disc(162, 52, 15, 0);
    ic_bat(162, 52, 7);
    /* faisceau */
    a_seg(150, 62, 116, 120, 1);
    a_seg(174, 62, 186, 120, 1);
}

/* immeubles + fenetres allumees */
static void skyline(void)
{
    static const uint8_t im[][3] = {   /* x, largeur, hauteur */
        {0, 26, 42}, {26, 18, 62}, {44, 24, 30}, {68, 20, 52}, {88, 30, 38},
        {118, 18, 66}, {136, 26, 44}, {162, 16, 26}, {178, 22, 56}
    };
    for (unsigned i = 0; i < sizeof(im) / sizeof(im[0]); i++) {
        int x = im[i][0], l = im[i][1], h = im[i][2], y = CIEL_H - h;
        int xx, yy;
        for (yy = y; yy < CIEL_H; yy++)
            for (xx = x; xx < x + l && xx < ART_W; xx++) a_px(xx, yy, 1);
        /* fenetres eclaircies : 2x3 trous blancs */
        for (yy = y + 4; yy < CIEL_H - 4; yy += 7)
            for (xx = x + 3; xx < x + l - 3; xx += 6) {
                if (((xx * 7 + yy * 3 + i * 5) % 3) == 0) continue;
                for (int k = 0; k < 2; k++)
                    for (int q = 0; q < 3; q++) a_px(xx + k, yy + q, 0);
            }
    }
    /* toits : antennes */
    a_vline(9, CIEL_H - 52, CIEL_H - 42, 1);
    a_vline(126, CIEL_H - 76, CIEL_H - 66, 1);
}

static void rue(void)
{
    a_hline(0, ART_W - 1, CIEL_H, 13);
    a_hline(0, ART_W - 1, CIEL_H - 1, 1);
    /* marquage de chaussee */
    for (int x = 6; x < ART_W; x += 22) a_hline(x, x + 9, CIEL_H + 6, 2);
}

/* ------------------------------------------------------------------ */
/* le personnage                                                      */
/* ------------------------------------------------------------------ */
static void pattes(int cx, int sol, int l, int frame, int ep)
{
    /* quatre phases de marche : 0 gauche avance, 1 serre, 2 droite avance, 3 serre */
    int g = 0, d = 0;
    switch (frame & 3) {
    case 0: g = -4; d = 4; break;
    case 1: g = 0;  d = 0; break;
    case 2: g = 4;  d = -4; break;
    default: g = 0; d = 0; break;
    }
    a_hline(cx - 3 + g, cx + 1 + g, sol - l, l);      /* jambes */
    a_hline(cx - 1 + d, cx + 3 + d, sol - l, l);
    for (int k = 0; k < l; k++) a_px(cx - 3 + g, sol - 1 - k, 1);
    for (int k = 0; k < l; k++) a_px(cx + 3 + d, sol - 1 - k, 1);
    a_hline(cx - 5 + g, cx - 3 + g, sol - 1, ep);     /* bottes */
    a_hline(cx + 3 + d, cx + 5 + d, sol - 1, ep);
}

static int dir_humeur = 0;   /* direction du regard (l'humeur l'oriente) */

/* tete du personnage : cagoule + oreilles pointues + yeux blancs */
static void tete_bat(int cx, int cy, int r, int endormi, int bebe, int triste)
{
    a_efill(cx, cy, r, r, 1);
    /* oreilles : hautes, larges a la base, pointe nette (signature du masque) */
    a_tri(cx - r - 1, cy - r / 2, cx - r + 1, cy - r - r, cx - 0, cy - r + 1, 1);
    a_tri(cx + r + 1, cy - r / 2, cx + r - 1, cy - r - r, cx + 0, cy - r + 1, 1);
    /* bas du visage clair */
    a_efill(cx, cy + r - 2, r / 2 + 2, r / 3 + 1, 0);
    if (endormi) {
        a_hline(cx - r / 2 - 2, cx - 2, cy, 2);
        a_hline(cx + 2, cx + r / 2 + 2, cy, 2);
        return;
    }
    /* yeux blancs : toujours visibles, c'est l'identite du personnage */
    a_efill(cx - r / 2 - 2, cy - 1, bebe ? r / 2 : r / 3 + 2, bebe ? r / 2 : 4, 0);
    a_efill(cx + r / 2 + 2, cy - 1, bebe ? r / 2 : r / 3 + 2, bebe ? r / 2 : 4, 0);
    /* pupilles : petites, dirigees selon l'humeur */
    a_disc(cx - r / 2 - 1 + (dir_humeur > 0 ? 1 : -1), cy + (triste ? 1 : 0), bebe ? 2 : 1, 1);
    a_disc(cx + r / 2 + 1 + (dir_humeur > 0 ? 1 : -1), cy + (triste ? 1 : 0), bebe ? 2 : 1, 1);
}

void sprite_bat(int cx, int sol, int stade, int forme, int frame, int dir, int endormi)
{
    int bob = ((frame & 3) == 1 || (frame & 3) == 3) ? 0 : 1;
    int r, corps_h, larg, jambes, ep = 2;

    switch (stade) {
    case ST_OEUF: {
        a_efill(cx, sol - 22, 15, 20, 1);
        a_efill(cx, sol - 24, 12, 15, 0);
        ic_bat(cx, sol - 24, 7);
        if (frame & 1) {                   /* la coquille craque */
            a_seg(cx - 9, sol - 26, cx - 3, sol - 20, 2);
            a_seg(cx - 3, sol - 20, cx + 2, sol - 27, 2);
            a_seg(cx + 2, sol - 27, cx + 9, sol - 19, 2);
        }
        a_hline(cx - 15, cx + 15, sol - 1, 2);
        return; }

    case ST_BEBE:
        /* bebe : enorme tete, cape courte, jambes minuscules */
        a_tri(cx - 11, sol - 20, cx + 11, sol - 20, cx - 8 * dir, sol - 2, 1);
        a_disc(cx, sol - 10, 9, 1);
        pattes(cx, sol, 7, frame, 2);
        tete_bat(cx, sol - 24 + bob, 11, endormi, 1, forme == FORME_GROGNON);
        return;

    case ST_ENFANT:  r = 10; corps_h = 14; larg = 8;  jambes = 15; break;
    case ST_ADO:     r = 12; corps_h = 19; larg = 9;  jambes = 20; break;
    case ST_VIEILLARD:
    case ST_ADULTE:
    default:         r = 13; corps_h = 24; larg = 11; jambes = 25; break;
    }
    {
        int ty = sol - jambes - corps_h - r + bob;
        int cape_w = (forme == FORME_HEROS) ? r + 16 : r + 11;
        if (forme == FORME_GROGNON) cape_w = r + 16;
        /* cape : silhouette a festons (le bord en chauve-souris), c'est ce qui
           identifie Batman a la silhouette meme quand tout est noir */
        {
            int yh = ty + r;
            a_tri(cx - cape_w / 2, yh, cx + cape_w / 2, yh, cx + cape_w / 2 + 4, sol - 2, 1);
            a_tri(cx - cape_w / 2, yh, cx + cape_w / 2 + 4, sol - 2, cx - cape_w / 2 - 4, sol - 2, 1);
            /* festons du bas : 3 pointes */
            a_tri(cx - cape_w / 2 - 4, sol - 2, cx - cape_w / 4, sol - 10, cx - cape_w / 6, sol - 2, 1);
            a_tri(cx - cape_w / 4, sol - 2, cx + cape_w / 4, sol - 12, cx + cape_w / 6, sol - 2, 1);
            a_tri(cx + cape_w / 6, sol - 2, cx + cape_w / 2 + 4, sol - 10, cx + cape_w / 3, sol - 2, 1);
            for (int p2 = 0; p2 < 4; p2++)          /* franges */
                a_tri(cx - cape_w / 2 + p2 * 3, sol - 2, cx + cape_w / 2 - 3 + p2, sol - 2,
                      cx, sol - 2 - 6, 1);
        }
        /* torse */
        a_tri(cx - larg, sol - jambes - corps_h, cx + larg, sol - jambes - corps_h,
              cx - larg / 2 - 2, sol - jambes, 1);
        a_tri(cx - larg, sol - jambes - corps_h, cx + larg, sol - jambes - corps_h,
              cx + larg / 2 + 2, sol - jambes, 1);
        pattes(cx, sol, jambes, frame, ep);
        /* bras */
        {
            int g = ((frame & 3) == 0) ? 3 : (((frame & 3) == 2) ? -3 : 0);
            a_seg(cx - larg, sol - jambes - corps_h + 3, cx - larg - 2 + g, sol - jambes - 2, 3);
            a_seg(cx + larg, sol - jambes - corps_h + 3, cx + larg + 2 - g, sol - jambes - 2, 3);
        }
        /* logo chauve-souris BLANC sur le torse noir (sinon invisible) */
        if (stade != ST_ENFANT) ic_bat_c(cx, sol - jambes - corps_h + 9, 6, 0);
        /* ceinture blanche, boucle noire : lisible sur le noir */
        if (stade == ST_ADULTE || stade == ST_VIEILLARD) {
            a_hline(cx - larg / 2 - 2, cx + larg / 2 + 2, sol - jambes - 3, 2);
            for (int k = 0; k < 2; k++) a_px(cx, sol - jambes - 3 + k, 0);
        }
        if (forme == FORME_MALADIF) {              /* cape dechiree + mouchoir */
            a_hline(cx - 6, cx + 6, sol - jambes - corps_h - 6, 2);
        }
        tete_bat(cx, ty, r, endormi, 0, stade == ST_VIEILLARD || forme == FORME_MALADIF);
        /* vieillard : dos courbe + canne */
        if (stade == ST_VIEILLARD) {
            a_seg(cx + 12 * dir, sol - jambes - 4, cx + 15 * dir, sol - 1, 2);
            a_arc(cx + 12 * dir, sol - jambes - 4, 4, 0, 180, 2);
        }
        if (endormi) {
            a_text(cx + 14, ty - 12, 2, "Z");
            a_text(cx + 24, ty - 22, 1, "Z");
        }
    }
}

/* ------------------------------------------------------------------ */
/* bandeau du bas                                                     */
/* ------------------------------------------------------------------ */
static void jauge(int x, int y, void (*icone)(int, int), int coeurs, int max)
{
    int i, a;
    icone(x + 8, y + 8);
    for (i = 0; i < max; i++) {
        int bx = x + 20 + i * 9;
        if (i < coeurs) {
            for (a = 0; a < 7; a++) for (int b = 0; b < 11; b++) a_px(bx + a, y + 2 + b, 1);
        } else {
            for (a = 0; a < 7; a++) { a_px(bx + a, y + 2, 2); a_px(bx + a, y + 11, 2); }
            for (a = 0; a < 11; a++) { a_px(bx, y + 2 + a, 2); a_px(bx + 6, y + 2 + a, 2); }
        }
    }
}

static void ic_one_food(int x, int y) { ic_food(x, y); }
static void ic_one_coeur(int x, int y) { ic_heart(x, y, 5, 1); }
static void ic_one_bolt(int x, int y) { ic_bolt(x, y, 6); }
static void ic_one_propre(int x, int y) { ic_ram(x, y); }

static void texte_gras(int x, int y, int sc, const char *t)
{
    a_text(x, y, sc, t);
    a_text(x + 1, y, sc, t);       /* seconde passe : plus epais, lisible en petit */
}

void bandeau_jauges(const pet_state_t *p, const monde_info_t *mi, int y0)
{
    char b[72];
    a_hline(0, ART_W - 1, y0 - 2, 1);

    jauge(2, y0 + 1, ic_one_food, (int)p->faim / 100, 4);
    jauge(52, y0 + 1, ic_one_coeur, (int)p->bonheur / 100, 4);
    jauge(102, y0 + 1, ic_one_bolt, (int)p->energie / 100, 4);
    jauge(152, y0 + 1, ic_one_propre, (int)p->proprete / 100, 4);

    /* ligne 2 : stade, age, forme */
    snprintf(b, sizeof(b), "%s J%u", pet_stade_nom(pet_stade(p)), (unsigned)(p->age_s / 86400));
    a_text(4, y0 + 17, 2, b);                       /* stade + age, gros */
    {
        int st = pet_stade(p);
        int x = 4 + a_text_w(2, b) + 8;
        if (st == ST_ADULTE || st == ST_VIEILLARD)
            a_text(x, y0 + 17, 2, pet_forme_nom(p->forme));
        else if (st == ST_ANGE)
            a_text(x, y0 + 17, 2, "PARTI");
    }
    texte_gras(4, y0 + 30, 1, p->parti ? "APPUIE POUR UN NOUVEL OEUF" : pet_message(p));

    /* ligne 3 : cpu, ram, carte sd (demande explicite de l'utilisateur) */
    snprintf(b, sizeof(b), "CPU %dMHZ RAM %d.%dM PSRAM %d.%dM",
             mi->cpu_mhz, mi->ram_libre_ko / 1024, (mi->ram_libre_ko % 1024) / 103,
             mi->psram_libre_ko / 1024, (mi->psram_libre_ko % 1024) / 103);
    texte_gras(4, y0 + 38, 1, b);
    snprintf(b, sizeof(b), "SANTE %u%%  SD %dG", pet_sante(p), mi->sd_libre_go);
    texte_gras(4, y0 + 45, 1, b);
}

/* ------------------------------------------------------------------ */
/* le monde                                                           */
/* ------------------------------------------------------------------ */
void monde_dessine(const pet_state_t *p, const monde_info_t *mi)
{
    int st = pet_stade(p);

    etoiles();
    projecteur();
    skyline();
    rue();

    if (st == ST_ANGE) {
        /* il est parti : son ombre s'envole vers le projecteur */
        int bx = (int)p->x, by = 92;
        ic_bat(bx, by, 9);
        ic_bat(bx - 18, by + 14, 5);
        ic_bat(bx + 16, by + 18, 4);
        a_text_c(100, 112, 2, "IL EST PARTI");
        a_text_c(100, 126, 1, "MERCI POUR TOUT");
        bandeau_jauges(p, mi, BANDEAU_Y);
        return;
    }
    dir_humeur = (p->humeur == 1 || p->humeur == 3 || p->humeur == 11 || p->humeur == 13) ? -1 : 1;
    sprite_bat((int)p->x, CIEL_H, st, p->forme,
               p->endormi ? 1 : p->pas, p->dir, p->endormi);

    /* bulle d'humeur au-dessus de la tete */
    {
        static const uint8_t sym_de_famille[14] = {
            X_HEART, X_DROP, X_ANGER, X_SWEAT, X_SPARK, X_DROP, X_QUESTION,
            X_DOTS, X_NONE, X_SPARK, X_HEART, X_SWEAT, X_HEART, X_PAIN
        };
        uint8_t sy = (p->humeur < 14) ? (uint8_t)sym_de_famille[p->humeur] : (uint8_t)X_NONE;
        if (sy != X_NONE) a_symbole(sy, (int)p->x + 24, 96);
    }
    bandeau_jauges(p, mi, BANDEAU_Y);
}

/* page visage : la grosse tete, bien lisible */
void monde_visage(const pet_state_t *p, int expr_id, const monde_info_t *mi)
{
    draw_bat_head(&EXPR[expr_id]);
    bandeau_jauges(p, mi, BANDEAU_Y);
}
