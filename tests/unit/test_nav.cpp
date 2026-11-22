/*  test_nav.cpp — la table des gestes, exactement celle de la page 1. */
#include "check.h"
#include "fdj_nav.h"

static void test_deplacement(void)
{
    fdj_nav_t n;
    fdj_nav_init(&n, 1, 0);                 /* page 2, premier item */
    OK(fdj_nav_geste(&n, 10, 1, GESTE_COURT, 80) == FDJ_ACT_SUIVANT, "B2 court : item suivant");
    OK(n.curseur == 1 && n.page == 1, "curseur avance, page inchangee");
    for (int i = 1; i < 10; i++) fdj_nav_geste(&n, 10, 1, GESTE_COURT, 80);
    OK(n.curseur == 0, "apres le dernier item, retour au premier");
    OK(n.page == 2, "et on passe a la page suivante : B2 suffit pour tout faire");

    fdj_nav_init(&n, FDJ_PAGES - 1, 0);      /* page 19, 4 items */
    for (int i = 0; i < 4; i++) fdj_nav_geste(&n, 4, 1, GESTE_COURT, 80);
    OK(n.page == 0 && n.curseur == 0, "apres la page 19, on revient page 1");

    fdj_nav_init(&n, 3, 0);
    OK(fdj_nav_geste(&n, 3, 1, GESTE_REPEAT, 1000) == FDJ_ACT_SUIVANT, "appui tenu : defilement");
    OK(n.curseur == 1 && n.page == 3, "le defilement avance d'un item");
}

static void test_validation(void)
{
    fdj_nav_t n;
    fdj_nav_init(&n, 6, 2);
    OK(fdj_nav_geste(&n, 5, 1, GESTE_DOUBLE, 90) == FDJ_ACT_PAGE, "B2x2 : valider la page");
    OK(n.curseur == 2, "valider ne deplace pas le curseur");

    OK(fdj_nav_geste(&n, 5, 0, GESTE_COURT, 70) == FDJ_ACT_COCHER, "B1 court : cocher");
    OK(fdj_nav_geste(&n, 5, 0, GESTE_DOUBLE, 90) == FDJ_ACT_DECOCHER, "B1x2 : annuler");
    OK(n.curseur == 2, "cocher ne deplace pas le curseur");

    fdj_nav_init(&n, 8, 0);
    OK(fdj_nav_geste(&n, 4, 0, GESTE_LONG, 700) == FDJ_ACT_PRECEDENT, "B1 long : page precedente");
    OK(n.page == 7, "on remonte d'une page");
    fdj_nav_init(&n, 0, 0);
    OK(fdj_nav_geste(&n, 4, 0, GESTE_LONG, 700) == FDJ_ACT_PRECEDENT, "page 1 : precedent existe");
    OK(n.page == FDJ_PAGES - 1, "page 1 -> page 19 (boucle)");
}

static void test_accueil(void)
{
    fdj_nav_t n;
    fdj_nav_init(&n, 12, 3);
    OK(fdj_nav_geste(&n, 4, 1, GESTE_LONG, 700) == FDJ_ACT_AUCUN, "B2 long 0,7 s : rien");
    OK(fdj_nav_geste(&n, 4, 1, GESTE_REPEAT, 2100) == FDJ_ACT_SUIVANT, "B2 tenu 2,1 s : on defile");
    OK(fdj_nav_geste(&n, 4, 1, GESTE_REPEAT, 3100) == FDJ_ACT_ACCUEIL, "B2 tenu 3,1 s : page 1");
    OK(n.page == 0 && n.curseur == 0, "retour a l'accueil, curseur remis");
    OK(fdj_nav_geste(&n, 4, 1, GESTE_REPEAT, 3500) == FDJ_ACT_AUCUN, "un seul retour par appui");
    OK(n.page == 0, "la page ne bouge plus");
}

static void test_code(void)
{
    fdj_nav_t n;
    fdj_nav_init(&n, 14, 0);
    fdj_nav_mode(&n, FDJ_MODE_CODE);
    OK(fdj_nav_code(&n) == 0, "code vide = 0000");
    for (int i = 0; i < 4; i++) fdj_nav_geste(&n, 6, 1, GESTE_COURT, 80);
    OK(n.pos == 0, "la position revient a 0 apres les 4 chiffres");
    fdj_nav_geste(&n, 6, 0, GESTE_COURT, 80);          /* +1 sur le 1er */
    fdj_nav_geste(&n, 6, 1, GESTE_COURT, 80);
    fdj_nav_geste(&n, 6, 0, GESTE_COURT, 80);
    OK(fdj_nav_code(&n) == 1100, "saisie chiffre par chiffre");
    fdj_nav_geste(&n, 6, 0, GESTE_DOUBLE, 90);         /* -1 -> 0100 */
    OK(fdj_nav_code(&n) == 1000, "double appui B1 : decrement du chiffre courant");
    OK(fdj_nav_geste(&n, 6, 1, GESTE_DOUBLE, 90) == FDJ_ACT_CODE_OK, "B2x2 : code soumis");
    OK(fdj_nav_geste(&n, 6, 0, GESTE_LONG, 700) == FDJ_ACT_CODE_ANNULE, "B1 long : annuler la saisie");
    /* 9 + 1 = 0 : on tourne */
    fdj_nav_mode(&n, FDJ_MODE_CODE);
    for (int i = 0; i < 10; i++) fdj_nav_geste(&n, 6, 0, GESTE_COURT, 80);
    OK(fdj_nav_code(&n) == 0, "9999 + 1 revient a 0000 sur le premier chiffre");
}

static void test_confirme(void)
{
    fdj_nav_t n;
    fdj_nav_init(&n, 17, 0);
    fdj_nav_mode(&n, FDJ_MODE_CONFIRME);
    OK(fdj_nav_geste(&n, 3, 0, GESTE_COURT, 80) == FDJ_ACT_AUCUN, "B1 inverse le choix");
    OK(n.oui, "OUI selectionne");
    OK(fdj_nav_geste(&n, 3, 1, GESTE_DOUBLE, 90) == FDJ_ACT_CONFIRME, "B2x2 confirme");
    fdj_nav_mode(&n, FDJ_MODE_CONFIRME);
    OK(fdj_nav_geste(&n, 3, 1, GESTE_DOUBLE, 90) == FDJ_ACT_CODE_ANNULE, "NON annule");
}

int main(void)
{
    test_deplacement();
    test_validation();
    test_accueil();
    test_code();
    test_confirme();
    FIN("navigation");
}
