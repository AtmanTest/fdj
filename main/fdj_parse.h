/*  fdj_parse.h — lecture des flux vivants (sports + info), sans ESP-IDF.
 *
 *  Entrees : le corps d'une reponse HTTP, tel quel.
 *  Sorties : fdj_live_t / fdj_flux_t, deja plies en ASCII et bornes.
 *  Aucune allocation : on ecrit dans les tableaux de l'appelant.
 *
 *  Teste sur des fixtures reelles (tests/fixtures) par tests/unit/test_parse.cpp :
 *  le meme JSON que celui qu'envoie le serveur.
 */
#ifndef FDJ_PARSE_H
#define FDJ_PARSE_H

#include "fdj_model.h"

/* TheSportsDB : .../livescore.php  ou  .../eventsday.php
   both_live = false -> garde aussi les matchs a venir (score vide). */
int fdj_parse_events(const char *json, bool garder_a_venir, fdj_live_t *out);

/* flux RSS franceinfo : les <title> des <item>, entites decodees */
int fdj_parse_news(const char *xml, fdj_flux_t *out);

/* decode &amp; &#233; &quot; ... en ASCII simple (utilise par le RSS) */
void fdj_entites(char *s);

#endif /* FDJ_PARSE_H */
