/*  pet.cpp - moteur du tamagotchi BATMino (voir pet.h).
 *  Aucune dependance : se compile pour l'ESP32-S3 et pour le PC.
 */
#include "pet.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* jauges en points : 100 points = 1 coeur, 400 = 4 coeurs */
#define PCOEUR 100
#define PLEIN 400

/* durees de stade en secondes de temps actif (le stade se deduit de l'age) */
static const uint32_t D_OEUF = 20;
static const uint32_t D_BEBE = 20 * 60;
static const uint32_t D_ENFANT = 3 * 3600;
static const uint32_t D_ADO = 3 * 3600;
static const uint32_t D_VIEUX = 2 * 24 * 3600;

static uint32_t duree_adulte(const pet_state_t *p)
{
    int q = 100 - (int)p->care_mistakes * 8 - (int)p->discipline_mistakes * 5;
    if (q < 0) q = 0;
    if (q > 100) q = 100;
    return 4u * 86400u + (uint32_t)q * (6u * 86400u) / 100u;
}

int pet_stade(const pet_state_t *p)
{
    uint32_t a;
    if (p->parti) return ST_ANGE;
    a = p->age_s;
    if (a < D_OEUF) return ST_OEUF;
    a -= D_OEUF;
    if (a < D_BEBE) return ST_BEBE;
    a -= D_BEBE;
    if (a < D_ENFANT) return ST_ENFANT;
    a -= D_ENFANT;
    if (a < D_ADO) return ST_ADO;
    a -= D_ADO;
    if (a < duree_adulte(p)) return ST_ADULTE;
    a -= duree_adulte(p);
    if (a < D_VIEUX) return ST_VIEILLARD;
    return ST_ANGE;
}

const char *pet_stade_nom(int st)
{
    switch (st) {
    case ST_OEUF:      return "OEUF";
    case ST_BEBE:      return "BEBE";
    case ST_ENFANT:    return "ENFANT";
    case ST_ADO:       return "ADO";
    case ST_ADULTE:    return "ADULTE";
    case ST_VIEILLARD: return "VIEUX";
    default:           return "ANGE";
    }
}

const char *pet_forme_nom(int f)
{
    switch (f) {
    case FORME_HEROS:    return "HEROS";
    case FORME_NORMAL:   return "BATMINO";
    case FORME_GROGNON:  return "GROGNON";
    default:             return "MALADIF";
    }
}

uint8_t pet_age_coeurs(const pet_state_t *p) { return (uint8_t)(p->obeissance / 25); }

uint8_t pet_sante(const pet_state_t *p)
{
    int s = 100;
    s -= (int)(PLEIN - p->faim) / 8;
    s -= (int)(PLEIN - p->bonheur) / 10;
    s -= (int)p->care_mistakes * 6;
    s -= (int)p->caca * 10;
    if (p->malade) s -= 35;
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    return (uint8_t)s;
}

void pet_nait(pet_state_t *p, uint16_t generation)
{
    memset(p, 0, sizeof(*p));
    p->generation = generation;
    p->faim = p->bonheur = p->energie = p->proprete = PLEIN;
    p->obeissance = 50;
    p->x = 100.0f;
    p->y = 0.0f;
    p->dir = 1;
    p->tempo_s = 2;
    p->humeur = 4;              /* surprise : l'eclosion */
    p->humeur_reste_s = 20;
}

const char *pet_message(const pet_state_t *p)
{
    if (p->malade) return "JE SUIS MALADE...";
    if (p->endormi) return "ZZZ...";
    if (p->faim <= 1 * PCOEUR) return "J'AI TRES FAIM !";
    if (p->caca > 0) return "BEURK, CA SENT...";
    if (p->bonheur <= 1 * PCOEUR) return "JE M'ENNUIE...";
    if (p->energie <= 1 * PCOEUR) return "JE TOMBE DE SOMMEIL";
    if (p->proprete <= 1 * PCOEUR) return "J'AIMERAIS UN BAIN";
    if (p->faim <= 2 * PCOEUR) return "J'AI FAIM";
    if (p->bonheur <= 3 * PCOEUR) return "ON JOUE ?";
    if (p->bonheur >= PLEIN && p->faim >= 3 * PCOEUR) return "JE SUIS TROP BIEN !";
    return "JE PATROUILLE";
}

pet_action_t pet_besoin(const pet_state_t *p)
{
    if (p->malade) return ACT_SOIGNER;
    if (p->faim <= 1 * PCOEUR) return ACT_MANGER;
    if (p->caca > 0 && p->proprete <= 2 * PCOEUR) return ACT_NETTOYER;
    if (p->bonheur <= 1 * PCOEUR) return ACT_JOUER;
    if (p->energie <= 1 * PCOEUR) return ACT_DORMIR;
    return ACT_AUCUNE;
}

