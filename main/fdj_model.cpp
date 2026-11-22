/*  fdj_model.cpp — la logique metier, sans une seule ligne ESP-IDF.
 *  Compile sur la carte (ESP32-S3) et sur le PC (tests/unit).
 */
#include "fdj_model.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

/* ================================================================== */
/* outillage                                                          */
/* ================================================================== */

void fdj_txt_copie(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* Plie l'UTF-8 en ASCII : le dessin de l'e-paper ne connait que 3x5 points,
   et une equipe polonaise s'affiche "SWIT SZCZECIN", pas "??". */
void fdj_ascii(const char *in, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (p && *p && o + 1 < cap) {
        unsigned c = *p;
        if (c < 0x80) {
            out[o++] = (char)c;
            p++;
            continue;
        }
        /* tables de pliage : les suites UTF-8 utiles (latin + latin ext) */
        unsigned cp = 0;
        int len = 1;
        if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xC0) != 0x80) { len = i; break; }
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        char r = '?';
        switch (cp) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: r = 'A'; break;
        case 0xC6: r = 'A'; break;
        case 0xC7: r = 'C'; break;
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: r = 'E'; break;
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: r = 'I'; break;
        case 0xD0: r = 'D'; break;
        case 0xD1: r = 'N'; break;
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: r = 'O'; break;
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: r = 'U'; break;
        case 0xDD: r = 'Y'; break;
        case 0xDE: r = 'T'; break;
        case 0xDF: r = 'S'; break;
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: r = 'a'; break;
        case 0xE6: r = 'a'; break;
        case 0xE7: r = 'c'; break;
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: r = 'e'; break;
        case 0xEC: case 0xED: case 0xEE: case 0xEF: r = 'i'; break;
        case 0xF0: r = 'd'; break;
        case 0xF1: r = 'n'; break;
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: r = 'o'; break;
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: r = 'u'; break;
        case 0xFD: case 0xFF: r = 'y'; break;
        case 0xFE: r = 't'; break;
        /* latin etendu A : les noms de clubs d'Europe centrale en ont besoin */
        case 0x100: case 0x102: case 0x104: case 0x1CD: r = 'A'; break;
        case 0x101: case 0x103: case 0x105: case 0x1CE: r = 'a'; break;
        case 0x106: case 0x10C: case 0x10E: r = 'C'; break;
        case 0x107: case 0x10D: case 0x10F: r = 'c'; break;
        case 0x110: r = 'D'; break;
        case 0x111: r = 'd'; break;
        case 0x118: case 0x11A: case 0x11C: case 0x11E: case 0x120: r = 'E'; break;
        case 0x119: case 0x11B: case 0x11D: case 0x11F: case 0x121: r = 'e'; break;
        case 0x130: r = 'I'; break;
        case 0x131: r = 'i'; break;
        case 0x136: r = 'K'; break;
        case 0x137: r = 'k'; break;
        case 0x139: case 0x13B: case 0x13D: case 0x141: r = 'L'; break;
        case 0x13A: case 0x13C: case 0x13E: case 0x142: r = 'l'; break;
        case 0x143: case 0x145: case 0x147: r = 'N'; break;
        case 0x144: case 0x146: case 0x148: r = 'n'; break;
        case 0x14C: case 0x14E: case 0x150: case 0x1D1: r = 'O'; break;
        case 0x14D: case 0x14F: case 0x151: case 0x1D2: r = 'o'; break;
        case 0x154: case 0x156: case 0x158: r = 'R'; break;
        case 0x155: case 0x157: case 0x159: r = 'r'; break;
        case 0x15A: case 0x15C: case 0x15E: case 0x160: r = 'S'; break;
        case 0x15B: case 0x15D: case 0x15F: case 0x161: r = 's'; break;
        case 0x162: case 0x164: case 0x166: r = 'T'; break;
        case 0x163: case 0x165: case 0x167: r = 't'; break;
        case 0x16A: case 0x16C: case 0x16E: case 0x170: case 0x172: r = 'U'; break;
        case 0x16B: case 0x16D: case 0x16F: case 0x171: case 0x173: r = 'u'; break;
        case 0x179: case 0x17B: case 0x17D: r = 'Z'; break;
        case 0x17A: case 0x17C: case 0x17E: r = 'z'; break;
        case 0x2013: case 0x2014: r = '-'; break;
        case 0x2018: case 0x2019: r = '\''; break;
        case 0x201C: case 0x201D: r = '"'; break;
        case 0x20AC: r = 'E'; break;
        default: r = '?'; break;
        }
        out[o++] = r;
        p += len > 0 ? len : 1;
    }
    out[o] = 0;
}

