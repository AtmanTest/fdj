/*  fdj_nav.h — la navigation a deux boutons, sans materiel, donc testable.
 *
 *  Le geste est lu par geste.h (deja teste separement) ; ici on le traduit en
 *  INTENTION. L'appli, elle, decide ce que "valider" veut dire sur la page 7.
 *
 *  TABLE DES GESTES (validee sur la maquette, documentee page 1) :
 *    BOUTON 2, appui court ..... item suivant ; apres le dernier, page suivante
 *    BOUTON 2, appui long ...... repetition : defilement rapide
 *    BOUTON 2, maintenu 3 s .... retour page 1 (le geste d'urgence)
 *    BOUTON 2, double appui .... valider la page (action principale)
 *    BOUTON 1, appui court ..... cocher / valider l'option du curseur  (+)
 *    BOUTON 1, double appui .... annuler / decocher                    (-)
 *    BOUTON 1, appui long ...... page precedente
 *
 *  Pourquoi ces choix : le bouton 2 ne fait QUE deplacer (jamais d'argent),
 *  le bouton 1 ne fait QUE decider (jamais de deplacement). On ne peut donc
 *  pas cocher un pari en croyant changer de page.
 */
#ifndef FDJ_NAV_H
#define FDJ_NAV_H

#include <stdint.h>
#include <stdbool.h>
#include "fdj_model.h"
#include "geste.h"

typedef enum {
    FDJ_ACT_AUCUN = 0,
    FDJ_ACT_SUIVANT,
    FDJ_ACT_PRECEDENT,
    FDJ_ACT_COCHER,        /* bouton 1 court : cocher l'item courant        */
    FDJ_ACT_DECOCHER,      /* bouton 1 double : annuler                    */
    FDJ_ACT_PAGE,          /* bouton 2 double : action principale de page  */
    FDJ_ACT_ACCUEIL,       /* bouton 2 tenu 3 s                            */
    FDJ_ACT_CODE_OK,       /* saisie du code terminee                      */
    FDJ_ACT_CODE_ANNULE,
    FDJ_ACT_CONFIRME,      /* confirmation RGPD acceptee                   */
} fdj_act_t;

typedef enum {
    FDJ_MODE_NAV = 0,
    FDJ_MODE_CODE,         /* saisie du code de demarrage                  */
    FDJ_MODE_CONFIRME,     /* "tout effacer ?"                             */
} fdj_mode_t;

typedef struct {
    uint8_t page, curseur;
    uint8_t mode;
    uint8_t chiffres[4];
    uint8_t pos;
    bool    oui;           /* confirmation : OUI selectionne               */
    bool    accueil_fait;  /* le LONG 3 s ne part qu'une fois              */
} fdj_nav_t;

void fdj_nav_init(fdj_nav_t *n, uint8_t page, uint8_t curseur);
void fdj_nav_mode(fdj_nav_t *n, uint8_t mode);

/* une passe : <items> = nombre d'items de la page courante.
   Rend l'intention ; met a jour page/curseur/mode. */
fdj_act_t fdj_nav_geste(fdj_nav_t *n, int items, uint8_t btn, uint8_t type,
                        uint16_t duree_ms);

/* le code saisi, quand FDJ_ACT_CODE_OK est revenu */
uint16_t fdj_nav_code(const fdj_nav_t *n);

#endif /* FDJ_NAV_H */