static char journal_buf[8][128];

const char *pet_action(pet_state_t *p, pet_action_t a)
{
    static int tour = 0;
    char *b = journal_buf[tour % 8];
    tour++;
    switch (a) {
    case ACT_MANGER:
        p->repas++;
        p->faim = PLEIN;
        p->bonheur = (uint16_t)(p->bonheur + (PLEIN - p->bonheur) / 4);
        snprintf(b, 128, "repas %u : je me suis regale", (unsigned)p->repas);
        break;
    case ACT_JOUER:
        p->jeux++;
        p->bonheur = PLEIN;
        p->energie = (p->energie > 60) ? (uint16_t)(p->energie - 60) : 0;
        p->faim = (p->faim > 40) ? (uint16_t)(p->faim - 40) : 0;
        p->obeissance = (uint8_t)((p->obeissance + 12 > 100) ? 100 : p->obeissance + 12);
        snprintf(b, 128, "partie %u : on a bien rigole", (unsigned)p->jeux);
        break;
    case ACT_CALIN:
        p->calins++;
        p->bonheur = (uint16_t)((p->bonheur + 150 > PLEIN) ? PLEIN : p->bonheur + 150);
        p->obeissance = (uint8_t)((p->obeissance + 10 > 100) ? 100 : p->obeissance + 10);
        snprintf(b, 128, "calin %u... je me sens aime", (unsigned)p->calins);
        break;
    case ACT_NETTOYER:
        p->nettoyages++;
        p->caca = 0;
        p->proprete = PLEIN;
        snprintf(b, 128, "nettoyage %u : tout est propre", (unsigned)p->nettoyages);
        break;
    case ACT_DORMIR:
        p->endormi = 1;
        snprintf(b, 128, "je m'endors, bonne nuit");
        break;
    case ACT_REVEILLER:
        p->endormi = 0;
        p->energie = (uint16_t)((p->energie + 200 > PLEIN) ? PLEIN : p->energie + 200);
        snprintf(b, 128, "reveil en fanfare");
        break;
    case ACT_SOIGNER:
        if (p->malade) {
            p->malade = 0;
            p->malade_depuis_s = 0;
            p->obeissance = (uint8_t)((p->obeissance + 8 > 100) ? 100 : p->obeissance + 8);
            snprintf(b, 128, "soins : je respire mieux");
        } else {
            snprintf(b, 128, "je n'ai pas besoin de soins");
        }
        break;
    default:
        snprintf(b, 128, "on ne fait rien");
        break;
    }
    p->reveil_demande = 0;
    return b;
}

/* la nuit tombe : main.cpp appelle ceci quand l'heure locale passe 22 h ou 8 h */
void pet_set_nuit(pet_state_t *p, bool nuit, int heure);

static void evolution(pet_state_t *p, int nouveau_stade)
{
    int q = 100 - (int)p->care_mistakes * 8 - (int)p->discipline_mistakes * 5;
    if (q < 0) q = 0;
    if (q > 100) q = 100;
    if (nouveau_stade == ST_BEBE || nouveau_stade == ST_ENFANT || nouveau_stade == ST_ADO) {
        /* pas de forme definitive avant l'age adulte */
        p->humeur = 4;                 /* surprise : la metamorphose */
        p->humeur_reste_s = 60;
        return;
    }
    if (nouveau_stade == ST_ADULTE) {
        if (q >= 75)      p->forme = FORME_HEROS;
        else if (q >= 45) p->forme = FORME_NORMAL;
        else if (q >= 20) p->forme = FORME_GROGNON;
        else              p->forme = FORME_MALADIF;
        /* le heros est le seul a porter le logo et la cape complete */
        p->humeur = 9;                 /* fierte */
        p->humeur_reste_s = 120;
        return;
    }
    if (nouveau_stade == ST_VIEILLARD) {
        p->humeur = 8;
        p->humeur_reste_s = 120;
    }
}

void pet_set_nuit(pet_state_t *p, bool nuit, int heure)
{
    if (p->parti) return;
    if (nuit && !p->endormi) p->endormi = 1;
    if (!nuit && p->endormi && heure >= 7 && heure <= 9) {
        p->endormi = 0;
        p->energie = (uint16_t)((p->energie + 250 > PLEIN) ? PLEIN : p->energie + 250);
    }
}

