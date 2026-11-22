/*  test_modele.cpp — tests unitaires du modele metier (aucune carte requise).
 *  Chaque test porte le nom de la regle qu'il protege.
 */
#include "check.h"
#include "fdj_model.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const int64_t T0 = 1760000000LL;      /* 2025-10-09, fixe pour etre deterministe */

static void test_defauts(void)
{
    fdj_etat_t e;
    fdj_etat_defaut(&e, 42);
    OK(fdj_etat_valide(&e), "etat par defaut valide");
    OK(e.nb_profils == 1, "un profil a l'usine");
    OK(e.profil_actif == 0, "le profil par defaut est actif");
    OK(e.profils[0].solde_ct == 10000, "solde de maquette : 100 EUR");
    OK(e.code_boot == 0, "code de demarrage 0000");
    OK(!e.code_perso, "code d'usine signale");
    OK(e.page == 0 && e.curseur == 0, "on demarre page 1");
    OK(!e.pause_24h && e.musique && e.news_auto, "options par defaut");
}

static void test_serialisation(void)
{
    fdj_etat_t e, d;
    uint8_t blob[2048];
    fdj_etat_defaut(&e, 7);
    fdj_profil_ajoute(&e, "EXPERT", 2, 5000, 20000);
    e.sel_cochee = 0x0205;
    e.page = 6;

    size_t n = fdj_etat_serialise(&e, blob, sizeof(blob));
    OK(n > 0, "serialisation ecrit un blob");
    OK(n == sizeof(fdj_etat_t) + 12, "taille du blob = entete + etat");

    PRES(fdj_etat_deserialise(&d, blob, n));
    OK(memcmp(&e, &d, sizeof(e)) == 0, "aller-retour identique");

    OK(!fdj_etat_deserialise(&d, blob, n - 1), "blob tronque refuse");
    blob[n - 5] ^= 0xFF;
    OK(!fdj_etat_deserialise(&d, blob, n), "blob corrompu refuse (CRC)");
    blob[n - 5] ^= 0xFF;

    OK(fdj_etat_serialise(&e, blob, 4) == 0, "tampon trop petit : 0 octet");
    OK(fdj_crc32("", 0) == 0, "CRC32 de rien vaut 0");
    OK(fdj_crc32("123456789", 9) == 0xCBF43926u, "CRC32 de reference");
}

static void test_code(void)
{
    fdj_etat_t e;
    fdj_etat_defaut(&e, 1);
    OK(fdj_code_verifie(&e, 0), "0000 ouvre la maquette");
    OK(!fdj_code_verifie(&e, 1234), "un autre code est refuse");
    OK(fdj_code_valide(9999), "9999 valide");
    OK(!fdj_code_valide(10000), "10000 refuse");

    PRES(fdj_code_change(&e, 4821));
    OK(e.code_perso, "code perso marque");
    OK(fdj_code_verifie(&e, 4821), "le nouveau code ouvre");
    OK(!fdj_code_verifie(&e, 0), "l'ancien code ne marche plus");
    OK(!fdj_code_change(&e, 12345), "changement hors bornes refuse");
}

static void test_profils(void)
{
    fdj_etat_t e;
    fdj_etat_defaut(&e, 1);
    OK(fdj_profil_ajoute(&e, "DEBUTANT", 0, 1000, 3000) == 1, "2e profil accepte");
    OK(fdj_profil_ajoute(&e, "EXPERT", 2, 5000, 20000) == 2, "3e profil accepte");
    OK(fdj_profil_ajoute(&e, "PAPA", 1, 3000, 9000) == 3, "4e profil accepte");
    OK(fdj_profil_ajoute(&e, "TROP", 1, 1000, 3000) == -1, "5e profil refuse");
    OK(fdj_profil_ajoute(&e, "expert", 1, 1000, 3000) == -1, "doublon refuse");
    OK(fdj_profil_ajoute(&e, "", 1, 1000, 3000) == -1, "nom vide refuse");
    OK(fdj_profil_ajoute(&e, "ZERO", 1, 0, 0) == -1, "plafond nul refuse");
    OK(fdj_profil_ajoute(&e, "INV", 1, 5000, 3000) == -1, "semaine > mois refusee");
    OK(e.nb_profils == 4, "4 profils maximum");
    OK(strcmp(e.profils[2].nom, "EXPERT") == 0, "nom range en majuscules");

    PRES(fdj_profil_selectionne(&e, 2));
    OK(e.profil_actif == 2, "profil selectionne");
    OK(fdj_profil_actif(&e) == &e.profils[2], "actif = expert");
    OK(!fdj_profil_selectionne(&e, 4), "profil inexistant refuse");
    OK(fdj_profil_actif_c(&e)->niveau == 2, "niveau conserve");
}