/* CRC32 (polynome 0xEDB88320) : detecte un blob NVS tronque ou corrompu. */
uint32_t fdj_crc32(const void *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        crc ^= b[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

/* ================================================================== */
/* etat par defaut                                                    */
/* ================================================================== */

void fdj_etat_defaut(fdj_etat_t *e, uint32_t graine)
{
    memset(e, 0, sizeof(*e));
    e->magic   = FDJ_MAGIC;
    e->version = FDJ_ETAT_VERSION;

    fdj_txt_copie(e->compte_nom, sizeof(e->compte_nom), "T. ZOU");
    fdj_txt_copie(e->compte_id, sizeof(e->compte_id), "FDJ-0001");
    e->code_boot  = 0;          /* 0000 : le code d'usine de la maquette */
    e->code_perso = false;

    /* profil par defaut : plafonds prudents, c'est celui de la maquette */
    e->nb_profils   = 1;
    e->profil_actif = 0;
    fdj_profil_t *p = &e->profils[0];
    fdj_txt_copie(p->nom, sizeof(p->nom), "DEFAUT");
    p->emoji        = '*';
    p->niveau       = 1;
    p->plafond_sem_ct  = 2500;      /* 25 EUR / 7 jours  */
    p->plafond_mois_ct = 6000;      /* 60 EUR / 30 jours */
    p->solde_ct        = 10000;     /* 100 EUR de demo   */
    p->cree            = true;

    e->hist_n    = 0;
    e->hist_next = 0;

    e->son_clic   = false;   /* ecran de démo : pas de son */
    e->news_auto  = true;
    e->gros_texte = false;
    e->mode_demo  = false;
    e->verif_age  = true;
    e->pause_24h  = false;
    e->pause_debut = 0;

    e->graine = graine ? graine : 0x1234567u;
    e->boots  = 0;
    e->page   = 0;
    e->curseur = 0;
    e->hist_off = 0;
}

bool fdj_etat_valide(const fdj_etat_t *e)
{
    if (!e) return false;
    if (e->magic != FDJ_MAGIC) return false;
    if (e->version != FDJ_ETAT_VERSION) return false;
    if (e->nb_profils < 1 || e->nb_profils > FDJ_PROFILS_MAX) return false;
    if (e->profil_actif >= e->nb_profils) return false;
    if (e->hist_n > FDJ_HIST_MAX) return false;
    if (e->hist_next >= FDJ_HIST_MAX) return false;
    if (e->code_boot > 9999) return false;
    if (e->page >= FDJ_PAGES) return false;
    if (e->curseur > 40) return false;
    for (int i = 0; i < e->nb_profils; i++) {
        const fdj_profil_t *p = &e->profils[i];
        if (!p->cree) return false;
        if (p->plafond_mois_ct && p->plafond_sem_ct > p->plafond_mois_ct) return false;
        if (p->nom[0] == 0) return false;
    }
    return true;
}

/* ================================================================== */
/* serialisation NVS : entete + struct brute + CRC                    */
/* ================================================================== */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t taille;
    uint32_t crc;
} fdj_blob_t;

size_t fdj_etat_serialise(const fdj_etat_t *e, uint8_t *out, size_t cap)
{
    size_t total = sizeof(fdj_blob_t) + sizeof(fdj_etat_t);
    if (!out || cap < total) return 0;
    fdj_blob_t h;
    h.magic   = FDJ_MAGIC;
    h.version = FDJ_ETAT_VERSION;
    h.taille  = (uint16_t)sizeof(fdj_etat_t);
    h.crc     = fdj_crc32(e, sizeof(fdj_etat_t));
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), e, sizeof(*e));
    return total;
}

