/*  fdj_model.h — le modele metier de l'application FDJ du BATMino.
 *
 *  AUCUN include ESP-IDF ici : ce fichier (et son .cpp) se compilent sur le PC,
 *  c'est ce qui rend la logique testable sans carte (tests/unit/test_modele.cpp).
 *
 *  Ce qui vit ici, et pas dans l'ecran :
 *    - l'etat persistant : compte, code de demarrage, profils, historique ;
 *    - les regles d'argent : plafonds semaine/mois PAR PROFIL, solde, mise ;
 *    - le modele sportif : Poisson applique au score et a la minute d'un match
 *      en direct, pour classer les 10 meilleures selections ;
 *    - le generateur de grilles de loto et le tirage aleatoire (xorshift) ;
 *    - les statistiques gains/pertes sur 7, 30 et 365 jours.
 *
 *  Tout est deterministe : a graine et entree egales, la sortie est egale.
 *  C'est ce qui permet de tester un tirage "aleatoire".
 */
#ifndef FDJ_MODEL_H
#define FDJ_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------- */
/* limites (taillees pour 320 Ko de RAM interne + 8 Mo de PSRAM)     */
/* ---------------------------------------------------------------- */
#define FDJ_PAGES             8
#define FDJ_PROFILS_MAX       4
#define FDJ_HIST_MAX         32
#define FDJ_HIST_PAGE         6     /* 6 paris visibles par page d'historique */
#define FDJ_MATCH_MAX        16
#define FDJ_SEL_MAX          10
#define FDJ_GRILLE_NB         5     /* 5 numeros au Loto */
#define FDJ_GRILLE_MAX        3     /* 3 grilles jouables d'un coup */
#define FDJ_TXT_L          26
#define FDJ_TXT_M          14
#define FDJ_TXT_S          10
#define FDJ_NEWS_MAX          6
#define FDJ_NEWS_LEN        118

/* ---------------------------------------------------------------- */
/* jeux FDJ (page 19 : les regles, page 3 : le loto)                 */
/* ---------------------------------------------------------------- */
typedef enum {
    FDJ_JEU_SPORT = 0,      /* Paris sportifs (1/N/2, plus/moins, BTTS) */
    FDJ_JEU_LOTO,           /* Loto : 5/49 + numero chance */
    FDJ_JEU_EUROMILLIONS,   /* 5/50 + 2 etoiles */
    FDJ_JEU_KENO,           /* 20 numeros sur 70 */
    FDJ_JEU_ILLIKO,         /* loterie immediate */
    FDJ_JEU_POKER,          /* poker en ligne */
    FDJ_JEU_COURSES,        /* courses hippiques */
    FDJ_JEU_CRILL,          /* Cash/Crillon */
    FDJ_JEU_NB
} fdj_jeu_t;

/* ---------------------------------------------------------------- */
/* profils : 4 maximum, chacun avec SON plafond semaine et mois     */
/* ---------------------------------------------------------------- */
typedef struct {
    char  nom[12];          /* "DEFAUT", "DEBUTANT", "EXPERT", ... */
    char  emoji;            /* 1 caractere ASCII affichable (m/, e/, x/) */
    uint8_t niveau;         /* 0 debutant, 1 regulier, 2 expert */
    uint32_t plafond_sem_ct;   /* centimes : 2500 = 25,00 EUR / 7 jours   */
    uint32_t plafond_mois_ct;  /* centimes : 6000 = 60,00 EUR / 30 jours  */
    uint32_t solde_ct;         /* centimes disponibles                   */
    bool  cree;
} fdj_profil_t;

/* ---------------------------------------------------------------- */
/* un pari (ou une grille) passe : la seule source de verite de      */
/* l'historique, et donc des depenses et des gains.                  */
/* ---------------------------------------------------------------- */
typedef struct {
    char     libelle[FDJ_TXT_L + 2];   /* "TROYES-LE HAVRE  +2,5"       */
    char     code[4];                  /* code aleatoire du coupon "7KQ" */
    uint32_t mise_ct;
    uint32_t gain_ct;                  /* 0 tant que non gagne          */
    int64_t  t;                        /* epoch secondes                */
    uint8_t  jeu;                      /* fdj_jeu_t                     */
    uint8_t  statut;                   /* fdj_pari_statut_t             */
} fdj_pari_t;

typedef enum {
    FDJ_PARI_EN_COURS = 0,
    FDJ_PARI_GAGNE,
    FDJ_PARI_PERDU,
} fdj_pari_statut_t;