static void test_plafonds(void)
{
    fdj_etat_t e;
    fdj_etat_defaut(&e, 1);          /* 25 EUR/sem, 60 EUR/mois, 100 EUR solde */
    OK(fdj_mise_verifie(&e, T0, 0) == 1, "mise nulle refusee");
    OK(fdj_mise_verifie(&e, T0, 2001) == 1, "mise > 20 EUR refusee");
    OK(fdj_mise_verifie(&e, T0, 2000) == 0, "mise 20 EUR acceptee");
    OK(fdj_mise_verifie(&e, T0, 20000) == 1, "mise > plafond unitaire refusee");

    /* 25 EUR sur 7 jours : deux paris de 12,50 passent, le 3e non */
    fdj_pari_ajoute(&e, "A", "AAA", 1250, 2500, T0 - 2 * 86400, FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);
    fdj_pari_ajoute(&e, "B", "BBB", 1250, 2500, T0 - 1 * 86400, FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);
    OK(fdj_mise_verifie(&e, T0, 100) == 2, "plafond semaine atteint");

    /* le mois : 60 EUR, mais la semaine bloque d'abord : on avance le temps */
    fdj_etat_t m;
    fdj_etat_defaut(&m, 1);
    fdj_pari_ajoute(&m, "V", "VVV", 2000, 4000, T0 - 20 * 86400, FDJ_JEU_SPORT, FDJ_PARI_PERDU);
    fdj_pari_ajoute(&m, "W", "WWW", 2000, 4000, T0 - 19 * 86400, FDJ_JEU_SPORT, FDJ_PARI_PERDU);
    OK(fdj_mise_verifie(&m, T0, 2000) == 0, "sous le mois : accepte");
    fdj_pari_ajoute(&m, "X", "XXX", 2000, 4000, T0 - 18 * 86400, FDJ_JEU_SPORT, FDJ_PARI_PERDU);
    OK(fdj_mise_verifie(&m, T0, 100) == 3, "plafond mois atteint");

    fdj_etat_t s;
    fdj_etat_defaut(&s, 1);
    s.profils[0].solde_ct = 500;
    OK(fdj_mise_verifie(&s, T0, 1000) == 4, "solde insuffisant");

    s.pause_24h = true;
    OK(fdj_mise_verifie(&s, T0, 100) == 5, "pause jeu responsable bloque");

    /* un profil debutant a son propre plafond, independant du defaut */
    s.pause_24h = false;
    fdj_profil_ajoute(&s, "DEBUTANT", 0, 1000, 3000);
    fdj_profil_selectionne(&s, 1);
    s.profils[1].solde_ct = 10000;
    OK(fdj_mise_verifie(&s, T0, 1500) == 2, "le plafond du profil actif s'applique");
}

static void test_pariers(void)
{
    fdj_etat_t e;
    fdj_etat_defaut(&e, 1);
    int i = fdj_pari_ajoute(&e, "troyes-le havre  +2,5", "7KQ", 500, 1200, T0, FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);
    OK(i == 0, "premier pari a l'indice 0");
    OK(e.hist_n == 1, "historique compte 1");
    OK(e.profils[0].solde_ct == 9500, "le solde baisse de la mise");
    OK(strcmp(e.hist[0].libelle, "TROYES-LE HAVRE  +2,5") == 0, "libelle rangé en majuscules");

    fdj_pari_regle(&e, 0, true);
    OK(e.hist[0].statut == FDJ_PARI_GAGNE, "pari gagne");
    OK(e.profils[0].solde_ct == 10700, "le gain est credite");
    fdj_pari_regle(&e, 0, false);
    OK(e.hist[0].statut == FDJ_PARI_GAGNE, "un pari regle ne change plus");

    /* anneau : 40 paris -> 32 gardes, le plus recent en tete */
    fdj_etat_t r;
    fdj_etat_defaut(&r, 1);
    for (int k = 0; k < 40; k++) {
        char lib[16];
        snprintf(lib, sizeof(lib), "PARI %d", k);
        fdj_pari_ajoute(&r, lib, "ABC", 10, 20, T0 + k, FDJ_JEU_SPORT, FDJ_PARI_PERDU);
    }
    OK(r.hist_n == FDJ_HIST_MAX, "historique plafonne a 32");
    OK(r.hist_next == (uint8_t)(40 % FDJ_HIST_MAX), "tete de l'anneau");
    int idx = fdj_hist_index_recence(&r, 0);
    OK(idx >= 0 && strcmp(r.hist[idx].libelle, "PARI 39") == 0, "le plus recent sort en premier");
    idx = fdj_hist_index_recence(&r, 31);
    OK(idx >= 0 && strcmp(r.hist[idx].libelle, "PARI 8") == 0, "le plus ancien garde");
    OK(fdj_hist_index_recence(&r, 32) == -1, "au-dela : rien");
}

