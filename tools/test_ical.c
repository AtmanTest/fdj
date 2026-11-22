/* test_ical.c : verifie sur le PC le parseur .ics du firmware, sans la carte.
 *
 *     gcc -O1 -o /tmp/test_ical tools/test_ical.c -lm
 *     TZ=Europe/Paris /tmp/test_ical /tmp/test.ics
 *
 * Le code de parsing est copie tel quel de main/main.cpp : si ca casse ici,
 * c'est le firmware qui est en cause, pas le reseau.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long jours_depuis_1970(int y, int m, int d)
{
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static long decalage_local(void)
{
    time_t now = time(NULL);
    struct tm l;
    localtime_r(&now, &l);
    long jours = jours_depuis_1970(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
    long meme_heure_en_utc = jours * 86400L + l.tm_hour * 3600L + l.tm_min * 60L + l.tm_sec;
    return meme_heure_en_utc - (long)now;
}

static long ical_epoch(const char *v)
{
    int Y, M, D, h = 0, mi = 0, s = 0;
    if (sscanf(v, "%4d%2d%2dT%2d%2d%2d", &Y, &M, &D, &h, &mi, &s) >= 3) {
        if (M < 1 || M > 12 || D < 1 || D > 31) return 0;
        long j = jours_depuis_1970(Y, M, D);
        long e = j * 86400L + h * 3600L + mi * 60L + s;
        if (!strchr(v, 'Z')) e -= decalage_local();
        return e;
    }
    return 0;
}

static long ical_occurrence(long debut, const char *rrule, long now)
{
    if (debut <= 0) return 0;
    if (!rrule || !*rrule) return debut >= now ? debut : 0;
    int daily = strstr(rrule, "FREQ=DAILY") != NULL;
    int weekly = strstr(rrule, "FREQ=WEEKLY") != NULL;
    if (!daily && !weekly) return debut >= now ? debut : 0;
    int intervalle = 1;
    const char *p = strstr(rrule, "INTERVAL=");
    if (p) { intervalle = atoi(p + 9); if (intervalle < 1) intervalle = 1; }
    long until = 0;
    p = strstr(rrule, "UNTIL=");
    if (p) until = ical_epoch(p + 6);
    int compte = -1;
    p = strstr(rrule, "COUNT=");
    if (p) compte = atoi(p + 6);
    long pas = (daily ? 86400L : 604800L) * intervalle;
    long t = debut;
    for (int i = 0; i < 700 && t < now + 30 * 86400L; i++) {
        if (t >= now) {
            if (until && t > until) return 0;
            return t;
        }
        t += pas;
        if (until && t > until) return 0;
        if (compte > 0 && i + 1 >= compte) return 0;
    }
    return 0;
}

static void ical_nettoie(char *s)
{
    char *d = s;
    for (char *p = s; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            *d++ = (*p == 'n' || *p == 'N') ? ' ' : *p;
        } else if (*p == '\r' || *p == '\n') {
            continue;
        } else {
            *d++ = *p;
        }
    }
    *d = 0;
    while (d > s && (d[-1] == ' ' || d[-1] == '\t')) *--d = 0;
}

int main(int argc, char **argv)
{
    const char *chemin = argc > 1 ? argv[1] : "/tmp/test.ics";
    FILE *f = fopen(chemin, "rb");
    if (!f) { perror("ouverture"); return 1; }
    fseek(f, 0, SEEK_END);
    long taille = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)taille + 1);
    fread(buf, 1, (size_t)taille, f);
    buf[taille] = 0;
    fclose(f);

    long now = (long)time(NULL);
    printf("octets lus : %ld, evenements : ", taille);
    int n = 0;
    for (char *q = buf; (q = strstr(q, "BEGIN:VEVENT")) != NULL; q += 12) n++;
    printf("%d\n", n);
    printf("decalage local : %ld s\n", decalage_local());

    long meilleur = 0;
    char meilleur_titre[64] = "";
    char ligne[512];
    int ll = 0;
    int dans = 0;
    long debut = 0;
    char titre[128] = "";
    char rrule[192] = "";
    const char *p = buf;
    while (*p) {
        const char *fin = strchr(p, '\n');
        int ln = fin ? (int)(fin - p) : (int)strlen(p);
        if (ln > 0 && (p[0] == ' ' || p[0] == '\t')) {
            if (ll + ln - 1 < (int)sizeof(ligne) - 1) {
                memcpy(ligne + ll, p + 1, (size_t)ln - 1);
                ll += ln - 1;
                ligne[ll] = 0;
            }
        } else {
            if (ll > 0) {
                if (!strncmp(ligne, "BEGIN:VEVENT", 12)) {
                    dans = 1; debut = 0; titre[0] = 0; rrule[0] = 0;
                } else if (!strncmp(ligne, "END:VEVENT", 10) && dans) {
                    long occ = ical_occurrence(debut, rrule, now);
                    printf("  evenement '%s' debut=%ld prochaine=%ld\n", titre, debut, occ);
                    if (occ > 0 && (meilleur == 0 || occ < meilleur)) {
                        meilleur = occ;
                        snprintf(meilleur_titre, sizeof(meilleur_titre), "%s",
                                 titre[0] ? titre : "reunion");
                    }
                    dans = 0;
                } else if (dans && !strncmp(ligne, "DTSTART", 7)) {
                    char *c = strchr(ligne, ':');
                    if (c) debut = ical_epoch(c + 1);
                } else if (dans && !strncmp(ligne, "SUMMARY", 7)) {
                    char *c = strchr(ligne, ':');
                    if (c) { snprintf(titre, sizeof(titre), "%s", c + 1); ical_nettoie(titre); }
                } else if (dans && !strncmp(ligne, "RRULE", 5)) {
                    char *c = strchr(ligne, ':');
                    if (c) snprintf(rrule, sizeof(rrule), "%s", c + 1);
                }
            }
            ln = 0;
            while (p[ln] && p[ln] != '\n' && p[ln] != '\r' && ln < (int)sizeof(ligne) - 1) {
                ligne[ln] = p[ln];
                ln++;
            }
            ligne[ln] = 0;
            ll = ln;
        }
        p = fin ? fin + 1 : p + strlen(p);
    }

    if (meilleur > 0) {
        struct tm e;
        time_t tt = (time_t)meilleur;
        localtime_r(&tt, &e);
        printf("RESULTAT : '%s' a %02d:%02d, dans %d min\n",
               meilleur_titre, e.tm_hour, e.tm_min, (int)((meilleur - now) / 60));
        return 0;
    }
    printf("RESULTAT : aucun rendez-vous a venir\n");
    return 2;
}