void pet_tick(pet_state_t *p, uint32_t dt_s)
{
    int st_avant, st_apres;
    if (p->parti) { p->temps_joue_s += dt_s; return; }
    if (p->age_s == 0 && p->faim == 0) pet_nait(p, p->generation);

    st_avant = pet_stade(p);

    /* --- sommeil : pas de jeu la nuit, l'energie remonte --- */
    if (p->endormi) {
        int gain = (int)(dt_s * 100 / 900);      /* 1 coeur / 15 min de sommeil */
        p->energie = (uint16_t)((p->energie + gain > PLEIN) ? PLEIN : p->energie + gain);
        p->humeur = 8 + 4;                        /* blase/zen */
        p->age_s += dt_s;
        p->temps_joue_s += dt_s;
        return;
    }

    /* --- jauges --- */
    if (p->faim > 0)
        p->faim = (uint16_t)((dt_s * 100 / 2400 >= p->faim) ? 0 : p->faim - dt_s * 100 / 2400);
    if (p->bonheur > 0)
        p->bonheur = (uint16_t)((dt_s * 100 / 3000 >= p->bonheur) ? 0 : p->bonheur - dt_s * 100 / 3000);
    if (p->energie > 0)
        p->energie = (uint16_t)((dt_s * 100 / 3600 >= p->energie) ? 0 : p->energie - dt_s * 100 / 3600);
    if (p->caca > 0 && p->proprete > 0)
        p->proprete = (uint16_t)((dt_s * 100 / 1800 >= p->proprete) ? 0 : p->proprete - dt_s * 100 / 1800);

    /* --- caca : arrive 20 a 40 min apres un repas, 3 max --- */
    if (p->faim >= 3 * PCOEUR && p->caca < 3) {
        p->caca_depuis_s += dt_s;
        if (p->caca_depuis_s > 25 * 60) {
            p->caca++;
            p->caca_depuis_s = 0;
            p->proprete = (uint16_t)(p->proprete > 120 ? p->proprete - 120 : 0);
            p->reveil_demande = 1;
        }
    } else if (p->caca == 0) {
        p->caca_depuis_s = 0;
    }

    /* --- care mistakes : un coeur vide ignore plus de 15 min --- */
    if (p->faim == 0 || p->bonheur == 0 || p->proprete == 0 || p->energie == 0) {
        p->reveil_demande = 1;
        p->dernier_appel_s += dt_s;
        if (p->dernier_appel_s >= 15 * 60) {
            if (p->care_mistakes < 250) p->care_mistakes++;
            p->dernier_appel_s = 0;
        }
    } else {
        p->dernier_appel_s = 0;
    }

    /* --- maladie : negligence prolongee, caca qui traine, proprete au sol --- */
    if (!p->malade) {
        if (p->faim == 0 && p->care_mistakes >= 3) p->malade = 1;
        if (p->caca >= 2) p->malade = 1;
        if (p->proprete == 0 && p->caca > 0) p->malade = 1;
        if (p->malade) p->malade_depuis_s = 0;
    } else {
        p->malade_depuis_s += dt_s;
        p->humeur = 13;                     /* douleur */
        if (p->malade_depuis_s >= 2 * 3600) {   /* non soigne 2 h : il s'en va */
            p->parti = 1;
            p->humeur = 1;                      /* tristesse : l'adieu */
            p->humeur_reste_s = 200;
            return;
        }
    }

    /* --- age et metamorphose --- */
    p->age_s += dt_s;
    p->temps_joue_s += dt_s;
    st_apres = pet_stade(p);
    if (st_apres == ST_ANGE) {           /* fin de vie naturelle : il s'en va */
        p->parti = 1;
        p->humeur = 1;
        p->humeur_reste_s = 200;
        return;
    }
    if (st_apres != st_avant && st_avant != ST_VIEILLARD) evolution(p, st_apres);

    /* --- humeur : le moteur choisit la famille, l'ecran choisit la variante --- */
    if (p->humeur_reste_s > dt_s) {
        p->humeur_reste_s -= (uint8_t)dt_s;
    } else {
        pet_action_t b = pet_besoin(p);
        p->humeur_reste_s = 20;
        switch (b) {
        case ACT_MANGER:   p->humeur = 1; break;   /* tristesse */
        case ACT_JOUER:    p->humeur = 7; break;   /* reflexion/ennui */
        case ACT_NETTOYER: p->humeur = 5; break;   /* degout */
        case ACT_DORMIR:   p->humeur = 13; break;  /* fatigue */
        case ACT_SOIGNER:  p->humeur = 13; break;
        default:
            if (pet_sante(p) >= 70) p->humeur = 0;         /* joie */
            else if (pet_sante(p) >= 40) p->humeur = 8;    /* calme */
            else p->humeur = 1;
            break;
        }
    }

    /* --- balade : un pas toutes les 2 a 4 s --- */
    if (p->tempo_s > dt_s) {
        p->tempo_s -= dt_s;
    } else {
        p->tempo_s = 2 + (uint32_t)(rand() % 3);
        if ((rand() % 5) == 0) p->dir = (int8_t)(-p->dir);
        p->pas = (uint8_t)((p->pas + 1) & 3);
        p->x += 6.0f * (float)p->dir;
        if (p->x < 34.0f)  { p->x = 34.0f;  p->dir = 1; }
        if (p->x > 166.0f) { p->x = 166.0f; p->dir = -1; }
        if (p->age_s / 86400 != p->jour) p->jour = (uint8_t)(p->age_s / 86400);
    }
}