static void test_periodes(void)
{
    fdj_etat_t e;
    fdj_etat_defaut(&e, 1);
    fdj_pari_ajoute(&e, "S1", "A", 1000, 2500, T0 - 2 * 86400,  FDJ_JEU_SPORT, FDJ_PARI_GAGNE);
    fdj_pari_ajoute(&e, "M1", "B", 2000, 0,    T0 - 20 * 86400, FDJ_JEU_SPORT, FDJ_PARI_PERDU);
    fdj_pari_ajoute(&e, "A1", "C", 3000, 6000, T0 - 300 * 86400, FDJ_JEU_SPORT, FDJ_PARI_GAGNE);

    fdj_periode_t p;
    fdj_periode_calcule(&e, T0, 7, &p);
    OK(p.nb == 1 && p.mise_ct == 1000 && p.gain_ct == 2500 && p.net_ct == 1500, "7 jours");
    fdj_periode_calcule(&e, T0, 30, &p);
    OK(p.nb == 2 && p.mise_ct == 3000 && p.gain_ct == 2500 && p.net_ct == -500, "30 jours");
    fdj_periode_calcule(&e, T0, 365, &p);
    OK(p.nb == 3 && p.mise_ct == 6000 && p.gain_ct == 8500 && p.net_ct == 2500, "1 an");
    fdj_periode_calcule(&e, T0, 1, &p);
    OK(p.nb == 0 && p.net_ct == 0, "24 h vides");

    uint32_t s = 0, m = 0;
    fdj_depenses(&e, T0, &s, &m);
    OK(s == 1000 && m == 3000, "depenses calculees depuis l'historique");
}

static void test_hasard(void)
{
    uint32_t a = 12345, b = 12345;
    bool meme = true;
    for (int i = 0; i < 100; i++) if (fdj_rng_next(&a) != fdj_rng_next(&b)) meme = false;
    OK(meme, "generateur deterministe a graine egale");

    uint32_t s = 99;
    int boite[10] = { 0 };
    for (int i = 0; i < 10000; i++) boite[fdj_rng_below(&s, 10)]++;
    bool uniforme = true;
    for (int i = 0; i < 10; i++) if (boite[i] < 850 || boite[i] > 1150) uniforme = false;
    OK(uniforme, "tirage quasi uniforme (10 000 tirages)");

    s = 5;
    bool jamais_hors = true;
    for (int i = 0; i < 5000; i++) if (fdj_rng_below(&s, 49) > 48) jamais_hors = false;
    OK(jamais_hors, "fdj_rng_below reste dans [0,n[");

    s = 77;
    bool code_ok = true;
    for (int i = 0; i < 200; i++) {
        char c[4];
        fdj_code_aleatoire(&s, c);
        if (strlen(c) != 3) code_ok = false;
        for (int k = 0; k < 3; k++) {
            if (!((c[k] >= 'A' && c[k] <= 'Z') || (c[k] >= '2' && c[k] <= '9'))) code_ok = false;
            if (c[k] == 'I' || c[k] == 'O') code_ok = false;    /* lisibilite */
        }
    }
    OK(code_ok, "codes de coupon lisibles (3 caracteres, sans I/O/0/1)");
}

