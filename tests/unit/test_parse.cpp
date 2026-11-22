/*  test_parse.cpp — les parseurs, nourris avec de VRAIES reponses serveur.
 *  Fixtures figees dans tests/fixtures : le reseau n'est pas necessaire.
 */
#include "check.h"
#include "fdj_model.h"
#include "fdj_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *lire(const char *chemin, long *taille)
{
    FILE *f = fopen(chemin, "rb");
    if (!f) { printf("  (fixture absente : %s)\n", chemin); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t lus = fread(b, 1, (size_t)n, f);
    b[lus] = 0;
    fclose(f);
    if (taille) *taille = (long)lus;
    return b;
}

static bool ascii_seul(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s >= 0x80) return false;
    return true;
}

static void test_livescore(void)
{
    long n = 0;
    char *txt = lire("tests/fixtures/thesportsdb_livescore.json", &n);
    if (!txt) { txt = lire("../tests/fixtures/thesportsdb_livescore.json", &n); }
    OK(txt != NULL, "fixture livescore presente");
    if (!txt) return;

    fdj_live_t l;
    memset(&l, 0, sizeof(l));
    int nb = fdj_parse_events(txt, false, &l);
    OK(nb > 0, "des matchs en direct sont lus");
    OK(l.n == nb && l.ok, "compteur et drapeau ok");
    OK(nb <= FDJ_MATCH_MAX, "tableau borne");

    bool champs = true, plies = true, minutes = true, scores = true;
    for (int i = 0; i < nb; i++) {
        const fdj_match_t *m = &l.m[i];
        if (!m->dom[0] || !m->ext[0] || !m->ligue[0]) champs = false;
        if (!ascii_seul(m->dom) || !ascii_seul(m->ext) || !ascii_seul(m->ligue)) plies = false;
        if (!m->live) minutes = false;
        if (m->minute < 0 || m->minute > 120) minutes = false;
        if (m->buts_dom < 0 || m->buts_ext < 0) scores = false;
        if (m->info[strlen(m->info) ? strlen(m->info) - 1 : 0] != '\'') minutes = false;
    }
    OK(champs, "chaque match a ligue et deux equipes");
    OK(plies, "les accents sont plies en ASCII (affichage 3x5)");
    OK(minutes, "minute de jeu plausible");
    OK(scores, "scores non negatifs");
    free(txt);
}

static void test_a_venir(void)
{
    long n = 0;
    char *txt = lire("tests/fixtures/thesportsdb_day.json", &n);
    OK(txt != NULL, "fixture du jour presente");
    if (!txt) return;

    fdj_live_t l;
    memset(&l, 0, sizeof(l));
    int nb = fdj_parse_events(txt, true, &l);
    OK(nb >= 0, "le parseur accepte le flux du jour");
    bool heure_ok = true, flag_ok = true;
    for (int i = 0; i < nb; i++) {
        if (!l.m[i].live && strchr(l.m[i].info, ':') == NULL) heure_ok = false;
        if (l.m[i].live && l.m[i].info[strlen(l.m[i].info) - 1] != '\'') heure_ok = false;
        if (!l.m[i].live && (l.m[i].buts_dom != 0 || l.m[i].buts_ext != 0)) flag_ok = false;
    }
    OK(heure_ok, "un match a venir affiche son heure, un direct sa minute");
    OK(flag_ok, "un match a venir part a 0-0");

    /* filtre : sans garder_a_venir, aucun match "NS" ne sort */
    fdj_live_t strict;
    memset(&strict, 0, sizeof(strict));
    int ns = fdj_parse_events(txt, false, &strict);
    bool aucun_ns = true;
    for (int i = 0; i < ns; i++) if (!strict.m[i].live) aucun_ns = false;
    OK(aucun_ns, "en mode direct strict, les matchs a venir sont filtres");
    free(txt);
}

static void test_robustesse(void)
{
    fdj_live_t l;
    memset(&l, 0, sizeof(l));
    OK(fdj_parse_events("", false, &l) == 0 && !l.ok, "reponse vide : 0 match, pas de plantage");
    OK(fdj_parse_events("pas du json", false, &l) == 0, "texte quelconque : 0 match");
    OK(fdj_parse_events("{}", false, &l) == 0, "objet sans evenement : 0 match");
    OK(fdj_parse_events("{\"livescore\":[]}", false, &l) == 0, "liste vide : 0 match");
    OK(fdj_parse_events("{\"livescore\":[{}]}", false, &l) == 0, "evenement vide ignore");
    OK(fdj_parse_events(NULL, false, &l) == 0, "pointeur nul tolere");

    fdj_flux_t f;
    memset(&f, 0, sizeof(f));
    OK(fdj_parse_news("", &f) == 0 && !f.ok, "RSS vide : 0 titre");
    OK(fdj_parse_news("<rss><channel></channel></rss>", &f) == 0, "RSS sans item : 0 titre");
    OK(fdj_parse_news(NULL, &f) == 0, "RSS nul tolere");
}

static void test_news(void)
{
    long n = 0;
    char *txt = lire("tests/fixtures/franceinfo.rss", &n);
    OK(txt != NULL, "fixture RSS presente");
    if (!txt) return;

    fdj_flux_t f;
    memset(&f, 0, sizeof(f));
    int nb = fdj_parse_news(txt, &f);
    OK(nb > 0 && f.ok, "des titres sont lus");
    OK(nb <= FDJ_NEWS_MAX, "tableau de titres borne");

    bool propre = true, source = true, quand = true;
    for (int i = 0; i < nb; i++) {
        if (!f.n[i].titre[0]) propre = false;
        if (strstr(f.n[i].titre, "<") || strstr(f.n[i].titre, "&")) propre = false;
        if (!ascii_seul(f.n[i].titre)) propre = false;
        if (strlen(f.n[i].titre) > FDJ_NEWS_LEN - 1) propre = false;
        if (strcmp(f.n[i].source, "FRANCEINFO") != 0) source = false;
        if (f.n[i].quand[0] && f.n[i].quand[2] != ':') quand = false;
    }
    OK(propre, "titres sans balise ni entite, bornes, ASCII");
    OK(source, "source renseignee");
    OK(quand, "heure au format HH:MM");

    /* entites isolees */
    char b[64];
    strcpy(b, "Caf&eacute; &amp; th&#233; &quot;ok&quot;");
    fdj_entites(b);
    OK(strcmp(b, "Cafe & the \"ok\"") == 0, "decodage des entites HTML");
    free(txt);
}

int main(void)
{
    test_livescore();
    test_a_venir();
    test_robustesse();
    test_news();
    FIN("parseurs de flux");
}
