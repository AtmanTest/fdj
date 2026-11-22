/*  fdj_app.cpp — l'application FDJ du BATMino : etat, reseau, sons, actions.
 *
 *  Trois taches, trois responsabilites :
 *    boutons  : deja dans main.cpp (echantillonnage) -> file de gestes
 *    app      : vide la file, applique l'intention, redessine
 *    reseau   : scores en direct (5 min) et titres (3 h), jamais bloquante
 *    sons     : une file de notes, pour qu'un bip ne retarde jamais l'ecran
 *
 *  Regle : aucune valeur inventee. Pas de reseau -> "N.D." ; pas de match ->
 *  "AUCUN MATCH EN DIRECT". Les pages le disent, l'utilisateur n'est pas dupe.
 */
#include "fdj_app.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/usb_serial_jtag.h"

#include "audio_bsp.h"
#include "art.h"
#include "geste.h"
#include "user_config.h"

#include "fdj_model.h"
#include "fdj_parse.h"
#include "fdj_ui.h"
#include "fdj_nav.h"

static const char *TAG = "fdj";

/* ------------------------------------------------------------------ */
/* etat global                                                       */
/* ------------------------------------------------------------------ */
static fdj_etat_t  etat;
static fdj_live_t  live;
static fdj_flux_t  flux;
static fdj_top_t   top;
static fdj_nav_t   nav;
static fdj_vue_t   vue;
static uint8_t     fb[ART_FB_LEN];
static uint8_t     fb_dernier[ART_FB_LEN];
static bool        fb_pret;

static SemaphoreHandle_t mux_etat;
static QueueHandle_t     q_gestes;
static QueueHandle_t     q_notes;

static fdj_ecran_fn g_ecran;
static const char  *g_ssid;
static bool         g_wifi, g_heure_ok, g_sd_ok;
static int          g_rssi;
static bool         g_code_affiche, g_code_erreur, g_code_change;
static char         g_message[48];
static int64_t      g_message_fin;
static int          g_rendu_ms, g_ecran_ms;
static int64_t      g_dernier_live, g_dernier_news;
static volatile bool g_rafraichir;
static volatile int  g_page_demande = -1;

static uint8_t g_grilles[FDJ_GRILLE_MAX][FDJ_GRILLE_NB];
static uint8_t g_chances[FDJ_GRILLE_MAX];

/* ------------------------------------------------------------------ */
/* petites aides                                                     */
/* ------------------------------------------------------------------ */
static int64_t maintenant(void) { return (int64_t)time(NULL); }

static void dit(const char *txt)
{
    snprintf(g_message, sizeof(g_message), "%s", txt);
    g_message_fin = esp_timer_get_time() / 1000 + 2600;
}

static bool message_vivant(void)
{
    return g_message[0] && (esp_timer_get_time() / 1000) < g_message_fin;
}

/* ------------------------------------------------------------------ */
/* NVS : un seul blob, versionne, avec CRC                           */
/* ------------------------------------------------------------------ */
#define NVS_NAMESPACE "fdj"
#define NVS_CLE       "etat"

static void etat_charge(void)
{
    nvs_handle_t h;
    uint8_t *blob = (uint8_t *)heap_caps_malloc(sizeof(fdj_etat_t) + 32, MALLOC_CAP_SPIRAM);
    size_t n = sizeof(fdj_etat_t) + 32;
    bool lu = false;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (blob && nvs_get_blob(h, NVS_CLE, blob, &n) == ESP_OK)
            lu = fdj_etat_deserialise(&etat, blob, n);
        nvs_close(h);
    }
    if (!lu) {
        uint32_t graine = (uint32_t)(esp_timer_get_time() ^ 0x5F3759DF);
        fdj_etat_defaut(&etat, graine);
        ESP_LOGI(TAG, "aucun etat sauvegarde : profil d'usine, code 0000");
    } else {
        ESP_LOGI(TAG, "etat relu : %d profils, %d paris", etat.nb_profils, etat.hist_n);
    }
    if (blob) free(blob);
    etat.boots++;
}

static void etat_sauve(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t blob[sizeof(fdj_etat_t) + 32];
    size_t n = fdj_etat_serialise(&etat, blob, sizeof(blob));
    if (n) nvs_set_blob(h, NVS_CLE, blob, n);
    nvs_commit(h);
    nvs_close(h);
}

/* ------------------------------------------------------------------ */
/* sons : file de notes, tache dediee                                */
/* ------------------------------------------------------------------ */
typedef struct { uint16_t freq; uint16_t ms; uint8_t vol; } note_t;