static void test_loto(void)
{
    uint32_t s = 2026;
    bool tout_bon = true;
    int vu[50] = { 0 };
    for (int g = 0; g < 2000; g++) {
        uint8_t n[FDJ_GRILLE_NB], ch = 0;
        fdj_loto_grille(&s, n, &ch);
        int somme = 0, pairs = 0, bas = 0;
        for (int i = 0; i < FDJ_GRILLE_NB; i++) {
            if (n[i] < 1 || n[i] > 49) tout_bon = false;
            vu[n[i]]++;
            somme += n[i];
            if ((n[i] & 1) == 0) pairs++;
            if (n[i] <= 24) bas++;
            for (int j = 0; j < i; j++) if (n[j] == n[i]) tout_bon = false;
            if (i && n[i] < n[i - 1]) tout_bon = false;      /* triee */
        }
        if (somme < 100 || somme > 180) tout_bon = false;
        if (pairs < 2 || pairs > 3) tout_bon = false;
        if (bas < 2 || bas > 3) tout_bon = false;
        if (ch < 1 || ch > 10) tout_bon = false;
    }
    OK(tout_bon, "2000 grilles : 5/49 uniques, triees, equilibrees, chance 1-10");
    int manquants = 0;
    for (int i = 1; i <= 49; i++) if (vu[i] == 0) manquants++;
    OK(manquants == 0, "tous les numeros 1..49 sortent (aucun oubli)");
}

static void test_poisson(void)
{
    float somme = 0;
    for (int k = 0; k < 30; k++) somme += fdj_poisson_pmf(k, 1.3f);
    PROCHE(somme, 1.0, 0.001, "la loi de Poisson somme a 1");

    float p1 = fdj_p_gagne(1.4f, 1.0f);
    float pn = fdj_p_nul(1.4f, 1.0f);
    float p2 = 1.0f - p1 - pn;
    PROCHE(p1 + pn + p2, 1.0, 0.001, "1 + N + 2 = 1");
    OK(p1 > p2, "le favori gagne plus souvent");
    OK(pn > 0.15f && pn < 0.35f, "match nul plausible");

    float o25 = fdj_p_plus_de(2.6f, 0, 2.5f);
    float o25_85 = fdj_p_plus_de(0.15f, 0, 2.5f);
    OK(o25 > 0.45f && o25 < 0.65f, "plus de 2,5 buts : ~55 % avant match");
    OK(o25 > o25_85, "la probabilite baisse quand il reste 5 minutes");
    PROCHE(fdj_p_plus_de(2.6f, 3, 2.5f), 1.0, 0.001, "3 buts deja marques : certain");
    PROCHE(fdj_p_plus_de(2.6f, 1, 1.5f), 1.0f - expf(-2.6f), 0.002, "1 but : il en faut un de plus");

    float b0 = fdj_p_btts(1.3f, 1.3f, 0, 0);
    float b1 = fdj_p_btts(1.3f, 1.3f, 1, 0);
    PROCHE(b1, 1.0f - expf(-1.3f), 0.002, "une equipe a marque : seule l'autre compte");
    OK(b0 < b1 && b0 > 0.3f, "BTTS coherent");
    PROCHE(fdj_p_btts(1.3f, 1.3f, 1, 1), 1.0, 0.001, "deux equipes ont marque");

    float ld, le, ld2, le2;
    fdj_lambdas(0, 0, 10, &ld, &le);
    fdj_lambdas(0, 0, 80, &ld2, &le2);
    OK(ld > ld2 && le > le2, "lambda restant decroit avec la minute");
    PROCHE(ld, le, 0.0001, "match equilibre : lambdas egales");

    OK(fdj_cote_milli(0.5f) == 2000, "proba 50 % -> cote 2,00");
    OK(fdj_cote_milli(0.95f) == 1053, "proba 95 % -> cote 1,05");
    OK(fdj_cote_milli(0.0f) == 50000, "proba nulle -> plafond");
}