bool fdj_etat_deserialise(fdj_etat_t *e, const uint8_t *in, size_t len)
{
    fdj_blob_t h;
    if (!e || !in) return false;
    if (len < sizeof(h) + sizeof(fdj_etat_t)) return false;
    memcpy(&h, in, sizeof(h));
    if (h.magic != FDJ_MAGIC || h.version != FDJ_ETAT_VERSION) return false;
    if (h.taille != sizeof(fdj_etat_t)) return false;
    if (h.crc != fdj_crc32(in + sizeof(h), sizeof(fdj_etat_t))) return false;
    memcpy(e, in + sizeof(h), sizeof(*e));
    return fdj_etat_valide(e);
}

/* ================================================================== */
/* code de demarrage                                                  */
/* ================================================================== */

bool fdj_code_valide(uint16_t code) { return code <= 9999; }

bool fdj_code_verifie(const fdj_etat_t *e, uint16_t code)
{
    if (!e || !fdj_code_valide(code)) return false;
    return e->code_boot == code;
}

bool fdj_code_change(fdj_etat_t *e, uint16_t code)
{
    if (!e || !fdj_code_valide(code)) return false;
    e->code_boot  = code;
    e->code_perso = (code != 0);
    return true;
}

/* ================================================================== */
/* profils                                                            */
/* ================================================================== */

int fdj_profil_ajoute(fdj_etat_t *e, const char *nom, uint8_t niveau,
                      uint32_t plafond_sem_ct, uint32_t plafond_mois_ct)
{
    if (!e) return -1;
    if (e->nb_profils >= FDJ_PROFILS_MAX) return -1;
    if (!nom || !nom[0]) return -1;
    if (niveau > 2) return -1;
    if (plafond_sem_ct == 0 || plafond_mois_ct == 0) return -1;
    if (plafond_sem_ct > plafond_mois_ct) return -1;
    /* pas de doublon de nom : les profils se ressemblent assez comme ca */
    for (int i = 0; i < e->nb_profils; i++)
        if (strcmp(e->profils[i].nom, nom) == 0) return -1;

    fdj_profil_t *p = &e->profils[e->nb_profils];
    memset(p, 0, sizeof(*p));
    fdj_ascii(nom, p->nom, sizeof(p->nom));
    for (char *c = p->nom; *c; c++)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    p->emoji            = niveau == 0 ? 'd' : (niveau == 1 ? 'r' : 'x');
    p->niveau           = niveau;
    p->plafond_sem_ct   = plafond_sem_ct;
    p->plafond_mois_ct  = plafond_mois_ct;
    p->solde_ct         = 0;
    p->cree             = true;
    return e->nb_profils++;
}

bool fdj_profil_selectionne(fdj_etat_t *e, uint8_t idx)
{
    if (!e || idx >= e->nb_profils || !e->profils[idx].cree) return false;
    e->profil_actif = idx;
    return true;
}

/* Suppression reelle d'un profil non actif : le tableau est tasse et
   l'index du profil actif est corrige. Un profil actif n'est jamais
   supprime ici (la page demande d'en choisir un autre d'abord). */
int fdj_profil_supprime(fdj_etat_t *e, uint8_t idx)
{
    if (!e || idx >= e->nb_profils || !e->profils[idx].cree) return 2;
    if (idx == e->profil_actif) return 1;
    for (uint8_t i = idx; i + 1 < e->nb_profils; i++)
        e->profils[i] = e->profils[i + 1];
    e->nb_profils--;
    memset(&e->profils[e->nb_profils], 0, sizeof(e->profils[0]));
    if (e->profil_actif > idx) e->profil_actif--;
    if (e->profil_actif >= e->nb_profils) e->profil_actif = 0;
    return 0;
}

fdj_profil_t *fdj_profil_actif(fdj_etat_t *e)
{
    if (!e || e->profil_actif >= e->nb_profils) return NULL;
    return &e->profils[e->profil_actif];
}

const fdj_profil_t *fdj_profil_actif_c(const fdj_etat_t *e)
{
    if (!e || e->profil_actif >= e->nb_profils) return NULL;
    return &e->profils[e->profil_actif];
}

/* ================================================================== */
/* argent                                                             */
/* ================================================================== */

