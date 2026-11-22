/*  apercu_tableau.cpp - rend la page unique sur le PC et ecrit un PNG.
 *
 *  g++ -O2 -o /tmp/apercu_tableau tools/apercu_tableau.cpp main/tableau.cpp \
 *      main/art.cpp main/petart.cpp main/font5.cpp -I main -lm
 *  /tmp/apercu_tableau plein  /tmp/tableau_plein
 *  /tmp/apercu_tableau vide   /tmp/tableau_vide
 *
 *  Ecrit <sortie>.fb (5000 octets, 1 bit/px) ; fb2png.py en fait une image.
 */
#include "art.h"
#include "tableau.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t fbuf[ART_FB_LEN];

static void ecrit(const char *base)
{
    char p[256];
    snprintf(p, sizeof(p), "%s.fb", base);
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); exit(1); }
    fwrite(fbuf, 1, ART_FB_LEN, f);
    fclose(f);
    printf("%s\n", p);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "plein";
    const char *out = argc > 2 ? argv[2] : "/tmp/tableau";

    tableau_t t;
    memset(&t, 0, sizeof(t));
    art_tables_init();

    if (!strcmp(mode, "vide")) {
        /* tout ce qui peut manquer manque : la page doit rester lisible */
        t.heure = 7; t.minute = 3;
        t.sem = 3; t.jour = 11; t.mois = 10;
        t.wifi = false;
        t.bat_pct = -1;
        t.capteur = false;
        t.meteo = false; t.dem_min = -999;
    } else {
        t.heure = 18; t.minute = 54;
        t.sem = 3; t.jour = 11; t.mois = 10;
        t.wifi = true; t.rssi = -52;
        t.bat_pct = 36;
        t.capteur = true; t.temp_int = 24.6f;
        t.meteo = true; t.meteo_c = 13.6f;
        snprintf(t.meteo_txt, sizeof(t.meteo_txt), "NUAGEUX");
        t.dem_min = 9; t.dem_max = 14; t.dem_pluie = 20;
        snprintf(t.rdv_quand, sizeof(t.rdv_quand), "DEMAIN 09:00");
        snprintf(t.rdv_qui, sizeof(t.rdv_qui), "DENTISTE");
    }

    art_begin(fbuf, 0);
    art_clear();
    if (!strcmp(mode, "astuce")) {
        astuce_dessine(
            "When to unfollow, live or offline? Desactive les alertes de follow "
            "pendant que tu joues : ton chat reste lisible.",
            "R/TWITCH", "12 NOV 06H07", true);
    } else if (!strcmp(mode, "astuce-vide")) {
        astuce_dessine("", "", "", false);
    } else {
        tableau_dessine(&t);
    }
    ecrit(out);
    return 0;
}
