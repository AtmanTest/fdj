/*  apercu du petit compagnon.
 *  g++ -O2 -o /tmp/kirby_apercu tools/kirby_apercu.cpp main/art.cpp -lm
 *  /tmp/kirby_apercu grand|petit   puis ffmpeg -i /tmp/kirby_<x>.pgm -vf scale=... /tmp/kirby_<x>.png
 */
#include "../main/art.h"
#include <stdio.h>
#include <string.h>

static unsigned char fb[ART_FB_LEN];

int main(int argc, char **argv)
{
    const char *q = (argc > 1) ? argv[1] : "grand";
    char pgm[64], png[64];
    int grand = !strcmp(q, "grand");
    int r = grand ? 38 : 17;

    art_begin(fb, 0);
    art_clear();
    if (grand) {                       /* ravi / tranquille / triste / endormi, en grand */
        draw_kirby(52, 52, r, 0, 0);
        draw_kirby(148, 52, r, 3, 1);
        draw_kirby(52, 148, r, 5, 2);
        draw_kirby(148, 148, r, 8, 3);
    } else {                           /* la taille du tableau de bord : ravi / triste / endormi */
        draw_kirby(100, 30, r, 0, 0);
        draw_kirby(100, 80, r, 5, 2);
        draw_kirby(100, 130, r, 8, 3);
        draw_kirby(100, 178, r, 3, 1);
    }

    snprintf(pgm, sizeof(pgm), "/tmp/kirby_%s.pgm", q);
    snprintf(png, sizeof(png), "/tmp/kirby_%s.png", q);
    FILE *f = fopen(pgm, "wb");
    if (!f) return 1;
    fprintf(f, "P5\n%d %d\n255\n", ART_W, ART_H);
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_W; x++) {
            unsigned char b = art_fb()[y * (ART_W / 8) + (x / 8)];
            int bit = (b >> (7 - (x & 7))) & 1;
            unsigned char v = bit ? 255 : 0;
            fwrite(&v, 1, 1, f);
        }
    fclose(f);
    printf("ok : %s\n", pgm);
    (void)png;
    return 0;
}