static void son_joue(uint16_t f, uint16_t ms, uint8_t vol)
{
    static int16_t pcm[512 * 2];
    uint8_t *octets = (uint8_t *)pcm;
    int total = (16000 * (int)ms) / 1000;
    int fait = 0;
    audio_playback_set_vol(vol);
    while (fait < total) {
        int n = total - fait;
        if (n > 512) n = 512;
        for (int i = 0; i < n; i++) {
            float t = (float)(fait + i) / 16000.0f;
            /* enveloppe : sans elle, chaque note claque (clics) */
            float env = 1.0f;
            int m = (fait + i) * 1000 / (total ? total : 1);
            if (m < 8) env = (float)m / 8.0f;
            if (m > 92) env = (float)(100 - m) / 8.0f;
            int16_t v = 0;
            if (f)
                v = (int16_t)(7000.0f * env * sinf(2.0f * 3.14159265f * (float)f * t));
            pcm[i * 2] = v;
            pcm[i * 2 + 1] = v;
        }
        audio_playback_write(octets, (uint32_t)n * 4);
        fait += n;
    }
}

static void son_file(uint16_t f, uint16_t ms, uint8_t vol)
{
    note_t nn = { f, ms, vol };
    if (q_notes) xQueueSend(q_notes, &nn, 0);
}

static void sons_tache(void *arg)
{
    (void)arg;
    note_t nn;
    for (;;) {
        if (xQueueReceive(q_notes, &nn, portMAX_DELAY) == pdTRUE)
            son_joue(nn.freq, nn.ms, nn.vol);
    }
}

static void son_positif(void)
{
    son_file(880, 70, 85);
    son_file(1320, 100, 85);
}

static void son_negatif(void)
{
    son_file(620, 80, 85);
    son_file(415, 140, 85);
}

/* le jingle d'accueil : 4,7 s, une fanfare originale (pas le theme FDJ) */
static void jingle(void)
{
    static const note_t M[] = {
        { 659, 180, 88 }, { 784, 180, 88 }, { 1047, 360, 88 }, { 0, 120, 0 },
        { 880, 180, 88 }, { 1047, 180, 88 }, { 1319, 540, 88 }, { 0, 160, 0 },
        { 1047, 180, 88 }, { 1175, 180, 88 }, { 1319, 360, 88 }, { 1568, 540, 88 },
        { 0, 300, 0 }, { 1319, 180, 88 }, { 1568, 180, 88 }, { 2093, 800, 88 },
    };
    for (size_t i = 0; i < sizeof(M) / sizeof(M[0]); i++)
        son_file(M[i].freq, M[i].ms, M[i].vol);
}

/* ------------------------------------------------------------------ */
/* HTTP : une reponse entiere en PSRAM, jamais de fuite              */
/* ------------------------------------------------------------------ */
static char *http_get(const char *url, size_t cap, size_t *len)
{
    if (!url || cap < 512) return NULL;
    char *buf = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)malloc(cap);
    if (!buf) return NULL;

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 12000;
    cfg.buffer_size = 4096;
    cfg.buffer_size_tx = 1024;
    cfg.disable_auto_redirect = false;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { free(buf); return NULL; }
    esp_http_client_set_header(c, "User-Agent", "BATMino-FDJ/3.0");
    esp_http_client_set_header(c, "Accept", "application/json, text/xml, */*");

    char *rendu = NULL;
    int total = 0;
    esp_err_t r = esp_http_client_open(c, 0);
    if (r == ESP_OK && esp_http_client_fetch_headers(c) >= 0) {
        int n;
        while ((n = esp_http_client_read(c, buf + total, (int)(cap - 1 - (size_t)total))) > 0) {
            total += n;
            if ((size_t)total >= cap - 2) break;
        }
        buf[total] = 0;
        if (total > 32) { rendu = buf; if (len) *len = (size_t)total; }
    }
    int statut = esp_http_client_get_status_code(c);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (!rendu) { free(buf); return NULL; }
    ESP_LOGI(TAG, "GET %s : HTTP %d, %d octets", url, statut, total);
    return rendu;
}

/* ------------------------------------------------------------------ */
/* donnees vivantes                                                   */
/* ------------------------------------------------------------------ */
#define URL_LIVE "https://www.thesportsdb.com/api/v1/json/123/livescore.php?s=Soccer"
#define URL_JOUR "https://www.thesportsdb.com/api/v1/json/123/eventsday.php?d=%04d-%02d-%02d&s=Soccer"

static void top_recalcule(void)
{
    uint32_t g = etat.graine;
    fdj_top_calcule(&live, &g, maintenant(), &top);
    /* on garde les coches qui existent encore */
    if (top.n == 0) etat.sel_cochee = 0;
}

