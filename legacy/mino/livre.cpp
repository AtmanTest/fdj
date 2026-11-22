/*  livre.cpp - lecteur de livres : encodage, pagination, rendu 5x10. */

#include "livre.h"
#include "font5.h"
#include "art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define LIVRE_ALLOC(n) heap_caps_malloc((size_t)(n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LIVRE_FREE(p)  heap_caps_free(p)
#else
#define LIVRE_ALLOC(n) malloc((size_t)(n))
#define LIVRE_FREE(p)  free(p)
#endif

#define LIVRE_FIN        0xFF       /* fin de ligne (un saut dans le texte) */
#define LIVRE_PAR        0xFE       /* fin de paragraphe : une ligne vide en plus */
#define LIVRE_FICHIER_MAX (6 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* UTF-8 -> codes de glyphes                                          */
/* ------------------------------------------------------------------ */

static uint32_t utf8_suivant(const unsigned char **p)
{
    const unsigned char *s = *p;
    uint32_t c = s[0];
    if (c < 0x80) { *p = s + 1; return c; }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *p = s + 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *p = s + 3;
        return ((c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *p = s + 4;
        return ((c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *p = s + 1;
    return 0xFFFD;
}

/* un point de code -> 1 a 3 codes de glyphe. renvoie le nombre ecrit. */
static int code_glyphe(uint32_t cp, uint8_t *out)
{
    uint8_t k;
    switch (cp) {
    case 0x00A0: cp = ' '; break;
    case 0x2018: case 0x2019: case 0x02BC: cp = '\''; break;
    case 0x201C: case 0x201D: cp = '"'; break;
    case 0x2013: case 0x2014: case 0x2212: cp = '-'; break;
    case 0x2022: cp = '-'; break;
    case 0x2026: k = font5_code('.'); out[0] = k; out[1] = k; out[2] = k; return 3;
    case 0x0153: out[0] = font5_code('o'); out[1] = font5_code('e'); return 2;
    case 0x0152: out[0] = font5_code('O'); out[1] = font5_code('E'); return 2;
    case 0x00E6: out[0] = font5_code('a'); out[1] = font5_code('e'); return 2;
    case 0x00C6: out[0] = font5_code('A'); out[1] = font5_code('E'); return 2;
    case 0x00DF: out[0] = font5_code('s'); out[1] = font5_code('s'); return 2;
    case 0x20AC: out[0] = font5_code('E'); return 1;
    case 0x00B0: out[0] = font5_code('o'); return 1;
    case 0x00B7: out[0] = font5_code('.'); return 1;
    default: break;
    }
    k = font5_code(cp);
    if (!k) k = font5_code('?');
    out[0] = k;
    return 1;
}

int livre_encode(const char *src, uint8_t *dst, int cap)
{
    const unsigned char *p = (const unsigned char *)src;
    int n = 0;
    bool espace = false;

    while (*p && n < cap) {
        uint32_t cp = utf8_suivant(&p);

        if (cp == '\r') continue;
        if (cp == '\n') {                       /* saut de ligne */
            while (n > 0 && dst[n - 1] == font5_code(' ')) n--;   /* espaces de fin */
            /* deux sauts de suite = paragraphe : une ligne vide a l'ecran */
            if (n > 0 && dst[n - 1] == LIVRE_FIN) dst[n - 1] = LIVRE_PAR;
            else if (n == 0 || dst[n - 1] != LIVRE_PAR) dst[n++] = LIVRE_FIN;
            espace = false;
            continue;
        }
        if (cp == ' ' || cp == '\t') {          /* espaces compresses */
            uint8_t prec = n > 0 ? dst[n - 1] : 0;
            if (prec && !espace && prec != LIVRE_FIN && prec != LIVRE_PAR) {
                dst[n++] = font5_code(' ');
                espace = true;
            }
            continue;
        }
        uint8_t tmp[3];
        int k = code_glyphe(cp, tmp);
        for (int i = 0; i < k && n < cap; i++) dst[n++] = tmp[i];
        espace = false;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* pagination                                                         */
/* ------------------------------------------------------------------ */

static bool pousse_ligne(livre_t *l, int debut, int longueur)
{
    if (l->nlignes >= l->cap) {
        int cap = l->cap ? l->cap * 2 : 512;
        int32_t *n = (int32_t *)LIVRE_ALLOC((size_t)cap * 2 * sizeof(int32_t));
        if (!n) return false;
        if (l->lignes) {
            memcpy(n, l->lignes, (size_t)l->nlignes * 2 * sizeof(int32_t));
            LIVRE_FREE(l->lignes);
        }
        l->lignes = n;
        l->cap = cap;
    }
    l->lignes[l->nlignes * 2] = debut;
    l->lignes[l->nlignes * 2 + 1] = longueur;
    l->nlignes++;
    return true;
}

static bool pagine(livre_t *l)
{
    const uint8_t esp = font5_code(' ');
    int i = 0;

    while (i < l->len) {
        if (l->txt[i] == LIVRE_PAR) {           /* paragraphe : une ligne vide */
            pousse_ligne(l, i, 0);
            i++;
            continue;
        }
        if (l->txt[i] == LIVRE_FIN) { i++; continue; }   /* simple fin de ligne */
        if (l->txt[i] == esp) { i++; continue; }

        int debut = i, fin = i, cur = 0;
        bool plein = false;
        while (i < l->len && !plein) {
            if (l->txt[i] == LIVRE_FIN || l->txt[i] == LIVRE_PAR) {
                plein = true;                    /* la ligne s'arrete ici */
                break;
            }
            int w = i;
            while (i < l->len && l->txt[i] != LIVRE_FIN && l->txt[i] != LIVRE_PAR &&
                   l->txt[i] != esp) i++;
            int wl = i - w;
            if (wl == 0) { i++; continue; }     /* espaces entre deux mots */
            if (cur + (cur ? 1 : 0) + wl <= l->cols) {
                cur += (cur ? 1 : 0) + wl;
                fin = w + wl;
            } else if (cur == 0) {              /* mot plus long que la ligne */
                fin = w + l->cols;
                i = fin;
                plein = true;
            } else {                            /* le mot passe a la ligne */
                i = w;
                plein = true;
            }
        }
        if (!pousse_ligne(l, debut, fin - debut)) return false;
        if (i == debut) i++;                    /* securite : jamais de boucle */
    }
    return true;
}

bool livre_charge(livre_t *l, const char *chemin, int cols, int rows)
{
    memset(l, 0, sizeof(*l));
    if (cols < 8 || rows < 2) return false;

    FILE *f = fopen(chemin, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long taille = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (taille <= 0 || taille > LIVRE_FICHIER_MAX) { fclose(f); return false; }

    uint8_t *buf = (uint8_t *)LIVRE_ALLOC((size_t)taille + 1);
    if (!buf) { fclose(f); return false; }
    size_t lu = fread(buf, 1, (size_t)taille, f);
    fclose(f);
    buf[lu] = 0;

    /* encodage en place : la sortie ne depasse jamais l'entree */
    int n = livre_encode((const char *)buf, buf, (int)lu);
    l->txt = buf;
    l->len = n;
    l->cols = cols;
    l->rows = rows;

    /* titre : nom du fichier sans son extension, sans le dossier */
    const char *base = strrchr(chemin, '/');
    base = base ? base + 1 : chemin;
    snprintf(l->nom, sizeof(l->nom), "%s", base);
    char *pt = strrchr(l->nom, '.');
    if (pt && pt != l->nom) *pt = 0;
    snprintf(l->chemin, sizeof(l->chemin), "%s", chemin);

    pagine(l);
    if (l->nlignes == 0) { livre_ferme(l); return false; }
    return true;
}

void livre_ferme(livre_t *l)
{
    if (l->txt) LIVRE_FREE(l->txt);
    if (l->lignes) LIVRE_FREE(l->lignes);
    memset(l, 0, sizeof(*l));
}

int livre_pages(const livre_t *l)
{
    if (!l->rows || l->nlignes == 0) return 0;
    return (l->nlignes + l->rows - 1) / l->rows;
}

int livre_pct(const livre_t *l)
{
    int n = livre_pages(l);
    if (n <= 1) return 100;
    return (l->page * 100) / (n - 1);
}

bool livre_va(livre_t *l, int page)
{
    int n = livre_pages(l);
    if (n <= 0) return false;
    if (page < 0) page = 0;
    if (page > n - 1) page = n - 1;
    bool change = (page != l->page);
    l->page = page;
    return change;
}

bool livre_avance(livre_t *l, int d)
{
    return livre_va(l, l->page + d);
}

const uint8_t *livre_ligne(const livre_t *l, int i, int *n)
{
    *n = 0;
    if (i < 0 || i >= l->nlignes) return NULL;
    *n = (int)l->lignes[i * 2 + 1];
    return l->txt + l->lignes[i * 2];
}

/* ------------------------------------------------------------------ */
/* rendu 5x10                                                         */
/* ------------------------------------------------------------------ */

void font5_texte(int x, int y, const uint8_t *codes, int n, int black)
{
    for (int k = 0; k < n; k++) {
        uint8_t c = codes[k];
        if (!c || c > FONT5_N) continue;
        const uint8_t *g = FONT5[c - 1];
        int gx = x + k * FONT5_PAS;
        for (int r = 0; r < FONT5_HAUTEUR; r++) {
            uint8_t m = g[r];
            if (!m) continue;
            for (int b = 0; b < FONT5_LARGEUR; b++)
                if (m & (1 << b)) a_px(gx + b, y + r, black);
        }
    }
}

int font5_texte_utf8(int x, int y, const char *s, int black)
{
    uint8_t codes[128];
    int n = livre_encode(s, codes, (int)sizeof(codes) - 1);
    /* livre_encode insere des sauts de ligne : ici on les traite comme rien */
    int sortie = 0;
    for (int i = 0; i < n; i++)
        if (codes[i] != LIVRE_FIN && codes[i] != LIVRE_PAR) codes[sortie++] = codes[i];
    font5_texte(x, y, codes, sortie, black);
    return sortie * FONT5_PAS - 1;
}

int font5_utf8_largeur(const char *s)
{
    uint8_t codes[128];
    int n = livre_encode(s, codes, (int)sizeof(codes) - 1), c = 0;
    for (int i = 0; i < n; i++) if (codes[i] != LIVRE_FIN && codes[i] != LIVRE_PAR) c++;
    return c * FONT5_PAS - 1;
}

/* ------------------------------------------------------------------ */
/* la page a l'ecran                                                  */
/* ------------------------------------------------------------------ */

void livre_dessine(const livre_t *l)
{
    uint8_t titre[LIVRE_TITRE_MAX * 3];
    int nt = livre_encode(l->nom, titre, (int)sizeof(titre));
    if (nt > 26) nt = 26;                    /* on laisse la place au compteur */
    font5_texte(4, 1, titre, nt, 1);

    char b[24];
    snprintf(b, sizeof(b), "%d/%d", l->page + 1, livre_pages(l));
    a_text(196 - a_text_w(1, b), 4, 1, b);

    a_hline(4, 195, 12, 1);

    for (int k = 0; k < l->rows; k++) {
        int n = 0;
        const uint8_t *s = livre_ligne(l, l->page * l->rows + k, &n);
        if (s && n) font5_texte(4, 14 + k * FONT5_INTERLIGNE, s, n, 1);
    }

    a_text(4, 191, 1, "1 SUIVANT  2 PRECEDENT");
    snprintf(b, sizeof(b), "%d%%", livre_pct(l));
    a_text(196 - a_text_w(1, b), 191, 1, b);
}