/* indice du k-ieme pari le plus recent (0 = le plus recent), -1 si absent */
int fdj_hist_index_recence(const fdj_etat_t *e, int k)
{
    if (!e || k < 0 || k >= e->hist_n) return -1;
    return (int)((e->hist_next + FDJ_HIST_MAX - 1 - k) % FDJ_HIST_MAX);
}

void fdj_depenses(const fdj_etat_t *e, int64_t now,
                  uint32_t *sem_ct, uint32_t *mois_ct)
{
    uint32_t s = 0, m = 0;
    if (e) {
        for (int i = 0; i < e->hist_n; i++) {
            int idx = fdj_hist_index_recence(e, i);
            if (idx < 0) continue;
            const fdj_pari_t *p = &e->hist[idx];
            if (p->t > now + 60) continue;            /* horodatage futur : ignore */
            int64_t age = now - p->t;
            if (age < 7LL * 86400)   s += p->mise_ct;
            if (age < 30LL * 86400)  m += p->mise_ct;
        }
    }
    if (sem_ct)  *sem_ct  = s;
    if (mois_ct) *mois_ct = m;
}

int fdj_mise_verifie(const fdj_etat_t *e, int64_t now, uint32_t mise_ct)
{
    if (!e) return 1;
    if (mise_ct == 0 || mise_ct > 2000) return 1;          /* 0 a 20 EUR par pari */
    if (fdj_pause_active(e, now)) return 5;
    const fdj_profil_t *p = fdj_profil_actif_c(e);
    if (!p) return 1;
    if (mise_ct > p->solde_ct) return 4;
    uint32_t sem = 0, mois = 0;
    fdj_depenses(e, now, &sem, &mois);
    uint32_t plaf_sem = fdj_plafond_sem_effectif(e);
    if (plaf_sem && sem + mise_ct > plaf_sem)  return 2;
    if (p->plafond_mois_ct && mois + mise_ct > p->plafond_mois_ct) return 3;
    return 0;
}

/* ---------------------------------------------------------------- */
/* jeu responsable : reponses et plafond effectif                    */
/* ---------------------------------------------------------------- */
int fdj_protection_score(const fdj_etat_t *e)
{
    if (!e) return 0;
    int n = 0;
    for (int i = 0; i < 5; i++)
        if (e->sel_cochee & (1u << (8 + i))) n++;
    return n;
}

/* Un joueur qui a commence a repondre et se declare fragile (moins de
   3 reponses positives) voit son plafond hebdomadaire divise par deux.
   Sans aucune reponse, on ne presume rien : le plafond du profil reste. */
uint32_t fdj_plafond_sem_effectif(const fdj_etat_t *e)
{
    const fdj_profil_t *p = fdj_profil_actif_c(e);
    if (!p) return 0;
    int s = fdj_protection_score(e);
    if (s > 0 && s < 3) return p->plafond_sem_ct / 2;
    return p->plafond_sem_ct;
}

/* ---------------------------------------------------------------- */
/* pause 24 h : une vraie echeance, pas un simple booléen            */
/* ---------------------------------------------------------------- */
bool fdj_pause_active(const fdj_etat_t *e, int64_t now)
{
    if (!e || !e->pause_24h) return false;
    /* sans horloge valide, on ne fabrique pas d'echeance : la pause reste
       active et l'ecran ecrit que la fin est inconnue. */
    if (now <= 0 || e->pause_debut <= 0) return true;
    return now < e->pause_debut + 86400;
}

void fdj_pause_active_maj(fdj_etat_t *e, int64_t now)
{
    if (!e || !e->pause_24h) return;
    if (now > 0 && e->pause_debut > 0 && now >= e->pause_debut + 86400) {
        e->pause_24h   = false;
        e->pause_debut = 0;
    }
}

/* secondes restantes, -1 si l'echeance ne peut pas etre calculee */
int64_t fdj_pause_restant_s(const fdj_etat_t *e, int64_t now)
{
    if (!fdj_pause_active(e, now)) return 0;
    if (now <= 0 || e->pause_debut <= 0) return -1;
    return e->pause_debut + 86400 - now;
}