static void live_rafraichit(void)
{
    size_t n = 0;
    char *js = http_get(URL_LIVE, 40 * 1024, &n);
    if (!js) { ESP_LOGW(TAG, "scores : pas de reponse"); return; }

    fdj_live_t nouveau;
    memset(&nouveau, 0, sizeof(nouveau));
    int nb = fdj_parse_events(js, false, &nouveau);
    free(js);

    if (nb == 0) {
        /* aucun direct : on prend les matchs du jour, clairement "a venir" */
        char url[160];
        time_t t = time(NULL);
        struct tm ti;
        localtime_r(&t, &ti);
        snprintf(url, sizeof(url), URL_JOUR, ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        js = http_get(url, 48 * 1024, &n);
        if (js) {
            nb = fdj_parse_events(js, true, &nouveau);
            free(js);
        }
    }
    if (xSemaphoreTake(mux_etat, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (nb > 0) {
            memcpy(&live, &nouveau, sizeof(live));
            live.ok = true;
            live.maj = maintenant();
            top_recalcule();
            ESP_LOGI(TAG, "scores : %d matchs, top %d selections", nb, top.n);
        } else {
            live.ok = false;
        }
        xSemaphoreGive(mux_etat);
    }
    g_rafraichir = true;
}

static void news_rafraichit(void)
{
    size_t n = 0;
    char *xml = http_get(INFOS_URL, 96 * 1024, &n);
    if (!xml) { ESP_LOGW(TAG, "titres : pas de reponse"); return; }
    fdj_flux_t f;
    memset(&f, 0, sizeof(f));
    int nb = fdj_parse_news(xml, &f);
    free(xml);
    if (xSemaphoreTake(mux_etat, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (nb > 0) {
            memcpy(&flux, &f, sizeof(flux));
            flux.ok = true;
            flux.maj = maintenant();
            ESP_LOGI(TAG, "titres : %d manchettes", nb);
        }
        xSemaphoreGive(mux_etat);
    }
    g_rafraichir = true;
}

static void net_tache(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));
    for (;;) {
        if (g_wifi) {
            int64_t t = maintenant();
            if (t - g_dernier_live > 300 || g_dernier_live == 0) {
                g_dernier_live = t;
                live_rafraichit();
            }
            if (t - g_dernier_news > 3 * 3600 || g_dernier_news == 0) {
                g_dernier_news = t;
                news_rafraichit();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* ------------------------------------------------------------------ */
/* rendu                                                             */
/* ------------------------------------------------------------------ */
static void grilles_genere(void)
{
    uint32_t g = etat.graine ^ 0xBEEF51u;
    for (int i = 0; i < FDJ_GRILLE_MAX; i++)
        fdj_loto_grille(&g, g_grilles[i], &g_chances[i]);
}

static void vue_remplit(void)
{
    bool pris = mux_etat && xSemaphoreTake(mux_etat, pdMS_TO_TICKS(200)) == pdTRUE;
    memset(&vue, 0, sizeof(vue));
    vue.page = nav.page;
    vue.curseur = nav.curseur;
    vue.e = &etat;
    vue.top = &top;
    vue.news = &flux;
    vue.live = &live;
    vue.now = g_heure_ok ? maintenant() : 0;
    vue.heure_ok = g_heure_ok;
    vue.ssid = g_ssid ? g_ssid : "";
    vue.wifi = g_wifi;
    vue.rssi = g_rssi;
    vue.bat_pct = -1;
    vue.heap_int_libre = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    vue.heap_int_max = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    vue.psram_libre = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    vue.sd_ok = g_sd_ok;
    vue.rendu_ms = g_rendu_ms;
    vue.ecran_ms = g_ecran_ms;
    vue.code_affiche = g_code_affiche;
    vue.nb_grilles = FDJ_GRILLE_MAX;
    memcpy(vue.grilles, g_grilles, sizeof(g_grilles));
    memcpy(vue.chances, g_chances, sizeof(g_chances));
    if (message_vivant()) vue.message = g_message;
    if (pris) xSemaphoreGive(mux_etat);
}

static void affiche(bool plein)
{
    vue_remplit();
    int64_t t0 = esp_timer_get_time();
    if (nav.mode == FDJ_MODE_CODE) {
        const char *titre = "CODE DE DEMARRAGE";
        fdj_ui_demande_code(fb, titre, nav.chiffres, nav.pos, g_code_erreur);
    } else if (nav.mode == FDJ_MODE_CONFIRME) {
        fdj_ui_confirme(fb, "EFFACER MES DONNEES", "TOUT PERDRE, POUR DE BON ?", nav.oui);
    } else {
        fdj_ui_dessine(&vue, fb);
    }
    g_rendu_ms = (int)((esp_timer_get_time() - t0) / 1000);
    int64_t t1 = esp_timer_get_time();
    if (g_ecran) g_ecran(fb, plein);
    g_ecran_ms = (int)((esp_timer_get_time() - t1) / 1000);
    memcpy(fb_dernier, fb, ART_FB_LEN);
    fb_pret = true;
}

/* ------------------------------------------------------------------ */
/* actions de page                                                   */
/* ------------------------------------------------------------------ */
static bool coupon_valide(void);

static void select_ou_cree_profil(void)
{
    if (nav.curseur < etat.nb_profils) {
        fdj_profil_selectionne(&etat, nav.curseur);
        dit("PROFIL CHANGE");
        son_positif();
        return;
    }
    if (etat.nb_profils >= FDJ_PROFILS_MAX) {
        dit("4 PROFILS MAXIMUM");
        son_negatif();
        return;
    }
    /* creation : le niveau suit le nombre de profils deja crees */
    static const struct { const char *n; uint8_t niv; uint32_t s, m; } P[3] = {
        { "DEBUTANT", 0, 1000, 3000 },
        { "REGULIER", 1, 2500, 6000 },
        { "EXPERT",   2, 5000, 20000 },
    };
    int k = etat.nb_profils % 3;
    if (fdj_profil_ajoute(&etat, P[k].n, P[k].niv, P[k].s, P[k].m) < 0) {
        dit("CREATION IMPOSSIBLE");
        son_negatif();
        return;
    }
    fdj_profil_selectionne(&etat, (uint8_t)(etat.nb_profils - 1));
    etat.profils[etat.profil_actif].solde_ct = 2000;
    dit("NOUVEAU PROFIL CREE");
    son_positif();
}

static void efface_tout(void)
{
    uint32_t graine = etat.graine;
    fdj_etat_defaut(&etat, graine);
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    etat_sauve();
    grilles_genere();
    fdj_nav_init(&nav, 0, 0);
    dit("DONNEES EFFACEES");
    son_negatif();
}

static void exporte_csv(void)
{
    if (!g_sd_ok) { dit("PAS DE CARTE SD"); son_negatif(); return; }
    FILE *f = fopen("/sdcard/fdj_historique.csv", "w");
    if (!f) { dit("ECRITURE IMPOSSIBLE"); son_negatif(); return; }
    fprintf(f, "date;jeu;libelle;code;mise_eur;gain_eur;statut\n");
    for (int i = 0; i < etat.hist_n; i++) {
        int idx = fdj_hist_index_recence(&etat, i);
        if (idx < 0) continue;
        const fdj_pari_t *p = &etat.hist[idx];
        time_t t = (time_t)p->t;
        struct tm ti;
        localtime_r(&t, &ti);
        fprintf(f, "%04d-%02d-%02d %02d:%02d;%s;%s;%s;%u,%02u;%u,%02u;%d\n",
                ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min,
                fdj_jeu_nom(p->jeu), p->libelle, p->code,
                (unsigned)(p->mise_ct / 100), (unsigned)(p->mise_ct % 100),
                (unsigned)(p->gain_ct / 100), (unsigned)(p->gain_ct % 100),
                (int)p->statut);
    }
    fclose(f);
    dit("EXPORTE SUR /sdcard");
    son_positif();
}

/* le coupon : tout est dans le modele (fdj_coupon_valide), teste sur le PC */
static bool coupon_valide(void)
{
    char msg[48];
    int r = fdj_coupon_valide(&etat, &top, g_grilles, g_chances, FDJ_GRILLE_MAX,
                              maintenant(), msg, sizeof(msg));
    if (r) {
        dit(msg);
        son_negatif();
        return false;
    }
    etat_sauve();
    dit(msg);
    son_positif();
    nav.page = 3;            /* on montre l'historique qui vient de se remplir */
    nav.curseur = 0;
    return true;
}

static void action_cocher(void)
{
    switch (nav.page) {
    case 0:          /* accueil : demarrer */
        nav.page = 1; nav.curseur = 0;
        son_positif();
        break;
    case 1: {        /* paris : cocher / decocher la selection */
        uint16_t bit = (uint16_t)(1u << nav.curseur);
        bool avant = (etat.sel_cochee & bit) != 0;
        etat.sel_cochee ^= bit;
        dit(avant ? "SELECTION RETIREE" : "SELECTION AJOUTEE AU COUPON");
        if (avant) son_negatif(); else son_positif();
        break;
    }
    case 2: {        /* loto : cocher une grille */
        uint16_t bit = (uint16_t)(1u << nav.curseur);
        if (nav.curseur < FDJ_GRILLE_MAX) {
            bool avant = (etat.grille_cochee & bit) != 0;
            etat.grille_cochee ^= bit;
            dit(avant ? "GRILLE RETIREE" : "GRILLE AU COUPON");
            if (avant) son_negatif(); else son_positif();
        } else {
            grilles_genere();
            dit("NOUVELLES GRILLES");
            son_positif();
        }
        break;
    }
    case 3:          /* historique : ouvrir le detail */
        etat.detail_ouvert = (uint8_t)((etat.detail_ouvert == nav.curseur) ? 0xFF : nav.curseur);
        son_positif();
        break;
    case 4:          /* gains : la periode mise en avant */
        dit("PERIODE SELECTIONNEE");
        son_positif();
        break;
    case 5:          /* news : marquer lu */
        etat.news_lu ^= (uint8_t)(1u << nav.curseur);
        son_positif();
        break;
    case 6: {        /* coupon */
        int base = 0;
        for (int i = 0; i < top.n; i++) if (etat.sel_cochee & (1u << i)) base++;
        for (int g = 0; g < FDJ_GRILLE_MAX; g++) if (etat.grille_cochee & (1u << g)) base++;
        if (nav.curseur < base) {
            /* retirer la selection : on retrouve son bit */
            int k = 0;
            for (int i = 0; i < top.n; i++) {
                if (!(etat.sel_cochee & (1u << i))) continue;
                if (k == nav.curseur) { etat.sel_cochee &= (uint16_t)~(1u << i); break; }
                k++;
            }
            for (int g = 0; k < base && g < FDJ_GRILLE_MAX; g++) {
                if (!(etat.grille_cochee & (1u << g))) continue;
                if (k == nav.curseur) { etat.grille_cochee &= (uint8_t)~(1u << g); break; }
                k++;
            }
            dit("RETIRE DU COUPON");
            son_negatif();
        } else if (nav.curseur == base) {
            coupon_valide();
        } else {
            etat.sel_cochee = 0;
            etat.grille_cochee = 0;
            dit("COUPON VIDE");
            son_negatif();
        }
        break;
    }
    case 7:          /* jeu responsable : repondre oui */
        if (nav.curseur < 5) {
            etat.sel_cochee |= (uint16_t)(1u << (8 + nav.curseur));
            son_positif();
        } else {
            etat.pause_24h = !etat.pause_24h;
            dit(etat.pause_24h ? "PAUSE 24 H ACTIVEE" : "PAUSE LEVEE");
            if (etat.pause_24h) son_negatif(); else son_positif();
        }
        break;
    case 8: case 9: case 10: case 11: case 12: case 13:
        dit("PAGE D'INFORMATION");
        break;
    case 14:         /* compte */
        if (nav.curseur == 3) {                 /* reveler le mot de passe */
            g_code_affiche = !g_code_affiche;
            dit(g_code_affiche ? "MOT DE PASSE REVELE" : "MOT DE PASSE MASQUE");
            son_positif();
        } else if (nav.curseur == 4) {          /* changer le code */
            g_code_change = true;
            fdj_nav_mode(&nav, FDJ_MODE_CODE);
            g_code_erreur = false;
            dit("NOUVEAU CODE : 4 CHIFFRES");
            son_positif();
        } else if (nav.curseur == 5) {          /* musique */
            etat.musique = !etat.musique;
            dit(etat.musique ? "MUSIQUE ON" : "MUSIQUE OFF");
            son_positif();
        } else {
            dit("CHAMP EN LECTURE SEULE");
        }
        break;
    case 15:
        select_ou_cree_profil();
        break;
    case 16:
        if (nav.curseur == 0) {
            etat.gros_texte = !etat.gros_texte;
            dit(etat.gros_texte ? "GROS TEXTE ON" : "GROS TEXTE OFF");
        } else {
            dit("REGLAGE FIXE");
        }
        son_positif();
        break;
    case 17:
        if (nav.curseur == 0) { fdj_nav_mode(&nav, FDJ_MODE_CONFIRME); son_negatif(); }
        else if (nav.curseur == 1) exporte_csv();
        else { nav.page = 18; nav.curseur = 0; son_positif(); }
        break;
    case 18:
        dit("CONDITIONS SUR FDJ.FR");
        break;
    default:
        break;
    }
    etat_sauve();
}

static void action_page(void)
{
    switch (nav.page) {
    case 0: nav.page = 1; nav.curseur = 0; son_positif(); break;
    case 1: etat.graine = fdj_rng_next(&etat.graine); top_recalcule(); dit("TOP 10 REGENERE"); son_positif(); break;
    case 2: grilles_genere(); dit("NOUVELLES GRILLES"); son_positif(); break;
    case 5: g_dernier_news = 0; g_rafraichir = true; dit("MISE A JOUR DES TITRES"); son_positif(); break;
    case 6: coupon_valide(); break;
    case 14:
        if (nav.curseur == 4) {
            g_code_change = true;
            fdj_nav_mode(&nav, FDJ_MODE_CODE);
            g_code_erreur = false;
            son_positif();
        } else {
            dit("RIEN A VALIDER ICI");
        }
        break;
    case 15: select_ou_cree_profil(); break;
    case 17:
        if (nav.curseur == 0)      fdj_nav_mode(&nav, FDJ_MODE_CONFIRME);
        else if (nav.curseur == 1) exporte_csv();
        else                       dit("FDT 3.0.0 - MAQUETTE");
        break;
    default: dit("PAGE D'INFORMATION"); break;
    }
    etat_sauve();
}

/* ------------------------------------------------------------------ */
/* boucle de l'application                                           */
/* ------------------------------------------------------------------ */
static void app_tache(void *arg)
{
    (void)arg;
    uint8_t page_prec = nav.page;
    int64_t dernier_minute = 0;

    affiche(true);

    for (;;) {
        geste_t g;
        bool recu = (xQueueReceive(q_gestes, &g, pdMS_TO_TICKS(1000)) == pdTRUE);

        if (g_page_demande >= 0) {
            nav.page = (uint8_t)g_page_demande;
            nav.curseur = 0;
            g_page_demande = -1;
            page_prec = 0xFF;
            affiche(true);
        }

        if (recu) {
            int items = fdj_page_items(nav.page, &vue);
            fdj_act_t a = fdj_nav_geste(&nav, items, g.btn, g.type, g.duree_ms);

            if (nav.mode == FDJ_MODE_CODE && a == FDJ_ACT_CODE_OK) {
                uint16_t code = fdj_nav_code(&nav);
                if (g_code_change) {
                    /* changement depuis la page 15 */
                    fdj_code_change(&etat, code);
                    etat_sauve();
                    fdj_nav_mode(&nav, FDJ_MODE_NAV);
                    g_code_change = false;
                    dit("NOUVEAU CODE ENREGISTRE");
                    son_positif();
                } else if (fdj_code_verifie(&etat, code)) {
                    fdj_nav_mode(&nav, FDJ_MODE_NAV);
                    g_code_erreur = false;
                    dit("BONJOUR");
                    son_positif();
                    if (etat.musique) jingle();
                } else {
                    g_code_erreur = true;
                    son_negatif();
                    fdj_nav_mode(&nav, FDJ_MODE_CODE);
                }
            } else if (nav.mode == FDJ_MODE_CODE && a == FDJ_ACT_CODE_ANNULE) {
                fdj_nav_mode(&nav, FDJ_MODE_NAV);
                g_code_change = false;
                g_code_erreur = false;
                dit("SAISIE ANNULEE");
            } else if (nav.mode == FDJ_MODE_CONFIRME && a == FDJ_ACT_CONFIRME) {
                efface_tout();
            } else if (nav.mode == FDJ_MODE_CONFIRME && a == FDJ_ACT_CODE_ANNULE) {
                fdj_nav_mode(&nav, FDJ_MODE_NAV);
                dit("EFFACEMENT ANNULE");
                son_negatif();
            } else {
                switch (a) {
                case FDJ_ACT_COCHER:   action_cocher(); break;
                case FDJ_ACT_DECOCHER: {
                    if (nav.page == 1) {
                        etat.sel_cochee &= (uint16_t)~(1u << nav.curseur);
                        dit("SELECTION RETIREE"); son_negatif();
                    } else if (nav.page == 2 && nav.curseur < FDJ_GRILLE_MAX) {
                        etat.grille_cochee &= (uint8_t)~(1u << nav.curseur);
                        dit("GRILLE RETIREE"); son_negatif();
                    } else if (nav.page == 7 && nav.curseur < 5) {
                        etat.sel_cochee &= (uint16_t)~(1u << (8 + nav.curseur));
                        dit("REPONSE EFFACEE"); son_negatif();
                    } else {
                        dit("RIEN A ANNULER");
                        son_negatif();
                    }
                    break;
                }
                case FDJ_ACT_PAGE:     action_page(); break;
                case FDJ_ACT_ACCUEIL:  son_negatif(); break;
                case FDJ_ACT_SUIVANT:  son_file(1200, 12, 45); break;   /* tic discret */
                case FDJ_ACT_PRECEDENT: son_file(700, 20, 45); break;
                default: break;
                }
            }

            /* on memorise ou on en etait */
            etat.page = nav.page;
            etat.curseur = nav.curseur;
            bool plein = (nav.page != page_prec) || (a == FDJ_ACT_ACCUEIL);
            page_prec = nav.page;
            affiche(plein);
        }

        /* le reseau a du neuf, ou la minute a tourne : on redessine */
        int64_t mn = esp_timer_get_time() / 1000 / 60000;
        if (g_rafraichir) {
            g_rafraichir = false;
            affiche(false);
        } else if (mn != dernier_minute && nav.mode == FDJ_MODE_NAV && nav.page == 0) {
            dernier_minute = mn;
            affiche(false);
        }

        if ((esp_timer_get_time() / 1000) > 0 && message_vivant())
            ; /* la prochaine image effacera le message */
    }
}

/* ------------------------------------------------------------------ */
/* wifi                                                              */
/* ------------------------------------------------------------------ */
static EventGroupHandle_t wifi_eg;
#define WIFI_CONNU_BIT 0x1

static void wifi_evenement(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi = false;
        if (wifi_eg) xEventGroupClearBits(wifi_eg, WIFI_CONNU_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_wifi = true;
        if (wifi_eg) xEventGroupSetBits(wifi_eg, WIFI_CONNU_BIT);
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) g_rssi = ap.rssi;
        ESP_LOGI(TAG, "wifi connecte : %s (%d dBm)", g_ssid ? g_ssid : "?", g_rssi);
        fdj_app_reseau_pret();
        g_rafraichir = true;
    }
}

static void wifi_demarre(void)
{
    wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_evenement, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_evenement, NULL, NULL));

    wifi_config_t wc = {};
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", WIFI_PASS);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void fdj_app_reseau_pret(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;
    esp_netif_sntp_init(&cfg);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000)) == ESP_OK) {
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        g_heure_ok = true;
        ESP_LOGI(TAG, "heure synchronisee");
    } else {
        ESP_LOGW(TAG, "pas d'heure : les pages afficheront N.D.");
    }
    g_rafraichir = true;
}


