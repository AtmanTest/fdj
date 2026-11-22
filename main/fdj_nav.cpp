/*  fdj_nav.cpp — traduction geste -> intention. Aucune dependance materielle. */
#include "fdj_nav.h"

void fdj_nav_init(fdj_nav_t *n, uint8_t page, uint8_t curseur)
{
    if (!n) return;
    n->page = page < FDJ_PAGES ? page : 0;
    n->curseur = curseur;
    n->mode = FDJ_MODE_NAV;
    n->chiffres[0] = n->chiffres[1] = n->chiffres[2] = n->chiffres[3] = 0;
    n->pos = 0;
    n->oui = false;
    n->accueil_fait = false;
}

void fdj_nav_mode(fdj_nav_t *n, uint8_t mode)
{
    if (!n) return;
    n->mode = mode;
    n->pos = 0;
    n->accueil_fait = false;
    n->oui = false;
    if (mode == FDJ_MODE_CODE)
        n->chiffres[0] = n->chiffres[1] = n->chiffres[2] = n->chiffres[3] = 0;
}

uint16_t fdj_nav_code(const fdj_nav_t *n)
{
    if (!n) return 0;
    return (uint16_t)(n->chiffres[0] * 1000 + n->chiffres[1] * 100 +
                      n->chiffres[2] * 10 + n->chiffres[3]);
}

/* le LONG 3 s est signale par un REPEAT dont la duree depasse 3000 ms :
   geste.h emet LONG a 600 ms puis un REPEAT toutes les 400 ms. */
static bool tenu_3s(uint8_t type, uint16_t duree_ms)
{
    return (type == GESTE_REPEAT || type == GESTE_LONG) && duree_ms >= 3000;
}

fdj_act_t fdj_nav_geste(fdj_nav_t *n, int items, uint8_t btn, uint8_t type,
                        uint16_t duree_ms)
{
    if (!n) return FDJ_ACT_AUCUN;
    if (items < 1) items = 1;

    /* ---- mode confirmation : B1 inverse, B2x2 confirme ---- */
    if (n->mode == FDJ_MODE_CONFIRME) {
        if (btn == 0 && type == GESTE_COURT)   n->oui = !n->oui;
        if (btn == 1 && type == GESTE_DOUBLE)  return n->oui ? FDJ_ACT_CONFIRME : FDJ_ACT_CODE_ANNULE;
        if (btn == 1 && tenu_3s(type, duree_ms)) return FDJ_ACT_CODE_ANNULE;
        return FDJ_ACT_AUCUN;
    }

    /* ---- mode saisie de code ---- */
    if (n->mode == FDJ_MODE_CODE) {
        if (btn == 1 && tenu_3s(type, duree_ms)) return FDJ_ACT_CODE_ANNULE;
        if (btn == 0) {
            if (type == GESTE_COURT) {
                n->chiffres[n->pos] = (uint8_t)((n->chiffres[n->pos] + 1) % 10);
            } else if (type == GESTE_DOUBLE) {
                n->chiffres[n->pos] = (uint8_t)((n->chiffres[n->pos] + 9) % 10);
            } else if (type == GESTE_LONG) {
                return FDJ_ACT_CODE_ANNULE;
            }
            return FDJ_ACT_AUCUN;
        }
        if (type == GESTE_COURT)  { n->pos = (uint8_t)((n->pos + 1) % 4); return FDJ_ACT_AUCUN; }
        if (type == GESTE_DOUBLE) return FDJ_ACT_CODE_OK;
        return FDJ_ACT_AUCUN;
    }

    /* ---- navigation normale ---- */
    if (btn == 1) {
        if (tenu_3s(type, duree_ms)) {            /* B2 tenu : page 1 */
            if (n->accueil_fait) return FDJ_ACT_AUCUN;
            n->accueil_fait = true;
            n->page = 0;
            n->curseur = 0;
            return FDJ_ACT_ACCUEIL;
        }
        n->accueil_fait = false;
        if (type == GESTE_DOUBLE) return FDJ_ACT_PAGE;
        if (type == GESTE_COURT) {
            n->curseur = (uint8_t)(n->curseur + 1);
            if (n->curseur >= items) {            /* fin de page : on tourne */
                n->page = (uint8_t)((n->page + 1) % FDJ_PAGES);
                n->curseur = 0;
            }
            return FDJ_ACT_SUIVANT;
        }
        if (type == GESTE_REPEAT) {               /* appui maintenu : defile */
            n->curseur = (uint8_t)(n->curseur + 1);
            if (n->curseur >= items) { n->page = (uint8_t)((n->page + 1) % FDJ_PAGES); n->curseur = 0; }
            return FDJ_ACT_SUIVANT;
        }
        return FDJ_ACT_AUCUN;
    }

    /* bouton 1 */
    if (type == GESTE_COURT)  return FDJ_ACT_COCHER;
    if (type == GESTE_DOUBLE) return FDJ_ACT_DECOCHER;
    if (type == GESTE_LONG)   {                   /* page precedente */
        n->page = (uint8_t)((n->page + FDJ_PAGES - 1) % FDJ_PAGES);
        n->curseur = 0;
        return FDJ_ACT_PRECEDENT;
    }
    return FDJ_ACT_AUCUN;
}