static void test_top10(void)
{
    fdj_live_t live;
    memset(&live, 0, sizeof(live));
    live.n = 3;
    fdj_txt_copie(live.m[0].ligue, sizeof(live.m[0].ligue), "LIGUE 1");
    fdj_txt_copie(live.m[0].dom, sizeof(live.m[0].dom), "TROYES");
    fdj_txt_copie(live.m[0].ext, sizeof(live.m[0].ext), "LE HAVRE");
    live.m[0].buts_dom = 1; live.m[0].buts_ext = 0; live.m[0].minute = 63; live.m[0].live = true;
    fdj_txt_copie(live.m[1].ligue, sizeof(live.m[1].ligue), "SERIE A");
    fdj_txt_copie(live.m[1].dom, sizeof(live.m[1].dom), "ROME");
    fdj_txt_copie(live.m[1].ext, sizeof(live.m[1].ext), "MILAN");
    live.m[1].buts_dom = 2; live.m[1].buts_ext = 2; live.m[1].minute = 80; live.m[1].live = true;
    fdj_txt_copie(live.m[2].ligue, sizeof(live.m[2].ligue), "BUNDESLIGA");
    fdj_txt_copie(live.m[2].dom, sizeof(live.m[2].dom), "DORTMUND");
    fdj_txt_copie(live.m[2].ext, sizeof(live.m[2].ext), "BAYERN");
    live.m[2].buts_dom = 0; live.m[2].buts_ext = 3; live.m[2].minute = 71; live.m[2].live = true;

    fdj_top_t top;
    uint32_t g = 4242;
    int n = fdj_top_calcule(&live, &g, T0, &top);
    OK(n > 0 && n <= FDJ_SEL_MAX, "10 selections au maximum");
    OK(top.n == n, "compte coherent");
    bool trie = true, codes = true;
    for (int i = 1; i < n; i++) if (top.s[i].score > top.s[i - 1].score + 1e-6f) trie = false;
    for (int i = 0; i < n; i++) if (strlen(top.s[i].code) != 3) codes = false;
    OK(trie, "classe par score decroissant");
    OK(codes, "chaque selection porte un code de coupon");

    int par[3] = { 0, 0, 0 };
    for (int i = 0; i < n; i++) if (top.s[i].idx_match < 3) par[top.s[i].idx_match]++;
    OK(par[0] <= 3 && par[1] <= 3 && par[2] <= 3, "au plus 3 selections par match");
    OK(par[0] + par[1] + par[2] == n, "toutes les selections viennent des matchs");

    bool probas_ok = true;
    for (int i = 0; i < n; i++) if (top.s[i].proba <= 0.05f || top.s[i].proba >= 0.97f) probas_ok = false;
    OK(probas_ok, "aucun pari deja gagne dans le top (ni 0 %, ni 100 %)");

    /* deterministe : meme graine, meme sortie */
    fdj_top_t top2;
    uint32_t g2 = 4242;
    fdj_top_calcule(&live, &g2, T0, &top2);
    OK(memcmp(&top.s, &top2.s, sizeof(top.s)) == 0, "meme graine = memes codes");

    fdj_live_t vide;
    memset(&vide, 0, sizeof(vide));
    OK(fdj_top_calcule(&vide, &g, T0, &top) == 0 && top.vide, "aucun match : aucun pari invente");
}

static void test_ascii(void)
{
    char b[32];
    fdj_ascii("Świt Szczecin", b, sizeof(b));
    OK(strcmp(b, "Swit Szczecin") == 0, "pliage du s accentue");
    fdj_ascii("São Paulo", b, sizeof(b));
    OK(strcmp(b, "Sao Paulo") == 0, "pliage du a tilde");
    fdj_ascii("Bodø/Glimt", b, sizeof(b));
    OK(strcmp(b, "Bodo/Glimt") == 0, "pliage du o barre");
    fdj_ascii("Murang'a Seal", b, sizeof(b));
    OK(strcmp(b, "Murang'a Seal") == 0, "apostrophe conservee");
    fdj_ascii("", b, sizeof(b));
    OK(b[0] == 0, "chaine vide");
    char petit[4];
    fdj_ascii("ABCDEFGH", petit, sizeof(petit));
    OK(strlen(petit) == 3, "troncature propre (pas de debordement)");

    fdj_txt_copie(b, sizeof(b), "123456789012345678901234567890123");
    OK(strlen(b) == sizeof(b) - 1, "copie bornee");
}

int main(void)
{
    test_defauts();
    test_serialisation();
    test_code();
    test_profils();
    test_plafonds();
    test_pariers();
    test_periodes();
    test_hasard();
    test_loto();
    test_poisson();
    test_top10();
    test_ascii();
    FIN("modele metier");
}