/* ------------------------------------------------------------------ */
/* API de verification a distance (Wi-Fi), sans effet de bord        */
/*   GET /etat            -> l'etat en JSON                          */
/*   GET /ecran           -> la derniere image en hexa (fb2png)      */
/*   GET /bouton?n=1&m=court|double|long|repeat                      */
/*   GET /page?n=6        -> force une page                          */
/* Rien ici ne modifie les donnees du compte : ce sont les memes     */
/* gestes que le doigt, pousses dans la meme file.                   */
/* ------------------------------------------------------------------ */
static int etat_json(char *b, size_t cap)
{
    const fdj_profil_t *pr = fdj_profil_actif_c(&etat);
    unsigned solde = pr ? (unsigned)pr->solde_ct : 0u;
    unsigned heap  = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    unsigned psram = (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return snprintf(b, cap,
        "{\"page\":%d,\"curseur\":%d,\"mode\":%d,\"items\":%d,"
        "\"profil\":\"%s\",\"solde_ct\":%u,\"hist\":%d,\"top\":%d,"
        "\"wifi\":%s,\"heure\":%s,\"rendu_ms\":%d,\"ecran_ms\":%d,"
        "\"heap\":%u,\"psram\":%u,\"version\":\"3.0.0\"}",
        (int)nav.page, (int)nav.curseur, (int)nav.mode, fdj_page_items(nav.page, &vue),
        pr ? pr->nom : "?", solde, (int)etat.hist_n, top.n,
        g_wifi ? "true" : "false", g_heure_ok ? "true" : "false",
        g_rendu_ms, g_ecran_ms, heap, psram);
}

static esp_err_t h_etat(httpd_req_t *req)
{
    char b[640];
    int n = etat_json(b, sizeof(b));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, b, n);
}

