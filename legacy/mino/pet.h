/*  pet.h - moteur du tamagotchi BATMino.
 *
 *  Regle d'or : cette boucle est LOCALE et DETERMINISTE. Elle ne depend ni du
 *  reseau ni du LLM. Le LLM ne fait que parler ; il ne touche jamais une jauge.
 *  Pur C++ sans dependance : testable sur le PC, tourne sur l'ESP32-S3.
 *
 *  Durees (issues de la recherche Tamagotchi, adaptees a du temps "actif"
 *  pour qu'un enfant voie une metamorphose dans une session) :
 *    oeuf 20 s -> bebe 20 min -> enfant 3 h -> ado 3 h -> adulte 4-10 j
 *    -> vieillard ~2 j -> depart (generation + 1)
 */
#ifndef PET_H_INCLUDED
#define PET_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ST_OEUF = 0, ST_BEBE, ST_ENFANT, ST_ADO, ST_ADULTE, ST_VIEILLARD, ST_ANGE, ST_NB
} pet_stade_t;

/* forme adulte obtenue selon les soins recus */
typedef enum { FORME_HEROS = 0, FORME_NORMAL, FORME_GROGNON, FORME_MALADIF, FORME_NB } pet_forme_t;

typedef enum {
    ACT_AUCUNE = 0, ACT_MANGER, ACT_JOUER, ACT_CALIN, ACT_NETTOYER, ACT_DORMIR,
    ACT_REVEILLER, ACT_SOIGNER
} pet_action_t;

typedef struct {
    /* temps */
    uint32_t age_s;              /* age en secondes de temps actif */
    uint32_t temps_joue_s;       /* duree de vie totale (pour le journal) */
    /* jauges en points : 100 points = 1 coeur, 400 = 4 coeurs */
    uint16_t faim;               /* 400 = repu */
    uint16_t bonheur;            /* 400 = ravi */
    uint16_t energie;            /* 400 = en forme */
    uint16_t proprete;           /* 400 = propre */
    uint8_t obeissance;          /* 0..100 -> affiche en 0..4 */
    /* compteurs caches : ce sont eux qui decident l'evolution */
    uint8_t care_mistakes;       /* un coeur vide et ignore > 15 min */
    uint16_t discipline_mistakes;
    uint8_t malade;              /* 0/1 */
    uint32_t malade_depuis_s;
    uint8_t caca;                /* 0..3 */
    uint32_t caca_depuis_s;
    uint8_t endormi;             /* 0/1 */
    uint16_t generation;
    uint16_t repas, jeux, calins, nettoyages;
    uint8_t forme;               /* pet_forme_t, fige a l'age adulte */
    uint8_t reveil_demande;      /* le pet appelle : il veut qu'on s'occupe de lui */
    uint32_t dernier_appel_s;
    /* monde : ou il se balade (coordonnees du framebuffer 200x200) */
    float x, y;
    int8_t dir;
    uint8_t pas;                 /* phase de marche 0..3 */
    uint32_t tempo_s;            /* temps avant le prochain pas */
    uint8_t humeur;              /* famille emotionnelle affichee */
    uint8_t humeur_reste_s;
    uint8_t parti;               /* 1 = il est parti (mort), on attend l'adieu */
    uint8_t jour;                /* jour depuis la naissance (journal) */
} pet_state_t;

/* remise a zero d'un oeuf (nouvelle generation) */
void pet_nait(pet_state_t *p, uint16_t generation);
/* une seconde de vie active : jauges, caca, maladie, evolution, mort */
void pet_tick(pet_state_t *p, uint32_t dt_s);
/* la nuit tombe : main.cpp previent le moteur (heure locale) */
void pet_set_nuit(pet_state_t *p, bool nuit, int heure);
/* action du joueur ; renvoie une ligne de journal (jamais NULL) */
const char *pet_action(pet_state_t *p, pet_action_t a);
/* ce que le pet veut (index d'action conseille), 0 si rien */
pet_action_t pet_besoin(const pet_state_t *p);
int pet_stade(const pet_state_t *p);        /* stade courant (deduit de l'age) */
const char *pet_stade_nom(int st);
const char *pet_forme_nom(int f);
uint8_t pet_age_coeurs(const pet_state_t *p);       /* obeissance 0..100 -> 0..4 */
/* etat de sante derive (0 = mourant, 100 = eclatant) */
uint8_t pet_sante(const pet_state_t *p);
/* phrase courte a afficher sous le personnage */
const char *pet_message(const pet_state_t *p);
/* sauvegarde compacte "cle=valeur" sur une ligne */
int pet_serialise(const pet_state_t *p, char *out, int max);
bool pet_deserialise(pet_state_t *p, const char *in);

#endif /* PET_H_INCLUDED */
