/*  petart.h - le monde, le sprite qui evolue et le bandeau de jauges. */
#ifndef PETART_H_INCLUDED
#define PETART_H_INCLUDED

#include "pet.h"
#include <stdint.h>

/* etat d'affichage du monde */
typedef struct {
    int cpu_mhz;
    int ram_libre_ko;
    int psram_libre_ko;
    int sd_go;
    int sd_libre_go;
    int heure, minute;
    bool wifi_ok;
    float temp;
} monde_info_t;

/* dessine le monde + le personnage + le bandeau du bas (200x200) */
void monde_dessine(const pet_state_t *p, const monde_info_t *mi);

/* juste le sprite (reutilisable : menus, ecran de reveil...) */
void sprite_bat(int cx, int sol_y, int stade, int forme, int frame, int dir, int endormi);

/* grosse tete du personnage (page visage) : remplit l'ecran sous la barre */
void monde_visage(const pet_state_t *p, int expr_id, const monde_info_t *mi);

/* barre de jauges seule (y0 = haut du bandeau) */
void bandeau_jauges(const pet_state_t *p, const monde_info_t *mi, int y0);

#endif /* PETART_H_INCLUDED */
