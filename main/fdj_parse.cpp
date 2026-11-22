/*  fdj_parse.cpp — parsing des flux, sans allocation, tolerant aux pannes.
 *
 *  Regle de la maison : une reponse incomprise ne fait JAMAIS planter la page.
 *  Elle rend 0 et la page affiche "N.D." — jamais un chiffre invente.
 */
#include "fdj_parse.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "cJSON.h"

/* ------------------------------------------------------------------ */
/* petits utilitaires                                                 */
/* ------------------------------------------------------------------ */

/* champ JSON numerique ou chaine : TheSportsDB melange les deux */
static int champ_int(const cJSON *o, const char *cle, int defaut)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, cle);
    if (!v) return defaut;
    if (cJSON_IsNumber(v)) return v->valueint;
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
        char *fin = NULL;
        long n = strtol(v->valuestring, &fin, 10);
        if (fin && fin != v->valuestring) return (int)n;
    }
    return defaut;
}

/* champ JSON chaine, plie en ASCII ; vide si absent */
static void champ_txt(const cJSON *o, const char *cle, char *dst, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, cle);
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (cJSON_IsString(v) && v->valuestring)
        fdj_ascii(v->valuestring, dst, cap);
}

/* "2026-11-20T18:00:00" -> "18:00" */
static void heure_de(const char *ts, char *dst, size_t cap)
{
    if (!dst || cap < 6) return;
    dst[0] = 0;
    if (!ts) return;
    const char *t = strchr(ts, 'T');
    if (!t || strlen(t) < 6) return;
    snprintf(dst, cap, "%.5s", t + 1);
}

/* ------------------------------------------------------------------ */
/* sports : livescore.php / eventsday.php                             */
/* ------------------------------------------------------------------ */

int fdj_parse_events(const char *json, bool garder_a_venir, fdj_live_t *out)
{
    if (!out) return 0;
    if (!json) { out->ok = false; return 0; }
    out->n = 0;

    cJSON *root = cJSON_Parse(json);
    if (!root) { out->ok = false; return 0; }

    /* les deux endpoints n'ont pas la meme cle racine */
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "livescore");
    if (!cJSON_IsArray(arr)) arr = cJSON_GetObjectItemCaseSensitive(root, "events");

    int n = 0;
    const cJSON *e = NULL;
    cJSON_ArrayForEach(e, arr) {
        if (n >= FDJ_MATCH_MAX) break;
        if (!cJSON_IsObject(e)) continue;

        const cJSON *sc1 = cJSON_GetObjectItemCaseSensitive(e, "intHomeScore");
        const cJSON *sc2 = cJSON_GetObjectItemCaseSensitive(e, "intAwayScore");
        int bd = champ_int(e, "intHomeScore", -1);
        int be = champ_int(e, "intAwayScore", -1);
        bool score_ok = bd >= 0 && be >= 0 &&
                        ((cJSON_IsString(sc1) && sc1->valuestring && sc1->valuestring[0]) ||
                         cJSON_IsNumber(sc1)) &&
                        ((cJSON_IsString(sc2) && sc2->valuestring && sc2->valuestring[0]) ||
                         cJSON_IsNumber(sc2));

        char statut[8] = "";
        champ_txt(e, "strStatus", statut, sizeof(statut));

        bool live = score_ok &&
                    (strcmp(statut, "1H") == 0 || strcmp(statut, "2H") == 0 ||
                     strcmp(statut, "HT") == 0 || strcmp(statut, "ET") == 0 ||
                     strcmp(statut, "P") == 0  || strcmp(statut, "LIVE") == 0);
        /* FT/NS/ABD : ni en direct ni a venir (un match fini n'a plus de modele) */
        if (!live && !garder_a_venir) continue;
        if (!live && strcmp(statut, "NS") != 0) continue;

        fdj_match_t *m = &out->m[n];
        memset(m, 0, sizeof(*m));
        champ_txt(e, "strLeague", m->ligue, sizeof(m->ligue));
        champ_txt(e, "strHomeTeam", m->dom, sizeof(m->dom));
        champ_txt(e, "strAwayTeam", m->ext, sizeof(m->ext));
        m->live     = live;
        m->buts_dom = live ? bd : 0;
        m->buts_ext = live ? be : 0;
        m->minute   = live ? champ_int(e, "strProgress", 45) : 0;
        if (m->minute < 0)   m->minute = 0;
        if (m->minute > 120) m->minute = 120;
        if (live) {
            snprintf(m->info, sizeof(m->info), "%d'", m->minute);
        } else {
            char ts[32] = "";
            champ_txt(e, "strTimestamp", ts, sizeof(ts));
            heure_de(ts, m->info, sizeof(m->info));
            if (!m->info[0]) snprintf(m->info, sizeof(m->info), "--:--");
        }
        /* une ligne sans equipes ne sert a rien */
        if (!m->dom[0] || !m->ext[0]) continue;
        n++;
    }
    cJSON_Delete(root);

    out->n  = n;
    out->ok = (n > 0);
    return n;
}

/* ------------------------------------------------------------------ */
/* RSS : <item><title>...</title>                                     */
/* ------------------------------------------------------------------ */

static bool commence_par(const char *s, const char *motif)
{
    return strncmp(s, motif, strlen(motif)) == 0;
}

