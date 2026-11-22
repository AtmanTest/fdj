/* affect.c - moteur affectif : evaluation -> systeme dynamique -> visage.
 *
 * Principe scientifique (deux theories qui se completent) :
 *  - theorie de l'evaluation (Scherer, composant-process) : une emotion nait de
 *    l'evaluation d'un evenement selon quelques axes (pertinence, accord avec
 *    mes buts, pouvoir d'agir, normes, lien social) ;
 *  - espace PAD (Mehrabian) : tout etat affectif se projette en valence,
 *    arousal, dominance ; les emotions sont des prototypes dans cet espace.
 * Le modele dynamique est un Ornstein-Uhlenbeck par axe (retour a la ligne de
 * base + bruit), plus une humeur lente, plus des neuromodulateurs qui modulent
 * le gain (le cortisol amplifie le negatif, l'ocytocine le positif, la fatigue
 * abaisse le seuil d'agacement). Rien n'est code en dur par situation : tout
 * sort des equations, donc deux insultes identiques ne produisent pas la meme
 * emotion selon l'etat interne.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "affect.h"

/* ---- prototypes : coordonnees PAD des expressions du visage --------------- */
static const proto_t PROTO[] = {
    /* joie */
    {"sourire_franc",    0.62f,  0.42f,  0.35f},
    {"rire_franc",       0.88f,  0.80f,  0.42f},
    {"beatitude",        0.78f,  0.10f,  0.30f},
    {"joie_exub",        1.00f,  0.95f,  0.50f},
    {"sourire_narquis",  0.45f,  0.35f,  0.55f},
    /* tendresse */
    {"tendresse",        0.62f,  0.05f,  0.28f},
    {"adoration",        0.85f,  0.30f,  0.15f},
    {"bienveillance",    0.52f,  0.05f,  0.32f},
    {"compassion",       0.12f, -0.18f, -0.20f},
    {"soulagement",      0.48f, -0.15f,  0.25f},
    /* fierte */
    {"fierte",           0.68f,  0.35f,  0.75f},
    {"triomphe",         0.85f,  0.62f,  0.85f},
    {"assurance",        0.45f,  0.15f,  0.70f},
    /* calme */
    {"neutre",           0.00f,  0.00f,  0.00f},
    {"calme",            0.15f, -0.45f,  0.25f},
    {"zen",              0.28f, -0.60f,  0.35f},
    {"sourire_en_coin",  0.30f, -0.10f,  0.30f},
    /* curiosite / reflexion */
    {"curiosite",        0.28f,  0.48f,  0.10f},
    {"fascination",      0.48f,  0.60f, -0.05f},
    {"concentration",    0.08f,  0.25f,  0.30f},
    {"reflexion",        0.00f,  0.12f,  0.18f},
    {"doute",           -0.08f,  0.22f, -0.20f},
    {"perplexite",      -0.18f,  0.45f, -0.35f},
    {"confusion",       -0.25f,  0.55f, -0.45f},
    /* surprise */
    {"surprise",         0.05f,  0.78f, -0.18f},
    {"etonnement",       0.22f,  0.60f, -0.05f},
    {"incredulite",     -0.12f,  0.48f,  0.10f},
    /* colere */
    {"agacement",       -0.42f,  0.42f,  0.22f},
    {"frustration",     -0.58f,  0.50f, -0.28f},
    {"colere_contenue", -0.55f,  0.32f,  0.12f},
    {"colere",          -0.75f,  0.70f,  0.55f},
    {"rage",            -0.95f,  0.95f,  0.65f},
    /* degout */
    {"mepris",          -0.60f,  0.25f,  0.45f},
    {"rejet",           -0.45f, -0.15f,  0.10f},
    /* peur / anxiete */
    {"inquietude",      -0.32f,  0.38f, -0.45f},
    {"anxiete",         -0.45f,  0.62f, -0.60f},
    {"peur",            -0.70f,  0.82f, -0.75f},
    {"tension",         -0.28f,  0.48f, -0.15f},
    /* tristesse */
    {"tristesse",       -0.62f, -0.35f, -0.50f},
    {"melancolie",      -0.48f, -0.42f, -0.38f},
    {"chagrin",         -0.80f,  0.15f, -0.60f},
    {"desespoir",       -0.92f, -0.10f, -0.80f},
    {"deception",       -0.55f, -0.10f, -0.30f},
    {"regret",          -0.42f,  0.10f, -0.35f},
    {"bouderie",        -0.35f, -0.20f,  0.02f},
    {"blase",           -0.22f, -0.35f, -0.15f},
    /* gene */
    {"gene",            -0.18f,  0.28f, -0.48f},
    {"embarras",        -0.28f,  0.42f, -0.55f},
    {"timidite",        -0.08f,  0.28f, -0.62f},
    {"malaise",         -0.25f,  0.18f, -0.50f},
    {"honte",           -0.58f,  0.30f, -0.70f},
    /* douleur / fatigue */
    {"douleur",         -0.85f,  0.58f, -0.55f},
    {"souffrance",      -0.78f,  0.08f, -0.70f},
    {"fatigue",         -0.30f, -0.65f, -0.48f},
    {"accablement",     -0.70f, -0.50f, -0.75f},
};
static const int PROTO_N = (int)(sizeof(PROTO) / sizeof(PROTO[0]));

