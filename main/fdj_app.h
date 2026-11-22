/*  fdj_app.h — le collage entre la carte et le modele.
 *  main.cpp ne connait que le materiel ; tout le reste est ici.
 */
#ifndef FDJ_APP_H
#define FDJ_APP_H

#include <stdint.h>
#include <stdbool.h>

/* ecran(fb 200x200 1 bit, plein = rafraichissement complet) */
typedef void (*fdj_ecran_fn)(const uint8_t *fb, bool plein);

void fdj_app_start(const char *ssid, bool sd_ok, fdj_ecran_fn ecran);

/* poussee par la tache des boutons (geste.h a deja classe le geste) */
void fdj_app_geste(uint8_t btn, uint8_t type, uint16_t duree_ms);

/* a appeler quand le wifi vient de se connecter (heure + donnees) */
void fdj_app_reseau_pret(void);

#endif /* FDJ_APP_H */