static esp_err_t h_ecran(httpd_req_t *req)
{
    static const char H[] = "0123456789abcdef";
    static char hex[ART_FB_LEN * 2 + 8];
    if (!fb_pret) return httpd_resp_send(req, "", 0);
    for (int i = 0; i < ART_FB_LEN; i++) {
        hex[i * 2]     = H[(fb_dernier[i] >> 4) & 0xF];
        hex[i * 2 + 1] = H[fb_dernier[i] & 0xF];
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, hex, ART_FB_LEN * 2);
}

static esp_err_t repond_400(httpd_req_t *req)
{
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "parametre invalide", HTTPD_RESP_USE_STRLEN);
}

static int qval(httpd_req_t *req, const char *cle, char *out, size_t cap)
{
    char q[96];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return -1;
    if (httpd_query_key_value(q, cle, out, cap) != ESP_OK) return -1;
    return 0;
}

static esp_err_t h_bouton(httpd_req_t *req)
{
    char v[16];
    int n = 0, m = 0;
    if (qval(req, "n", v, sizeof(v)) == 0) n = atoi(v);
    if (qval(req, "m", v, sizeof(v)) != 0) return repond_400(req);
    if      (strcmp(v, "court") == 0)  m = GESTE_COURT;
    else if (strcmp(v, "double") == 0) m = GESTE_DOUBLE;
    else if (strcmp(v, "long") == 0)   m = GESTE_LONG;
    else if (strcmp(v, "repeat3") == 0) m = GESTE_REPEAT;
    else return repond_400(req);

    uint16_t duree = 80;
    if (strcmp(v, "long") == 0)    duree = 700;
    if (strcmp(v, "repeat3") == 0) duree = 3100;
    fdj_app_geste((uint8_t)(n ? 1 : 0), (uint8_t)m, duree);
    return httpd_resp_send(req, "ok", 2);
}

