/*  tools/preview.cpp - apercu hote du rendu e-paper (meme code que le firmware).
 *  g++ -O2 -o preview preview.cpp ../main/art.cpp -lm -lz
 *  ./preview exprs  -> montage de toutes les expressions
 *  ./preview one <nom> [fichier]
 */
#include "../main/art.h"
#include "../main/petart.h"
#include "../main/pet.h"
#include "../main/livre.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void write_png(const char *path, const uint8_t *fb, int w, int h, int scale)
{
    int W = w * scale, H = h * scale, x, y;
    unsigned char *raw = (unsigned char *)malloc((size_t)H * ((size_t)W + 1));
    uLongf clen = compressBound((uLong)H * ((size_t)W + 1));
    unsigned char *comp = (unsigned char *)malloc(clen);
    FILE *f;
    if (!raw || !comp) { fprintf(stderr, "memoire\n"); exit(1); }
    for (y = 0; y < H; y++) {
        unsigned char *row = raw + (size_t)y * ((size_t)W + 1);
        row[0] = 0;
        for (x = 0; x < W; x++) {
            int sx = x / scale, sy = y / scale;
            unsigned char b = fb[sy * (w / 8) + (sx / 8)];
            int bit = (b >> (7 - (sx & 7))) & 1;
            row[x + 1] = bit ? 255 : 0;
        }
    }
    compress2(comp, &clen, raw, (uLong)H * ((size_t)W + 1), 9);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    {
        unsigned char ihdr[13];
        unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        unsigned int v;
        v = W; memcpy(ihdr, &v, 4); ihdr[3] = (unsigned char)W; /* big endian fix ci-dessous */
        ihdr[0] = (unsigned char)(W >> 24); ihdr[1] = (unsigned char)(W >> 16);
        ihdr[2] = (unsigned char)(W >> 8);  ihdr[3] = (unsigned char)W;
        ihdr[4] = (unsigned char)(H >> 24); ihdr[5] = (unsigned char)(H >> 16);
        ihdr[6] = (unsigned char)(H >> 8);  ihdr[7] = (unsigned char)H;
        ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        fwrite(sig, 1, 8, f);
        {
            unsigned char *ch;
            unsigned int len, crc;
            len = 13; crc = crc32(0, (const Bytef *)"IHDR", 4); crc = crc32(crc, ihdr, 13);
            ch = (unsigned char *)malloc(8 + 13 + 4);
            ch[0] = 0; ch[1] = 0; ch[2] = 0; ch[3] = 13;
            memcpy(ch + 4, "IHDR", 4); memcpy(ch + 8, ihdr, 13);
            ch[21] = (unsigned char)(crc >> 24); ch[22] = (unsigned char)(crc >> 16);
            ch[23] = (unsigned char)(crc >> 8);  ch[24] = (unsigned char)crc;
            fwrite(ch, 1, 25, f); free(ch);
        }
        {
            unsigned char *ch = (unsigned char *)malloc(8 + clen + 4);
            unsigned int crc = crc32(0, (const Bytef *)"IDAT", 4);
            crc = crc32(crc, comp, (uInt)clen);
            ch[0] = (unsigned char)(clen >> 24); ch[1] = (unsigned char)(clen >> 16);
            ch[2] = (unsigned char)(clen >> 8);  ch[3] = (unsigned char)clen;
            memcpy(ch + 4, "IDAT", 4); memcpy(ch + 8, comp, clen);
            ch[8 + clen] = (unsigned char)(crc >> 24); ch[9 + clen] = (unsigned char)(crc >> 16);
            ch[10 + clen] = (unsigned char)(crc >> 8); ch[11 + clen] = (unsigned char)crc;
            fwrite(ch, 1, 12 + clen, f); free(ch);
        }
        { unsigned char iend[12] = {0,0,0,0,'I','E','N','D',0xAE,0x42,0x60,0x82}; fwrite(iend, 1, 12, f); }
    }
    fclose(f);
    free(raw); free(comp);
    printf("%s (%dx%d)\n", path, W, H);
}

/* copie un framebuffer 200x200 dans un montage */
static void blit(uint8_t *dst, int dst_w, int dx, int dy, const uint8_t *src)
{
    int y, x;
    for (y = 0; y < 200; y++) for (x = 0; x < 200; x++) {
        int sb = src[y * 25 + (x >> 3)];
        int bit = (sb >> (7 - (x & 7))) & 1;
        int px = dx + x, py = dy + y;
        int i = py * (dst_w / 8) + (px >> 3);
        unsigned char m = (unsigned char)(0x80 >> (px & 7));
        if (bit) dst[i] |= m; else dst[i] &= (unsigned char)~m;
    }
}

