/*  tools/pettest.cpp - verifie le moteur tamagotchi en accelere (hors carte).
 *  g++ -O2 -o pettest pettest.cpp ../main/pet.cpp
 */
#include "../main/pet.h"
#include <stdio.h>
#include <string.h>

static const char *nom_stade(int s) { return pet_stade_nom(s); }

/* scenario : 1 = enfant parfait, 2 = negligent, 3 = moyen */
static void scenario(int mode)
{
    pet_state_t p;
    pet_nait(&p, 1);
    int st_vu = -1, sante_vue = -1;
    printf("\n=== scenario %d ===\n", mode);
    for (uint32_t t = 0; t < 25u * 86400u; t += 60) {
        /* la nuit : 22 h -> 8 h */
        int heure = (int)((t / 3600) % 24);
        pet_set_nuit(&p, heure >= 22 || heure < 8, heure);
        pet_tick(&p, 60);
        if (p.parti) {
            printf("t=%6.2f j : PARTI (age %.2f j, care_mistakes %u, malade %u)\n",
                   t / 86400.0, p.age_s / 86400.0, p.care_mistakes, p.malade);
            break;
        }
        /* un parent qui s'occupe : toutes les 20 min */
        if ((t % 1200) == 0 && mode != 2) {
            pet_action_t b = pet_besoin(&p);
            if (b == ACT_AUCUNE && mode == 1) {
                if (p.bonheur < 300) b = ACT_JOUER;
                else if (p.faim < 300) b = ACT_MANGER;
                else if (p.caca) b = ACT_NETTOYER;
            }
            if (b != ACT_AUCUNE) pet_action(&p, b);
        }
        int st = (p.age_s < 20) ? ST_OEUF : 0;
        int cur = 0;
        {   /* stade courant, recalcule comme le fait pet_tick */
            uint32_t a = p.age_s;
            if (a >= 20) cur = ST_BEBE;
            if (a >= 20 + 1200) cur = ST_ENFANT;
            if (a >= 20 + 1200 + 10800) cur = ST_ADO;
            if (a >= 20 + 1200 + 10800 + 10800) cur = ST_ADULTE;
        }
        (void)st;
        if (cur != st_vu) {
            st_vu = cur;
            printf("t=%6.2f j : stade %-7s forme %-8s sante %3u care %u dm %u jauges %u/%u/%u/%u obe %u\n",
                   t / 86400.0, nom_stade(cur), pet_forme_nom(p.forme), pet_sante(&p),
                   p.care_mistakes, p.discipline_mistakes,
                   p.faim / 100, p.bonheur / 100, p.energie / 100, p.proprete / 100, p.obeissance);
        }
        int s = pet_sante(&p);
        if (s / 20 != sante_vue / 20) {
            if (sante_vue >= 0)
                printf("t=%6.2f j : sante %d -> %d (faim %u bonh %u prop %u caca %u malade %u)\n",
                       t / 86400.0, sante_vue, s, p.faim, p.bonheur, p.proprete, p.caca, p.malade);
            sante_vue = s;
        }
    }
    printf("fin : age %.2f j, stade %d, forme %s, sante %u, care_mistakes %u, obeissance %u\n",
           p.age_s / 86400.0, (int)(p.age_s / 86400), pet_forme_nom(p.forme),
           pet_sante(&p), p.care_mistakes, p.obeissance);

    /* sauvegarde / relecture */
    char buf[512];
    int n = pet_serialise(&p, buf, sizeof(buf));
    pet_state_t q;
    memset(&q, 0, sizeof(q));
    pet_deserialise(&q, buf);
    printf("persistance : %d octets -> age %u/%u faim %u/%u care %u/%u forme %u/%u %s\n",
           n, p.age_s, q.age_s, p.faim, q.faim, p.care_mistakes, q.care_mistakes,
           p.forme, q.forme,
           (p.age_s == q.age_s && p.faim == q.faim && p.forme == q.forme) ? "OK" : "ECHEC");
}

int main(void)
{
    scenario(1);
    scenario(2);
    scenario(3);
    return 0;
}