static esp_err_t h_page(httpd_req_t *req)
{
    char v[8];
    if (qval(req, "n", v, sizeof(v)) != 0) return repond_400(req);
    int p = atoi(v);
    if (p < 0 || p >= FDJ_PAGES) return repond_400(req);
    g_page_demande = p;
    g_rafraichir = true;
    return httpd_resp_send(req, "ok", 2);
}

static void api_demarre(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "api : serveur indisponible");
        return;
    }
    static const httpd_uri_t U[] = {
        { "/etat",   HTTP_GET, h_etat,   NULL },
        { "/ecran",  HTTP_GET, h_ecran,  NULL },
        { "/bouton", HTTP_GET, h_bouton, NULL },
        { "/page",   HTTP_GET, h_page,   NULL },
    };
    for (size_t i = 0; i < sizeof(U) / sizeof(U[0]); i++)
        httpd_register_uri_handler(srv, &U[i]);
    ESP_LOGI(TAG, "api de verification prete : /etat /ecran /bouton /page");
}


/* ------------------------------------------------------------------ */
/* console USB : de quoi verifier la carte sans wifi                 */
/*   p 7        -> aller page 7        e -> etat (JSON)               */
/*   b 1 court  -> geste bouton 1      s -> ecran (hexa)              */
/* ------------------------------------------------------------------ */
static void console_execute(const char *l)
{
    char b[640];
    if (l[0] == 'p' && l[1] == ' ') {
        int p = atoi(l + 2);
        if (p >= 1 && p <= FDJ_PAGES) { g_page_demande = p - 1; g_rafraichir = true; }
        printf("page %d\n", p);
    } else if (l[0] == 'b' && l[1] == ' ') {
        int n = 0; char mode[12] = { 0 };
        if (sscanf(l + 2, "%d %11s", &n, mode) >= 1) {
            int m = -1; uint16_t d = 80;
            if (!strcmp(mode, "court"))        m = GESTE_COURT;
            else if (!strcmp(mode, "double"))  m = GESTE_DOUBLE;
            else if (!strcmp(mode, "long"))  { m = GESTE_LONG;  d = 700; }
            else if (!strcmp(mode, "r3"))    { m = GESTE_REPEAT; d = 3100; }
            if (m >= 0) fdj_app_geste((uint8_t)(n ? 1 : 0), (uint8_t)m, d);
            printf("bouton %d %s\n", n, mode);
        }
    } else if (l[0] == 'e') {
        etat_json(b, sizeof(b));
        printf("%s\n", b);
    } else if (l[0] == 's') {
        static const char H[] = "0123456789abcdef";
        static char hex[ART_FB_LEN * 2 + 8];
        if (!fb_pret) { printf("aucune image\n"); return; }
        for (int i = 0; i < ART_FB_LEN; i++) {
            hex[i * 2]     = H[(fb_dernier[i] >> 4) & 0xF];
            hex[i * 2 + 1] = H[fb_dernier[i] & 0xF];
        }
        printf("FB %s\n", hex);
    } else if (l[0] == 'h') {
        printf("p N | b N court|double|long|r3 | e | s\n");
    }
}