/* quelques entites nommees, celles qui sortent vraiment d'un flux fr */
static const struct { const char *nom; char car; } ENTITES[] = {
    { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' },
    { "apos;", '\'' }, { "nbsp;", ' ' }, { "eacute;", 'e' }, { "egrave;", 'e' },
    { "ecirc;", 'e' }, { "agrave;", 'a' }, { "acirc;", 'a' }, { "ccedil;", 'c' },
    { "ugrave;", 'u' }, { "ocirc;", 'o' }, { "icirc;", 'i' }, { "laquo;", '"' },
    { "raquo;", '"' }, { "hellip;", '.' }, { "euro;", 'E' }, { "deg;", 'o' },
    { "middot;", '.' }, { "rsquo;", '\'' }, { "lsquo;", '\'' }, { "mdash;", '-' },
    { "ndash;", '-' },
};

void fdj_entites(char *s)
{
    if (!s) return;
    char *r = s, *w = s;
    while (*r) {
        if (*r == '&') {
            bool fait = false;
            /* 1. numerique decimal (&#233;) ou hexadecimal (&#xE9;) */
            if (r[1] == '#') {
                char *fin = NULL;
                long cp;
                if (r[2] == 'x' || r[2] == 'X') cp = strtol(r + 3, &fin, 16);
                else                            cp = strtol(r + 2, &fin, 10);
                if (fin && *fin == ';' && cp > 0 && cp < 0x10000) {
                    if (cp == 0x2026) {                 /* … -> "..." */
                        *w++ = '.'; *w++ = '.'; *w++ = '.';
                    } else if (cp == 0xA0 || cp == 0x20) {
                        *w++ = ' ';
                    } else {
                        char tmp[8] = { 0 }, fold[8] = { 0 };
                        if (cp < 0x80) tmp[0] = (char)cp;
                        else if (cp < 0x800) {
                            tmp[0] = (char)(0xC0 | (cp >> 6));
                            tmp[1] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            tmp[0] = (char)(0xE0 | (cp >> 12));
                            tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            tmp[2] = (char)(0x80 | (cp & 0x3F));
                        }
                        fdj_ascii(tmp, fold, sizeof(fold));
                        for (const char *f = fold; *f; f++) *w++ = *f;
                    }
                    r = fin + 1;
                    fait = true;
                }
            }
            /* 2. entites nommees */
            if (!fait) {
                for (size_t i = 0; i < sizeof(ENTITES) / sizeof(ENTITES[0]); i++) {
                    if (commence_par(r + 1, ENTITES[i].nom)) {
                        *w++ = ENTITES[i].car;
                        r += 1 + strlen(ENTITES[i].nom);
                        fait = true;
                        break;
                    }
                }
            }
            if (fait) continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

/* cherche la fin d'une balise ouvrante et rend le debut du contenu */
static const char *contenu_apres(const char *p, const char *balise)
{
    const char *d = strstr(p, balise);
    if (!d) return NULL;
    return d + strlen(balise);
}

static int nb_entites = 0;   /* compteur de debug, jamais lu par l'UI */

int fdj_parse_news(const char *xml, fdj_flux_t *out)
{
    if (!out) return 0;
    if (!xml) { out->ok = false; return 0; }
    out->nb = 0;

    const char *p = xml;
    int n = 0;
    while (n < FDJ_NEWS_MAX) {
        const char *item = strstr(p, "<item");
        if (!item) break;
        const char *fin_item = strstr(item, "</item>");
        if (!fin_item) break;

        const char *t = contenu_apres(item, "<title>");
        if (!t || t > fin_item) { p = fin_item + 7; continue; }
        const char *tf = strstr(t, "</title>");
        if (!tf || tf > fin_item) { p = fin_item + 7; continue; }

        char brut[FDJ_NEWS_LEN * 3];
        size_t len = (size_t)(tf - t);
        if (len >= sizeof(brut)) len = sizeof(brut) - 1;
        memcpy(brut, t, len);
        brut[len] = 0;

        /* CDATA eventuel */
        char *cd = strstr(brut, "<![CDATA[");
        if (cd) {
            char *fin = strstr(cd, "]]>");
            if (fin) *fin = 0;
            memmove(brut, cd + 9, strlen(cd + 9) + 1);
        }

        fdj_news_t *nx = &out->n[n];
        memset(nx, 0, sizeof(*nx));
        fdj_entites(brut);
        fdj_ascii(brut, nx->titre, sizeof(nx->titre));
        nb_entites++;

        /* l'heure de publication, si le flux la donne */
        const char *d = contenu_apres(item, "<pubDate>");
        const char *df = d ? strstr(d, "</pubDate>") : NULL;
        if (d && df && df < fin_item) {
            char pd[64];
            size_t l = (size_t)(df - d);
            if (l >= sizeof(pd)) l = sizeof(pd) - 1;
            memcpy(pd, d, l);
            pd[l] = 0;
            /* "Fri, 20 Nov 2026 12:39:12 +0100" -> "12:39" */
            char *c1 = strchr(pd, ':');
            if (c1 && c1 - pd >= 2 && isdigit((unsigned char)c1[-2]) &&
                isdigit((unsigned char)c1[-1])) {
                snprintf(nx->quand, sizeof(nx->quand), "%c%c:%c%c",
                         c1[-2], c1[-1], c1[1], c1[2]);
            }
        }
        fdj_txt_copie(nx->source, sizeof(nx->source), "FRANCEINFO");
        n++;
        p = fin_item + 7;
    }

    out->nb = n;
    out->ok = (n > 0);
    return n;
}
