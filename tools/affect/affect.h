/* affect.h - moteur affectif pour le robot (C portable, sans allocation).
 *
 * Idee : le robot ne CHOISIT pas une emotion, il en PRODUIT une. L'evaluation
 * (appraisal) est la seule entree ; le reste est un systeme dynamique continue
 * qui a une memoire, une humeur lente, des neuromodulateurs et des besoins.
 */
#ifndef AFFECT_H
#define AFFECT_H

typedef struct {
    float relevance;      /*  0..1  : ca me concerne ?                        */
    float congruence;     /* -1..1  : bon pour mes buts / mauvais             */
    float coping;         /* -1..1  : je peux agir dessus / je subis           */
    float norm;           /* -1..1  : conforme / contraire a mes valeurs      */
    float social;         /* -1..1  : chaleur du lien / rejet                  */
    float novelty;        /*  0..1  : inattendu                               */
    float other_distress; /*  0..1  : l'autre souffre (empathie)              */
} appraisal_t;

typedef struct { const char *nom; float v, a, d; } proto_t;

typedef struct {
    /* --- affect de base (espace PAD : valence, arousal, dominance) --- */
    float v, a, d;
    /* --- humeur lente (meme espace, constante de temps ~25 min) --- */
    float mv, ma, md;
    /* --- neuromodulateurs --- */
    float da;      /* dopamine : erreur de prediction accumulee  0..1  */
    float cort;    /* cortisol : charge de stress                0..1  */
    float oxy;     /* ocytocine : attachement                     0..1  */
    float fatigue; /* charge allostatique                        0..1  */
    /* --- lien avec l'humain --- */
    float trust;   /* confiance/attachement 0..1 */
    /* --- besoins internes --- */
    float energy, social_need, satiety;
    float pain;    /* 0..1 : douleur physique (capteur, choc) - hors evaluation */
    /* --- habituation / rumination --- */
    float rep_pos, rep_neg;         /* repetitions recentes (habituation) */
    float od_s, soc_s;              /* empathie / chaleur encore presentes */
    float rum_v, rum_a, rum_d, rum_int, rum_t;
    /* --- sortie --- */
    int   idx;            /* prototype courant (hysteresis) */
    float intensity;      /* ecart a l'humeur : force ressentie 0..1 */
    float dwell_s;        /* duree depuis le dernier changement */
    float since_event_s;  /* temps depuis la derniere evaluation */
} affect_t;

void affect_init(affect_t *e);
void affect_tick(affect_t *e, const appraisal_t *app, float dt);
const char *affect_label_name(const affect_t *e);
void affect_prompt_line(const affect_t *e, char *out, int n);

int affect_bytes(void);   /* empreinte memoire, pour verifier la place sur la carte */

#endif