/* --- persistance compacte --- */
int pet_serialise(const pet_state_t *p, char *out, int max)
{
    return snprintf(out, max,
        "v=1 age=%u faim=%u bonheur=%u energie=%u proprete=%u obe=%u cm=%u dm=%u "
        "mal=%u mald=%u caca=%u cacad=%u dort=%u gen=%u rep=%u jeux=%u cal=%u net=%u "
        "forme=%u jour=%u x=%.1f dir=%d cm2=%u",
        (unsigned)p->age_s, (unsigned)p->faim, (unsigned)p->bonheur, (unsigned)p->energie,
        (unsigned)p->proprete, (unsigned)p->obeissance, (unsigned)p->care_mistakes,
        (unsigned)p->discipline_mistakes, (unsigned)p->malade, (unsigned)p->malade_depuis_s,
        (unsigned)p->caca, (unsigned)p->caca_depuis_s, (unsigned)p->endormi,
        (unsigned)p->generation, (unsigned)p->repas, (unsigned)p->jeux,
        (unsigned)p->calins, (unsigned)p->nettoyages, (unsigned)p->forme,
        (unsigned)p->jour, (double)p->x, (int)p->dir, (unsigned)p->parti);
}

bool pet_deserialise(pet_state_t *p, const char *in)
{
    char buf[512];
    char *tok, *save = NULL;
    if (!in || !*in || strlen(in) > 500) return false;
    snprintf(buf, sizeof(buf), "%s", in);
    for (tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        char *eq = strchr(tok, '=');
        long v;
        if (!eq) continue;
        *eq = 0;
        v = strtol(eq + 1, NULL, 10);
        if      (!strcmp(tok, "age")) p->age_s = (uint32_t)v;
        else if (!strcmp(tok, "faim")) p->faim = (uint8_t)v;
        else if (!strcmp(tok, "bonheur")) p->bonheur = (uint8_t)v;
        else if (!strcmp(tok, "energie")) p->energie = (uint8_t)v;
        else if (!strcmp(tok, "proprete")) p->proprete = (uint8_t)v;
        else if (!strcmp(tok, "obe")) p->obeissance = (uint8_t)v;
        else if (!strcmp(tok, "cm")) p->care_mistakes = (uint8_t)v;
        else if (!strcmp(tok, "dm")) p->discipline_mistakes = (uint8_t)v;
        else if (!strcmp(tok, "mal")) p->malade = (uint8_t)v;
        else if (!strcmp(tok, "mald")) p->malade_depuis_s = (uint32_t)v;
        else if (!strcmp(tok, "caca")) p->caca = (uint8_t)v;
        else if (!strcmp(tok, "cacad")) p->caca_depuis_s = (uint32_t)v;
        else if (!strcmp(tok, "dort")) p->endormi = (uint8_t)v;
        else if (!strcmp(tok, "gen")) p->generation = (uint16_t)v;
        else if (!strcmp(tok, "rep")) p->repas = (uint16_t)v;
        else if (!strcmp(tok, "jeux")) p->jeux = (uint16_t)v;
        else if (!strcmp(tok, "cal")) p->calins = (uint16_t)v;
        else if (!strcmp(tok, "net")) p->nettoyages = (uint16_t)v;
        else if (!strcmp(tok, "forme")) p->forme = (uint8_t)v;
        else if (!strcmp(tok, "jour")) p->jour = (uint8_t)v;
        else if (!strcmp(tok, "dir")) p->dir = (int8_t)v;
        else if (!strcmp(tok, "cm2")) p->parti = (uint8_t)v;
        else if (!strcmp(tok, "x")) p->x = (float)atof(eq + 1);
    }
    return true;
}
