/*  fdj_ui.h — les 20 pages de la maquette FDJ, dessinees en 1 bit.
 *
 *  Pur C++/art.h : aucun include ESP-IDF, donc le MEME code dessine sur la
 *  carte et dans l'apercu PNG du PC (tools/apercu_fdj.cpp). C'est ainsi que
 *  les 20 pages sont verifiees a l'oeil avant de flasher.
 *
 *  Une page = un titre en haut (toujours), une liste d'items navigables,
 *  un pied qui rappelle les gestes. Le curseur est un rectangle plein, la
 *  coche un carre a droite : lisible meme sans couleur.
 */
#ifndef FDJ_UI_H_INCLUDED
#define FDJ_UI_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include "fdj_model.h"

#define FDJ_ECRAN_W 200
#define FDJ_ECRAN_H 200

/* --- les 8 ecrans : un par role, pas de sous-menu ------------------- *
 * accueil, connexion, paris, coupon, compte, responsable, preuves,     *
 * reglages. Chaque ecran tient en une dalle de 200x200 et se lit a      *
 * 50 cm : rien en dessous de la police 5x10.                           */
typedef enum {
    FDJ_M_ACCUEIL = 0,
    FDJ_M_CONNEXION,
    FDJ_M_PARIS,
    FDJ_M_COUPON,
    FDJ_M_COMPTE,
    FDJ_M_RESP,
    FDJ_M_PREUVES,
    FDJ_M_REGLAGES,
    FDJ_M_NB
} fdj_module_t;

int  fdj_ecran_module(int page);        /* module affiche par cet ecran     */
bool fdj_ecran_est_intertitre(int page);
int  fdj_module_page(int module);       /* page de contenu d'un module      */
const char *fdj_module_nom(int module); /* nom court du module              */

/* ce que l'ecran a besoin de savoir du monde, rassemble par l'appli */
typedef struct {
    int page, curseur;         /* page affichee et item sous le curseur */
    const fdj_etat_t *e;
    const fdj_top_t  *top;
    const fdj_flux_t *news;
    const fdj_live_t *live;
    int64_t now;
    const char *ssid;
    bool wifi;
    int  bat_pct;              /* -1 = pas de batterie */
    int  rssi;
    uint32_t heap_int_libre;   /* octets */
    uint32_t heap_int_max;     /* plus gros bloc libre */
    uint32_t heap_int_total;   /* capacite reelle (heap_caps_get_total_size) */
    uint32_t psram_libre;
    uint32_t psram_total;
    bool  sd_ok;
    int   rendu_ms;            /* duree du dernier dessin complet   */
    int   ecran_ms;            /* duree du dernier rafraichissement */
    bool  code_affiche;        /* page 15 : mot de passe revele     */
    bool  gains_detail;        /* page 5 : ventilation par jeu      */
    const char *message;       /* bandeau temporaire, sinon NULL    */
    bool  heure_ok;            /* false : l'heure n'est pas synchronisee */
    /* grilles de loto du jour, calculees une fois par graine */
    uint8_t grilles[FDJ_GRILLE_MAX][FDJ_GRILLE_NB];
    uint8_t chances[FDJ_GRILLE_MAX];
    uint8_t nb_grilles;
} fdj_vue_t;

/* dessine une page entiere dans le framebuffer (art_begin deja appele) */
void fdj_ui_dessine(const fdj_vue_t *v, uint8_t *fb);

/* titre de la page, pour le journal et les tests */
const char *fdj_page_titre(int page);

/* aide de pied de page, propre a la page (coherente avec les gestes reels) */
const char *fdj_page_aide(int page);

/* nombre d'items navigables de la page (>= 1) */
int fdj_page_items(int page, const fdj_vue_t *v);

/* detail plein ecran d'un pari de l'historique (B1 sur une ligne) */
void fdj_ui_detail(uint8_t *fb, const fdj_pari_t *p, int idx);

/* saisie du code de demarrage / changement de code : par-dessus la page */
void fdj_ui_demande_code(uint8_t *fb, const char *titre, const uint8_t chiffres[4],
                         int position, bool erreur);

/* petite confirmation plein ecran (effacement RGPD) */
void fdj_ui_confirme(uint8_t *fb, const char *titre, const char *question,
                     bool oui_selectionne);

#endif /* FDJ_UI_H_INCLUDED */
