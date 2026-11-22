/*  apercu de l'ecran TIMER.
 *  g++ -O2 -o /tmp/apercu_timer tools/apercu_timer.cpp main/art.cpp -lm
 */
#include "../main/art.h"
#include <stdio.h>
#include <string.h>

static unsigned char fb[ART_FB_LEN];

static void pgm(const char *f)
{
    FILE *o = fopen(f, "wb");
    fprintf(o, "P5\n200 200\n255\n");
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 200; x++) {
            int bit = (fb[y * 25 + (x >> 3)] >> (7 - (x & 7))) & 1;
            unsigned char v = bit ? 255 : 0;
            fwrite(&v, 1, 1, o);
        }
    fclose(o);
    printf("%s ecrit\n", f);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "liste";
    art_begin(fb, 0);
    art_clear();

    if (!strcmp(mode, "vide")) {
        ui_ecran_timer("TIMER", "LONG 1 = AJOUTE",
                       NULL, NULL, NULL, 0, -1,
                       "LONG 1 = AJOUTE UN TIMER", "LONG 2 = DUREE  5 A 60 MIN");
    } else if (!strcmp(mode, "sonne")) {
        const char *noms[] = {"Cuisson", "Sieste"};
        const char *sous[] = {"sonne !", "8:12 / 15:00"};
        int pcts[] = {100, 55};
        ui_ecran_timer("TIMER", "LONG 1 = ARRETE", noms, sous, pcts, 2, 0,
                       "N'IMPORTE QUEL BOUTON ARRETE", "PRESET 15 MIN");
    } else {
        const char *noms[] = {"Cuisson", "Sieste", "Reveil"};
        const char *sous[] = {"2:31 / 5:00", "8:12 / 15:00", "dans 42 min"};
        int pcts[] = {50, 45, 30};
        ui_ecran_timer("TIMER", "LONG 1 = AJOUTE", noms, sous, pcts, 3, 1,
                       "LONG 1 = AJOUTE   LONG 2 = DUREE", "PRESET 15 MIN");
    }
    pgm(argc > 2 ? argv[2] : "/tmp/timer.pgm");
    return 0;
}