int fdj_pari_ajoute(fdj_etat_t *e, const char *libelle, const char *code,
                    uint32_t mise_ct, uint32_t gain_pot_ct, int64_t now,
                    uint8_t jeu, uint8_t statut)
{
    if (!e || !libelle) return -1;
    if (statut > FDJ_PARI_PERDU) return -1;
    if (jeu >= FDJ_JEU_NB) return -1;

    fdj_pari_t *p = &e->hist[e->hist_next];
    memset(p, 0, sizeof(*p));
    fdj_ascii(libelle, p->libelle, sizeof(p->libelle));
    for (char *c = p->libelle; *c; c++)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    if (code) fdj_txt_copie(p->code, sizeof(p->code), code);
    p->mise_ct = mise_ct;
    p->gain_ct = gain_pot_ct;
    p->t       = now;
    p->jeu     = jeu;
    p->statut  = statut;

    int idx = e->hist_next;
    e->hist_next = (uint8_t)((e->hist_next + 1) % FDJ_HIST_MAX);
    if (e->hist_n < FDJ_HIST_MAX) e->hist_n++;

    /* le solde baisse immediatement : l'argent est engage, pas depense. */
    fdj_profil_t *pr = fdj_profil_actif(e);
    if (pr && pr->solde_ct >= mise_ct) pr->solde_ct -= mise_ct;
    return idx;
}

int fdj_coupon_valide(fdj_etat_t *e, const fdj_top_t *top,
                      const uint8_t grilles[][FDJ_GRILLE_NB],
                      const uint8_t *chances, int nb_grilles,
                      int64_t now, char *msg, size_t cap)
{
    if (msg && cap) msg[0] = 0;
    if (!e) return 1;

    int coches = 0;
    if (top) for (int i = 0; i < top->n; i++)
        if (e->sel_cochee & (1u << i)) coches++;
    int loto = 0;
    for (int g = 0; g < nb_grilles && g < FDJ_GRILLE_MAX; g++)
        if (e->grille_cochee & (1u << g)) loto++;

    if (coches == 0 && loto == 0) {
        if (msg) snprintf(msg, cap, "AUCUNE SELECTION");
        return 1;
    }

    uint32_t total = (uint32_t)coches * 200u + (uint32_t)loto * 220u;
    int refus = fdj_mise_verifie(e, now, total);
    if (refus) {
        static const char *M[6] = {
            "", "MISE INVALIDE", "PLAFOND SEMAINE ATTEINT", "PLAFOND MOIS ATTEINT",
            "SOLDE INSUFFISANT", "PAUSE JEU RESPONSABLE",
        };
        if (msg) snprintf(msg, cap, "%s", M[refus > 5 ? 1 : refus]);
        return refus;
    }

    for (int i = 0; top && i < top->n; i++) {
        if (!(e->sel_cochee & (1u << i))) continue;
        char lib[FDJ_TXT_L + 4];
        snprintf(lib, sizeof(lib), "%s %s", top->s[i].match, top->s[i].marche);
        uint32_t gain = top->s[i].proba > 0.02f ? (uint32_t)(200.0f / top->s[i].proba) : 200u;
        fdj_pari_ajoute(e, lib, top->s[i].code, 200u, gain, now,
                        FDJ_JEU_SPORT, FDJ_PARI_EN_COURS);
    }
    for (int g = 0; g < nb_grilles && g < FDJ_GRILLE_MAX; g++) {
        if (!(e->grille_cochee & (1u << g))) continue;
        char lib[FDJ_TXT_L + 4];
        snprintf(lib, sizeof(lib), "LOTO %02u %02u %02u %02u %02u C%u",
                 grilles[g][0], grilles[g][1], grilles[g][2],
                 grilles[g][3], grilles[g][4], chances ? chances[g] : 0);
        fdj_pari_ajoute(e, lib, "LOT", 220u, 0, now, FDJ_JEU_LOTO, FDJ_PARI_EN_COURS);
    }
    e->sel_cochee = 0;
    e->grille_cochee = 0;
    if (msg) snprintf(msg, cap, "COUPON VALIDE");
    return 0;
}