int main(int argc, char **argv)
{
    static uint8_t fb[ART_FB_LEN];
    art_tables_init();


    /* montage du monde : ./preview monde [sortie.png] */
    if (argc >= 2 && !strcmp(argv[1], "monde")) {
        const char *out = argc >= 3 ? argv[2] : "monde.png";
        static const char *noms[12] = {
            "oeuf", "bebe", "enfant", "ado", "adulte heros", "adulte normal",
            "adulte grognon", "vieillard", "endormi", "malade", "parti", "visage"
        };
        monde_info_t mi;
        memset(&mi, 0, sizeof(mi));
        mi.cpu_mhz = 240; mi.ram_libre_ko = 4270; mi.psram_libre_ko = 4150;
        mi.sd_go = 119; mi.sd_libre_go = 96; mi.heure = 22; mi.minute = 41;
        mi.wifi_ok = 1; mi.temp = 22.0f;
        int W = 4 * 200, H = 3 * 200, n = 12;
        uint8_t *big = (uint8_t *)malloc((size_t)(W / 8) * H);
        memset(big, 0xFF, (size_t)(W / 8) * H);
        for (int i = 0; i < n; i++) {
            pet_state_t p;
            pet_nait(&p, 1);
            switch (i) {
            case 0: p.age_s = 5; break;
            case 1: p.age_s = 20 + 300; break;
            case 2: p.age_s = 20 + 1200 + 600; break;
            case 3: p.age_s = 20 + 1200 + 10800 + 600; break;
            case 4: p.age_s = 20 + 1200 + 21600 + 40000; p.forme = FORME_HEROS; break;
            case 5: p.age_s = 20 + 1200 + 21600 + 40000; p.forme = FORME_NORMAL; break;
            case 6: p.age_s = 20 + 1200 + 21600 + 40000; p.forme = FORME_GROGNON; break;
            case 7: p.age_s = 20 + 1200 + 21600 + 40000 + 5u * 86400u; p.forme = FORME_HEROS; break;
            case 8: p.age_s = 20 + 1200 + 21600 + 40000; p.forme = FORME_HEROS; p.endormi = 1; break;
            case 9: p.age_s = 20 + 1200 + 21600 + 40000; p.forme = FORME_HEROS; p.malade = 1; p.humeur = 13; break;
            case 10: p.age_s = 20 + 1200 + 21600 + 40000; p.parti = 1; break;
            default:
                p.age_s = 20 + 1200 + 21600 + 40000; p.humeur = 2; p.x = 100; p.pas = 0;
                break;
            }
            art_begin(fb, 0);
            art_clear();
            if (i == 11) {
                draw_bat_head(&EXPR[art_expr_id("fierte")]);
                bandeau_jauges(&p, &mi, 150);
            } else {
                monde_dessine(&p, &mi);
            }
            blit(big, W, (i % 4) * 200, (i / 4) * 200, fb);
            printf("%d:%s ", i, noms[i]);
        }
        printf("\n");
        write_png(out, big, W, H, 2);
        free(big);
        return 0;
    }


    /* une scene en grand : ./preview pet <stade> <forme> [sortie.png] */
    if (argc >= 2 && !strcmp(argv[1], "pet")) {
        int st = argc >= 3 ? atoi(argv[2]) : 4;
        int fo = argc >= 4 ? atoi(argv[3]) : 0;
        const char *out = argc >= 5 ? argv[4] : "pet.png";
        monde_info_t mi;
        memset(&mi, 0, sizeof(mi));
        mi.cpu_mhz = 240; mi.ram_libre_ko = 4270; mi.psram_libre_ko = 4150;
        mi.sd_go = 119; mi.sd_libre_go = 96; mi.temp = 22.0f; mi.wifi_ok = 1;
        pet_state_t p;
        pet_nait(&p, 1);
        p.age_s = (st == 0) ? 5 :
                  (st == 1) ? 20 + 300 :
                  (st == 2) ? 20 + 1200 + 600 :
                  (st == 3) ? 20 + 1200 + 10800 + 600 :
                  (st == 6) ? 20 + 1200 + 21600 + 40000 + 5u * 86400u :
                              20 + 1200 + 21600 + 40000;
        p.forme = (uint8_t)fo;
        p.pas = 0;
        art_begin(fb, 0);
        art_clear();
        if (argc >= 6 && !strcmp(argv[5], "visage")) {
            bandeau_jauges(&p, &mi, 150);
            draw_bat_head(&EXPR[art_expr_id(argv[6])]);
        } else {
            monde_dessine(&p, &mi);
        }
        write_png(out, fb, 200, 200, 3);
        return 0;
    }


    /* les cinq grandes expressions cote a cote : ./preview cinq [sortie.png] */
    if (argc >= 2 && !strcmp(argv[1], "cinq")) {
        const char *out = argc >= 3 ? argv[2] : "cinq.png";
        enum { SW = 200 * 5, SBPL = SW / 8 };
        static uint8_t strip[SBPL * 200];
        memset(strip, 0xFF, sizeof(strip));
        for (int i = 0; i < 5; i++) {
            art_begin(strip, 0);
            art_clear();
            for (int x = 0; x < 200; x++) a_px(x, 0, 0);
            for (int y = 0; y < 200; y++)
                for (int x = 0; x < 200; x++) a_px(x, y, 0);   /* fond blanc */
            draw_bat_grand(i);
            a_text_c(100, 191, 1, BAT5_NOM[i]);
            for (int y = 0; y < 200; y++)
                for (int x = 0; x < 200; x++) {
                    uint8_t b = art_fb()[y * 25 + (x >> 3)];
                    if (!((b >> (7 - (x & 7))) & 1))
                        strip[y * SBPL + ((i * 200 + x) >> 3)] &= (uint8_t)~(0x80 >> ((i * 200 + x) & 7));
                }
        }
        write_png(out, strip, SW, 200, 2);
        printf("%s ecrit (5 expressions, %dx%d)\n", out, SW * 2, 400);
        return 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "one")) {
        const char *nom = argc >= 3 ? argv[2] : "neutre";
        const char *out = argc >= 4 ? argv[3] : "one.png";
        int id = art_expr_id(nom);
        if (id < 0) { fprintf(stderr, "expression inconnue : %s\n", nom); return 1; }
        art_begin(fb, 0);
        art_clear();
        draw_bat_head(&EXPR[id]);
        write_png(out, fb, 200, 200, 3);
        return 0;
    }


    /* montage d'une selection : ./preview sel nom1,nom2,... [sortie.png] */
    if (argc >= 3 && !strcmp(argv[1], "sel")) {
        const char *list = argv[2];
        const char *out = argc >= 4 ? argv[3] : "sel.png";
        char buf[1024];
        char *tok, *save = NULL;
        int n = 0, cols = 4, i;
        int ids[32];
        snprintf(buf, sizeof(buf), "%s", list);
        for (tok = strtok_r(buf, ",", &save); tok && n < 32; tok = strtok_r(NULL, ",", &save)) {
            ids[n] = art_expr_id(tok);
            if (ids[n] < 0) { fprintf(stderr, "inconnue : %s\n", tok); return 1; }
            n++;
        }
        {
            int rows = (n + cols - 1) / cols;
            int W = cols * 200, H = rows * 200;
            size_t len = (size_t)(W / 8) * H;
            uint8_t *big = (uint8_t *)malloc(len);
            if (!big) return 1;
            memset(big, 0xFF, len);
            for (i = 0; i < n; i++) {
                art_begin(fb, 0); art_clear();
                draw_bat_head(&EXPR[ids[i]]);
                blit(big, W, (i % cols) * 200, (i / cols) * 200, fb);
            }
            write_png(out, big, W, H, 2);
            for (i = 0; i < n; i++) printf("%d:%s ", i, EXPR[ids[i]].nom);
            printf("\n");
            free(big);
        }
        return 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "table")) {
        int i, f;
        printf("%d expressions, %d familles\n", EXPR_N, FAMILLE_N);
        for (f = 0; f < FAMILLE_N; f++) {
            printf("  %-14s %2d :", FAMILLES[f], FAMILLE_TAILLE[f]);
            for (i = 0; i < EXPR_N; i++) if (EXPR[i].famille == f) printf(" %s", EXPR[i].nom);
            printf("\n");
        }
        return 0;
    }

    /* lecteur de livres : ./preview livre fichier.txt [page] [sortie.png] */
    if (argc >= 3 && !strcmp(argv[1], "livre")) {
        const char *in = argv[2];
        int page = argc >= 4 ? atoi(argv[3]) : 0;
        const char *out = argc >= 5 ? argv[4] : "livre.png";
        livre_t l;
        if (!livre_charge(&l, in, LIVRE_COLS, LIVRE_ROWS)) {
            fprintf(stderr, "lecture impossible : %s\n", in);
            return 1;
        }
        livre_va(&l, page);
        art_begin(fb, 0);
        art_clear();
        livre_dessine(&l);
        printf("%s : %d lignes, %d pages, page %d (%d%%)\n",
               in, l.nlignes, livre_pages(&l), l.page + 1, livre_pct(&l));
        livre_ferme(&l);
        write_png(out, fb, 200, 200, 3);
        return 0;
    }

    /* montage : 8 colonnes */
    {
        int cols = 8, rows = (EXPR_N + cols - 1) / cols, i;
        int W = cols * 200, H = rows * 200;
        size_t len = (size_t)(W / 8) * H;
        uint8_t *big = (uint8_t *)malloc(len);
        if (!big) return 1;
        memset(big, 0xFF, len);
        for (i = 0; i < EXPR_N; i++) {
            art_begin(fb, 0);
            art_clear();
            draw_bat_head(&EXPR[i]);
            blit(big, W, (i % cols) * 200, (i / cols) * 200, fb);
            if ((i % cols) == cols - 1 || i == EXPR_N - 1) {
                printf("ligne %d :", i / cols);
                for (int k = i - (i % cols); k <= i; k++) printf(" %s", EXPR[k].nom);
                printf("\n");
            }
        }
        write_png("exprs.png", big, W, H, 1);
        free(big);
    }
    return 0;
}