/* ---- constantes de temps (secondes) -------------------------------------- */
static const float TAU_V = 200.0f, TAU_A = 75.0f, TAU_D = 420.0f;
static const float TAU_MOOD = 1800.0f;
static const float SIGMA = 0.38f;             /* nettete des prototypes */
static const float WV = 1.00f, WA = 0.55f, WD = 0.70f;

static float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* bruit deterministe : pas de rand() qui partage un etat global avec le reste */
static unsigned int g_seed = 0x12345678u;
static float bruit(void)
{
    g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5;
    return ((float)(g_seed & 0xFFFF) / 32768.0f) - 1.0f;   /* -1..1 */
}

void affect_init(affect_t *e)
{
    memset(e, 0, sizeof(*e));
    e->cort = 0.15f; e->oxy = 0.35f; e->trust = 0.50f;
    e->energy = 0.85f; e->social_need = 0.70f; e->satiety = 0.80f;
    e->idx = -1;
}

static float deficit(const affect_t *e)
{
    return clampf((1.0f - e->energy) * 0.30f +
                  (1.0f - e->social_need) * 0.35f +
                  (1.0f - e->satiety) * 0.35f, 0.0f, 1.0f);
}

void affect_tick(affect_t *e, const appraisal_t *app, float dt)
{
    if (dt <= 0.0f) return;
    if (dt > 5.0f) dt = 5.0f;                 /* pas de saut numerique */

    float def = deficit(e);
    float uv = 0.0f, ua = 0.0f, ud = 0.0f;

    if (app) {
        float R = clampf(app->relevance, 0.0f, 1.0f);
        /* le gain depend de l'etat : fatigue et cortisol rendent plus sensible
           au negatif, l'ocytocine au positif. C'est ce couplage qui fait qu'une
           meme phrase ne produit pas la meme emotion deux jours de suite. */
        float gain_pos = clampf(1.0f + 0.70f * e->oxy - 0.25f * e->fatigue, 0.40f, 1.80f);
        float gain_neg = clampf(1.0f + 1.20f * e->cort + 1.60f * e->fatigue, 0.40f, 3.00f)
                         * (1.0f - 0.45f * e->trust);

        /* habituation : le dixieme reproche identique pese moins... sauf si le
           cortisol monte, ou la repetition sensibilise au contraire. */
        float hab_p = 1.0f / (1.0f + 0.60f * e->rep_pos);
        float hab_n = 1.0f / (1.0f + 0.35f * e->rep_neg) * (1.0f + 0.50f * e->cort);

        float c   = clampf(app->congruence, -1.0f, 1.0f);
        float soc = clampf(app->social, -1.0f, 1.0f);
        float od  = clampf(app->other_distress, 0.0f, 1.0f);
        float g   = (c >= 0.0f) ? gain_pos * hab_p : gain_neg * hab_n;

        uv = R * (0.90f * c * g + 0.35f * soc - 0.30f * od);
        ua = R * (0.50f * fabsf(c) + 0.35f * fabsf(app->coping) + 0.25f * app->novelty)
             * (1.0f + 0.45f * e->cort);
        ud = R * (0.55f * app->coping + 0.15f * app->norm);
        /* l'autre a franchi la ligne (insulte, injustice) : la dominance monte.
           C'est ce qui separe la colere (je me grandis) de la peur (je me tas). */
        if (app->norm < -0.50f) ud += 0.30f * R;

        /* impulsion (l'evenement) : elle se dissipe ensuite selon tau */
        e->v += uv; e->a += ua; e->d += ud;
        e->since_event_s = 0.0f;

        /* traces persistantes : l'empathie et la chaleur ne s'evanouissent pas
           au tick suivant, sinon le visage change avant meme qu'on ait repondu */
        e->od_s  = clampf(e->od_s + 0.85f * od * R, 0.0f, 1.5f);
        e->soc_s = clampf(e->soc_s + 0.60f * soc * R, -1.0f, 1.0f);

        /* neuromodulateurs : increments discrets, pas une petite derive d'une
           seconde (un evenement est bref, ses consequences sont longues) */
        e->cort  = clampf(e->cort + 0.30f * fmaxf(0.0f, -uv)
                          + 0.10f * fmaxf(0.0f, -soc) * R, 0.0f, 1.0f);
        e->oxy   = clampf(e->oxy + 0.12f * fmaxf(0.0f, soc) * R + 0.08f * od * R, 0.0f, 1.0f);
        e->trust = clampf(e->trust
                          + 0.05f * fmaxf(0.0f, soc) * R
                          + 0.03f * (c > 0.40f ? 1.0f : 0.0f)
                          - 0.09f * (soc < -0.60f ? 1.0f : 0.0f) * R
                          - 0.04f * (c < -0.60f ? 1.0f : 0.0f) * R, 0.0f, 1.0f);
        e->da    = clampf(e->da + 0.35f * R * (0.8f * c + 0.2f * soc), -1.0f, 1.0f);

        if (uv > 0.05f) e->rep_pos += 1.0f;
        else if (uv < -0.05f) {
            e->rep_neg += 1.0f;
            e->rum_int = clampf(e->rum_int * 0.5f + fabsf(uv), 0.0f, 1.0f);
        }
    } else {
        e->since_event_s += dt;
    }

    /* ligne de base : les besoins et la charge allostatique deplacent le point
       d'equilibre (faim + fatigue = monde plus sombre et plus irritable). */
    float base_v = -0.28f * def - 0.42f * e->fatigue + 0.15f * e->oxy;
    float base_a =  0.30f * def - 0.45f * e->fatigue + 0.20f * e->cort - 0.10f * e->oxy;
    float base_d = -0.20f * def + 0.10f * (e->trust - 0.5f);

    /* relaxation vers la base + rappel de l'humeur + bruit biologique */
    e->v += dt / TAU_V  * (base_v - e->v) + dt / (3.0f * TAU_V) * (e->mv - e->v);
    e->a += dt / TAU_A  * (base_a - e->a) + dt / (3.0f * TAU_A) * (e->ma - e->a);
    e->d += dt / TAU_D  * (base_d - e->d) + dt / (3.0f * TAU_D) * (e->md - e->d);
    e->v += 0.018f * sqrtf(dt) * bruit();
    e->a += 0.014f * sqrtf(dt) * bruit();
    e->d += 0.010f * sqrtf(dt) * bruit();

    /* rumination : quand le valence est basse et l'activation faible, la
       pensee tourne en boucle et rattrape le retour a l'equilibre. */
    if (!app && e->v < -0.15f && e->a < 0.40f && e->rum_int > 0.05f) {
        e->rum_t += dt;
        if (e->rum_t > 90.0f) {
            e->rum_t = 0.0f;
            e->v -= 0.06f * e->rum_int * (1.0f + 0.5f * e->cort);
            e->a += 0.03f * e->rum_int;
            e->rum_int *= 0.85f;
        }
    } else {
        e->rum_t = 0.0f;
        e->rum_int *= expf(-dt / 900.0f);
    }

    /* humeur : moyenne lente de l'affect vecu */
    e->mv += dt / TAU_MOOD * (e->v - e->mv);
    e->ma += dt / TAU_MOOD * (e->a - e->ma);
    e->md += dt / TAU_MOOD * (e->d - e->md);

    /* degradation lente ------------------------------------------------------
       le fond revient vers un niveau personnel, pas vers zero : un robot qui a
       un lien solide reste plus doux qu'un robot qui vient d'etre insulte. */
    e->da   += dt / 120.0f  * (0.0f - e->da);
    e->cort += dt / 900.0f  * ((0.08f + 0.35f * def) - e->cort);
    e->oxy  += dt / 1800.0f * ((0.25f + 0.35f * e->trust) - e->oxy);
    e->od_s  *= expf(-dt / 600.0f);
    e->soc_s *= expf(-dt / 500.0f);

    /* charge allostatique : monte avec le stress, redescend lentement au calme */
    float montee = (0.60f * e->cort + 0.50f * fmaxf(0.0f, -e->v) + 0.30f * def) / 14400.0f;
    float descente = (0.05f + 0.15f * fmaxf(0.0f, e->v)) / 3600.0f;
    e->fatigue = clampf(e->fatigue + dt * (montee - descente), 0.0f, 1.0f);

    /* habituation : les repetitions s'effacent en ~10 min */
    e->rep_pos *= expf(-dt / 600.0f);
    e->rep_neg *= expf(-dt / 600.0f);

    /* bornes */
    e->v = clampf(e->v, -1.0f, 1.0f);
    e->a = clampf(e->a, -1.0f, 1.0f);
    e->d = clampf(e->d, -1.0f, 1.0f);

    /* ---- surface : quel visage ? ---- */
    float meilleur = -1e9f, second = -1e9f;
    int bi = 0;
    for (int i = 0; i < PROTO_N; i++) {
        float dv = e->v - PROTO[i].v, da = e->a - PROTO[i].a, dd = e->d - PROTO[i].d;
        float d2 = WV * dv * dv + WA * da * da + WD * dd * dd;
        float score = -d2 / (2.0f * SIGMA * SIGMA);

        /* biais venant de l'etat interne : l'empathie, le lien, la charge.
           Ils utilisent les traces persistantes (od_s, soc_s) : sinon le visage
           retomberait sur autre chose des le tick suivant l'evenement. */
        float od = e->od_s;
        if (!strcmp(PROTO[i].nom, "compassion"))   score += 1.20f * od;
        if (!strcmp(PROTO[i].nom, "tendresse"))    score += 0.60f * od + 0.30f * e->oxy;
        if (!strcmp(PROTO[i].nom, "souffrance"))   score += 0.25f * od;
        if (!strcmp(PROTO[i].nom, "douleur") || !strcmp(PROTO[i].nom, "souffrance"))
            score += (e->pain > 0.20f) ? (1.80f * e->pain) : -2.50f;
        if (!strcmp(PROTO[i].nom, "bienveillance")) score += 0.30f * e->trust;
        if (!strcmp(PROTO[i].nom, "anxiete"))      score += 0.30f * e->cort;
        if (!strcmp(PROTO[i].nom, "tension"))      score += 0.25f * e->cort;
        if (!strcmp(PROTO[i].nom, "fatigue"))      score += 0.35f * e->fatigue;
        if (!strcmp(PROTO[i].nom, "agacement"))    score += 0.30f * def + 0.60f * e->fatigue;

        if (score > meilleur) { second = meilleur; meilleur = score; bi = i; }
        else if (score > second) second = score;
    }

    float intens = sqrtf((e->v - e->mv) * (e->v - e->mv)
                         + 0.6f * (e->a - e->ma) * (e->a - e->ma)
                         + 0.5f * (e->d - e->md) * (e->d - e->md)) / 1.4f;
    e->intensity = clampf(intens, 0.0f, 1.0f);

    if (e->idx < 0) {
        e->idx = bi; e->dwell_s = 0.0f;
    } else {
        float dv = e->v - PROTO[e->idx].v, da = e->a - PROTO[e->idx].a, dd = e->d - PROTO[e->idx].d;
        float d2c = WV * dv * dv + WA * da * da + WD * dd * dd;
        float score_cur = -d2c / (2.0f * SIGMA * SIGMA);
        e->dwell_s += dt;
        /* hysteresis : il faut un vrai basculement, et pas avant 2,5 s, sauf
           effondrement (un evenement fort change le visage tout de suite). */
        bool fort = (e->intensity > 0.25f);
        if (bi != e->idx &&
            ((e->dwell_s > 2.5f && (meilleur > score_cur + (fort ? 1.20f : 0.15f))) ||
             (meilleur > score_cur + 6.00f))) {
            e->idx = bi; e->dwell_s = 0.0f;
        }
    }
}

const char *affect_label_name(const affect_t *e)
{
    if (e->idx < 0 || e->idx >= PROTO_N) return "neutre";
    return PROTO[e->idx].nom;
}

void affect_prompt_line(const affect_t *e, char *out, int n)
{
    const char *humeur = (e->mv > 0.18f) ? "claire"
                        : (e->mv < -0.18f) ? "assombrie" : "proche du neutre";
    snprintf(out, (size_t)n,
             "MON ETAT INTERIEUR (il est reel, ne l'invente pas) : je ressens %s, "
             "intensite %.0f%%, depuis %.0f secondes. Mon humeur est %s. "
             "Stress %.0f%%, attachement %.0f%%, fatigue %.0f%%, energie %.0f%%. "
             "Mes mots doivent aller avec cet etat.",
             affect_label_name(e), e->intensity * 100.0f,
             (double)e->dwell_s, humeur,
             (double)(e->cort * 100.0f), (double)(e->trust * 100.0f),
             (double)(e->fatigue * 100.0f), (double)(e->energy * 100.0f));
}

int affect_bytes(void) { return (int)sizeof(affect_t); }