void fdj_pari_regle(fdj_etat_t *e, uint8_t idx, bool gagne)
{
    if (!e || idx >= FDJ_HIST_MAX) return;
    fdj_pari_t *p = &e->hist[idx];
    if (p->statut != FDJ_PARI_EN_COURS) return;
    p->statut = gagne ? FDJ_PARI_GAGNE : FDJ_PARI_PERDU;
    if (gagne) {
        fdj_profil_t *pr = fdj_profil_actif(e);
        if (pr) pr->solde_ct += p->gain_ct;
    }
}

void fdj_periode_calcule(const fdj_etat_t *e, int64_t now, int jours,
                         fdj_periode_t *out)
{
    fdj_periode_t r = { 0, 0, 0, 0 };
    if (e) {
        int64_t fenetre = (int64_t)jours * 86400;
        for (int i = 0; i < e->hist_n; i++) {
            int idx = fdj_hist_index_recence(e, i);
            if (idx < 0) continue;
            const fdj_pari_t *p = &e->hist[idx];
            int64_t age = now - p->t;
            if (age < 0 || age >= fenetre) continue;
            r.mise_ct += p->mise_ct;
            /* un pari en cours n'a pas encore rapporte : son gain potentiel
               n'entre pas dans les gains, sinon la page mentirait. */
            if (p->statut == FDJ_PARI_GAGNE) r.gain_ct += p->gain_ct;
            r.nb++;
        }
    }
    r.net_ct = (int32_t)r.gain_ct - (int32_t)r.mise_ct;
    if (out) *out = r;
}

/* ================================================================== */
/* hasard                                                            */
/* ================================================================== */

uint32_t fdj_rng_next(uint32_t *s)
{
    if (!s) return 0;
    uint32_t x = *s ? *s : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0x6D2B79F5u;
    return *s;
}

/* tirage sans biais : on rejette la zone qui ne se partage pas en n. */
uint32_t fdj_rng_below(uint32_t *s, uint32_t n)
{
    if (n == 0) return 0;
    uint32_t lim = 0xFFFFFFFFu - (0xFFFFFFFFu % n);
    uint32_t r;
    do { r = fdj_rng_next(s); } while (r >= lim);
    return r % n;
}