/* ---------------------------------------------------------------- */
/* l'etat complet, sauvegarde en NVS d'un bloc (une seule ecriture)  */
/* ---------------------------------------------------------------- */
typedef struct {
    uint32_t magic;               /* 'FDJ1' */
    uint16_t version;             /* FDJ_ETAT_VERSION */

    /* --- compte --- */
    char     compte_nom[16];      /* "T. ZOU" */
    char     compte_id[10];       /* "FDJ-0001" */
    uint16_t code_boot;           /* 0..9999, 0000 par defaut */
    bool     code_perso;          /* false = code d'usine 0000 */
    bool     connecte;            /* session ouverte sur ce compte   */

    /* --- profils --- */
    uint8_t      profil_actif;
    uint8_t      nb_profils;
    fdj_profil_t profils[FDJ_PROFILS_MAX];

    /* --- historique (anneau) --- */
    fdj_pari_t hist[FDJ_HIST_MAX];
    uint8_t    hist_n;
    uint8_t    hist_next;         /* prochaine case a ecrire */

    /* --- options --- */
    bool news_auto;               /* un flash info toutes les 3 h */
    bool gros_texte;              /* accessibilite : lignes principales agrandies */
    bool mode_demo;               /* donnees de demonstration, clairement marquees */
    bool verif_age;               /* majorite verifiee */
    bool pause_24h;               /* jeu responsable : auto-exclusion */
    int64_t pause_debut;          /* debut de la pause ; expire apres 24 h */

    /* --- divers --- */
    uint32_t graine;              /* etat du xorshift, avance a chaque boot */
    uint32_t boots;
    uint8_t  page, curseur;       /* ou on en etait */
    uint16_t sel_cochee;          /* bitmask des selections cochées (page 2) */
    uint8_t  grille_cochee;       /* bitmask des grilles de loto cochées    */
    uint8_t  news_lu;             /* bitmask des titres lus                 */
    uint8_t  detail_ouvert;       /* page 4 : entree dont le detail est ouvert */
    uint8_t  hist_off;            /* page 4 : premier pari affiche (pagination) */
} fdj_etat_t;

#define FDJ_ETAT_VERSION 6
#define FDJ_MAGIC 0x46444A31u    /* "FDJ1" */
#define FDJ_VERSION "5.0.1"      /* doit suivre le fichier VERSION */

/* ---------------------------------------------------------------- */
/* donnees vivantes : matchs en direct + titres + mesures           */
/* ---------------------------------------------------------------- */
typedef struct {
    char    ligue[FDJ_TXT_M + 2];
    char    dom[FDJ_TXT_S + 2];
    char    ext[FDJ_TXT_S + 2];
    char    info[8];              /* "63'" en direct, "18:00" a venir */
    int     buts_dom, buts_ext;
    int     minute;               /* 0..90, 90 = fin */
    bool    live;                 /* true = en cours, false = a venir */
} fdj_match_t;

typedef struct {
    fdj_match_t m[FDJ_MATCH_MAX];
    int         n;
    bool        ok;               /* false : aucune donnee fraiche */
    int64_t     maj;              /* epoch de la derniere reponse */
    uint32_t    essais;
} fdj_live_t;

typedef struct {
    char  match[FDJ_TXT_L + 2];   /* "TROYES-LE HAVRE" */
    char  marche[FDJ_TXT_S + 2];  /* "1", "N", "2", "+2,5", "BTTS O" */
    char  ligue[FDJ_TXT_S + 2];   /* "LIGUE 1" */
    char  code[4];                /* code aleatoire "7KQ" */
    float proba;                  /* 0..1, modele Poisson */
    float score;                  /* proba + priorite championnat (tri)   */
    uint8_t idx_match;
    uint8_t coche;
} fdj_sel_t;

typedef struct {
    fdj_sel_t s[FDJ_SEL_MAX];
    int       n;
    int64_t   calcule;            /* epoch du calcul */
    bool      vide;
} fdj_top_t;

typedef struct {
    char titre[FDJ_NEWS_LEN];
    char source[FDJ_TXT_S + 2];
    char quand[FDJ_TXT_S + 2];    /* "12:40" */
} fdj_news_t;

typedef struct {
    fdj_news_t n[FDJ_NEWS_MAX];
    int        nb;
    bool       ok;
    int64_t    maj;
} fdj_flux_t;

/* gains/pertes par periode */
typedef struct {
    uint32_t mise_ct, gain_ct;
    int32_t  net_ct;
    int      nb;
} fdj_periode_t;

/* ---------------------------------------------------------------- */
/* cycle de vie de l'etat                                           */
/* ---------------------------------------------------------------- */
void fdj_etat_defaut(fdj_etat_t *e, uint32_t graine);
bool fdj_etat_valide(const fdj_etat_t *e);
uint32_t fdj_crc32(const void *p, size_t n);

/* serialisation : blob autonome, versionne, avec CRC.
   rend le nombre d'octets ecrits, 0 si le tampon est trop petit. */
size_t fdj_etat_serialise(const fdj_etat_t *e, uint8_t *out, size_t cap);
bool   fdj_etat_deserialise(fdj_etat_t *e, const uint8_t *in, size_t len);

/* ---------------------------------------------------------------- */
/* compte / code de demarrage                                       */
/* ---------------------------------------------------------------- */
bool fdj_code_verifie(const fdj_etat_t *e, uint16_t code);
bool fdj_code_change(fdj_etat_t *e, uint16_t code);
bool fdj_code_valide(uint16_t code);              /* 0..9999 */

/* ---------------------------------------------------------------- */
/* profils                                                          */
/* ---------------------------------------------------------------- */
int  fdj_profil_ajoute(fdj_etat_t *e, const char *nom, uint8_t niveau,
                       uint32_t plafond_sem_ct, uint32_t plafond_mois_ct);