static void console_tache(void *arg)
{
    (void)arg;
    char ligne[64];
    int n = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(200)) == 1) {
            if (c == '\r' || c == '\n') {
                if (n) { ligne[n] = 0; console_execute(ligne); n = 0; }
            } else if (n < (int)sizeof(ligne) - 1 && c >= 32 && c < 127) {
                ligne[n++] = (char)c;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* demarrage                                                         */
/* ------------------------------------------------------------------ */
void fdj_app_geste(uint8_t btn, uint8_t type, uint16_t duree_ms)
{
    geste_t g = { btn, type, duree_ms };
    if (q_gestes) xQueueSend(q_gestes, &g, 0);
}

void fdj_app_start(const char *ssid, bool sd_ok, fdj_ecran_fn ecran)
{
    g_ecran = ecran;
    g_ssid  = ssid;
    g_sd_ok = sd_ok;

    mux_etat  = xSemaphoreCreateMutex();
    q_gestes  = xQueueCreate(16, sizeof(geste_t));
    q_notes   = xQueueCreate(24, sizeof(note_t));

    memset(&live, 0, sizeof(live));
    memset(&flux, 0, sizeof(flux));
    memset(&top, 0, sizeof(top));

    etat_charge();
    grilles_genere();
    /* le code ouvert, on arrive TOUJOURS sur l'accueil : c'est le debut
       de la demo, pas la page ou on s'etait arrete la veille. */
    fdj_nav_init(&nav, 0, 0);
    fdj_nav_mode(&nav, FDJ_MODE_CODE);          /* le code est demande au boot */

    xTaskCreate(sons_tache, "fdj-sons", 3072, NULL, 4, NULL);
    xTaskCreate(app_tache,  "fdj-app",  6144, NULL, 5, NULL);
    /* TLS a besoin de 16 Ko de pile : en RAM interne, la tache deborde et la
       carte redemarre (constate a la premiere recette). Pile en PSRAM. */
    if (xTaskCreateWithCaps(net_tache, "fdj-net", 32 * 1024, NULL, 3, NULL,
                            MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE(TAG, "reseau : pas de pile PSRAM, scores indisponibles");

    wifi_demarre();
    api_demarre();
    xTaskCreate(console_tache, "fdj-console", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "FDT 3.0.0 pret : 19 pages, code d'usine 0000");
}