void fdj_code_aleatoire(uint32_t *s, char out[4])
{
    static const char A[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* sans I/O/0/1 */
    for (int i = 0; i < 3; i++) out[i] = A[fdj_rng_below(s, sizeof(A) - 1)];
    out[3] = 0;
}

/* ================================================================== */
/* loto                                                              */
/* ================================================================== */

static int cmp_u8(const void *a, const void *b)
{
    return (int)(*(const uint8_t *)a) - (int)(*(const uint8_t *)b);
}

/* Une grille "equilibree" : c'est la seule astuce honnete a donner.
   Elle ne change PAS la probabilite (1 sur 19 068 840), elle evite juste
   les grilles que des milliers de joueurs cochent aussi (1-2-3-4-5). */
static bool grille_equilibree(const uint8_t *n)
{
    int somme = 0, pairs = 0, bas = 0;
    for (int i = 0; i < FDJ_GRILLE_NB; i++) {
        somme += n[i];
        if ((n[i] & 1) == 0) pairs++;
        if (n[i] <= 24) bas++;
    }
    if (somme < 100 || somme > 180) return false;
    if (pairs < 2 || pairs > 3) return false;          /* 2 ou 3 pairs */
    if (bas < 2 || bas > 3) return false;              /* 2 ou 3 bas   */
    for (int i = 1; i < FDJ_GRILLE_NB; i++)
        if (n[i] == n[i - 1]) return false;
    /* pas plus de 3 numeros consecutifs : ca arrive, mais rarement */
    int suite = 1, max_suite = 1;
    for (int i = 1; i < FDJ_GRILLE_NB; i++) {
        if (n[i] == n[i - 1] + 1) { suite++; if (suite > max_suite) max_suite = suite; }
        else suite = 1;
    }
    return max_suite <= 3;
}

bool fdj_loto_grille(uint32_t *s, uint8_t num[FDJ_GRILLE_NB], uint8_t *chance)
{
    if (!s || !num) return false;
    uint8_t t[FDJ_GRILLE_NB];
    for (int essai = 0; essai < 400; essai++) {
        for (int i = 0; i < FDJ_GRILLE_NB; i++) {
            bool dup;
            do {
                t[i] = (uint8_t)(1 + fdj_rng_below(s, 49));
                dup = false;
                for (int j = 0; j < i; j++) if (t[j] == t[i]) { dup = true; break; }
            } while (dup);
        }
        qsort(t, FDJ_GRILLE_NB, 1, cmp_u8);
        if (grille_equilibree(t)) {
            memcpy(num, t, sizeof(t));
            if (chance) *chance = (uint8_t)(1 + fdj_rng_below(s, 10));
            return true;
        }
    }
    /* 400 essais sans grille acceptable : on rend la derniere, triee.
       (statistiquement ~1 chance sur 10^4, mais on ne laisse jamais
       l'ecran sans grille) */
    memcpy(num, t, sizeof(t));
    if (chance) *chance = (uint8_t)(1 + fdj_rng_below(s, 10));
    return false;
}

/* ================================================================== */
/* modele sportif : Poisson                                          */
/* ================================================================== */

float fdj_poisson_pmf(int k, float lambda)
{
    if (k < 0) return 0.0f;
    if (lambda <= 0.0f) return k == 0 ? 1.0f : 0.0f;
    if (lambda > 12.0f) lambda = 12.0f;
    double p = exp(-(double)lambda);
    for (int i = 1; i <= k; i++) p *= (double)lambda / (double)i;
    return (float)p;
}

void fdj_lambdas(int buts_dom, int buts_ext, int minute, float *ldom, float *lext)
{
    if (minute < 0) minute = 0;
    if (minute > 90) minute = 90;
    float rem = (float)(90 - minute) / 90.0f;
    /* 2,6 buts par match en moyenne (Ligue 1 2023-2025 : 2,7) */
    float l = 1.30f * rem;
    int diff = buts_dom - buts_ext;
    if (diff >= 2)       l *= 0.92f;      /* un match plie se calme */
    else if (diff <= -2) l *= 0.92f;
    if (ldom) *ldom = l;
    if (lext) *lext = l;
}

float fdj_p_gagne(float ldom, float lext)
{
    float p = 0.0f;
    for (int b = 0; b <= 7; b++) {          /* buts du domicile  */
        float pb = fdj_poisson_pmf(b, ldom);
        for (int a = 0; a <= 7; a++) {      /* buts de l'exterieur */
            if (b > a) p += pb * fdj_poisson_pmf(a, lext);
        }
    }
    return p;
}

float fdj_p_nul(float ldom, float lext)
{
    float p = 0.0f;
    for (int a = 0; a <= 7; a++)
        p += fdj_poisson_pmf(a, ldom) * fdj_poisson_pmf(a, lext);
    return p;
}

float fdj_p_plus_de(float lambda_total, int deja_marques, float seuil)
{
    if (deja_marques < 0) deja_marques = 0;
    int besoin = (int)ceilf(seuil);                 /* 2,5 -> 3 buts au total */
    int reste  = besoin - deja_marques;
    if (reste <= 0) return 1.0f;
    float p_moins = 0.0f;
    for (int k = 0; k < reste; k++)
        p_moins += fdj_poisson_pmf(k, lambda_total);
    if (p_moins > 1.0f) p_moins = 1.0f;
    return 1.0f - p_moins;
}

float fdj_p_btts(float ldom, float lext, int buts_dom, int buts_ext)
{
    float ph = buts_dom > 0 ? 1.0f : 1.0f - expf(-ldom);
    float pa = buts_ext > 0 ? 1.0f : 1.0f - expf(-lext);
    float p = ph * pa;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

uint32_t fdj_cote_milli(float proba)
{
    if (proba <= 0.001f) return 50000;               /* plafond d'affichage */
    float c = 1.0f / proba;
    if (c < 1.01f) c = 1.01f;
    if (c > 50.0f) c = 50.0f;
    return (uint32_t)(c * 1000.0f + 0.5f);
}

/* Classement : on note chaque selection par sa probabilite, on garde
   au plus 3 par match (sinon les 10 lignes parlent d'un seul match),
   et on ne garde que les marches qu'un joueur comprend en 3 mots.
   Deux garde-fous de demo :
     - un pari deja gagne (p > 97 %) n'a aucun interet, il est retire ;
     - a probabilite voisine, un grand championnat passe devant, parce que
       c'est ce que le visiteur reconnait. Le % affiche reste le modele. */
static float bonus_ligue(const char *ligue)
{
    if (!ligue) return 0.0f;
    if (strstr(ligue, "LIGUE 1"))      return 0.08f;
    if (strstr(ligue, "LIGUE 2"))      return 0.05f;
    if (strstr(ligue, "PREMIER"))      return 0.07f;
    if (strstr(ligue, "SERIE A"))      return 0.06f;
    if (strstr(ligue, "BUNDESLIGA"))   return 0.06f;
    if (strstr(ligue, "LIGA"))         return 0.05f;
    if (strstr(ligue, "CHAMPIONS"))    return 0.06f;
    return 0.0f;
}

int fdj_top_calcule(const fdj_live_t *live, uint32_t *graine,
                    int64_t now, fdj_top_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->calcule = now;
    if (!live || live->n <= 0) { out->vide = true; return 0; }

    fdj_sel_t tmp[FDJ_MATCH_MAX * 6];
    int n = 0;

    for (int i = 0; i < live->n && i < FDJ_MATCH_MAX; i++) {
        const fdj_match_t *m = &live->m[i];
        float ld, le;
        fdj_lambdas(m->buts_dom, m->buts_ext, m->minute, &ld, &le);
        float total = ld + le;
        int deja = m->buts_dom + m->buts_ext;

        float p1  = fdj_p_gagne(ld, le);
        float pn  = fdj_p_nul(ld, le);
        float p2  = 1.0f - p1 - pn;
        float po  = fdj_p_plus_de(total, deja, 2.5f);
        float pb  = fdj_p_btts(ld, le, m->buts_dom, m->buts_ext);

        struct { const char *nom; float p; } mk[6] = {
            { "1",      p1 },
            { "N",      pn },
            { "2",      p2 },
            { "+2,5",   po },
            { "-2,5",   1.0f - po },
            { "BTTS O", pb },
        };
        for (int k = 0; k < 6 && n < (int)(sizeof(tmp) / sizeof(tmp[0])); k++) {
            if (mk[k].p < 0.05f || mk[k].p > 0.97f) continue;   /* deja joue */
            fdj_sel_t *s = &tmp[n++];
            memset(s, 0, sizeof(*s));
            snprintf(s->match, sizeof(s->match), "%s-%s", m->dom, m->ext);
            fdj_txt_copie(s->marche, sizeof(s->marche), mk[k].nom);
            fdj_txt_copie(s->ligue, sizeof(s->ligue), m->ligue);
            s->proba    = mk[k].p;
            s->score    = mk[k].p + bonus_ligue(m->ligue);
            s->idx_match = (uint8_t)i;
        }
    }
    if (n == 0) { out->vide = true; return 0; }

    /* tri par score decroissant (insertion : n <= 96) */
    for (int i = 1; i < n; i++) {
        fdj_sel_t v = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].score < v.score) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = v;
    }

    int par_match[FDJ_MATCH_MAX] = { 0 };
    int pris = 0;
    for (int i = 0; i < n && pris < FDJ_SEL_MAX; i++) {
        uint8_t im = tmp[i].idx_match;
        if (im < FDJ_MATCH_MAX && par_match[im] >= 3) continue;
        if (im < FDJ_MATCH_MAX) par_match[im]++;
        out->s[pris] = tmp[i];
        if (graine) fdj_code_aleatoire(graine, out->s[pris].code);
        else        fdj_txt_copie(out->s[pris].code, 4, "---");
        pris++;
    }
    out->n = pris;
    out->vide = (pris == 0);
    return pris;
}

const char *fdj_jeu_nom(uint8_t jeu)
{
    static const char *N[FDJ_JEU_NB] = {
        "PARIS SPORTIFS", "LOTO", "EUROMILLIONS", "KENO",
        "ILLIKO", "POKER", "COURSES", "CRILL",
    };
    return jeu < FDJ_JEU_NB ? N[jeu] : "JEU";
}

const char *fdj_niveau_nom(uint8_t niveau)
{
    static const char *N[3] = { "DEBUTANT", "REGULIER", "EXPERT" };
    return niveau < 3 ? N[niveau] : "?";
}