bool fdj_profil_selectionne(fdj_etat_t *e, uint8_t idx);
/* supprime un profil NON actif et tasse le tableau. 0 = fait,
   1 = refuse (actif), 2 = index invalide. */
int  fdj_profil_supprime(fdj_etat_t *e, uint8_t idx);
fdj_profil_t *fdj_profil_actif(fdj_etat_t *e);
const fdj_profil_t *fdj_profil_actif_c(const fdj_etat_t *e);

/* ---------------------------------------------------------------- */
/* argent : depenses reelles (historique), plafonds, mises          */
/* ---------------------------------------------------------------- */
void fdj_depenses(const fdj_etat_t *e, int64_t now,
                  uint32_t *sem_ct, uint32_t *mois_ct);
/* reponses de la page jeu responsable : 0..5 (bits 8..12 de sel_cochee) */
int  fdj_protection_score(const fdj_etat_t *e);
/* plafond semaine effectif : divise par deux sous 3 reponses de protection */
uint32_t fdj_plafond_sem_effectif(const fdj_etat_t *e);
int  fdj_mise_verifie(const fdj_etat_t *e, int64_t now, uint32_t mise_ct);
/* 0 = ok ; 1 = mise nulle/trop grande ; 2 = plafond semaine ;
   3 = plafond mois ; 4 = solde insuffisant ; 5 = pause jeu responsable  */
/* valide le coupon : verifie le budget, engage les mises, ecrit l'historique.
   Rend 0 si accepte, sinon le code de refus de fdj_mise_verifie.
   msg recoit une phrase courte, prete a afficher. */
int  fdj_coupon_valide(fdj_etat_t *e, const fdj_top_t *top,
                       const uint8_t grilles[][FDJ_GRILLE_NB],
                       const uint8_t *chances, int nb_grilles,
                       int64_t now, char *msg, size_t cap);

int  fdj_pari_ajoute(fdj_etat_t *e, const char *libelle, const char *code,
                     uint32_t mise_ct, uint32_t gain_pot_ct, int64_t now,
                     uint8_t jeu, uint8_t statut);
void fdj_pari_regle(fdj_etat_t *e, uint8_t idx, bool gagne);   /* solde + gain */
void fdj_periode_calcule(const fdj_etat_t *e, int64_t now, int jours,
                         fdj_periode_t *out);
int  fdj_hist_index_recence(const fdj_etat_t *e, int k);  /* 0 = plus recent */

/* pause de jeu responsable : active seulement 24 h, expiree toute seule.
   fdj_pause_verifie() leve le drapeau si l'echeance est passee (horloge
   valide seulement : sans heure synchronisee, now <= 0 ne fait rien). */
bool fdj_pause_active(const fdj_etat_t *e, int64_t now);
void fdj_pause_active_maj(fdj_etat_t *e, int64_t now);
int64_t fdj_pause_restant_s(const fdj_etat_t *e, int64_t now);

/* ---------------------------------------------------------------- */
/* hasard : xorshift32, uniforme sans biais modulo                   */
/* ---------------------------------------------------------------- */
uint32_t fdj_rng_next(uint32_t *s);
uint32_t fdj_rng_below(uint32_t *s, uint32_t n);     /* [0, n[ sans biais */
void     fdj_code_aleatoire(uint32_t *s, char out[4]); /* "7KQ" */

/* ---------------------------------------------------------------- */
/* loto : grille equilibree (somme, pairs/impairs, bas/haut)        */
/* ---------------------------------------------------------------- */
bool fdj_loto_grille(uint32_t *s, uint8_t num[FDJ_GRILLE_NB], uint8_t *chance);

/* ---------------------------------------------------------------- */
/* modele sportif                                                   */
/* ---------------------------------------------------------------- */
float fdj_poisson_pmf(int k, float lambda);
float fdj_p_gagne(float ldom, float lext);        /* P(domicile gagne)  */
float fdj_p_nul(float ldom, float lext);
float fdj_p_plus_de(float lambda_total, int deja_marques, float seuil);
float fdj_p_btts(float ldom, float lext, int buts_dom, int buts_ext);
/* lambda restants d'un match, a partir du score et de la minute */
void  fdj_lambdas(int buts_dom, int buts_ext, int minute, float *ldom, float *lext);
/* classe les 10 meilleures selections de tous les matchs en direct.
   Rend le nombre de selections (0 si aucun match). */
int   fdj_top_calcule(const fdj_live_t *live, uint32_t *graine,
                      int64_t now, fdj_top_t *out);
uint32_t fdj_cote_milli(float proba);             /* cote "juste" x1000 */
const char *fdj_jeu_nom(uint8_t jeu);
const char *fdj_niveau_nom(uint8_t niveau);

/* ---------------------------------------------------------------- */
/* texte : pliage ASCII (les noms d'equipes arrivent en UTF-8)      */
/* ---------------------------------------------------------------- */
void fdj_ascii(const char *in, char *out, size_t cap);
void fdj_txt_copie(char *dst, size_t cap, const char *src);

#endif /* FDJ_MODEL_H */
