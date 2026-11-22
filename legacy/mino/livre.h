/*  livre.h - le lecteur de livres (e-reader) du robot.
 *
 *  Le texte est ramene a UN OCTET PAR CARACTERE AFFICHE (indice dans la
 *  police 5x10) : la pagination compte alors des colonnes, pas des octets
 *  UTF-8, et le rendu ne fait plus aucune conversion. Tout est en memoire
 *  (PSRAM sur la carte, tas du PC dans l'apercu), donc la tourne de page est
 *  instantanee : aucune lecture SD entre deux pages.
 *
 *  Pur C++ sans dependance ESP-IDF (sauf l'allocateur PSRAM, choisi a la
 *  compilation) : testable et affichable sur le PC.
 */
#ifndef LIVRE_H_INCLUDED
#define LIVRE_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#define LIVRE_TITRE_MAX   40
#define LIVRE_CHEMIN_MAX  96

typedef struct {
    uint8_t *txt;            /* texte encode, 1 octet = 1 glyphe */
    int      len;
    int32_t *lignes;         /* [nlignes][2] : debut, longueur */
    int      nlignes, cap;
    int      cols, rows;     /* colonnes par ligne, lignes par page */
    int      page;           /* page courante, 0 = premiere */
    char     nom[LIVRE_TITRE_MAX];   /* titre affiche (nom de fichier, sans .txt) */
    char     chemin[LIVRE_CHEMIN_MAX];
} livre_t;

/* charge et pagine. false si le fichier est absent, vide ou trop gros. */
bool livre_charge(livre_t *l, const char *chemin, int cols, int rows);
void livre_ferme(livre_t *l);

int  livre_pages(const livre_t *l);            /* nb de pages (>= 0) */
int  livre_pct(const livre_t *l);              /* 0..100 */
bool livre_va(livre_t *l, int page);           /* page absolue */
bool livre_avance(livre_t *l, int d);          /* relative */

/* ligne i du livre (0 = premiere ligne) ; renvoie le pointeur + la longueur */
const uint8_t *livre_ligne(const livre_t *l, int i, int *n);

/* --- rendu (la police 5x10) ------------------------------------- */
void font5_texte(int x, int y, const uint8_t *codes, int n, int black);
int  font5_texte_utf8(int x, int y, const char *s, int black);  /* renvoie la largeur */
int  font5_utf8_largeur(const char *s);

/* convertit de l'UTF-8 en codes de glyphes (accents compris, ponctuation
   typographique ramenee a l'ASCII). renvoie la longueur ecrite. */
int  livre_encode(const char *src, uint8_t *dst, int cap);

/* dessine la page courante en plein ecran (en-tete, texte, pied). Le meme
   code tourne sur la carte et dans l'apercu PNG du PC. */
void livre_dessine(const livre_t *l);

/* colonnes et lignes d'une page pour l'ecran 200x200 */
#define LIVRE_COLS 32
#define LIVRE_ROWS 16

#endif /* LIVRE_H_INCLUDED */
