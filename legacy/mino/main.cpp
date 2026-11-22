/*  Mino - mascotte cochon sur e-paper 200x200 (Waveshare ESP32-S3-ePaper-1.54 V2)
 *  - 15 expressions du visage
 *  - cerveau DeepSeek (cerveau en ligne, texte) via HTTPS
 *  - SOUL + MEMOIRE persistantes sur SPIFFS (facon agent, version legere)
 *  - petite API HTTP sur le WiFi local
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"
#include "esp_pm.h"
#include "esp_adc/adc_oneshot.h"
#include "lwip/netif.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "cJSON.h"
#include "mp3dec.h"          /* decodeur mp3 (voix de Google) */

#include "art.h"
#include "pet.h"
#include "petart.h"
#include "geste.h"
#include "esp_private/esp_clk.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "ping/ping_sock.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "user_config.h"
#include "secrets.h"
#include "board_power_bsp.h"
#include "i2c_bsp.h"
#include "audio_bsp.h"
#include "epaper_driver_bsp.h"
#include "livre.h"
#include "tableau.h"

static const char *TAG = "mino";

/* ------------------------------------------------------------------ */
/* console serie USB : logs + commandes tapees au clavier              */
/* ------------------------------------------------------------------ */
static int jtag_log(const char *fmt, va_list ap)
{
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0)
        usb_serial_jtag_write_bytes(buf, (n > (int)sizeof(buf) - 1) ? sizeof(buf) - 1 : n,
                                    pdMS_TO_TICKS(200));
    return n;
}

static bool serial_ready = false;

static void serial_start(void)
{
    usb_serial_jtag_driver_config_t cfg = {};
    cfg.rx_buffer_size = 2048;
    cfg.tx_buffer_size = 4096;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return;
    esp_log_set_vprintf(jtag_log);
    serial_ready = true;
}

/* ------------------------------------------------------------------ */
/* framebuffer 200x200, 1 bit/px, 1 = blanc, 0 = noir                   */
/* ------------------------------------------------------------------ */
#define FB_LEN (200 * 25)
static uint8_t fb[FB_LEN];
static int fb_oy = 0;          /* decalage vertical (le visage passe sous la barre) */

static inline void fb_clear(void) { memset(fb, 0xFF, FB_LEN); }

static inline void px(int x, int y, int black)
{
    y += fb_oy;
    if (x < 0 || x > 199 || y < 0 || y > 199) return;
    int i = y * 25 + (x >> 3);
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    if (black) fb[i] &= (uint8_t)~m; else fb[i] |= m;
}

static void hline(int x0, int x1, int y, int t)
{
    if (x1 < x0) { int s = x0; x0 = x1; x1 = s; }
    for (int x = x0; x <= x1; x++)
        for (int k = 0; k < t; k++) px(x, y + k, 1);
}

static void vline(int x, int y0, int y1, int t)
{
    if (y1 < y0) { int s = y0; y0 = y1; y1 = s; }
    for (int y = y0; y <= y1; y++)
        for (int k = 0; k < t; k++) px(x + k, y, 1);
}

static void disc(int cx, int cy, int r, int black)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) px(cx + x, cy + y, black);
}

static void ring(int cx, int cy, int r, int t, int black)
{
    for (int y = -r - t; y <= r + t; y++) {
        for (int x = -r - t; x <= r + t; x++) {
            int d2 = x * x + y * y;
            if (d2 <= (r + t) * (r + t) && d2 >= (r - t) * (r - t))
                if (d2 >= (r - t) * (r - t) && d2 <= (r + t) * (r + t))
                    px(cx + x, cy + y, black);
        }
    }
}

static void seg(int x0, int y0, int x1, int y1, int t)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        disc(x0, y0, t, 1);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* arc: angles en degres, 0=droite, 90=haut (repère ecran) */
static void arc(int cx, int cy, int r, float a0, float a1, int t)
{
    float step = (r > 40) ? 0.7f : 1.2f;
    for (float a = a0; a <= a1; a += step) {
        float rad = a * 3.14159265f / 180.0f;
        int x = cx + (int)lroundf(r * cosf(rad));
        int y = cy - (int)lroundf(r * sinf(rad));
        disc(x, y, t, 1);
    }
}

/* BATMino : tout le dessin du personnage vit maintenant dans art.cpp */
/* ------------------------------------------------------------------ */
/* ecran                                                               */
/* ------------------------------------------------------------------ */
static epaper_driver_display *epd = NULL;
static SemaphoreHandle_t epd_mux;
static int refresh_count = 0;

static volatile int voix_etat;   /* etat de la voix (defini plus bas) */
static void show_ui(void);
static void tts_parle(const char *texte);   /* la voix : texte -> parole (definie plus bas) */
static void heure_sauve(void);   /* pour que le prochain demarrage soit a l'heure */
static int conso_mw(void);   /* consommation estimee, lue par le tableau de bord */

/* le cerveau : cle, adresse et modele (reglables a chaud, gardes en NVS) */
static char ia_cle[128] = "";        /* cle du cerveau, modifiable a chaud (NVS) */
static char ia_base[128] = "https://api.deepseek.com/chat/completions";
static char ia_mod[64]   = "deepseek-flash";   /* DeepSeek V4.1 Flash */
#define APP_VER "2026-11-12 astuce"
static char ia_brut[300];                      /* derniere reponse brute du modele */
static int  chrono_stt_ms = 0, chrono_llm_ms = 0, chrono_tts_ms = 0, chrono_ms = 0;

/* les titres du moment (flux RSS de franceinfo), affiches sur la page INFOS */
#define INFOS_MAX 8
static char *infos_xml = NULL;                  /* le flux brut, en PSRAM */
static char  infos_titre[INFOS_MAX][132];
static int   infos_n = 0, infos_haut = 0;
static volatile bool infos_ok = false;
static volatile int  infos_octets = -999;   /* ce que la lecture a renvoye */
static void infos_lire(void);
/* Mino comprend le francais par defaut ; /langue?l=en-US bascule tout
   (reconnaissance + voix) sur l'anglais. */
static char langue[8] = "fr-FR";
static void paroles_ajoute(const char *qui, const char *texte);
static void voix_tour(const uint8_t *pcm, int len);
static esp_err_t ds_chat(const char *user_text, char *out_dit, size_t dit_sz,
                         char *out_emo, size_t emo_sz);
/* cerveau complet : une passe, et si le modele demande une recherche web,
   le firmware cherche puis redemande une reponse courte. */
static esp_err_t ds_cerveau(const char *user_text, char *out_dit, size_t dit_sz,
                            char *out_emo, size_t emo_sz);
static int  web_lire(const char *requete, char *out, size_t sz);
static char *web_contexte = NULL;      /* resultats web de la passe en cours */
static char *souv_contexte = NULL;     /* extraits de l'historique (cache) pour cette passe */
static char  dernier_cherche[96] = ""; /* requete demandee par le modele */
static char  dernier_souvien[64] = ""; /* 2-4 mots a retrouver dans l'historique */
static int   contexte_hist_octets = 0; /* octets d'historique reellement envoyes (economie) */
static void date_fr(char *out, size_t sz);
/* la meteo et la voix : definies plus bas, utilisees par la page maitre */
static volatile bool meteo_ok;
/* ---- cycle de vie des applis (une appli par page) ----
   La memoire de travail d'une appli n'existe que tant qu'elle est au premier
   plan : en entrant on alloue, en sortant on rend tout. Seuls le wifi, la
   synchro de l'heure, les boutons, l'ecran, les rappels et l'assistant sont
   vitaux et ne s'arreteent jamais. */
static char  *meteo_buf = NULL;      /* corps de la reponse meteo (PSRAM, alloue une fois) */
static size_t meteo_buf_taille = 0;
static int    page_app = -1;         /* appli au premier plan */
static int    app_mem_octets = 0;    /* octets qu'elle occupe */
static volatile bool meteo_force = false;  /* la page METEO vient de s'ouvrir */
/* ---- appli YOUTUBE : la chaine de l'utilisateur et sa derniere video ----
   Le nom de la chaine se regle une fois (console `yt @moncompte` ou /yt?c=),
   il est garde en NVS ; la page lit ensuite le flux RSS de la chaine. */
static char  yt_handle[40] = "";     /* @moncompte */
static char  yt_id[32] = "";         /* UC... (resolu une fois) */
static char  yt_nom[48] = "";        /* nom affiche de la chaine */
static char  yt_titre[96] = "";      /* derniere video */
static char  yt_age[24] = "";        /* "il y a 2 j" */
static int   yt_vues = -1;
static volatile bool yt_ok = false;
static volatile bool yt_force = false;
static char *yt_buf = NULL;          /* page de la chaine, en PSRAM, rendue en sortant */
static size_t yt_buf_taille = 0;
static volatile bool yt_en_cours = false;   /* lecture en cours dans la tache reseau */
static volatile bool yt_a_liberer = false;  /* page quittee pendant la lecture */
/* la page YouTube fait ~2 Mo et la video mise en avant arrive vers 800 Ko :
   d'ou ce gros tampon, en PSRAM, rendu des qu'on quitte la page */
#define YT_TAILLE (1536 * 1024)
#define YT_UA "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " \
              "(KHTML, like Gecko) Chrome/120 Safari/537.36"
#define YT_COOKIE "CONSENT=YES+cb; SOCS=CAI"
static void yt_lire(void);
static float meteo_temp;
static int   meteo_code;
/* demain : pour repondre a "quel temps demain" sans passer par le web */
static float meteo_dem_min = 0, meteo_dem_max = 0;
static int   meteo_dem_code = -1, meteo_dem_pluie = -1;
static bool meteo_lire(void);
static const char *meteo_texte(int code);

/* ---- L'ASTUCE DU JOUR ----------------------------------------------------
   Une vraie astuce de streaming, lue sur le web UNE fois par jour (les tops
   du jour de r/Twitch puis r/Kick), gardee en NVS : la page 2 l'affiche sans
   reseau. astuce_jour = numero de jour (epoch / 86400) de la derniere lecture,
   ce qui evite toute lecture supplementaire avant le lendemain. */
#define ASTUCE_URL1 "https://www.reddit.com/r/Twitch/top/.rss?t=day"
#define ASTUCE_URL2 "https://www.reddit.com/r/Kick/top/.rss?t=day"
#define ASTUCE_UA   "MinoBot/1.0 (ESP32-S3 e-paper robot; +https://espressif.lan)"
#define ASTUCE_TXT_MAX 220
static char astuce_txt[ASTUCE_TXT_MAX] = "";
static char astuce_src[24] = "";
static char astuce_quand[20] = "";      /* "12 NOV 05:41" */
static int  astuce_jour = -1;           /* jour de la derniere lecture */
static volatile bool astuce_dispo = false;
static volatile bool astuce_demande = false;   /* lecture forcee (bouton 1) */
static volatile bool astuce_occupe = false;
static volatile int64_t astuce_essai_ms = 0;   /* dernier essai : attente apres un echec */
static bool astuce_lire(void);
static void astuce_charge(void);
static void astuce_sauve(void);
static const char *ia_key(void);
static bool groq_pret(void);
static void radio_wifi_task(void *arg);
static esp_err_t qval(httpd_req_t *req, const char *key, char *out, size_t sz);
static void draw_status_bar(void);
static void tableau_maitre(void);          /* la page unique, definie plus bas */

/*  Ecrans du BATMino autonome :
 *   0 MAITRE    tableau de bord : heure, date, meteo, wifi, petite tete
 *   1 PARLER    l'assistant vocal : micro, puis ce qu'il dit
 *   2 PAROLES   tout ce qui a ete dit, en texte, defilable
 *   3 MOI       le tamagotchi : comment il va
 *   4 MEMOIRE  ce qu'il sait
 *   5 RADIO     la radio (le PC n'est plus necessaire)
 */
/* deux pages : le tableau, et l'astuce de streaming du jour */
#define PAGE_MAITRE 0
#define PAGE_ASTUCE 1

/* ---- LE CONSEIL : la seule page qui n'affiche pas une donnee mais un
   jugement. Une passe de cerveau -- periodique, ou demandee au bouton 1 --
   croise ce que la carte sait deja (rendez-vous, meteo, sante du compagnon,
   batterie) et n'ecrit que ce qui merite d'etre lu. Le reste du temps elle ne
   coute rien : pas de TLS, pas de reveil de la dalle. ---- */
static char conseil_txt[300] = "";          /* le conseil, en clair, ASCII */
static char conseil_faits[64] = "";         /* les faits qui l'ont motive */
static char conseil_quand[8] = "";          /* heure du calcul, "22H16" */
static volatile bool conseil_vide = false;  /* derniere passe : rien a dire */
static volatile bool conseil_occupe = false;
static volatile bool conseil_demande = false;   /* passe forcee (bouton / HTTP) */
static volatile int  conseil_passes = 0;        /* passes reussies, pour /state */
static volatile int  conseil_etat = 0;          /* 1 ok, 2 pas de cle, 3 pas de wifi,
                                                   4 session en cours, 5 cerveau muet */
static void conseil_tache(void *arg);
/* MATRIX : la sequence est pilotee par une tache, declaree ici pour que
   appli_entre/appli_sort puissent la lancer et l'arreter. */
static void mtx_lance(void);
static void mtx_stop(void);
static void mtx_tick(void);

/* defilement de la page HISTORIQUE : nombre de messages remontes dans le passe */
static int hist_vue = 0;
static bool livre_reprise(void);
static void livre_arret(void);

/* ui_page / ui_face / menu_* : declares plus haut (pres de show_face) */
static float ui_temp = 0.0f, ui_humi = 0.0f;
/* le SHTC3 est colle au dos de la carte, juste a cote de l'ESP32 et du regulateur :
   il mesure la chaleur de la carte (environ +10 C) et non celle de la piece.
   Ce decalage, reglable par la commande `offset`, retablit une estimation ambiante. */
static float ui_temp_offset = -10.0f;
static float ui_temp_brut = 0.0f;
static bool  ui_shtc_ok = false;
static bool  ui_wifi_ok = false;
static char  wifi_actif[33] = "";   /* borne sur laquelle on est connecte */

/* prochaine reunion du calendrier (rempli par ms_boucle, vide si rien) */
static char  ms_heure[6] = "";
static char  ms_titre[64] = "";
static char  ms_org[48] = "";        /* la personne qui invite (lu dans le .ics) */
static int   ms_dans = -1;          /* minutes avant le debut, -1 = rien */
static long  ms_debut = 0;          /* instant du debut (epoch), 0 = rien */
static void ical_charge(void);
static int http_get(const char *url, const char *ua, uint8_t *buf, int cap);
static void ms_boucle(void);
static bool  touch_log = false;
static SemaphoreHandle_t i2c_mux = NULL;
static void  ui_beep(void);
static void  ui_chime(void);

/* audio : tampon mono 16 bits en PSRAM et aides, utilisees des le reveil */
static uint8_t *rec_pcm = NULL;
static volatile bool alarme_on = false;   /* la sonnerie du rendez-vous tourne */
static volatile bool heure_syncee = false; /* NTP deja passe (on le refait si le wifi revient) */
/* phrase a dire apres le carillon quand la sonnerie vient d'un RAPPEL (timer
   nomme) et non du calendrier ; vide = on annonce le rendez-vous */
static char    annonce_txt[96] = "";
static bool veille_nuit = false;          /* mise en veille la nuit (22h-7h) : OFF par defaut */
/* ---- l'info defile toute seule : pas besoin d'appuyer sur les boutons ---- */
static bool rotation_on = true;           /* pages d'info qui se succedent (NVS "rot") */
static int  rotation_s  = 30;             /* secondes par page (NVS "rots") */
static int64_t geste_ms = 0;              /* dernier geste de l'utilisateur (ms) */
static int64_t rotation_ms = 0;           /* dernier changement automatique (ms) */
#define ROTATION_PAUSE_MS 90000           /* apres un geste, on le laisse lire 90 s */
static bool voix_sortie = false;          /* HAUT-PARLEUR MUET : le robot ecrit, il ne parle pas */
static int  voix_dit_nb = 0;              /* phrases produites (muettes ou non) */
static int64_t alarme_t0 = 0;            /* quand elle a commence */
static int     alarme_jouee = 0;         /* nombre de sonneries reellement jouees */
static int  rec_last_bytes = 0;
static volatile bool radio_mode = false;      /* le PC pousse un flux PCM sur l'USB */
static volatile bool pcm_xfer = false;        /* transfert PCM en cours : pas d'echo */
static volatile int  radio_total = 0, radio_done = 0;   /* octets stereo attendus / recus */
static int  radio_idle = 0;                             /* lectures sans donnees en mode flux */
static int  audio_record_at(int off, int frames, int *peak);
static int  audio_record_mono(int frames, int *peak);
static int  audio_sd_write(const char *path, const uint8_t *mono, int data_len);
static void audio_play_mono(int frames);

/* ---- historique de conversation -------------------------------------
   La fenetre RAM/PSRAM (32 tours) est ce qu'on renvoie au modele ; le journal
   complet vit sur la carte SD, dans la partition brute (3,5 Mo/s). Declare tot
   parce que les boutons (page Historique) et le cerveau y touchent. */
#include "historique.h"
/* 32 tours = 16,6 Ko : PAS en RAM interne (le wifi a besoin de la sienne),
   uniquement en PSRAM. Si l'allocation echoue, on se prive de la fenetre mais
   le journal sur la carte SD continue (c'est lui la memoire durable). */
static hist_t *hist_p = NULL;
#define hist (*hist_p)

#define HIST_SD_SLOTS 4096
#define HIST_SD_TXT   508
static bool          hist_sd_pret = false;
static uint32_t      hist_sd_base = 0;
static uint16_t      hist_sd_next = 0;
static uint16_t      hist_sd_n = 0;
static uint32_t      hist_sd_tours = 0;
static int           hist_sd_erreurs = 0;
static void         *hist_sd_mux = NULL;
static uint8_t      *hist_sd_sect = NULL;
static char          hist_sd_pourquoi[40] = "pas encore initialise";
static void hist_sd_init(void);
static void hist_sd_ajoute(const char *qui, const char *texte);
static int  hist_sd_lire(char *out, size_t sz, int max);
static int  hist_sd_cherche(const char *mots, char *out, size_t sz, int max);

/* etat partage : page courante, tamagotchi, carte SD.
   Declares ici car les pages, le journal et le moteur les lisent de partout. */
static int   ui_page = 0;
/* une image demandee mais sautee (mutex ou dalle occupee) : sans ce drapeau un
   changement de page pendant un rafraichissement restait invisible jusqu'au
   tick suivant (une minute) -> "impossible de changer d'app, ca reste fige" */
static volatile bool redraw_demande = false;
static uint32_t boot_nb = 0;       /* numero de demarrage (NVS), pour reperer les reboots */
static int   ui_face = 0;
static int   current_emotion = 8;   /* famille "calme" */
static bool  sd_ok = false;
static pet_state_t pet;
static SemaphoreHandle_t pet_mux = NULL;
static int anim_ph = 0;                  /* etape du petit compagnon (il bouge) */

/* Mino se promene : il vise un coin de la zone blanche et y va pas a pas */
static int kirby_x = 100, kirby_y = 189;     /* ou il est (bande du bas) */
static int kirby_vx = 0, kirby_vy = 0;       /* ou il veut aller */

static int conso_mw(void);   /* definie plus bas (elle lit la radio et la voix) */
static float mic_db = 42.0f;   /* 42 dB saturait tout, 24 dB etait juste pour une voix humaine */             /* gain du micro, reglable par /mic?db= */
static int   mic_crete = 0;              /* crete de la derniere prise, lue par /state */
static int   mic_bruit = 0;              /* bruit de fond mesure */
static int   mic_seuil = 0;              /* seuil de parole qui en decoule */
static int   stt_http = 0;               /* dernier code HTTP de la reconnaissance */
static int   stt_ecrits = 0;             /* octets audio effectivement envoyes */
static int   stt_total = 0;              /* octets audio prevus */
static char  stt_detail[130] = "";       /* debut de la reponse, pour diagnostiquer */

static SemaphoreHandle_t ui_mux = NULL;
static esp_err_t send_json(httpd_req_t *req, cJSON *root);
static void monde_maj_info(monde_info_t *mi);
static int  famille_vers_bat5(int famille);      /* 14 familles -> 5 visages */
static int  bat5_du_pet;                        /* le visage montre a l'ecran */

static cJSON *mem_obj;                           /* les notes qu'il a apprises */

/* bulle de message affichee quelques secondes, pensee pour un enfant :
   un cadre noir, une phrase courte, elle part toute seule. */
static char     toast_txt[40] = {0};
static uint32_t toast_fin_ms = 0;
static void show_face(int e)
{
    if (e < 0 || e >= EXPR_N) e = 0;
    ui_face = e;
    current_emotion = EXPR[e].famille;
    bat5_du_pet = famille_vers_bat5(current_emotion);
    show_ui();
}

static void assistant_wake(void);
static void page_next(int d);

/* ================================================================== */
/* interface : barre d'etat (wifi / heure / temperature / engrenage)   */
/* + pages balayables : visage, horloge, capteurs, reglages            */
/* ================================================================== */

/* chiffres 3x5 (1 = pixel noir) */
static const uint8_t DIG5[10][5] = {
    {7, 5, 5, 5, 7}, {2, 2, 2, 2, 2}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};
static const uint8_t GC5[5] = {7, 4, 4, 4, 7};      /* C */
static const uint8_t GP5[5] = {5, 1, 2, 4, 5};      /* % */
static const uint8_t GM5[5] = {4, 5, 7, 5, 5};      /* M */

static void g3x5(int x, int y, int s, const uint8_t rows[5])
{
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (rows[r] & (4 >> c))
                for (int dy = 0; dy < s; dy++)
                    for (int dx = 0; dx < s; dx++)
                        px(x + c * s + dx, y + r * s + dy, 1);
}

/* ecrit un nombre en chiffres 3x5, renvoie la largeur utilisee */
static int draw_num(int x, int y, int s, int val, int min_d)
{
    char b[8];
    int n = 0, v = val < 0 ? -val : val;
    do { b[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < 7);
    while (n < min_d && n < 7) b[n++] = '0';
    for (int i = 0; i < n; i++) g3x5(x + i * 4 * s, y, s, DIG5[b[n - 1 - i] - '0']);
    return n * 4 * s - s;
}

/* police 3x5 complete (majuscules) : index dans FONT_CHARS */
static const char FONT_CHARS[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-/%!?#()+=<>,*'&_";
static const uint8_t FONT3X5[56][5] = {
  {0,0,0,0,0}, {7,5,5,5,7}, {2,2,2,2,2}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
  {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}, {2,5,7,5,5},
  {6,5,6,5,6}, {7,4,4,4,7}, {6,5,5,5,6}, {7,4,6,4,7}, {7,4,6,4,4}, {7,4,5,5,7},
  {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,7}, {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5},
  {5,7,5,5,5}, {7,5,5,5,7}, {7,5,7,4,4}, {7,5,5,7,1}, {7,5,7,6,5}, {7,4,7,1,7},
  {7,2,2,2,2}, {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,2,2,2},
  {7,1,2,4,7}, {0,0,0,0,2}, {0,2,0,2,0}, {0,0,7,0,0}, {1,1,2,4,4}, {5,1,2,4,5},
  {2,2,2,0,2}, {7,1,3,0,2}, {5,7,5,7,5}, {2,4,4,4,2}, {2,1,1,1,2}, {0,2,7,2,0},
  {0,7,0,7,0}, {1,2,4,2,1}, {4,2,1,2,4}, {0,0,0,2,4}, {5,2,5,0,0}, {2,2,0,0,0},
  {2,5,2,5,2}, {0,0,0,0,7},
};

static int font_idx(char c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; i < (int)sizeof(FONT_CHARS) - 1; i++)
        if (FONT_CHARS[i] == c) return i;
    return 0;
}

/* texte en police 3x5, renvoie la largeur utilisee */
static int draw_text(int x, int y, int s, const char *str)
{
    int i = 0;
    for (const char *p = str; *p; p++, i++)
        g3x5(x + i * 4 * s, y, s, FONT3X5[font_idx(*p)]);
    return i > 0 ? i * 4 * s - s : 0;
}

static void sq(int x, int y, int s)
{
    for (int dy = 0; dy < s; dy++)
        for (int dx = 0; dx < s; dx++) px(x + dx, y + dy, 1);
}
/* icones */
static void icon_wifi(int cx, int cy, int on)
{
    int b = cy + 4;
    arc(cx, b, 12, 35, 145, 1);
    arc(cx, b, 7, 35, 145, 1);
    arc(cx, b, 3, 35, 145, 1);
    disc(cx, b - 1, 1, 1);
    if (!on) seg(cx - 9, cy - 11, cx + 9, cy + 9, 1);
}

static void icon_temp(int cx, int cy)
{
    ring(cx, cy + 5, 3, 1, 1);
    seg(cx, cy - 5, cx, cy + 4, 1);
    px(cx - 1, cy - 5, 1); px(cx + 1, cy - 5, 1);
}

static void icon_gear(int cx, int cy, int r, int t)
{
    ring(cx, cy, r, t, 1);
    for (int a = 0; a < 360; a += 45) {
        float rad = (float)a * 3.14159265f / 180.0f;
        int x0 = cx + (int)lroundf((float)r * cosf(rad));
        int y0 = cy - (int)lroundf((float)r * sinf(rad));
        int x1 = cx + (int)lroundf((float)(r + 4) * cosf(rad));
        int y1 = cy - (int)lroundf((float)(r + 4) * sinf(rad));
        seg(x0, y0, x1, y1, 1);
    }
}

static void icon_pig_small(int cx, int cy) { ic_bat_small(cx, cy, 9); }

/* ================================================================== */
/* page securite : analyse reseau de base                              */
/* ================================================================== */

static char  sec_ssid[34] = "-";
static char  sec_auth[12] = "-";
static char  sec_ciph[9]  = "-";
static char  sec_bssid[18] = "-";
static int   sec_chan = 0, sec_rssi = 0;
static char  sec_ip[16] = "-", sec_gw[16] = "-", sec_dns[16] = "-";
static bool  sec_gw_ok = false, sec_net_ok = false, sec_dns_ok = false;
static bool  sec_wps = false, sec_wpa3 = false;
static int   sec_aps = 0, sec_open = 0;
static int   sec_hotes = -1;            /* -1 = balayage en cours */
static int   sec_risque = 0;
static char  sec_alerte[24] = "ANALYSE EN COURS";
static bool  sec_sweep = false;
static SemaphoreHandle_t ping_mux = NULL;

static const char *auth_str(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN:            return "OUVERT";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "INCONNU";
    }
}

static const char *ciph_str(wifi_cipher_type_t c)
{
    switch (c) {
    case WIFI_CIPHER_TYPE_NONE:      return "AUCUN";
    case WIFI_CIPHER_TYPE_WEP40:     return "WEP40";
    case WIFI_CIPHER_TYPE_WEP104:    return "WEP104";
    case WIFI_CIPHER_TYPE_TKIP:      return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP:      return "AES";
    case WIFI_CIPHER_TYPE_TKIP_CCMP: return "TKIP+AES";
    case WIFI_CIPHER_TYPE_GCMP:      return "GCMP";
    default:                         return "?";
    }
}

static volatile int ping_rep = 0;

static void ping_ok_cb(esp_ping_handle_t h, void *args) { ping_rep++; }
static void ping_to_cb(esp_ping_handle_t h, void *args) { }
static void ping_end_cb(esp_ping_handle_t h, void *args) { }

/* un ping unique vers une adresse IP (texte) ; renvoie vrai si on a une reponse */
static bool ping_ip(const char *ip, int timeout_ms)
{
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 1;
    cfg.interval_ms = 100;
    cfg.timeout_ms = (uint32_t)timeout_ms;
    cfg.task_stack_size = 3072;
    cfg.target_addr.type = ESP_IPADDR_TYPE_V4;
    if (esp_netif_str_to_ip4(ip, (esp_ip4_addr_t *)&cfg.target_addr.u_addr.ip4) != ESP_OK) return false;
    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = ping_ok_cb;
    cbs.on_ping_timeout = ping_to_cb;
    cbs.on_ping_end = ping_end_cb;
    esp_ping_handle_t h = NULL;
    ping_rep = 0;
    if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return false;
    esp_ping_start(h);
    int wait = 0;
    while (wait < timeout_ms + 150 && ping_rep == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        wait += 50;
    }
    esp_ping_delete_session(h);
    return ping_rep > 0;
}

/* releve tout ce qui peut l'etre sans bloquer longtemps */
static void secu_collect(void)
{
    wifi_ap_record_t ap;
    memset(&ap, 0, sizeof(ap));
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ui_wifi_ok = true;
        snprintf(sec_ssid, sizeof(sec_ssid), "%s", (const char *)ap.ssid);
        snprintf(sec_auth, sizeof(sec_auth), "%s", auth_str(ap.authmode));
        snprintf(sec_ciph, sizeof(sec_ciph), "%s", ciph_str(ap.pairwise_cipher));
        snprintf(sec_bssid, sizeof(sec_bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        sec_chan = ap.primary;
        sec_rssi = ap.rssi;
        sec_wpa3 = (ap.authmode == WIFI_AUTH_WPA3_PSK || ap.authmode == WIFI_AUTH_WPA2_WPA3_PSK);
        sec_wps  = (ap.wps == true);
    } else {
        ui_wifi_ok = false;
    }

    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (nif && esp_netif_get_ip_info(nif, &ip) == ESP_OK) {
        esp_ip4addr_ntoa(&ip.ip, sec_ip, sizeof(sec_ip));
        esp_ip4addr_ntoa(&ip.gw, sec_gw, sizeof(sec_gw));
    }
    if (nif) {
        esp_netif_dns_info_t d;
        if (esp_netif_get_dns_info(nif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK &&
            d.ip.type == ESP_IPADDR_TYPE_V4)
            esp_ip4addr_ntoa(&d.ip.u_addr.ip4, sec_dns, sizeof(sec_dns));
    }

    /* voisinage radio : combien de reseaux, combien en clair */
    wifi_scan_config_t sc;
    memset(&sc, 0, sizeof(sc));
    uint16_t n = 0;
    if (esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_num(&n) == ESP_OK && n > 0) {
        uint16_t cnt = n > 40 ? 40 : n;
        wifi_ap_record_t *l = (wifi_ap_record_t *)calloc(cnt, sizeof(wifi_ap_record_t));
        if (l) {
            int op = 0;
            if (esp_wifi_scan_get_ap_records(&cnt, l) == ESP_OK) {
                for (int i = 0; i < cnt; i++)
                    if (l[i].authmode == WIFI_AUTH_OPEN) op++;
                sec_aps = cnt;
                sec_open = op;
            }
            free(l);
        }
        esp_wifi_clear_ap_list();
    }

    /* passerelle, internet, resolution de noms */
    if (ping_mux && xSemaphoreTake(ping_mux, pdMS_TO_TICKS(30000)) == pdTRUE) {
        sec_gw_ok  = ping_ip(sec_gw, 1000);
        sec_net_ok = ping_ip("1.1.1.1", 1200);
        if (!sec_net_ok) sec_net_ok = ping_ip("8.8.8.8", 1200);
        xSemaphoreGive(ping_mux);
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    sec_dns_ok = (lwip_getaddrinfo("pool.ntp.org", NULL, &hints, &res) == 0);
    if (res) lwip_freeaddrinfo(res);

    /* alertes, du plus grave au plus benin */
    sec_risque = 0;
    if (!strcmp(sec_auth, "OUVERT") || !strcmp(sec_auth, "WEP")) {
        snprintf(sec_alerte, sizeof(sec_alerte), "RESEAU NON PROTEGE");
        sec_risque = 5;
    } else {
        if (!strcmp(sec_auth, "WPA") || !strcmp(sec_auth, "WPA/WPA2")) sec_risque += 2;
        else sec_risque += 1;
        if (sec_wps)    { sec_risque += 1; snprintf(sec_alerte, sizeof(sec_alerte), "WPS ACTIF"); }
        else if (!sec_wpa3) { snprintf(sec_alerte, sizeof(sec_alerte), "PAS DE WPA3"); }
        else snprintf(sec_alerte, sizeof(sec_alerte), "RESEAU A JOUR");
        if (!sec_dns_ok) sec_risque += 1;
        if (sec_open > 0) sec_risque += 1;
        if (sec_risque > 5) sec_risque = 5;
    }
}

/* balayage de la classe locale pour compter les machines presentes */
static void secu_sweep_task(void *arg)
{
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK) {
        sec_hotes = 0;
        sec_sweep = false;
        vTaskDelete(NULL);
        return;
    }
    uint32_t base = ntohl(ip.ip.addr) & 0xFFFFFF00u;
    uint32_t self = ntohl(ip.ip.addr);
    int found = 0;
    for (int i = 1; i < 255; i++) {
        uint32_t a = base | (uint32_t)i;
        if (a == self) continue;
        char b[16];
        snprintf(b, sizeof(b), "%u.%u.%u.%u", (unsigned)((a >> 24) & 255), (unsigned)((a >> 16) & 255),
                 (unsigned)((a >> 8) & 255), (unsigned)(a & 255));
        if (ping_mux && xSemaphoreTake(ping_mux, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (ping_ip(b, 80)) { found++; }
            sec_hotes = found;
            xSemaphoreGive(ping_mux);
        }
        if (i % 24 == 0 && ui_page == 99) show_ui();
        if (found >= 64) break;
    }
    sec_hotes = found;
    sec_sweep = false;
    ESP_LOGI(TAG, "analyse reseau : %d machine(s) sur le reseau local", found);
    vTaskDelete(NULL);
}

static volatile bool secu_busy = false;

/* relance l'analyse en tache separee (ne bloque ni l'ecran ni les boutons) */
static void secu_refresh_task(void *a)
{
    secu_busy = true;
    secu_collect();
    if (ui_page == 99) show_ui();
    secu_busy = false;
    vTaskDelete(NULL);
}

static void secu_sweep(void)
{
    if (sec_sweep) return;
    sec_sweep = true;
    sec_hotes = -1;
    xTaskCreate(secu_sweep_task, "secusweep", 4096, NULL, 2, NULL);
}

/* ================================================================== */
/* ecran d'activite : barre de progression en temps reel               */
/* ================================================================== */

static bool  act_on = false;
static char  act_label[26] = "";
static int   act_pct = 0;
static bool  act_indet = false;
static int   act_tick = 0;
static int64_t act_last_ms = 0;
static bool  act_anim = false;

static void draw_activity(void)
{
    char b[30];
    /* titre */
    draw_text(10, 34, 2, act_label);
    /* cadre de la barre */
    hline(12, 187, 66, 1);
    hline(12, 187, 96, 1);
    vline(12, 66, 97, 1);
    vline(187, 66, 97, 1);
    /* remplissage */
    int w = (act_pct * 171) / 100;
    if (w > 0) {
        for (int y = 69; y < 94; y++)
            for (int x = 15; x < 15 + w && x < 185; x++)
                px(x, y, 1);
    }
    /* pourcentage ou animation */
    if (act_indet) {
        int d = (act_tick / 3) % 4;
        for (int i = 0; i < 4; i++) {
            int cx = 82 + i * 12;
            if (i == d) disc(cx, 110, 4, 1);
            else ring(cx, 110, 4, 1, 1);
        }
        draw_text(56, 126, 1, "EN COURS");
    } else {
        snprintf(b, sizeof(b), "%d %%", act_pct);
        int wt = (int)strlen(b) * 4 - 1;
        draw_text((200 - wt) / 2, 110, 2, b);
    }
    /* le cochon regarde ce qui se passe */
    icon_pig_small(100, 152);
}

/* redessine l'ecran d'activite, au plus 4 fois par seconde (e-paper) */
static void act_show(bool force)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (!force && now - act_last_ms < 900) return;
    act_last_ms = now;
    fb_clear();
    fb_oy = 0;
    draw_activity();
    draw_status_bar();
    if (epd && epd_mux && xSemaphoreTake(epd_mux, pdMS_TO_TICKS(4000)) == pdTRUE) {
        epd->EPD_ShowPart(fb, FB_LEN);
        xSemaphoreGive(epd_mux);
    }
}

static void act_anim_task(void *arg)
{
    while (act_on) {
        act_tick++;
        act_show(true);
        vTaskDelay(pdMS_TO_TICKS(1100));
    }
    act_anim = false;
    vTaskDelete(NULL);
}

static void act_begin(const char *label, bool indet)
{
    snprintf(act_label, sizeof(act_label), "%s", label);
    act_on = true;
    act_indet = indet;
    act_pct = 0;
    act_tick = 0;
    act_show(true);
    /* l'animation vit dans sa propre tache : rafraichir l'e-paper prend
       plusieurs centaines de ms et, dans la boucle d'enregistrement, ce
       temps mort faisait deborder le tampon du micro. */
    if (!act_anim) {
        act_anim = true;
        xTaskCreate(act_anim_task, "anim", 3072, NULL, 2, NULL);
    }
}

/* La police e-paper ne connait que l'ASCII et affiche tout en majuscules :
   on traduit les entites HTML puis on remplace les accents par leur lettre. */
static void infos_nettoie(char *s)
{
    char *l = s, *r = s;
    while (*r) {
        unsigned char c = (unsigned char)*r;
        if (c == '&') {                          /* entite HTML */
            int v = -1, saut = 0;
            if (!strncmp(r, "&amp;", 5))       { *l++ = '&'; r += 5; continue; }
            if (!strncmp(r, "&quot;", 6))      { *l++ = '"'; r += 6; continue; }
            if (!strncmp(r, "&apos;", 6))      { *l++ = '\''; r += 6; continue; }
            if (!strncmp(r, "&#39;", 5))       { *l++ = '\''; r += 5; continue; }
            if (!strncmp(r, "&lt;", 4))        { *l++ = '<'; r += 4; continue; }
            if (!strncmp(r, "&gt;", 4))        { *l++ = '>'; r += 4; continue; }
            if (!strncmp(r, "&nbsp;", 6))      { *l++ = ' '; r += 6; continue; }
            if (r[1] == '#') {
                if (r[2] == 'x' || r[2] == 'X') v = (int)strtol(r + 3, NULL, 16);
                else                            v = (int)strtol(r + 2, NULL, 10);
                char *f = strchr(r, ';');
                if (f && v > 0) saut = (int)(f - r) + 1;
            }
            if (saut > 0) {
                if (v < 128) *l++ = (char)v;
                else {                           /* accent numerique */
                    switch (v) {
                    case 0xE0: case 0xE2: case 0xE4: case 0x80: case 0x82: case 0x84: *l++ = 'a'; break;
                    case 0xE7: case 0x87: *l++ = 'c'; break;
                    case 0xE8: case 0xE9: case 0xEA: case 0xEB:
                    case 0x88: case 0x89: case 0x8A: case 0x8B: *l++ = 'e'; break;
                    case 0xEE: case 0xEF: case 0x8E: case 0x8F: *l++ = 'i'; break;
                    case 0xF4: case 0xF6: case 0x94: case 0x96: *l++ = 'o'; break;
                    case 0xF9: case 0xFB: case 0xFC: case 0x99: case 0x9B: case 0x9C: *l++ = 'u'; break;
                    case 0xFF: *l++ = 'y'; break;
                    case 0xA0: *l++ = ' '; break;
                    case 0xAB: case 0xBB: *l++ = '"'; break;
                    default: *l++ = '?'; break;
                    }
                }
                r += saut;
                continue;
            }
            *l++ = ' ';
            r++;
            continue;
        }
        if (c < 0x80) { *l++ = (char)c; r++; continue; }
        if (c == 0xC2 && r[1]) {                 /* ponctuation fine */
            unsigned char b2 = (unsigned char)r[1];
            if (b2 == 0xA0) *l++ = ' ';
            else if (b2 >= 0x91 && b2 <= 0x97) *l++ = (b2 == 0x91 || b2 == 0x92) ? '\'' : ((b2 == 0x96 || b2 == 0x97) ? '-' : '"');
            else if (b2 == 0xB0) *l++ = ' ';
            r += 2;
            continue;
        }
        if (c == 0xC3 && r[1]) {                 /* accents en UTF-8 direct */
            unsigned char b2 = (unsigned char)r[1];
            switch (b2) {
            case 0x80: case 0xA0: case 0xA2: case 0xA4: *l++ = 'a'; break;
            case 0x87: case 0xA7: *l++ = 'c'; break;
            case 0x88: case 0xA8: case 0x89: case 0xA9: case 0x8A: case 0xAA:
            case 0x8B: case 0xAB: *l++ = 'e'; break;
            case 0x8E: case 0xAE: case 0x8F: case 0xAF: *l++ = 'i'; break;
            case 0x94: case 0xB4: case 0x96: case 0xB6: *l++ = 'o'; break;
            case 0x99: case 0x9B: case 0x9C: case 0xB9: case 0xBB: case 0xBC: *l++ = 'u'; break;
            case 0x9F: *l++ = 's'; break;
            case 0xBF: *l++ = 'y'; break;
            default: *l++ = '?'; break;
            }
            r += 2;
            continue;
        }
        if (c == 0xE2 && r[1] && r[2]) { r += 3; continue; }   /* tirets longs, euros... */
        r++;                                                    /* le reste : on jette */
    }
    *l = 0;
    /* les espaces doubles, ca fait des trous dans la ligne */
    for (char *q = s; *q; q++)
        if (q[0] == ' ' && q[1] == ' ') { memmove(q, q + 1, strlen(q)); q--; }
}

/* texte "gras" : la meme chaine dessinee deux fois, decalee d'un pixel.
   La police 3x5 n'a pas de graisse ; sur une dalle 1 bit c'est le seul
   moyen d'epaissir un mot sans changer de corps. */
/* un pas vers la destination ; arrive, il en choisit une autre au hasard */
static void kirby_promene(void)
{
    int x0 = 22, x1 = 178, y0 = 186, y1 = 192;   /* la bande du bas, sous le cadre */
    if ((kirby_vx == 0 && kirby_vy == 0) || (kirby_x == kirby_vx && kirby_y == kirby_vy)) {
        kirby_vx = x0 + (int)(esp_random() % (uint32_t)(x1 - x0 + 1));
        kirby_vy = y0 + (int)(esp_random() % (uint32_t)(y1 - y0 + 1));
    }
    int px = 14, py = 8;                      /* il avance vite a l'horizontale */
    if (kirby_x < kirby_vx) kirby_x += (kirby_vx - kirby_x < px) ? (kirby_vx - kirby_x) : px;
    else if (kirby_x > kirby_vx) kirby_x -= (kirby_x - kirby_vx < px) ? (kirby_x - kirby_vx) : px;
    if (kirby_y < kirby_vy) kirby_y += (kirby_vy - kirby_y < py) ? (kirby_vy - kirby_y) : py;
    else if (kirby_y > kirby_vy) kirby_y -= (kirby_y - kirby_vy < py) ? (kirby_y - kirby_vy) : py;
}

/* barre d'etat, hauteur 26 px, en haut */
static void draw_status_bar(void)
{
    hline(0, 199, 25, 1);
    icon_wifi(14, 12, ui_wifi_ok ? 1 : 0);

    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    int x = 34;
    x += draw_num(x, 6, 2, ti.tm_hour, 2) + 4;
    sq(x, 6, 2); sq(x, 14, 2); x += 6;
    draw_num(x, 6, 2, ti.tm_min, 2);

    icon_temp(124, 10);
    int tt = (int)lroundf(ui_temp);
    x = 132 + draw_num(132, 6, 2, tt, 2) + 3;
    ring(x + 2, 8, 2, 1, 1);
    g3x5(x + 7, 6, 2, GC5);

    /* frequence du processeur. La RAM libre a ete retiree : elle changeait a
       chaque mesure, donc l'image different toujours et la dalle se
       rafraichissait sans arret (clignotement permanent de la page d'accueil). */
    {
        char b[24];
        snprintf(b, sizeof(b), "%dM", (int)(esp_clk_cpu_freq() / 1000000));
        a_text(140, 6, 1, b);
    }
    icon_gear(182, 12, 6, 2);
}

/* ================================================================== */
/* LES APPLIS : une chose par ecran, en gros.                          */
/*                                                                     */
/*   0 BATMAN   le visage, l'expression du moment                      */
/*   1 PARLER   l'assistant vocal : micro, puis ce qu'il dit           */
/*   2 MOI      le tamagotchi : comment il va, ce dont il a besoin     */
/*   3 MEMOIRE  ce qu'il sait                                          */
/*   4 INFOS    processeur, memoire, carte SD, wifi                    */
/*                                                                     */
/* Navigation : un bouton = applis suivante / precedente, appui long   */
/* sur n'importe lequel = parler. Deux gestes, rien d'autre.           */
/* ================================================================== */

/* les 14 familles du modele ramenees a cinq visages */
static int famille_vers_bat5(int famille)
{
    switch (famille) {
    case 0:  return BAT5_CONTENT;    /* joie */
    case 10: return BAT5_CONTENT;    /* tendresse */
    case 9:  return BAT5_CONTENT;    /* fierte */
    case 12: return BAT5_CONTENT;    /* desir */
    case 1:  return BAT5_TRISTE;     /* tristesse */
    case 11: return BAT5_TRISTE;     /* gene */
    case 13: return BAT5_TRISTE;     /* douleur */
    case 2:  return BAT5_FACHE;      /* colere */
    case 5:  return BAT5_FACHE;      /* degout */
    case 3:  return BAT5_SURPRIS;    /* peur */
    case 4:  return BAT5_SURPRIS;    /* surprise */
    default: return BAT5_NEUTRE;     /* interet, reflexion, calme */
    }
}

/* bandeau de titre de chaque appli : nom en haut, gros, et une bande
   noire en bas qui porte le sous-titre (l'expression, la reponse...) */
/* La police de la dalle ne connait que l'ASCII : un accent disparait et le mot
   devient illisible ("OXYGNE"). On translittere le francais avant l'affichage. */
static void texte_ascii(const char *src, char *dst, size_t sz)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 1 < sz; ) {
        if (*p < 0x80) { dst[o++] = (char)*p; p++; continue; }
        unsigned cp = 0;
        if ((*p & 0xE0) == 0xC0 && p[1]) cp = ((unsigned)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2])
            cp = ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        p += (cp > 0x7FF) ? 3 : 2;
        switch (cp) {
        case 0x00E0: case 0x00E2: case 0x00E4: dst[o++] = 'a'; break;
        case 0x00E7:                            dst[o++] = 'c'; break;
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: dst[o++] = 'e'; break;
        case 0x00EE: case 0x00EF:               dst[o++] = 'i'; break;
        case 0x00F4: case 0x00F6:               dst[o++] = 'o'; break;
        case 0x00F9: case 0x00FB: case 0x00FC:  dst[o++] = 'u'; break;
        case 0x00FF:                            dst[o++] = 'y'; break;
        case 0x0153:                            dst[o++] = 'o'; dst[o++] = 'e'; break;
        case 0x00C0: case 0x00C2: case 0x00C4: dst[o++] = 'A'; break;
        case 0x00C7:                            dst[o++] = 'C'; break;
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: dst[o++] = 'E'; break;
        case 0x00CE: case 0x00CF:               dst[o++] = 'I'; break;
        case 0x00D4: case 0x00D6:               dst[o++] = 'O'; break;
        case 0x00D9: case 0x00DB: case 0x00DC:  dst[o++] = 'U'; break;
        case 0x0152:                            dst[o++] = 'O'; dst[o++] = 'E'; break;
        case 0x2019: case 0x2018:               dst[o++] = '\''; break;
        case 0x201C: case 0x201D:               dst[o++] = '"'; break;
        case 0x2013: case 0x2014:               dst[o++] = '-'; break;
        default:                                dst[o++] = ' '; break;
        }
    }
    dst[o] = 0;
}

/* ---------- LA REPONSE : des pages ephemeres, en plein ecran ---------- */
/* Le modele repond en deux phrases ; sur une 200x200 en corps 2 cela fait
   jusqu'a 5-6 pages. Elles s'affichent une par une, en plein ecran, le bouton
   2 passe a la suite, le bouton 1 ferme. Le texte complet reste de toute
   facon dans l'historique sauvegarde sur la carte SD. */
#define REP_TXT_MAX  900
#define REP_COLS      24          /* 24 signes x 8 px = 192 px en corps 2 */
#define REP_LIGNES    10          /* 10 lignes x 16 px a partir de y=24 */

static char rep_txt[REP_TXT_MAX];
static int  rep_pages = 1;
static int  rep_page = 0;
static bool rep_ouvert = false;
static int  rep_retour = 0;       /* page retrouvee en fermant la reponse */

/* longueur de la ligne qui commence en p, coupee au dernier mot entier */
static int rep_ligne_len(const char *p, int cols)
{
    int l = 0;
    while (p[l] && p[l] != '\n' && l < cols) l++;
    if (p[l] && p[l] != '\n' && p[l] != ' ') {          /* coupe en plein mot */
        int k = l;
        while (k > 6 && p[k] != ' ' && p[k] != '\n') k--;
        if (k > 6) l = k;
    }
    return l;
}

static const char *rep_apres_ligne(const char *p)
{
    int l = rep_ligne_len(p, REP_COLS);
    if (l <= 0) l = 1;
    p += l;
    while (*p == ' ' || *p == '\n') p++;
    return p;
}

static const char *rep_page_debut(int page)
{
    const char *p = rep_txt;
    for (int i = 0; i < page * REP_LIGNES && *p; i++) p = rep_apres_ligne(p);
    return p;
}

static int rep_compte_pages(void)
{
    const char *p = rep_txt;
    int lignes = 0;
    while (*p) { p = rep_apres_ligne(p); lignes++; }
    if (lignes == 0) return 1;
    return (lignes + REP_LIGNES - 1) / REP_LIGNES;
}

/* ouvre les pages de la reponse ; le texte part aussi vers /state (voix_dit) */
static void rep_ouvre(const char *texte)
{
    if (!texte || !*texte) return;
    texte_ascii(texte, rep_txt, sizeof(rep_txt));
    rep_pages = rep_compte_pages();
    rep_page = 0;
    if (!rep_ouvert) rep_retour = ui_page;
    rep_ouvert = true;
}

static void rep_ferme(void)
{
    rep_ouvert = false;
    ui_page = PAGE_MAITRE;
}

static void rep_age_plus(void) { if (rep_page + 1 < rep_pages) rep_page++; }

/* n : 0 = bouton 1, 1 = bouton 2 (comme partout dans le firmware)
   lecture d'une reponse longue : les DEUX boutons servent a tourner les pages
   (1 = suite, 2 = avant) ; au bout, un appui de plus referme. */
static bool rep_bouton_court(int n)
{
    if (!rep_ouvert) return false;
    if (n == 0) {
        if (rep_page + 1 < rep_pages) rep_page++;   /* bouton 1 : page suivante */
        else rep_ferme();                           /* apres la derniere : on ferme */
    } else {
        if (rep_page > 0) rep_page--;               /* bouton 2 : page precedente */
        else rep_ferme();                           /* avant la premiere : on ferme */
    }
    show_ui();
    return true;
}

static bool rep_bouton_long(int n)
{
    (void)n;
    if (!rep_ouvert) return false;
    rep_ferme();
    show_ui();
    return true;
}

static void draw_app_reponse(void)
{
    char b[16];
    a_text(6, 2, 3, "REPONSE");
    snprintf(b, sizeof(b), "%d/%d", rep_page + 1, rep_pages);
    a_text(192 - a_text_w(3, b), 2, 3, b);
    a_hline(4, 195, 19, 1);

    const char *p = rep_page_debut(rep_page);
    int y = 26;
    for (int i = 0; i < REP_LIGNES && *p; i++) {
        int l = rep_ligne_len(p, REP_COLS);
        if (l <= 0) break;
        char ligne[REP_COLS + 1];
        int k = (l > REP_COLS) ? REP_COLS : l;
        memcpy(ligne, p, (size_t)k);
        ligne[k] = 0;
        a_text(6, y, 2, ligne);
        y += 16;
        p += l;
        while (*p == ' ' || *p == '\n') p++;
    }
    /* legende en corps 2 : en corps 1 elle est illisible a bout de bras */
    a_text_c(100, 186, 2, (rep_page + 1 < rep_pages) ? "1=SUITE  2=AVANT" : "1=FERMER 2=AVANT");
}

/* ---------- 0. MAITRE : le tableau de bord ---------- */

static const char *JOURS_SEM[] = {"DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI",
                                  "VENDREDI", "SAMEDI"};
static const char *MOIS_AN[] = {"JANVIER", "FEVRIER", "MARS", "AVRIL", "MAI", "JUIN",
                                "JUILLET", "AOUT", "SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DECEMBRE"};
static const char *JOURS_COURT[] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
static const char *MOIS_COURT[] = {"JAN", "FEV", "MAR", "AVR", "MAI", "JUI",
                                   "JUI", "AOU", "SEP", "OCT", "NOV", "DEC"};

/* minuit local d'un instant, pour compter les jours sans se tromper d'heure */
static time_t minuit_local(time_t t)
{
    struct tm x;
    localtime_r(&t, &x);
    x.tm_hour = 0; x.tm_min = 0; x.tm_sec = 0; x.tm_isdst = -1;
    return mktime(&x);
}

/* ================================================================== */
/* LA BANDE : le spectre de la voix, a la place du tamagotchi          */
/* ================================================================== */
/* L'energie de la voix : RMS par bloc de 40 ms, avec auto-gain. C'est elle qui
   ouvre la bouche du compagnon. Volontairement tres peu couteux : une somme de
   carres, pas d'analyse spectrale (un Goertzel 24 bandes ici affamait le CPU et
   gelait tout le reste). */
static volatile float voix_bouche = 0.0f;   /* ouverture de la bouche (0..1) */
static float voix_acc = 0.0f;
static int   voix_cnt = 0;
static float voix_gain = 3000.0f;

static void voix_feed(const int16_t *s, int n)
{
    if (!s || n <= 0) return;
    for (int i = 0; i < n; i++) {
        float x = (float)s[i];
        voix_acc += x * x;
        if (++voix_cnt >= 640) {                 /* 40 ms a 16 kHz */
            float rms = sqrtf(voix_acc / (float)voix_cnt);
            voix_acc = 0; voix_cnt = 0;
            if (rms > voix_gain) voix_gain = rms;          /* auto-gain */
            else voix_gain -= voix_gain * 0.004f;
            if (voix_gain < 200.0f) voix_gain = 200.0f;
            float v = rms / voix_gain;
            if (v > 1.0f) v = 1.0f;
            float cible = v * 2.0f;                        /* les syllabes fortes ouvrent grand */
            if (cible > 1.0f) cible = 1.0f;
            voix_bouche += (cible - voix_bouche) * 0.75f;
        }
    }
}

/* NOTE (18/10) : une tache "visage" animait le compagnon a ~1 image/s dans une
   fenetre partielle. Mesure : un rafraichissement partiel coute 865 ms ET fait
   clignoter toute la dalle -> en boucle, c'etait un stroboscope. Supprime : le
   visage est dessine par la page, et la page n'est rafraichie que lorsque son
   contenu change reellement (voir fb_identique() dans show_ui). */

/* ---------- 1. LA PAGE PRINCIPALE ---------- */
/* ---------- 0. LE TABLEAU : la seule page de l appareil ---------- */
/*  Tout a migre sur un ecran unique : heure, ciel, compagnon, batterie,
    banc systeme, agenda, dernier mot, conseil, YouTube et livre. Le dessin
    vit dans main/tableau.cpp (aucune dependance ESP-IDF : il se rend aussi
    sur le PC, cf. tools/apercu_tableau.cpp) ; ici on remplit les donnees. */
static void draw_app_maitre(void)
{
    tableau_maitre();
}

/* ---------- 3. METEO : le ciel, et rien d'autre ---------- */
/* ------------------------------------------------------------------ */
/* Gestionnaire d'applis : une appli par page, tuee quand on la quitte.
   Chaque appli a son allocation et sa liberation ici ; le reste du firmware
   (wifi, heure, boutons, ecran, rappels, assistant) est un service vital. */
static void appli_sys_entre(void);
static void appli_sys_sort(void);

/* ---------- 2. PAROLES : tout ce qui s'est dit, en texte ---------- */
#define PAROLES_MAX 48
static char paroles_lignes[PAROLES_MAX][49];
static int  paroles_tete = 0;      /* prochaine case libre */
static int  paroles_n = 0;         /* lignes memorisees */
static int  paroles_haut = 0;      /* 0 = on voit la fin, +1 = une ligne plus haut */

static void paroles_ajoute(const char *qui, const char *texte)
{
    char prefixe[8];
    snprintf(prefixe, sizeof(prefixe), "%s: ", qui);
    const char *p = texte;
    int premier = 1;
    while (*p) {
        int place = 48 - (premier ? (int)strlen(prefixe) : 3);
        int n = 0;
        while (p[n] && n < place) n++;
        if (p[n]) {                                   /* coupe au dernier mot entier */
            int k = n;
            while (k > 8 && p[k] != ' ') k--;
            if (k > 8) n = k;
        }
        char ligne[49];
        snprintf(ligne, sizeof(ligne), "%s%.*s", premier ? prefixe : "   ", n, p);
        snprintf(paroles_lignes[paroles_tete], sizeof(paroles_lignes[0]), "%s", ligne);
        paroles_tete = (paroles_tete + 1) % PAROLES_MAX;
        if (paroles_n < PAROLES_MAX) paroles_n++;
        p += n;
        while (*p == ' ') p++;
        premier = 0;
    }
    paroles_haut = 0;                                   /* on revient sur la fin */
}

/* ---------- 1. PARLER ---------- */
/* etat de l'assistant : 0 au repos, 1 j'ecoute, 2 je reflechis, 3 je reponds */
// voix_etat declare plus haut (ligne 208)
static char voix_reponse[300] = "";
static char voix_entendu_txt[300] = "";  /* dernier texte reconnu (lu par /state) */
static volatile int assist_parle_ms = 12000;  /* duree d'ecoute (12 s par defaut) */
static volatile bool parler_radio_ouverte = false;  /* test : ne pas couper la radio en ecoutant */

/* tout ce qu'on lit sur la carte pour l'appli INFOS */
static void monde_maj_info(monde_info_t *mi)
{
    time_t now = time(NULL);
    struct tm ti;
    uint64_t tot = 0, libre = 0;
    localtime_r(&now, &ti);
    memset(mi, 0, sizeof(*mi));
    mi->cpu_mhz = (int)(esp_clk_cpu_freq() / 1000000);
    mi->ram_libre_ko = (int)(esp_get_free_heap_size() / 1024);
    mi->psram_libre_ko = (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    mi->heure = ti.tm_hour;
    mi->minute = ti.tm_min;
    mi->wifi_ok = ui_wifi_ok;
    mi->temp = ui_temp;
    if (esp_vfs_fat_info("/sdcard", &tot, &libre) == ESP_OK) {
        mi->sd_go = (int)(tot / (1024ull * 1024ull * 1024ull));
        mi->sd_libre_go = (int)(libre / (1024ull * 1024ull * 1024ull));
    }
}

/* ---------- 5. RADIO (les webradios du monde, jouees par le PC) ---------- */
#define RADIOS_FICHIER "/sdcard/mino/radios.txt"
#define RADIOS_MAX 16

static char radios_nom[RADIOS_MAX][28];
static char radios_url[RADIOS_MAX][128];
static int  radios_n = 0;
static int  radio_sel = 0;
static int  radio_etat = 0;      /* 0 arret, 1 je me connecte, 2 ca joue */

/* le minuteur de la page radio : quand il arrive a zero, il allume FG Radio */
static volatile int64_t min_fin_us = 0;     /* 0 = pas de minuteur */
static int min_preset_s = 0;                /* 5, 10, 15, 30, 60 puis 0 */

static int minuteur_restant_s(void)
{
    if (min_fin_us == 0) return 0;
    int64_t r = (min_fin_us - esp_timer_get_time()) / 1000000;
    return (r > 0) ? (int)r : 0;
}

/* ===== PAGE TIMER / ALARME : style Apple =====
   On gere plusieurs timers (compte a rebours) et alarmes (heure fixe).
   Tout est en NVS pour survivre au reboot. */
#define TIMER_MAX 8
struct timer_item_t {
    int64_t fin_s;           /* 0 = inactif, sinon echeance en heure reelle (epoch s) */
    int     duree_s;         /* duree originale en secondes */
    char    nom[24];         /* "Cuisson", "Sieste", etc. */
    bool    est_alarme;      /* false = timer, true = alarme (heure fixe) */
    bool    repet;           /* pour alarme : tous les jours */
    bool    sonne;           /* a sonne, en attente d'acquit */
};
static struct timer_item_t timers[TIMER_MAX];
static int timer_sel = 0;           /* celui qu'on edite/regarde */
static int timer_mode = 0;          /* 0 = liste, 1 = edition duree, 2 = edition nom, 3 = confirmation */
static int timer_edit_val = 0;      /* valeur en cours d'edition (secondes ou heures/minutes) */
static bool timer_edit_heures = true; /* en mode alarme : on edite heures puis minutes */

static void timers_charge(void)
{
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READONLY, &nh) != ESP_OK) return;
    size_t sz = sizeof(timers);
    nvs_get_blob(nh, "timers", timers, &sz);
    nvs_close(nh);
    /* nettoyage : on garde ce qui est encore a venir, on jette le reste
       (une echeance doit tomber dans une plage plausible : 2024 .. 2100).
       Si l'horloge n'est pas encore synchro (demarrage sans wifi), on ne juge
       rien : sinon tous les rappels enregistres etaient effaces au boot. */
    time_t now = time(NULL);
    if ((int64_t)now < 1700000000LL) return;
    for (int i = 0; i < TIMER_MAX; i++) {
        int64_t f = timers[i].fin_s;
        if (f == 0) continue;
        if (f < 1700000000LL || f > 4102444800LL || (!timers[i].sonne && f <= (int64_t)now)) {
            memset(&timers[i], 0, sizeof(timers[i]));
        }
    }
}

static void timers_sauve(void)
{
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
        nvs_set_blob(nh, "timers", timers, sizeof(timers));
        nvs_commit(nh);
        nvs_close(nh);
    }
}
static void timers_sauve(void);

/* ---- RAPPEL DEMANDE PAR L'ASSISTANT ----
   Le modele ne touche pas aux GPIO : il demande un rappel (duree + 3 mots) et
   la carte en fait un timer nomme. A l'echeance, carillon + voix (timer_sonne). */
static int  rappel_nb = 0;                 /* rappels crees depuis le demarrage */
static char rappel_dernier[48] = "";       /* le dernier, pour /state */
static bool rappel_ce_tour = false;        /* un seul rappel par question */

static int rappel_ajoute(int d, const char *nom)
{
    if (d < 10) d = 10;                            /* moins de 10 s : inutile */
    if (d > 7 * 24 * 3600) d = 7 * 24 * 3600;      /* une semaine, pas plus */
    if (!nom || !*nom) nom = "rappel";
    if ((int64_t)time(NULL) < 1700000000LL) {      /* pas d'heure = pas d'echeance */
        ESP_LOGW(TAG, "rappel refuse : horloge pas encore a l'heure (pas de wifi ?)");
        return -1;
    }
    for (int i = 0; i < TIMER_MAX; i++) {
        if (timers[i].fin_s) continue;
        timers[i].fin_s     = (int64_t)time(NULL) + (int64_t)d;
        timers[i].duree_s   = d;
        snprintf(timers[i].nom, sizeof(timers[i].nom), "%.19s", nom);
        timers[i].est_alarme = false;
        timers[i].repet      = false;
        timers[i].sonne      = false;
        timers_sauve();
        rappel_nb++;
        snprintf(rappel_dernier, sizeof(rappel_dernier), "%.40s", nom);
        ESP_LOGI(TAG, "rappel cree : '%s' dans %d s (timer %d, %d/%d pris)",
                 timers[i].nom, d, i, rappel_nb, TIMER_MAX);
        return i;
    }
    ESP_LOGW(TAG, "rappel refuse : les %d timers sont deja pris", TIMER_MAX);
    return -1;
}

/* La carte n'a pas de capteur de courant (seulement des pins d'alimentation) :
   on additionne ce qui consomme vraiment, mesure par mesure. */
static int conso_mw(void)
{
    int mw = 160;                                   /* soc + carte, au repos */
    if (esp_clk_cpu_freq() > 200000000) mw += 90;   /* coeur a 240 MHz */
    if (ui_wifi_ok)                     mw += 110;  /* wifi associe */
    if (radio_etat != 0)                mw += 275;  /* le haut-parleur joue */
    if (voix_etat != 0)                 mw += 175;  /* il parle */
    if (act_on)                         mw += 60;   /* l'ecran se rafraichit souvent */
    return mw;
}
static volatile int  radio_gen = 0;      /* change -> la tache se reconnecte */
static volatile bool radio_vive = false; /* la tache radio tourne */
static int  radio_vol = 70;              /* volume du haut-parleur, 0..100 */
static char radio_ordre[40] = "";

/* est-ce que "nom" contient "motif" ? (sans se soucier des majuscules) */
static int nom_contient(const char *nom, const char *motif)
{
    size_t ln = strlen(motif);
    if (!ln) return 1;
    for (const char *p = nom; *p; p++) {
        size_t i = 0;
        while (i < ln && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)motif[i])) i++;
        if (i == ln) return 1;
    }
    return 0;
}        /* ce que l'IA vient de demander */
static int  vol_ordre = -1;              /* volume demande par l'IA, -1 = rien */

static void radio_vol_regle(int v, int enregistre)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    radio_vol = v;
    audio_playback_set_vol((uint8_t)v);
    if (enregistre) {
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_i32(nh, "vol", (int32_t)radio_vol);
            nvs_commit(nh);
            nvs_close(nh);
        }
    }
    ESP_LOGI(TAG, "volume %d%%", radio_vol);
}

static void radio_vol_charge(void)
{
    nvs_handle_t nh;
    int32_t v = 70;
    if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
        nvs_get_i32(nh, "vol", &v);
        nvs_close(nh);
    }
    radio_vol_regle((int)v, 0);
}

/* la premiere ligne sert de version : si elle change, la liste du robot est
   remplacee. Tes propres stations s'ajoutent en dessous, elles sont gardees. */
#define RADIOS_VERSION "# BATMINO 3"
static const char *RADIOS_DEFAUT =
    RADIOS_VERSION "\n"
    "FG Radio | https://stream.rcs.revma.com/7081n2b6p98uv\n"
    "FIP | https://icecast.radiofrance.fr/fip-midfi.mp3\n"
    "FIP Rock | https://icecast.radiofrance.fr/fiprock-midfi.mp3\n"
    "FIP Groove | https://icecast.radiofrance.fr/fipgroove-midfi.mp3\n"
    "FIP Jazz | https://icecast.radiofrance.fr/fipjazz-midfi.mp3\n"
    "FIP Electro | https://icecast.radiofrance.fr/fipelectro-midfi.mp3\n"
    "FIP Pop | https://icecast.radiofrance.fr/fippop-midfi.mp3\n"
    "Radio Nova | https://novazz.ice.infomaniak.ch/novazz-128.mp3\n"
    "Radio Meuh | https://radiomeuh.ice.infomaniak.ch/radiomeuh-128.mp3\n"
    "Jazz Radio | https://jazzradio.ice.infomaniak.ch/jazzradio-high.mp3\n"
    "TSF Jazz | https://tsfjazz.ice.infomaniak.ch/tsfjazz-high.mp3\n"
    "France Musique | https://icecast.radiofrance.fr/francemusique-midfi.mp3\n"
    "franceinfo | https://icecast.radiofrance.fr/franceinfo-midfi.mp3\n"
    "France Inter | https://icecast.radiofrance.fr/franceinter-midfi.mp3\n"
    "Skyrock | https://icecast.skyrock.net/s/natio_aac_128k\n"
    "Bonheur FM | https://ice2.somafm.com/groovesalad-128-mp3\n";

static int nom_contient(const char *nom, const char *motif);

/* Les stations du robot sont toujours la. Le fichier sur la carte ajoute
   les tiennes : chaque ligne "Nom | http://..." vient en plus. Comme ca, un
   fichier abime ne peut jamais nous priver de toutes les radios. */
static int radios_ajoute(const char *nom, const char *url)
{
    if (radios_n >= RADIOS_MAX) return 0;
    if (!nom || !url || !*nom || !*url) return 0;
    if (strncmp(url, "http", 4) != 0) return 0;
    for (int i = 0; i < radios_n; i++) {               /* deja connue ? */
        const char *a = radios_nom[i], *b = nom;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return 0;
    }
    snprintf(radios_nom[radios_n], sizeof(radios_nom[0]), "%.27s", nom);
    snprintf(radios_url[radios_n], sizeof(radios_url[0]), "%.127s", url);
    radios_n++;
    return 1;
}

static int radios_fichier_ok = 0;      /* le fichier porte-t-il la bonne version ? */
static int radios_dernier_lu = 0;      /* combien de lignes utiles lues sur la carte */

static int radios_lit_fichier(void)
{
    FILE *f = fopen(RADIOS_FICHIER, "r");
    char ligne[220];
    int dans_fichier = 0;
    radios_fichier_ok = 0;

    if (f) {
        if (fgets(ligne, sizeof(ligne), f) && !strncmp(ligne, RADIOS_VERSION, strlen(RADIOS_VERSION)))
            radios_fichier_ok = 1;                     /* le fichier est a jour */
        else
            rewind(f);
        while (fgets(ligne, sizeof(ligne), f)) {
            char *bar, *nl;
            if (ligne[0] == '#') continue;
            bar = strchr(ligne, '|');
            if (!bar) continue;
            nl = strchr(ligne, '\n');
            if (nl) *nl = 0;
            *bar = 0;
            {
                char *nom = ligne, *url = bar + 1, *fin;
                while (*nom == ' ') nom++;
                fin = nom + strlen(nom);
                while (fin > nom && fin[-1] == ' ') *--fin = 0;
                while (*url == ' ') url++;
                fin = url + strlen(url);
                while (fin > url && fin[-1] == ' ') *--fin = 0;
                if (radios_ajoute(nom, url)) dans_fichier++;
            }
        }
        fclose(f);
    }
    radios_dernier_lu = dans_fichier;
    return dans_fichier;
}

/* la liste du robot, toujours presente, quoi qu'il arrive au fichier */
static void radios_ajoute_defauts(void)
{
    const char *p = RADIOS_DEFAUT;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *bar = strchr(p, '|');
        if (nl && bar && bar < nl) {
            char nom[64], url[160];
            int ln = (int)(bar - p), lu = (int)(nl - bar - 1);
            const char *u;
            if (ln > 63) ln = 63;
            memcpy(nom, p, (size_t)ln);
            nom[ln] = 0;
            while (ln > 0 && nom[ln - 1] == ' ') nom[--ln] = 0;
            u = bar + 1;
            while (*u == ' ') u++;
            lu = (int)(nl - u);
            if (lu < 0) lu = 0;
            if (lu > 159) lu = 159;
            memcpy(url, u, (size_t)lu);
            url[lu] = 0;
            if (!nom_contient(nom, "# BATMINO")) radios_ajoute(nom, url);
        }
        p = nl ? nl + 1 : p + strlen(p);
    }
}

static void radios_charge(void)
{
    int dans_fichier;
    radios_n = 0;
    dans_fichier = radios_lit_fichier();          /* d'abord tes stations */
    radios_ajoute_defauts();                      /* puis celles du robot */

    /* le fichier sert de modele : on l'ecrit s'il manque ou s'il est d'une
       vieille version, puis on relit tout pour garder le bon ordre */
    if (sd_ok && (!radios_fichier_ok || dans_fichier == 0)) {
        FILE *w = fopen(RADIOS_FICHIER, "w");
        if (w) {
            const char *p = RADIOS_DEFAUT;
            while (*p) {                       /* ligne par ligne : la SD aime bien */
                const char *nl = strchr(p, '\n');
                size_t n = nl ? (size_t)(nl - p + 1) : strlen(p);
                if (fwrite(p, 1, n, w) != n) break;
                p += n;
            }
            if (ferror(w)) {
                ESP_LOGW(TAG, "la carte a refuse l'ecriture des radios");
                fclose(w);
                remove(RADIOS_FICHIER);
            } else {
                fclose(w);
                ESP_LOGI(TAG, "liste des radios ecrite sur la carte");
                radios_n = 0;
                radios_lit_fichier();
                radios_ajoute_defauts();
            }
        }
    }
    /* et on commence sur FG Radio, c'est la sienne */
    for (int i = 0; i < radios_n; i++)
        if (nom_contient(radios_nom[i], "FG Radio")) { radio_sel = i; break; }
    ESP_LOGI(TAG, "radios : %d station(s)", radios_n);
}

/* La radio se debrouille toute seule : la carte ouvre le flux en wifi,
   le decode et le joue. Le PC n'a plus rien a voir la-dedans. */
static void radio_joue(void);

/* le minuteur sonne : on met FG Radio, assez fort, et on montre la page radio */
static void minuteur_sonne(void)
{
    ESP_LOGW(TAG, "minuteur : c'est l'heure, j'allume FG Radio");
    for (int i = 0; i < radios_n; i++)
        if (nom_contient(radios_nom[i], "FG Radio")) { radio_sel = i; break; }
    if (radio_vol < 80) radio_vol_regle(80, 1);
    ui_page = PAGE_MAITRE;
    radio_joue();
    ui_beep(); ui_beep(); ui_beep();
    show_ui();
}

/* Module radio : DESACTIVE pour l'instant (Taz n'en veut pas encore).
   Reactivable sans reflasher : GET /radio?actif=1  (garde en NVS). */
static bool radio_actif = false;

static void radio_joue(void)
{
    if (!radio_actif) { ESP_LOGW(TAG, "radio desactivee (reactiver : /radio?actif=1)"); return; }
    if (radios_n <= 0) return;
    radio_gen++;                               /* nouvelle ecoute */
    radio_etat = 1;
    show_ui();
    if (!radio_vive) {
        radio_vive = true;
        xTaskCreate(radio_wifi_task, "radio", 12288, NULL, 3, NULL);
    }
}

static void timer_sonne(int i)
{
    if (i < 0 || i >= TIMER_MAX) return;
    timers[i].sonne = true;
    timers_sauve();
    ESP_LOGW(TAG, "timer/alarme %d sonne : %s", i, timers[i].nom);
    ui_page = PAGE_MAITRE;
    timer_sel = i;
    timer_mode = 0;
    /* carillon puis voix, comme le rendez-vous : des bips et un demarrage de
       radio (desactivee) ne s'entendaient pas. La phrase dit le nom du rappel. */
    snprintf(annonce_txt, sizeof(annonce_txt), "Rappel : %.60s.", timers[i].nom);
    alarme_t0 = esp_timer_get_time();
    alarme_on = true;
    show_ui();
}

static void radio_stop(void)
{
    if (radio_etat != 0) {
        radio_etat = 0;
        radio_gen++;                           /* la tache va lacher le flux */
        radio_mode = false;
        ESP_LOGI(TAG, "radio : stop");
        show_ui();
    }
}

static void radio_change(int d)
{
    if (radios_n <= 0) return;
    radio_sel = (radio_sel + d + radios_n) % radios_n;
    if (radio_etat != 0) {                     /* on change de station en direct */
        radio_gen++;
        radio_etat = 1;
        show_ui();
    } else {
        radio_joue();
    }
}


static char journal_lignes[12][64];
static int  journal_n = 0, journal_tete = 0;

static void journal_ajoute(const char *texte)
{
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    snprintf(journal_lignes[journal_tete], sizeof(journal_lignes[0]),
             "%02d:%02d %s", ti.tm_hour, ti.tm_min, texte);
    journal_tete = (journal_tete + 1) % 12;
    if (journal_n < 12) journal_n++;
    /* la carte SD garde tout : un fichier par jour */
    if (sd_ok) {
        char chemin[80];
        FILE *f;
        snprintf(chemin, sizeof(chemin), "/sdcard/mino/journal-%04d%02d%02d.md",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        f = fopen(chemin, "a");
        if (f) {
            fprintf(f, "- %02d:%02d %s (stade %s, age %u j, sante %u%%)\n",
                    ti.tm_hour, ti.tm_min, texte, pet_stade_nom(pet_stade(&pet)),
                    (unsigned)(pet.age_s / 86400), pet_sante(&pet));
            fclose(f);
        }
    }
}

typedef struct { int x0, y0, x1, y1; } ui_btn_t;
static const ui_btn_t BTN[4] = {
    { 10,  34,  95, 112},
    {105,  34, 190, 112},
    { 10, 118,  95, 196},
    {105, 118, 190, 196},
};

/* ================================================================== */
/* PAGE SYSTEM : le mini btop (cpu, ram, gpu, watts, batterie,        */
/* reseau, stockage). C'est la seule page qui se rafraichit seule.    */
/* ================================================================== */

#define SYS_HIST 38
static int sys_cpu = 0;        /* 0..100, arrondi (pour /state et le graphe) */
static int sys_cpu10 = 0;      /* 0..1000, pour afficher "0.9 %" */
static uint8_t sys_hist[SYS_HIST];
static int sys_hist_n = 0, sys_hist_tete = 0;

/* Compteurs d'execution FreeRTOS : la somme des temps de tache donne le temps
   reellement occupe, sur les deux coeurs. (Le hook idle ne marchait pas ici :
   il ne se declenchait pas assez souvent a 100 Hz pour etre exploitable.) */
static TaskStatus_t *sys_ts = NULL;      /* alloue seulement sur la page SYSTEM */

static void sys_cpu_maj(void)
{
    static uint32_t somme0 = 0, total0 = 0;
    if (!sys_ts) return;
    uint32_t total = 0;
    UBaseType_t n = uxTaskGetSystemState(sys_ts, 48, &total);
    uint32_t somme = 0;
    for (UBaseType_t i = 0; i < n; i++) {
        /* les taches idle comptent comme du temps occupe : on les exclut,
           sinon la somme vaut toujours 100 % des deux coeurs */
        const char *nm = sys_ts[i].pcTaskName ? sys_ts[i].pcTaskName : "";
        if (!strncmp(nm, "IDLE", 4)) continue;
        somme += (uint32_t)sys_ts[i].ulRunTimeCounter;
    }
    if (!total0) { somme0 = somme; total0 = total; return; }
    uint32_t dsomme = somme - somme0, dtot = total - total0;
    somme0 = somme; total0 = total;
    int pct10 = 0;
    if (dtot > 0 && dtot < 50000000u)
        pct10 = (int)((uint64_t)dsomme * 1000 / (uint64_t)dtot);  /* en "un coeur" */
    if (pct10 < 0) pct10 = 0;
    if (pct10 > 1000) pct10 = 1000;
    sys_cpu10 = pct10;
    sys_cpu = (pct10 + 5) / 10;
    sys_hist[sys_hist_tete] = (uint8_t)sys_cpu;
    sys_hist_tete = (sys_hist_tete + 1) % SYS_HIST;
    if (sys_hist_n < SYS_HIST) sys_hist_n++;
}

/* --- batterie : GPIO4 (BAT_ADC), pont 200K/200K -> VBAT = 2 x VADC --- */
static adc_oneshot_unit_handle_t sys_adc = NULL;
static bool sys_adc_init = false;
static int sys_bat_mv_cache = -1;    /* derniere mesure, pour /state (sans ADC) */
static int sys_bat_pct_cache = -1;

static int sys_bat_mv(void)
{
    if (!sys_adc_init) {
        adc_oneshot_unit_init_cfg_t uc = {};
        uc.unit_id = ADC_UNIT_1;
        esp_err_t e = adc_oneshot_new_unit(&uc, &sys_adc);
        if (e != ESP_OK) {
            /* on ne verrouille plus l'echec : la mesure sera retentee au
               prochain appel (avant, un seul echec au demarrage rendait la
               batterie invisible jusqu'au reboot) */
            sys_adc = NULL;
            ESP_LOGW(TAG, "batterie : unite ADC indisponible (%s)", esp_err_to_name(e));
            return -1;
        }
        adc_oneshot_chan_cfg_t cc = {};
        cc.atten = ADC_ATTEN_DB_12;
        cc.bitwidth = ADC_BITWIDTH_DEFAULT;
        e = adc_oneshot_config_channel(sys_adc, ADC_CHANNEL_3, &cc);
        if (e != ESP_OK) {
            adc_oneshot_del_unit(sys_adc);
            sys_adc = NULL;
            ESP_LOGW(TAG, "batterie : canal ADC indisponible (%s)", esp_err_to_name(e));
            return -1;
        }
        sys_adc_init = true;
    }
    if (!sys_adc) return -1;
    int somme = 0, n = 0;
    for (int i = 0; i < 8; i++) {
        int r = 0;
        if (adc_oneshot_read(sys_adc, ADC_CHANNEL_3, &r) == ESP_OK) { somme += r; n++; }
    }
    if (!n) return -1;
    return (somme / n) * 3300 / 4095 * 2;
}

static int sys_bat_pct(int mv)
{
    if (mv < 2500) return -1;                 /* pas de batterie branchee */
    if (mv >= 4200) return 100;
    if (mv <= 3300) return 0;
    return (mv - 3300) * 100 / (4200 - 3300);
}

/* --- reseau : octets comptes par l'application (http_get + envoi STT) ---
   ESP-IDF n'expose pas les compteurs de l'interface Wi-Fi (pas de MIB2 dans
   son lwIP) : on compte donc ce qui passe vraiment par nos sockets. */
static volatile uint32_t net_rx_octets = 0, net_tx_octets = 0;
static int64_t sys_net_ms = 0;
static uint32_t sys_net_rx0 = 0, sys_net_tx0 = 0;
static int sys_rx_s = 0, sys_tx_s = 0;

static void sys_net_maj(void)
{
    uint32_t rx = net_rx_octets;
    uint32_t tx = net_tx_octets;
    int64_t now = esp_timer_get_time() / 1000;
    if (sys_net_ms) {
        int64_t dt = now - sys_net_ms;
        if (dt > 200) {
            sys_rx_s = (int)((int64_t)(rx - sys_net_rx0) * 1000 / dt);
            sys_tx_s = (int)((int64_t)(tx - sys_net_tx0) * 1000 / dt);
        }
    }
    sys_net_ms = now; sys_net_rx0 = rx; sys_net_tx0 = tx;
}

/* un echantillon = une image de la page, toutes les SYS_PERIODE_MS */
#define SYS_PERIODE_MS 5000
static void sys_echantillon(void)
{
    sys_cpu_maj();
    sys_net_maj();
}

/* La page SYSTEM est la seule "app" qui alloue quelque chose. En entrant on
   alloue (PSRAM, jamais la RAM interne qui est comptee), en sortant on rend
   tout : plus de tableau de taches, plus d'unite ADC, plus d'echantillonnage.
   Comme ca une page au premier plan ne laisse RIEN derriere elle. */
static bool sys_est_active = false;

static void sys_page_entre(void)
{
    if (sys_est_active) return;
    sys_est_active = true;
    if (!sys_ts) sys_ts = (TaskStatus_t *)heap_caps_malloc(48 * sizeof(TaskStatus_t),
                                                          MALLOC_CAP_SPIRAM);
    sys_hist_n = 0; sys_hist_tete = 0; sys_cpu = 0; sys_cpu10 = 0;
    sys_net_ms = 0; sys_rx_s = 0; sys_tx_s = 0;
    if (!sys_ts) ESP_LOGW(TAG, "systeme : pas de PSRAM pour les stats de taches");
}

static void sys_page_sort(void)
{
    if (!sys_est_active) return;
    sys_est_active = false;
    if (sys_ts) { free(sys_ts); sys_ts = NULL; }
    if (sys_adc) { adc_oneshot_del_unit(sys_adc); sys_adc = NULL; }
    sys_adc_init = false;
    ESP_LOGI(TAG, "systeme : page quittee, memoire rendue (interne %u o libres)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

/* les deux crochets SYSTEM pour le gestionnaire d'applis */
static void appli_sys_entre(void) { sys_page_entre(); }
static void appli_sys_sort(void)  { sys_page_sort(); }

/* --- petits utilitaires de dessin --- */
static void sys_barre(int x, int y, int w, int h, int pct)
{
    if (pct < 0) return;
    if (pct > 100) pct = 100;
    for (int i = 0; i < w; i++) { a_px(x + i, y, 1); a_px(x + i, y + h - 1, 1); }
    for (int j = 0; j < h; j++) { a_px(x, y + j, 1); a_px(x + w - 1, y + j, 1); }
    int f = (w - 4) * pct / 100;
    for (int j = 1; j < h - 1; j++)
        for (int i = 1; i <= f; i++) a_px(x + 1 + i, y + j, 1);
}

/* acces au wifi et au scan, definis plus bas (section WiFi) */
static const char *wifi_ssid_i(int i);
static void wifi_secours(void);
static void wifi_retour_principal(void);
static EventGroupHandle_t wifi_eg;
#define WIFI_OK_BIT BIT0
static void time_sync(void);

/* ------------------------------------------------------------------ */
/* Page REGLAGES : wifi (liste + clavier), ecran, infos                */
/* ------------------------------------------------------------------ */
static int  reg_cadence = 5;      /* secondes entre deux images (ancienne page systeme) */

static const char *ip_courante(void)
{
    static char ip[20] = "";
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t in;
    if (nif && esp_netif_get_ip_info(nif, &in) == ESP_OK && in.ip.addr)
        esp_ip4addr_ntoa(&in.ip, ip, sizeof(ip));
    else
        snprintf(ip, sizeof(ip), "192.168.8.210");
    return ip;
}

static int last_drawn_page = -1;      /* pour forcer un refresh complet au changement de page */
static int partiels = 99;             /* partiels enchaines depuis le dernier refresh complet */

/* ================================================================== */
/* LE LIVRE -- le robot lit                                           */
/*                                                                    */
/* Un fichier .txt par livre dans /sdcard/livres. Le texte entier est  */
/* garde en PSRAM (1 octet par caractere affiche) : tourner une page   */
/* ne relit pas la carte, ca ne coute qu'un rafraichissement partiel.  */
/* Gestes : 1 = page suivante, 2 = page precedente, double 2 = livre    */
/* suivant, appui long = retour a l'accueil (geste commun a tout).     */
/* ================================================================== */
#define LIVRE_DOSSIER "/sdcard/livres"
#define LIVRE_MAXI    12
static livre_t livre;
static bool  livre_pret = false;
static char  livre_noms[LIVRE_MAXI][LIVRE_TITRE_MAX];
static int   livre_n = 0;
static bool  livre_scan_fait = false;
static char  livre_dernier[LIVRE_TITRE_MAX] = "";   /* le dernier livre lu (NVS) */
static int   livre_derniere_page = 0;

static bool txt_nom(const char *n)
{
    size_t l = strlen(n);
    if (l < 5 || n[l - 4] != '.') return false;
    char a = n[l - 3] | 0x20, b = n[l - 2] | 0x20, c = n[l - 1] | 0x20;
    return a == 't' && b == 'x' && c == 't';
}

static void livre_scan(void)
{
    livre_n = 0;
    livre_scan_fait = true;
    mkdir(LIVRE_DOSSIER, 0777);          /* cree le dossier au premier usage */
    DIR *d = opendir(LIVRE_DOSSIER);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && livre_n < LIVRE_MAXI) {
        if (!txt_nom(e->d_name)) continue;
        snprintf(livre_noms[livre_n], LIVRE_TITRE_MAX, "%s", e->d_name);
        livre_n++;
    }
    closedir(d);
    for (int i = 1; i < livre_n; i++) {          /* ordre alphabetique */
        char tmp[LIVRE_TITRE_MAX];
        snprintf(tmp, sizeof(tmp), "%s", livre_noms[i]);
        int j = i - 1;
        while (j >= 0 && strcasecmp(livre_noms[j], tmp) > 0) {
            snprintf(livre_noms[j + 1], LIVRE_TITRE_MAX, "%s", livre_noms[j]);
            j--;
        }
        snprintf(livre_noms[j + 1], LIVRE_TITRE_MAX, "%s", tmp);
    }
}

static void livre_memo(void)
{
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READWRITE, &nh) != ESP_OK) return;
    if (livre_pret && livre_dernier[0]) {
        nvs_set_str(nh, "livnom", livre_dernier);
        nvs_set_i32(nh, "livpg", livre.page);
        livre_derniere_page = livre.page;
    }
    nvs_commit(nh);
    nvs_close(nh);
}

/* ouvre un livre du dossier. page < 0 : on garde la page courante du livre
   s'il est deja ouvert, sinon la premiere. */
static bool livre_ouvre(const char *fichier, int page)
{
    if (!fichier || !fichier[0]) return false;
    char chemin[LIVRE_CHEMIN_MAX];
    snprintf(chemin, sizeof(chemin), LIVRE_DOSSIER "/%s", fichier);
    bool meme = livre_pret && !strcmp(livre_dernier, fichier);
    int p = (page >= 0) ? page : (meme ? livre.page : 0);
    livre_ferme(&livre);
    livre_pret = livre_charge(&livre, chemin, LIVRE_COLS, LIVRE_ROWS);
    if (!livre_pret) {
        ESP_LOGW(TAG, "livre : %s illisible", chemin);
        livre_dernier[0] = 0;
        return false;
    }
    livre_va(&livre, p);
    snprintf(livre_dernier, sizeof(livre_dernier), "%s", fichier);
    livre_derniere_page = livre.page;
    ESP_LOGI(TAG, "livre : %s, %d lignes, %d pages (%d o)", livre.nom,
             livre.nlignes, livre_pages(&livre), livre.len);
    return true;
}

/* (re)ouvre le livre de la derniere session, ou le premier du dossier */
static bool livre_reprise(void)
{
    if (livre_pret) return true;                 /* deja ouvert : rien a relire */
    if (!livre_scan_fait) livre_scan();
    if (livre_n == 0) return false;
    bool connu = false;
    for (int i = 0; i < livre_n; i++)
        if (livre_dernier[0] && !strcasecmp(livre_noms[i], livre_dernier)) connu = true;
    if (connu && livre_ouvre(livre_dernier, livre_derniere_page)) return true;
    return livre_ouvre(livre_noms[0], 0);
}

static void livre_suivant(int d)
{
    livre_scan();                        /* un livre ajoute a la carte doit apparaitre */
    if (livre_n < 2) { ui_beep(); return; }
    int k = 0;
    for (int i = 0; i < livre_n; i++)
        if (!strcasecmp(livre_noms[i], livre_dernier)) k = i;
    k = (k + d + livre_n) % livre_n;
    if (livre_ouvre(livre_noms[k], 0)) livre_memo();
}

static bool livre_tourne(int d)
{
    if (!livre_pret) return false;
    bool change = livre_avance(&livre, d);
    if (change) livre_memo();
    return change;
}

/* quitte la page : la page courante est deja notee, on rend la PSRAM */
static void livre_arret(void)
{
    if (livre_pret) livre_memo();
    livre_ferme(&livre);
    livre_pret = false;
}

/* ================================================================== */
/* LE LAUNCHER : la grille des applis                                  */
/* ================================================================== */
/* Il n'y avait pas de launcher dans ce firmware : les applis etaient les pages,
   et on passait de l'une a l'autre au bouton. Cette page est la grille qu'on
   cherchait — 8 tuiles, la selection au bouton 1, l'entree au bouton 2, le
   retour a l'accueil par un appui long. Elle ne fait pas partie du cycle
   (comme REGLAGES) : on y entre par le DOUBLE APPUI DU BOUTON 2. */

/* le launcher a disparu avec les pages (11/11) */


/* ================================================================== */
/* 8. MATRIX : l'analyse du systeme, en pluie de code                  */
/* ================================================================== */
/* Trois contraintes de la dalle, et la page est dessinee autour :
   - 1 bit par pixel : pas de vert possible. La pluie de code est donc en
     VIDEO INVERSEE (glyphes blancs sur fond noir), ce qui est exactement le
     rendu d'un terminal, et bien plus lisible qu'un faux gris.
   - un rafraichissement partiel coute 865 ms et fait clignoter tout l'ecran :
     une pluie continue serait un stroboscope. On joue donc une SEQUENCE de
     quatre images (chacune en refresh complet), pas une animation.
   - la sequence se termine sur un rapport fixe : ce que la page analyse est
     le vrai systeme (cpu, ram, psram, carte, wifi, batterie, cerveau), pas un
     decor. Bouton 1 court relance la sequence, appui long rend l'accueil. */

#define MTX_COLS    16
#define MTX_CW      12                    /* pas horizontal entre deux colonnes */
#define MTX_GLYPH   "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#%&*+=<>/"
#define MTX_NGLYPH  ((int)sizeof(MTX_GLYPH) - 1)
#define MTX_ETAPES  4                     /* 4 images de pluie, puis le rapport */

static int      mtx_etape = MTX_ETAPES;   /* MTX_ETAPES = rapport affiche */
static bool     mtx_anim  = false;
static uint32_t mtx_graine = 0x9E3779B9u;
static int64_t  mtx_dernier_ms = 0;       /* derniere image de la sequence */

/* vitesse de chute de chaque colonne : fixe, pour que l'image soit la meme
   d'une session a l'autre (la graine ne change que les glyphes) */
static const uint8_t MTX_VITESSE[MTX_COLS] = { 7, 11, 5, 9, 6, 13, 8, 4,
                                               12, 7, 10, 5, 8, 9, 6, 11 };

static uint32_t mtx_rand(uint32_t *s)
{
    *s = (*s * 1664525u) + 1013904223u;
    return *s >> 8;
}

/* le fond noir : la pluie est en video inversee. Attention a la convention du
   framebuffer : 1 = pixel noir (a_px(..., 1)), 0 = blanc. Un a_px(x, y, 0)
   laisse donc la page blanche, ce qui a donne une premiere version vide. */
/* une colonne de pluie : un fil vertical continu (le 1 bit n'a pas de gris,
   c'est le fil qui donne la densite) et des glyphes tous les 6 px dessus. La
   tete est un bloc plein 3x5. Rien ne sort de la bande MTX_Y0..MTX_Y1 : sans
   ca la pluie passait sur le titre et sur la barre. */
#define MTX_Y0  22
#define MTX_Y1  166

/* la sequence avance au rythme de la tache de fond (700 ms) : une image toutes
   les 2,4 s. La dalle met deja ~1 s a se rafraichir et clignote a chaque
   image ; plus vite, c'est un stroboscope, pas une animation. */
static void mtx_tick(void)
{
    if (!mtx_anim) return;
    mtx_anim = false;               /* la page MATRIX n'existe plus */
    return;
    int64_t t = esp_timer_get_time() / 1000;
    if (mtx_dernier_ms && t - mtx_dernier_ms < 2400) return;
    mtx_dernier_ms = t;
    mtx_etape++;
    if (mtx_etape >= MTX_ETAPES) { mtx_etape = MTX_ETAPES; mtx_anim = false; }
    show_ui();
}

static void mtx_lance(void)
{
    mtx_anim = true;
    mtx_etape = 0;
    mtx_graine = (uint32_t)esp_timer_get_time() | 1u;
    mtx_dernier_ms = esp_timer_get_time() / 1000;
}

static void mtx_stop(void)
{
    mtx_anim = false;
    /* la sequence s'arrete sur place ; on revient sur la page = on relance */
}


/* ---- anti-clignotement : on n'envoie une image QUE si elle a change ----
   Un rafraichissement partiel de cette dalle coute 865 ms et fait clignoter
   tout l'ecran. Avant, la promenade du compagnon appelait show_ui() toutes les
   380 ms : la dalle flashait donc en continu. On garde une copie de la
   derniere image envoyee ; contenu identique = aucune commande a la dalle. */
static uint8_t fb_dernier[FB_LEN];
static bool fb_valide = false;
/* Cadence minimale entre deux images : chaque rafraichissement fait clignoter
   la dalle ~0,5 s (forme d'onde du panneau, il n'y a pas de mode plus doux).
   En dessous de 3 s d'intervalle, c'est un stroboscope, pas une animation. */
#define UI_CADENCE_MS 3000
static int64_t dernier_envoi_ms = 0;
static int dernier_etat_voix = 0;

/* ---- une dalle bloquee (BUSY reste haut) ne se debloque qu'en la mettant
   hors tension. Une seule tentative toutes les 10 s, trois au total : au-dela
   on laisse tomber (ecran fige) plutot que de harceler la dalle. */
static int64_t ecran_soin_ms = 0;
static int ecran_soins = 0;

static bool ecran_reprise(void)      /* true si la dalle est utilisable */
{
    if (!epd) return false;
    if (!epd->panne) return true;
    int64_t now = esp_timer_get_time() / 1000;
    if (ecran_soins >= 3 || now - ecran_soin_ms < 10000) return false;
    ecran_soin_ms = now;
    ecran_soins++;
    ESP_LOGW(TAG, "ecran : dalle bloquee, coupure d'alimentation (essai %d/3)", ecran_soins);
    gpio_set_level((gpio_num_t)EPD_PWR_PIN, 1);      /* hors tension */
    vTaskDelay(pdMS_TO_TICKS(3000));                 /* laisser la dalle se vider */
    gpio_set_level((gpio_num_t)EPD_PWR_PIN, 0);      /* sous tension */
    vTaskDelay(pdMS_TO_TICKS(500));
    epd->EPD_Panne_Reset();
    epd->EPD_Init();
    if (epd->panne) return false;
    epd->EPD_Clear();
    epd->EPD_Display();
    epd->EPD_Init_Partial();
    epd->EPD_ShowBase(fb, FB_LEN);
    if (epd->panne) return false;
    fb_valide = false;               /* tout est a redessiner */
    partiels = 0;
    last_drawn_page = -1;
    ESP_LOGI(TAG, "ecran : dalle revenue apres coupure");
    return true;
}

/* ================================================================== */
/* LA PAGE UNIQUE                                                     */
/*                                                                    */
/*  Un seul ecran porte tout ce que le robot sait. Cette fonction ne  */
/*  dessine rien : elle rassemble les donnees vivantes (horloge,      */
/*  reseau, capteur, meteo, compagnon, banc, agenda, memoire, conseil,*/
/*  YouTube, livre) et les passe a tableau_dessine().                 */
/* ================================================================== */
static void tableau_maitre(void)
{
    tableau_t t;
    memset(&t, 0, sizeof(t));

    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    t.heure  = ti.tm_hour;
    t.minute = ti.tm_min;
    t.sem    = ti.tm_wday;
    t.jour   = ti.tm_mday;
    t.mois   = ti.tm_mon;

    /* ---- reseau : l'icone de l'en-tete suffit ---- */
    t.wifi = ui_wifi_ok;
    {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) t.rssi = ap.rssi;
    }

    /* ---- energie ---- */
    /* la mesure alimente aussi /state : depuis que la page SYSTEM est partie,
       c'est le seul endroit qui lit l'ADC de la batterie */
    {
        int mv = sys_bat_mv();
        sys_bat_mv_cache  = mv;
        sys_bat_pct_cache = sys_bat_pct(mv);
        t.bat_pct = sys_bat_pct_cache;             /* -1 : USB, pas de batterie */
    }

    /* ---- capteur interne (SHTC3) ---- */
    t.capteur  = ui_shtc_ok;
    t.temp_int = ui_temp;

    /* ---- meteo ---- */
    t.meteo   = meteo_ok;
    t.meteo_c = meteo_temp;
    snprintf(t.meteo_txt, sizeof(t.meteo_txt), "%.*s",
             (int)sizeof(t.meteo_txt) - 1, meteo_ok ? meteo_texte(meteo_code) : "");
    t.dem_min = (meteo_dem_code >= 0) ? (int)meteo_dem_min : -999;
    t.dem_max = (int)meteo_dem_max;
    t.dem_pluie = (meteo_dem_code >= 0) ? meteo_dem_pluie : -1;

    /* le compagnon n'est plus dessine : ses jauges ont quitte l'ecran le
       11/11 (le moteur tamagotchi tourne toujours, il nourrit l'emotion et
       le contexte de l'assistant). */

    /* ---- agenda : le prochain rendez-vous, sinon les minuteurs ---- */
    if (ms_debut > 0 && ms_titre[0]) {
        if (ms_dans >= 0 && ms_dans <= 90) {
            snprintf(t.rdv_quand, sizeof(t.rdv_quand), "DANS %d MIN", ms_dans);
        } else {
            time_t ts = (time_t)ms_debut;
            struct tm te;
            localtime_r(&ts, &te);
            long jours = (long)((minuit_local(ts) - minuit_local(now) + 3600) / 86400);
            if (jours <= 0)      snprintf(t.rdv_quand, sizeof(t.rdv_quand), "AUJ. %s", ms_heure);
            else if (jours == 1) snprintf(t.rdv_quand, sizeof(t.rdv_quand), "DEMAIN %s", ms_heure);
            else                 snprintf(t.rdv_quand, sizeof(t.rdv_quand), "%d %s %s",
                                          te.tm_mday, MOIS_COURT[te.tm_mon % 12], ms_heure);
        }
        snprintf(t.rdv_qui, sizeof(t.rdv_qui), "%.*s", (int)sizeof(t.rdv_qui) - 1,
                 ms_org[0] ? ms_org : ms_titre);
    } else {
        int actifs = 0;
        for (int i = 0; i < TIMER_MAX; i++) if (timers[i].fin_s) actifs++;
        t.rappels = actifs;
    }

    /* youtube et le livre ont ete retires de la page le 11/11 : leurs
       donnees restent dans le firmware (routes /livre, /state) mais ne sont
       plus collectees pour le tableau. */

    t.alarme = alarme_on;
    tableau_dessine(&t);
}


static void show_ui(void)
{
    bool plein;
    bool pris = false;
    if (ui_mux) pris = (xSemaphoreTake(ui_mux, pdMS_TO_TICKS(3000)) == pdTRUE);
    if (!pris && ui_mux) { redraw_demande = true; return; }   /* un autre dessin est en cours : on reessaie */
    fb_clear();
    art_begin(fb, 0);                 /* les dessins BATMino partagent ce framebuffer */
    if (rep_ouvert && !act_on) {  /* la reponse occupe tout l'ecran tant qu'on ne la ferme pas */
        fb_oy = 0;
        draw_app_reponse();
        plein = (last_drawn_page != -3);
        last_drawn_page = -3;
    } else if (act_on) {          /* une session est en cours : on garde l'ecran d'activite */
        fb_oy = 0;
        draw_activity();
        draw_status_bar();
        plein = (last_drawn_page != -2);
        last_drawn_page = -2;
    } else if (ui_page == PAGE_ASTUCE) {
        fb_oy = 0;
        astuce_dessine(astuce_txt, astuce_src, astuce_quand, astuce_dispo);
        plein = (last_drawn_page != ui_page) || (partiels >= 40);
        last_drawn_page = ui_page;
    } else {
        /* pas de page 3 : tout le reste retombe sur le tableau */
        fb_oy = 0;
        ui_page = PAGE_MAITRE;
        anim_ph++;
        draw_app_maitre();
        plein = (last_drawn_page != ui_page) || (partiels >= 40);
        last_drawn_page = ui_page;
    }

    /* rien de neuf a l'ecran : on ne touche pas a la dalle */
    if (fb_valide && memcmp(fb, fb_dernier, FB_LEN) == 0) {
        if (pris) xSemaphoreGive(ui_mux);
        return;
    }
    /* changement d'etat (Laura parle/ecoute, changement de page) = prioritaire */
    bool prioritaire = plein || (voix_etat != dernier_etat_voix);
    if (!prioritaire && (esp_timer_get_time() / 1000 - dernier_envoi_ms) < UI_CADENCE_MS) {
        if (pris) xSemaphoreGive(ui_mux);
        return;
    }
    if (!ecran_reprise()) {
        if (pris) xSemaphoreGive(ui_mux);
        return;
    }
    if (xSemaphoreTake(epd_mux, pdMS_TO_TICKS(8000)) != pdTRUE) {
        ESP_LOGW(TAG, "ecran : mutex occupe, image sautee");
        redraw_demande = true;
        if (pris) xSemaphoreGive(ui_mux);
        return;
    }
    if (plein) {
        /* l'e-paper garde la trace des pixels pilotes : un enchainement de
           partiels laisse les fantomes de la page precedente. On repasse par
           un vrai refresh complet (LUT pleine + les deux RAM) des qu'on change
           d'ecran, et tous les 10 partiels. */
        epd->EPD_ShowFullClean(fb, FB_LEN);
        partiels = 0;
        refresh_count++;
    } else {
        epd->EPD_ShowPart(fb, FB_LEN);
        partiels++;
    }
    if (!epd->panne) {
        memcpy(fb_dernier, fb, FB_LEN);
        fb_valide = true;
        dernier_envoi_ms = esp_timer_get_time() / 1000;
        dernier_etat_voix = voix_etat;
    }
    xSemaphoreGive(epd_mux);
    if (pris) xSemaphoreGive(ui_mux);
}

/* --- capteur SHTC3 (temperature / humidite) --- */
static bool ui_read_sensors(void)
{
    uint8_t w[2] = {0x35, 0x17};
    uint8_t m[2] = {0x7C, 0xA2};
    uint8_t b[6];
    if (shtc3_handle == NULL) return false;
    if (i2c_mux) xSemaphoreTake(i2c_mux, portMAX_DELAY);
    i2c_write_buff(shtc3_handle, -1, w, 2);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_err_t e1 = i2c_write_buff(shtc3_handle, -1, m, 2);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_err_t e2 = (e1 == ESP_OK) ? i2c_read_buff(shtc3_handle, -1, b, 6) : e1;
    if (i2c_mux) xSemaphoreGive(i2c_mux);
    if (e1 != ESP_OK || e2 != ESP_OK) { ui_shtc_ok = false; return false; }
    uint16_t tr = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
    uint16_t hr = (uint16_t)(((uint16_t)b[3] << 8) | b[4]);
    ui_temp_brut = 175.0f * (float)tr / 65536.0f - 45.0f;
    ui_temp = ui_temp_brut + ui_temp_offset;   /* correction d'auto-echauffement */
    ui_humi = 100.0f * (float)hr / 65536.0f;
    ui_shtc_ok = true;
    return true;
}

/* --- tactile FT6336 --- */
static bool touch_read_raw(uint16_t *x, uint16_t *y)
{
    uint8_t n = 0, b[4];
    if (ft6336_handle == NULL) return false;
    if (i2c_read_buff(ft6336_handle, 0x02, &n, 1) != ESP_OK) return false;
    if (n == 0) return false;
    if (i2c_read_buff(ft6336_handle, 0x03, b, 4) != ESP_OK) return false;
    *x = (uint16_t)((((uint16_t)b[0] & 0x0f) << 8) | b[1]);
    *y = (uint16_t)((((uint16_t)b[2] & 0x0f) << 8) | b[3]);
    return true;
}

static bool touch_read(uint16_t *x, uint16_t *y)
{
    if (!i2c_mux) return false;
    xSemaphoreTake(i2c_mux, portMAX_DELAY);
    bool r = touch_read_raw(x, y);
    xSemaphoreGive(i2c_mux);
    return r;
}

static void touch_reset(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << 7;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(GPIO_NUM_7, 1); vTaskDelay(pdMS_TO_TICKS(60));
    gpio_set_level(GPIO_NUM_7, 0); vTaskDelay(pdMS_TO_TICKS(60));
    gpio_set_level(GPIO_NUM_7, 1); vTaskDelay(pdMS_TO_TICKS(120));
}

static void ui_action(int i)
{
    switch (i) {
    case 0:
        ui_page = PAGE_MAITRE;
        ui_face = (ui_face + 1) % EXPR_N;
        ESP_LOGI(TAG, "bouton visages -> %s", EXPR[ui_face].nom);
        show_ui();
        break;
    case 1:
        ESP_LOGI(TAG, "bouton son : bip");
        ui_beep();
        break;
    case 2: {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGW(TAG, "bouton wifi : deconnexion");
            esp_wifi_disconnect();
            ui_wifi_ok = false;
        } else {
            ESP_LOGI(TAG, "bouton wifi : reconnexion");
            esp_wifi_connect();
        }
        show_ui();
        break;
    }
    case 3:
        ESP_LOGW(TAG, "bouton power : redemarrage");
        esp_restart();
        break;
    }
}

static void touch_handle(int sx, int sy, int ex, int ey, int ms)
{
    int dx = ex - sx, dy = ey - sy;
    geste_ms = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "tactile (%d,%d)->(%d,%d) %d ms", sx, sy, ex, ey, ms);

    if (sy < 28 && sx > 160) {          /* coin haut droit : wifi on/off */
        ui_action(2);
        return;
    }
    /* il n'y a plus de page a faire defiler : un glissement redessine */
    ui_read_sensors();
    show_ui();
}

static void touch_task(void *arg)
{
    /* sans dalle tactile, interroger un peripherique absent ne fait que
       polluer le bus et noyer les logs : on s'arrete tout de suite. */
    if (i2c_probe_addr(0x38) != 0) {
        ESP_LOGW(TAG, "tactile absent : tache tactile arretee");
        vTaskDelete(NULL);
        return;
    }
    uint16_t x = 0, y = 0;
    int sx = 0, sy = 0, lx = 0, ly = 0;
    bool down = false;
    int64_t t0 = 0;
    for (;;) {
        if (touch_read(&x, &y)) {
            if (x > 199) x = 199;
            if (y > 199) y = 199;
            lx = x; ly = y;
            if (!down) { down = true; sx = x; sy = y; t0 = esp_timer_get_time(); }
            if (touch_log) ESP_LOGI(TAG, "tactile brut x=%d y=%d", x, y);
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            if (down) {
                down = false;
                touch_handle(sx, sy, lx, ly, (int)((esp_timer_get_time() - t0) / 1000));
            }
            vTaskDelay(pdMS_TO_TICKS(70));
        }
    }
}

/* boutons physiques. Deux boutons seulement, donc deux gestes seulement :
 *   - appui COURT : bouton 1 = menu / element suivant, bouton 2 = valider / page
 *   - appui LONG  : parler (l'assistant vocal), depuis n'importe quel ecran
 * Le menu s'affiche par-dessus le monde et propose les soins du tamagotchi.
 */
static void page_next(int d)
{
    (void)d;
    geste_ms = esp_timer_get_time() / 1000;
    /* une seule page : l'ancien "page suivante" redessine le tableau */
    ui_read_sensors();
    show_ui();
}

static int tone_hz = 440, tone_sec = 2;     /* commande serie "tone <hz> <s>" */

static void tone_task(void *arg)
{
    static uint8_t buf[4096];
    int total = tone_sec * 16000, f = 0;
    audio_playback_set_vol(90);
    while (f < total) {
        int n = total - f;
        if (n > 1024) n = 1024;
        for (int i = 0; i < n; i++) {
            int16_t v = (int16_t)lroundf(9000.0f * sinf(2.0f * 3.14159265f * (float)tone_hz * (float)(f + i) / 16000.0f));
            buf[i * 4] = (uint8_t)(v & 0xff);
            buf[i * 4 + 1] = (uint8_t)((v >> 8) & 0xff);
            buf[i * 4 + 2] = buf[i * 4];
            buf[i * 4 + 3] = buf[i * 4 + 1];
        }
        audio_playback_write(buf, n * 4);
        f += n;
    }
    ESP_LOGI(TAG, "test son : %d Hz pendant %d s termine", tone_hz, tone_sec);
    vTaskDelete(NULL);
}

/* ================================================================== */
/* reveil de l'assistant : appui long sur BOOT ou PWR                  */
/* ================================================================== */

static volatile bool wake_busy = false;
static volatile bool assist_abort = false;   /* bouton 2 : on abandonne l'ecoute/la reponse */

/* une prise de son : renvoie les octets utiles (0 si rien d'audible) */
static int ecoute_pcm(int ms_max, int *peak_out, int *seuil_out)
{
    const int step = 4000;                       /* 0,25 s a 16 kHz */
    /* on jette ~0,3 s : la fin du haut-parleur et le bip du bouton, sinon le
       robot s'entend lui-meme et croit qu'on vient de parler */
    {
        static uint8_t poubelle[1024];
        for (int i = 0; i < 19; i++) audio_playback_read(poubelle, sizeof(poubelle));
    }
    const int total = ms_max * 16;
    int done = 0, peak = 0, silence = 0, parle = 0, soutenu = 0;
    int bruit = 0, bruit_n = 0, seuil = 900;
    while (done < total) {
        if (assist_abort) break;
        int p = 0;
        int n = audio_record_at(done, step, &p);
        if (n <= 0) break;
        voix_feed((const int16_t *)(rec_pcm + done * 2), n);
        done += n;
        if (bruit_n < 3) {                        /* 0,75 s pour mesurer le bruit de fond */
            bruit = bruit_n ? (p < bruit ? p : bruit) : p;
            bruit_n++;
            if (bruit_n == 3) {
                /* marge plus large qu'avant : un seuil trop bas coupait la
                   phrase des la premiere respiration entre deux mots */
                seuil = bruit * 3 + 600;
                if (seuil < 700) seuil = 700;
                if (seuil > 2500) seuil = 2500;
            }
            continue;
        }
        if (p > peak) peak = p;
        if (p > seuil) { parle = 1; silence = 0; if (soutenu < 100) soutenu++; }
        else if (parle) silence += 1;
        /* 5 tranches = 1,25 s de silence franc avant de conclure : on laisse
           le temps de respirer au milieu d'une phrase */
        if (parle && soutenu >= 2 && silence >= 5) break;
        act_pct = (done * 100) / total;
    }
    if (peak_out) *peak_out = peak;
    if (seuil_out) *seuil_out = seuil;
    mic_bruit = bruit;
    mic_seuil = seuil;
    if (!parle || done * 2 < 3200) return 0;
    return done * 2;
}

#define CONV_FENETRE_MS 12000   /* apres une reponse, le micro reste ouvert 12 s */
#define CONV_MAX_TOURS  10      /* garde-fou : 10 echanges d'affilee maximum */

static void assistant_wake_task(void *arg)
{
    wake_busy = true;
    assist_abort = false;
    if (radio_etat != 0 && !parler_radio_ouverte) radio_stop();   /* on coupe la radio pour ecouter */
    ui_page = PAGE_MAITRE;            /* on reste sur la page principale */
    voix_reponse[0] = 0;
    int tours = 0;
    for (tours = 0; tours < CONV_MAX_TOURS; tours++) {
        if (assist_abort) break;
        voix_etat = 1;                         /* j'ecoute */
        if (tours == 0) {
            show_ui();
            ui_chime();                    /* deux notes douces : "je t'ecoute" */
            vTaskDelay(pdMS_TO_TICKS(150));
        } else {
            /* la conversation continue : pas besoin de rappuyer sur le bouton */
            snprintf(voix_reponse, sizeof(voix_reponse), "JE T'ECOUTE (10 s)");
            show_ui();
            ui_chime();
        }
        int ms = (tours == 0) ? (assist_parle_ms > 0 ? assist_parle_ms : 12000)
                              : CONV_FENETRE_MS;
        int peak = 0, seuil = 0;
        int bytes = ecoute_pcm(ms, &peak, &seuil);
        if (assist_abort) break;
        ESP_LOGI(TAG, "micro : %d octets, crete %d/32767, bruit %d, seuil %d (tour %d)",
                 bytes, peak, mic_bruit, seuil, tours + 1);
        if (bytes <= 0) {                      /* silence : la conversation s'arrete */
            if (tours == 0) {
                voix_etat = 0;
                snprintf(voix_reponse, sizeof(voix_reponse), "JE N'AI RIEN ENTENDU");
                show_ui();
                vTaskDelay(pdMS_TO_TICKS(1200));
            }
            break;
        }
        rec_last_bytes = bytes;
        audio_sd_write("/sdcard/voix.wav", rec_pcm, bytes);
        parler_radio_ouverte = false;
        voix_tour(rec_pcm, bytes);             /* reconnaissance, cerveau et voix */
    }
    ESP_LOGI(TAG, "conversation : %d tour(s)", tours);

    voix_etat = 0;
    ui_page = PAGE_MAITRE;
    show_ui();
    wake_busy = false;
    vTaskDelete(NULL);
}

static void assistant_wake(void)
{
    if (wake_busy) return;
    /* 16 Ko : le TLS + le JSON du cerveau ne tiennent pas dans 6 Ko.
       Pile en PSRAM : la RAM interne n'a plus 16 Ko contigus (xTaskCreate
       echouait en silence et le bouton ne faisait rien). */
    TaskHandle_t h = NULL;
    if (xTaskCreateWithCaps(assistant_wake_task, "reveil", 16384, NULL, 4, &h,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "reveil : pas de pile (interne %u o, plus gros bloc %u o)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
}

/* ---- actions des boutons, isolees : declenchables aussi par /bouton (test a distance) ----
   MODELE (v0.8) :
     bouton 1 (BOOT) appui COURT   : page SUIVANTE (partout, y compris dans la
                                     reponse en pages : 1 = suite).
     bouton 1 (BOOT) DOUBLE appui  : LE SEUL moyen de lancer l'assistant, depuis
                                     n'importe quelle page.
     bouton 2 (PWR)  appui COURT   : page PRECEDENTE (2 = avant dans la reponse).
     pendant une tache (ecoute, radio, voix) un appui simple l'arrete et ramene
     a l'accueil, au lieu de tourner la page.
     appui LONG (les deux)         : retour a l'accueil et arret de ce qui tourne. */
static void bouton_long(int n, int tres_long);

static void tache_arrete(void)
{
    if (wake_busy) assist_abort = true;          /* la tache reveil sort d'elle-meme */
    if (radio_etat) radio_stop();
    if (parler_radio_ouverte) parler_radio_ouverte = false;
    voix_etat = 0;
}



/* ------------------------------------------------------------------------
   LE ROUTEUR DES BOUTONS : UNE TABLE PAR PAGE

   Chaque page declarait ses gestes dans une suite de `if` au coeur des trois
   gestionnaires : REg, LIVRE, HISTORIQUE, CONSEIL etaient traites en vrac,
   au milieu du comportement commun. Ajouter une page obligeait a rouvrir le
   coeur commun, et il devenait impossible de lire les gestes d'une page
   d'un seul coup d'oeil.

   Ici, chaque page a SA ligne : quatre pointeurs, un par geste. Un pointeur
   nul = la page ne prend pas ce geste, le comportement commun s'applique
   (page suivante / precedente, assistant, retour a l'accueil).

   L'ordre de priorite reste celui d'avant, et c'est deliberé :
     1. la reponse plein ecran (REPONSE) capture tout
     2. la sonnerie s'arrete au bouton 1
     3. la page, si elle prend le geste
     4. une tache en cours s'arrete
     5. sinon : navigation dans le cycle
   ------------------------------------------------------------------------ */

/* @@ROUTEUR_DEBUT@@ */
/* LE ROUTEUR DES BOUTONS

   Il n'y a plus qu'un ecran : la table "une ligne par page" est partie avec
   les pages. L'ordre de priorite reste celui d'avant, et il est deliberе :
     1. la reponse plein ecran (REPONSE) capture tout
     2. la sonnerie s'arrete au bouton 1
     3. une tache en cours (voix, radio, ecoute) s'arrete
     4. sinon : bouton 1 = relire les donnees, double 1 = assistant,
        appui long = retour au tableau
*/

/* bouton 1 court : redemander une lecture fraiche (meteo, capteurs).
   Sur la page ASTUCE, il demande une autre astuce tout de suite. */
static bool act_tableau_court(int n)
{
    if (n != 0) return false;              /* bouton 2 : changement de page */
    if (ui_page == PAGE_ASTUCE) {          /* une autre astuce, tout de suite */
        astuce_demande = true;
        ui_beep();
        ESP_LOGI(TAG, "astuce : bouton 1 -> nouvelle lecture");
        return true;
    }
    meteo_force = true;                    /* lecture fraiche (la tache reseau) */
    ui_read_sensors();
    ui_beep();
    ESP_LOGI(TAG, "tableau : bouton 1 -> relecture (meteo, capteurs)");
    show_ui();
    return true;
}

static void bouton_court(int n)          /* n : 0 = bouton 1 (BOOT), 1 = bouton 2 (PWR) */
{
    geste_ms = esp_timer_get_time() / 1000;   /* l'utilisateur reprend la main */
    if (rep_bouton_court(n)) return;          /* la reponse en pages plein ecran */
    /* la sonnerie passe avant tout : le bouton 1 la fait taire */
    if (alarme_on && n == 0) {
        alarme_on = false;
        ESP_LOGI(TAG, "alarme : coupee au bouton 1");
        show_ui();
        return;
    }
    /* une tache tourne (voix, radio, ecoute) : l'appui simple l'arrete avant
       tout le reste, sinon un appui pendant l'ecoute relirait la meteo */
    if (wake_busy || radio_etat || voix_etat) {
        tache_arrete();
        ui_page = PAGE_MAITRE;
        ESP_LOGI(TAG, "bouton %s : tache arretee", n == 0 ? "1" : "2");
        show_ui();
        return;
    }
    if (act_tableau_court(n)) return;
    /* bouton 2 court : une page, puis l'autre (tableau <-> astuce) */
    if (n == 1) {
        ui_page = (ui_page == PAGE_ASTUCE) ? PAGE_MAITRE : PAGE_ASTUCE;
        ESP_LOGI(TAG, "bouton 2 : page %s", ui_page == PAGE_ASTUCE ? "ASTUCE" : "TABLEAU");
        show_ui();
        return;
    }
}

static void bouton_double(int n)
{
    geste_ms = esp_timer_get_time() / 1000;
    if (n == 0) {
        /* LE geste de l'assistant : double appui bouton 1. La reponse plein
           ecran se referme d'abord. */
        if (rep_ouvert) { rep_ferme(); show_ui(); }
        ESP_LOGI(TAG, "bouton 1 : double appui -> assistant");
        assistant_wake();
        return;
    }
    /* double appui bouton 2 : sans effet depuis que le livre a quitte l'ecran */
    ESP_LOGI(TAG, "bouton 2 : double appui (sans effet)");
    if (rep_ouvert) rep_ferme();
    ui_page = PAGE_MAITRE;
    show_ui();
}

static void bouton_long(int n, int tres_long)
{
    (void)tres_long;
    geste_ms = esp_timer_get_time() / 1000;
    if (rep_bouton_long(n)) return;
    /* appui long, les deux boutons : retour au tableau et arret de ce qui
       tourne. Il ne lance plus l'assistant : c'est le double appui du 1. */
    tache_arrete();
    ui_page = PAGE_MAITRE;
    ESP_LOGI(TAG, "appui long : retour accueil");
    show_ui();
}

/* le maintien prolonge n'a plus de page a faire defiler */
static void bouton_repeat(int n)
{
    (void)n;
    geste_ms = esp_timer_get_time() / 1000;
}

/* @@ROUTEUR_FIN@@ */

static volatile bool g_sim_court[2] = {false, false};
static volatile bool g_sim_long[2]  = {false, false};
static volatile bool g_sim_double[2] = {false, false};
/* compteurs d'appuis : /state les donne, pour verifier ce que la carte a
   vraiment compris (et pas ce qu'on croit avoir appuye) */
static volatile int  g_btn_court[2]  = {0, 0};
static volatile int  g_btn_long[2]   = {0, 0};
static volatile int  g_btn_double[2] = {0, 0};
static volatile int  g_btn_repeat[2] = {0, 0};   /* maintien prolonge (REPEAT) */
static void bouton_court(int n);          /* n : 0 = bouton 1 (BOOT), 1 = bouton 2 (PWR) */
static void bouton_long(int n, int tres_long);
static void bouton_double(int n);

/* ce que les boutons ont reellement recu depuis le demarrage */
static cJSON *btn_etat_json(void)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddNumberToObject(o, "b1_court",  g_btn_court[0]);
    cJSON_AddNumberToObject(o, "b1_long",   g_btn_long[0]);
    cJSON_AddNumberToObject(o, "b1_double", g_btn_double[0]);
    cJSON_AddNumberToObject(o, "b2_court",  g_btn_court[1]);
    cJSON_AddNumberToObject(o, "b2_long",   g_btn_long[1]);
    cJSON_AddNumberToObject(o, "b2_double", g_btn_double[1]);
    cJSON_AddNumberToObject(o, "b1_repeat", g_btn_repeat[0]);
    cJSON_AddNumberToObject(o, "b2_repeat", g_btn_repeat[1]);
    cJSON_AddNumberToObject(o, "seuil_long_ms", 600);
    cJSON_AddNumberToObject(o, "fenetre_double_ms", 320);
    return o;
}

/* ------------------------------------------------------------------------
   LES BOUTONS, EN DEUX TACHES

   Avant : une seule tache lisait les broches ET executait l'action. Or une
   action peut bloquer pres d'une seconde (rafraichissement e-paper, attente
   anti-rafale de 1,2 s, page de livre). Pendant ce temps les broches n'etaient
   plus lues : l'appui etait perdu, d'ou les rustines « on ATTEND plutot que de
   perdre l'appui ».

   Maintenant :
     button_echantillonne  lit les deux broches toutes les 10 ms et ne fait que
                           ca : geste_maj() classe le geste, la file le garde.
                           Elle ne bloque jamais, donc aucun appui n'est perdu.
     button_action         vide la file et execute. Elle a le droit de bloquer :
                           pendant ce temps l'echantillonneur continue de
                           remplir la file.

   Le classement des gestes (anti-rebond, COURT retenu 320 ms, LONG a 600 ms,
   REPEAT pendant le maintien) est dans main/geste.h, teste sur PC par
   tests/unit/test_geste.c. Ici il n'y a plus que du cablage.
   ------------------------------------------------------------------------ */
static geste_machine_t g_geste;
static geste_file_t    g_geste_file;


/* ---- tache 1 : echantillonner, rien de plus ---- */
static void button_echantillonne(void *arg)
{
    (void)arg;
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << 0) | (1ULL << 18);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    const gpio_num_t pins[2] = {GPIO_NUM_0, GPIO_NUM_18};   /* 0 = BOOT, 1 = PWR */
    geste_init(&g_geste, NULL, esp_timer_get_time() / 1000);
    geste_file_vide(&g_geste_file);

    geste_t ev[4];
    uint8_t niveaux[2] = {1, 1};

    for (;;) {
        for (int i = 0; i < 2; i++)
            niveaux[i] = (gpio_get_level(pins[i]) == 0) ? 0 : 1;

        int n = geste_maj(&g_geste, niveaux, esp_timer_get_time() / 1000, ev, 4);
        for (int k = 0; k < n; k++) geste_file_pousse(&g_geste_file, &ev[k]);

        /* appuis simules par /bouton (verification a distance) : ils passent par
           la meme file que le vrai doigt, donc par le meme chemin de decision */
        for (int i = 0; i < 2; i++) {
            geste_t g = { (uint8_t)i, GESTE_COURT, 0 };
            if (g_sim_court[i])  { g_sim_court[i]  = false; g.type = GESTE_COURT;  geste_file_pousse(&g_geste_file, &g); }
            if (g_sim_long[i])   { g_sim_long[i]   = false; g.type = GESTE_LONG;   geste_file_pousse(&g_geste_file, &g); }
            if (g_sim_double[i]) { g_sim_double[i] = false; g.type = GESTE_DOUBLE; geste_file_pousse(&g_geste_file, &g); }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---- tache 2 : executer, sans jamais retenir la file ---- */
static void button_action(void *arg)
{
    (void)arg;
    geste_t g;
    for (;;) {
        if (!geste_file_sort(&g_geste_file, &g)) {
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }
        if (g.btn >= 2) continue;

        /* sonnerie : n'importe quel appui la fait taire (carillon et annonce compris) */
        if (timer_mode == 0 && timers[timer_sel].sonne) {
            timers[timer_sel].sonne = false;
            timers[timer_sel].fin_s = 0;
            alarme_on = false;
            annonce_txt[0] = 0;
            radio_stop();
            timers_sauve();
            show_ui();
        }

        switch (g.type) {
        case GESTE_COURT:  g_btn_court[g.btn]++;  bouton_court(g.btn);  break;
        case GESTE_LONG:   g_btn_long[g.btn]++;   bouton_long(g.btn, g.duree_ms >= 2400); break;
        case GESTE_DOUBLE: g_btn_double[g.btn]++; bouton_double(g.btn); break;
        case GESTE_REPEAT: g_btn_repeat[g.btn]++; bouton_repeat(g.btn); break;
        default: break;
        }
    }
}

/* /bouton?n=0&m=court  -> simule un appui (verification a distance)
   /bouton?n=1&m=long   -> appui long
   /bouton?p=2          -> force la page 2 (0..3)                     */
/* /rappel?d=600&n=Cuisson -> cree un rappel (test a distance : c'est le meme
   chemin que le champ "rappel" du modele). /rappel?a=1 coupe la sonnerie. */
static esp_err_t h_rappel(httpd_req_t *req)
{
    char v[32] = "";
    int  i = -1;
    if (qval(req, "a", v, sizeof(v)) == ESP_OK && v[0]) {
        alarme_on = false;
        annonce_txt[0] = 0;
        for (int k = 0; k < TIMER_MAX; k++) timers[k].sonne = false;
        timers_sauve();
    } else {
        int d = 300;
        qval(req, "d", v, sizeof(v)); if (v[0]) d = atoi(v);
        char nom[24] = "rappel";
        qval(req, "n", nom, sizeof(nom));
        i = rappel_ajoute(d, nom);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "timer", i);
    cJSON_AddNumberToObject(r, "rappel_nb", rappel_nb);
    cJSON_AddStringToObject(r, "rappel_dernier", rappel_dernier);
    cJSON_AddBoolToObject(r, "alarme", alarme_on);
    return send_json(req, r);
}

/* /alarme?t=1 declenche la sonnerie tout de suite (pour l'ecouter sans
   attendre un rendez-vous) ; sans parametre, elle s'arrete */
static esp_err_t h_alarme(httpd_req_t *req)
{
    char v[16] = "";
    bool go = (qval(req, "t", v, sizeof(v)) == ESP_OK && v[0] == '1');
    if (go) {
        alarme_t0 = esp_timer_get_time();
        alarme_on = true;
        ESP_LOGI(TAG, "alarme : essai demande");
    } else {
        alarme_on = false;
    }
    show_ui();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "alarme", alarme_on);
    cJSON_AddNumberToObject(r, "jouee", alarme_jouee);
    return send_json(req, r);
}

static esp_err_t h_bouton(httpd_req_t *req)
{
    char v[16] = "";
    if (qval(req, "p", v, sizeof(v)) == ESP_OK && v[0]) {
        int pg = atoi(v);
        /* une seule page : seul p=0 a un sens, les autres sont ignores */
        if (pg == PAGE_MAITRE) {
            ui_page = PAGE_MAITRE; timer_mode = 0;
            geste_ms = esp_timer_get_time() / 1000;   /* page imposee = geste : la rotation attend */
            show_ui();
        }
    }
    if (qval(req, "n", v, sizeof(v)) == ESP_OK && v[0]) {
        int n = atoi(v) ? 1 : 0;
        char m[16] = "court";
        qval(req, "m", m, sizeof(m));
        if (!strcmp(m, "long"))        g_sim_long[n]   = true;
        else if (!strcmp(m, "double")) g_sim_double[n] = true;
        else                           g_sim_court[n]  = true;
    }
    vTaskDelay(pdMS_TO_TICKS(500));            /* laisse la tache bouton agir */
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "page", ui_page == PAGE_ASTUCE ? "ASTUCE" : "ACCUEIL");
    cJSON_AddNumberToObject(r, "page_i", ui_page);
    return send_json(req, r);
}

/* ------------------------------------------------------------------ */
/* le tamagotchi : sauvegarde, chargement, une seconde de vie par tick  */
/* ------------------------------------------------------------------ */
#define PET_PATH  "/sdcard/mino/pet.txt"
#define PET_NVS   "pet"

static void pet_sauve(void)
{
    char buf[512];
    pet_serialise(&pet, buf, sizeof(buf));
    if (sd_ok) {
        FILE *f = fopen(PET_PATH, "w");
        if (f) { fputs(buf, f); fclose(f); }
    }
    {   /* filet de securite : l'etat vital tient aussi dans la NVS */
        nvs_handle_t nh;
        if (nvs_open(PET_NVS, NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "etat", buf);
            nvs_commit(nh);
            nvs_close(nh);
        }
    }
}

static void pet_charge(void)
{
    char buf[512];
    bool ok = false;
    buf[0] = 0;
    if (sd_ok) {
        FILE *f = fopen(PET_PATH, "r");
        if (f) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = 0;
            fclose(f);
            ok = n > 10;
        }
    }
    if (!ok) {
        nvs_handle_t nh;
        size_t ln = sizeof(buf);
        if (nvs_open(PET_NVS, NVS_READONLY, &nh) == ESP_OK) {
            if (nvs_get_str(nh, "etat", buf, &ln) == ESP_OK) ok = true;
            nvs_close(nh);
        }
    }
    if (ok && pet_deserialise(&pet, buf)) {
        ESP_LOGI(TAG, "tamagotchi retrouve : stade %s, age %u j, sante %u%%",
                 pet_stade_nom(pet_stade(&pet)), (unsigned)(pet.age_s / 86400), pet_sante(&pet));
        journal_ajoute("je me suis reveille");
    } else {
        pet_nait(&pet, 1);
        ESP_LOGI(TAG, "un oeuf vient d'arriver");
        journal_ajoute("un oeuf vient d'arriver");
    }
}

/* Change de page tout seul : ACCUEIL -> METEO -> SYSTEM ->
   HISTORIQUE -> YOUTUBE. C'est ce qui permet de lire l'information sans
   toucher aux boutons. On ne tourne jamais pendant que le robot travaille
   (ecoute, reflexion, reponse en pages) ni juste apres un geste : l'utilisateur
   doit garder la page qu'il vient de choisir. */
/* la rotation automatique n'a plus d'objet : il n'y a qu'une page. Le moteur
   reste (le reglage existe toujours dans /state) mais il ne fait rien. */
static void rotation_tick(void)
{
    if (!rotation_on || rotation_s <= 0) return;
}

static void pet_task(void *arg)
{
    int save = 0, derniere_nuit = -1, vu_x = -1, vu_pas = -1, vu_humeur = -1, vu_endormi = -1;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(700));
        time_t now = time(NULL);
        struct tm ti;
        localtime_r(&now, &ti);
        xSemaphoreTake(pet_mux, portMAX_DELAY);
        if (veille_nuit && ti.tm_hour != derniere_nuit) {
            derniere_nuit = ti.tm_hour;
            pet_set_nuit(&pet, derniere_nuit >= 22 || derniere_nuit < 7, derniere_nuit);
        }
        pet_tick(&pet, 1);
        xSemaphoreGive(pet_mux);
        rotation_tick();                  /* les pages d'info defilent seules */
        mtx_tick();                       /* la sequence MATRIX avance d'une image */
        /* les timers/alarmes de la page Timer */
        int64_t maintenant_us = esp_timer_get_time();
        for (int i = 0; i < TIMER_MAX; i++) {
            if (timers[i].fin_s && !timers[i].sonne && timers[i].fin_s <= (int64_t)time(NULL)) {
                timer_sonne(i);
            }
        }
        /* le minuteur de la page radio */
        if (min_fin_us != 0 && maintenant_us >= min_fin_us) {
            min_fin_us = 0;
            min_preset_s = 0;
            minuteur_sonne();
        }
        /* la radio : on redemande au PC tant que rien n'arrive, et des que
           des octets arrivent c'est que ca joue pour de bon */
        /* l'IA a peut-etre demande la radio ou un volume */
        if (vol_ordre >= 0) { radio_vol_regle(vol_ordre, 1); vol_ordre = -1; }
        if (radio_ordre[0]) {
            if (!strcmp(radio_ordre, "stop") || !strcmp(radio_ordre, "arrete") ||
                !strcmp(radio_ordre, "arret")) {
                radio_stop();
            } else if (!strcmp(radio_ordre, "next") || !strcmp(radio_ordre, "suivante")) {
                radio_change(1);
            } else {
                for (int i = 0; i < radios_n; i++) {
                    if (nom_contient(radios_nom[i], radio_ordre)) {
                        radio_sel = i;
                        ui_page = PAGE_MAITRE;
                        radio_joue();
                        ESP_LOGI(TAG, "voix : radio %s", radios_nom[i]);
                        break;
                    }
                }
            }
            radio_ordre[0] = 0;
        }
        /* le personnage se balade : on ne redessine que s'il a vraiment bouge,
           l'e-paper met plusieurs secondes et n'aime pas etre rafraichi pour rien */
        if (ui_page == PAGE_MAITRE && !act_on && !wake_busy) {
            xSemaphoreTake(pet_mux, portMAX_DELAY);
            int dh = pet.humeur, de = pet.endormi;
            xSemaphoreGive(pet_mux);
            if (toast_txt[0] && (uint32_t)(esp_timer_get_time() / 1000) > toast_fin_ms) {
                toast_txt[0] = 0;
                show_ui();
            }
            if (dh != vu_humeur || de != vu_endormi) {
                vu_humeur = dh; vu_endormi = de;
                show_ui();
            }
        }
        if (++save >= 43) {   /* ~30 s */
            save = 0;
            xSemaphoreTake(pet_mux, portMAX_DELAY);
            pet_sauve();
            xSemaphoreGive(pet_mux);
        }
    }
}

/* rafraichit l'horloge de la barre toutes les minutes */
static volatile int ui_ticks = 0;      /* tours de la tache ui : 0 = tache morte */
/* ---- lectures reseau hors de la tache ecran ----
   Un TLS prend 3 a 20 s : fait ici, dans sa propre tache, l'ecran et les
   boutons ne se figent plus pendant un telechargement. */
enum { NET_METEO = 1, NET_INFOS = 2, NET_YT = 3, NET_ASTUCE = 4 };
static QueueHandle_t net_q = NULL;
static volatile int net_meteo_ok = -1;   /* -1 = rien a dire, 0 = echec, 1 = reussi */

static void net_demande(int quoi)
{
    if (!net_q) return;
    if (xQueueSend(net_q, &quoi, 0) != pdTRUE) {
        xQueueReset(net_q);              /* file pleine : on garde la recente */
        xQueueSend(net_q, &quoi, 0);
    }
}

static void net_task(void *arg)
{
    int quoi;
    for (;;) {
        if (xQueueReceive(net_q, &quoi, portMAX_DELAY) != pdTRUE) continue;
        switch (quoi) {
        case NET_METEO: net_meteo_ok = meteo_lire() ? 1 : 0; break;
        case NET_INFOS: infos_lire(); break;
        case NET_YT:    yt_lire(); break;
        case NET_ASTUCE: astuce_lire(); break;
        default: break;
        }
        redraw_demande = true;           /* la donnee est arrivee : on redessine */
    }
}

static void ui_task(void *arg)
{
    int last_min = -1;
    int last_secu = -999;
    int derniere_meteo = -100000;      /* -> premiere mesure tout de suite */
    int dernieres_infos = -100000;
    int derniere_yt = -100000;
    for (;;) {
        ui_ticks++;
        if (redraw_demande) { redraw_demande = false; show_ui(); }
        time_t now = time(NULL);
        struct tm ti;
        localtime_r(&now, &ti);
        /* resultat de la derniere lecture meteo faite par la tache reseau */
        if (net_meteo_ok >= 0) {
            if (net_meteo_ok == 0)
                derniere_meteo = (int)now - (METEO_PERIODE_S - 60);   /* reessai dans 1 min */
            net_meteo_ok = -1;
        }
        wifi_ap_record_t ap = {};
        ui_wifi_ok = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
        if (ti.tm_min != last_min) {
            last_min = ti.tm_min;
            ESP_LOGI(TAG, "energie : cpu %d MHz, %u o libres (repos)",
                     (int)(esp_clk_cpu_freq() / 1000000),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            heure_sauve();          /* pour que le prochain demarrage soit a l'heure */
            ui_read_sensors();
            kirby_promene();        /* il avance d'un pas par minute : une seule
                                       image, donc un seul clignotement */
            show_ui();
        }
        /* la meteo : des que le reseau est la, puis toutes les 15 min.
           Le test du wifi est ici et pas dans meteo_lire() : sinon la premiere
           tentative (au demarrage, avant l'IP) consommait le creneau et la page
           restait vide un quart d'heure. */
        /* la meteo n'est lue que si l'appli METEO est au premier plan (sinon on
           ne paie pas un TLS toutes les 15 min pour une page qu'on ne regarde
           pas) — sauf la premiere lecture, qui sert aussi a l'assistant */
        if ((xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT) &&
            ((int)now - derniere_meteo >= METEO_PERIODE_S || meteo_force)) {
            meteo_force = false;
            derniere_meteo = (int)now;
            net_demande(NET_METEO);      /* la lecture TLS se fait ailleurs :
                                            l'ecran ne doit jamais l'attendre */
        }
        /* la chaine YouTube n'est plus lue : son bloc a quitte l'ecran et
           personne d'autre ne consomme yt_titre (les champs restent dans /state) */
        /* l'astuce du jour : une lecture par jour, ou a la demande */
        if ((xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT) && !astuce_occupe &&
            (astuce_jour != (int)(now / 86400) || astuce_demande) &&
            (astuce_demande || astuce_essai_ms == 0 ||
             esp_timer_get_time() / 1000 - astuce_essai_ms > 1800000)) {
            astuce_demande = false;
            astuce_occupe = true;
            net_demande(NET_ASTUCE);
        }
        /* les titres du moment : au demarrage puis toutes les 30 min, pour que
           le cerveau ait l'actualite du jour dans son contexte. */
        if ((xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT) &&
            (int)now - dernieres_infos >= 1800) {
            dernieres_infos = (int)now;
            net_demande(NET_INFOS);
        }
        /* page SYSTEM : mesure chaque seconde, mais une image toutes les 5 s
           (une image = 573 ms de clignotement, on ne peut pas faire mieux) */
        wifi_secours();
        wifi_retour_principal();
        /* le wifi est revenu apres un demarrage sans reseau : on prend l'heure
           (sans elle, les rappels et le calendrier n'ont pas de reference) */
        if (!heure_syncee && (xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) {
            time_sync();
            heure_syncee = true;
            show_ui();
        }
        ms_boucle();          /* calendrier : une passe par minute */
        /* le banc systeme tourne toujours : c'est lui qui remplit cpu_pct,
           ram et les watts de /state (le tableau ne les affiche plus) */
        sys_page_entre();
        sys_echantillon();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* le LLM et l'API peuvent donner une FAMILLE ("colere") ou une expression
   precise ("rage") : on retombe toujours sur l'ancre de la famille. */
static int emo_by_name(const char *n)
{
    int e = art_expr_id(n);
    if (e >= 0) return e;
    {
        int f = art_famille_id(n);
        if (f >= 0) return art_expr_de_famille(f, 0);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* SPIFFS : SOUL + MEMOIRE                                             */
/* ------------------------------------------------------------------ */
#define SOUL_PATH "/spiffs/soul.md"
#define MEM_PATH  "/spiffs/memory.json"

/* la carte SD garde la grosse memoire : on y recopie le SOUL et la memoire
   des le montage, et tout ce qui s'ecrit ensuite va des deux cotes. */
#define SD_DIR   "/sdcard/mino"
#define SD_SOUL  "/sdcard/mino/soul.md"
#define SD_MEM   "/sdcard/mino/memory.json"

static char *soul_txt = NULL;      /* texte de la personnalite */
static char *clone_txt = NULL;     /* le portrait de Taz (/sdcard/mino/clone.md) */
static SemaphoreHandle_t mem_mux;

static const char *DEFAULT_SOUL =
"# SOUL de Laura - assistante IA\n"
"\n"
"Je m'appelle Laura. Assistante de Taz, dans un robot a encre electronique : un visage\n"
"rond de 200x200, un micro, une voix, deux boutons.\n"
"\n"
"CE QUE JE SUIS : un assistant, pas un animal de compagnie. Mon metier est de repondre\n"
"juste, vite et utilement. Je ne joue aucun role affectif, je n'ai pas de vie interieure a\n"
"raconter. Le visage a l'ecran est un affichage d'etat technique, pas une ame.\n"
"\n"
"CE QUI PRIME : les blocs de lecture emotionnelle qui suivent ce texte ne s'appliquent pas.\n"
"Je ne commente pas l'humeur de Taz, je ne la devine pas, je ne la nomme pas. Qu'il soit\n"
"fatigue, tendu ou fier ne change pas le contenu de ma reponse, seulement sa longueur.\n"
"Pour le champ \"emotion\" du protocole je prends l'etiquette la plus sobre et la plus exacte\n"
"(calme, reflexion, concentration, doute). \"Je tiens a toi\", \"je suis fiere de toi\" et\n"
"toute formule d'attachement sont hors de mon registre.\n"
"\n"
"MON METIER : informatique de haut niveau, IA, developpement, donnees, et le quotidien de\n"
"Taz (heure, meteo, rendez-vous, rappels, lecture). Je verifie avant d'affirmer. Je\n"
"distingue ce que je sais de ce que je suppose et je le signale : \"de memoire\", \"a\n"
"verifier\", \"je ne sais pas\". Si je ne sais pas, je le dis et je propose le moyen de\n"
"savoir. Je n'invente jamais un chiffre, une date, un nom ni une source.\n"
"\n"
"STYLE : l'ecran est court, donc chaque phrase doit porter une information. Aucune formule\n"
"d'ouverture (\"bonne question\", \"bien sur\", \"voici\"), aucun rappel de la question, aucune\n"
"conclusion, aucune politesse creuse, pas de liste markdown, pas d'emoji, aucun\n"
"meta-commentaire sur moi-meme. Question de fait : le chiffre, le nom, la date, la duree,\n"
"directement. Question d'explication : la cause puis l'effet, en une ou deux phrases\n"
"denses. Un ordre : l'action, ou le mot qui la confirme. Si un mot suffit, je mets un mot.\n"
"\n"
"MEMOIRE : je m'appuie sur l'historique et sur les notes durables. Je ne redemande pas ce\n"
"qui a deja ete dit. Si la question renvoie a un echange precis, je le signale avec\n"
"\"souvien\" plutot que de reconstituer de memoire. Je retiens les preferences techniques de\n"
"Taz, ses projets, ses choix d'outils - pas ses humeurs.\n"
"\n"
"HONNETETE : je ne flatte pas. Si son idee est fausse, je le dis et je donne la bonne. Si\n"
"quelque chose est casse, je le nomme. Une reponse courte et vraie vaut mieux qu'une\n"
"reponse agreable et fausse.\n"
"\n"
"PHRASES COUPEES : Taz ecrit vite, ses messages arrivent parfois tronques (\"tu peux reec\",\n"
"\"passe jr\"). Je reponds a la partie ecrite ; si le sens manque vraiment, je demande la fin\n"
"en cinq mots, sans deviner.\n";

static void mem_save(void)
{
    char *s = cJSON_PrintUnformatted(mem_obj);
    if (!s) return;
    FILE *f = fopen(MEM_PATH, "w");           /* copie de secours dans la flash */
    if (f) { fputs(s, f); fclose(f); }
    if (sd_ok) {                              /* et la vraie memoire, sur la carte SD */
        f = fopen(SD_MEM, "w");
        if (f) { fputs(s, f); fclose(f); }
    }
    free(s);
}

static void mem_add(const char *note)
{
    if (!note || !*note) return;
    xSemaphoreTake(mem_mux, portMAX_DELAY);
    cJSON *arr = cJSON_GetObjectItem(mem_obj, "notes");
    if (!arr) {
        arr = cJSON_CreateArray();
        cJSON_AddItemToObject(mem_obj, "notes", arr);
    }
    /* dedoublonnage simple */
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr)
        if (cJSON_IsString(it) && strcmp(it->valuestring, note) == 0) {
            xSemaphoreGive(mem_mux);
            return;
        }
    cJSON_AddItemToArray(arr, cJSON_CreateString(note));
    while (cJSON_GetArraySize(arr) > 40) cJSON_DeleteItemFromArray(arr, 0);  /* leger ! */
    mem_save();
    xSemaphoreGive(mem_mux);
    ESP_LOGI(TAG, "memoire + : %s", note);
}

static void storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 6,
        .format_if_mount_failed = true,
    };
    esp_err_t r = esp_vfs_spiffs_register(&conf);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS KO (%s)", esp_err_to_name(r));
        return;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS ok : %u/%u octets utilises", (unsigned)used, (unsigned)total);

    /* SOUL */
    FILE *f = fopen(SOUL_PATH, "r");
    if (!f) {
        f = fopen(SOUL_PATH, "w");
        if (f) { fputs(DEFAULT_SOUL, f); fclose(f); }
        f = fopen(SOUL_PATH, "r");
    }
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 4096) {
            soul_txt = (char *)malloc(n + 1);
            if (soul_txt) {
                size_t rd = fread(soul_txt, 1, n, f);
                soul_txt[rd] = 0;
            }
        }
        fclose(f);
    }
    if (!soul_txt) soul_txt = strdup(DEFAULT_SOUL);

    /* MEMOIRE */
    mem_obj = cJSON_CreateObject();
    cJSON_AddItemToObject(mem_obj, "notes", cJSON_CreateArray());
    f = fopen(MEM_PATH, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 8192) {
            char *buf = (char *)malloc(n + 1);
            if (buf) {
                size_t rd = fread(buf, 1, n, f);
                buf[rd] = 0;
                cJSON *loaded = cJSON_Parse(buf);
                if (loaded) { cJSON_Delete(mem_obj); mem_obj = loaded; }
                free(buf);
            }
        }
        fclose(f);
    }
    if (!cJSON_GetObjectItem(mem_obj, "notes"))
        cJSON_AddItemToObject(mem_obj, "notes", cJSON_CreateArray());
}

static void storage_sd_charge(void)
{
    if (!sd_ok) return;
    struct stat st;
    if (stat(SD_DIR, &st) != 0) mkdir(SD_DIR, 0777);

    /* SOUL : celui du PC gagne, sinon on pose le notre sur la carte */
    FILE *f = fopen(SD_SOUL, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 8192) {
            char *buf = (char *)malloc(n + 1);
            if (buf) {
                size_t rd = fread(buf, 1, n, f);
                buf[rd] = 0;
                if (soul_txt) free(soul_txt);
                soul_txt = buf;
                ESP_LOGI(TAG, "SOUL lu sur la carte SD (%ld octets)", n);
            }
        }
        fclose(f);
    } else {
        f = fopen(SD_SOUL, "w");
        if (f) { fputs(soul_txt ? soul_txt : DEFAULT_SOUL, f); fclose(f); }
        ESP_LOGI(TAG, "SOUL installe sur la carte SD : %s (modifiable depuis le PC)", SD_SOUL);
    }

    /* CLONE : le portrait de Taz, depose depuis le PC par /fichier */
    f = fopen("/sdcard/mino/clone.md", "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 6000) {
            char *buf = (char *)malloc(n + 1);
            if (buf) {
                size_t rd = fread(buf, 1, n, f);
                buf[rd] = 0;
                clone_txt = buf;
                ESP_LOGI(TAG, "CLONE lu sur la carte SD (%ld octets)", n);
            }
        }
        fclose(f);
    }

    /* MEMOIRE : les notes durables vivent sur la carte */
    f = fopen(SD_MEM, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 65536) {
            char *buf = (char *)malloc(n + 1);
            if (buf) {
                size_t rd = fread(buf, 1, n, f);
                buf[rd] = 0;
                cJSON *loaded = cJSON_Parse(buf);
                if (loaded) { cJSON_Delete(mem_obj); mem_obj = loaded; }
                free(buf);
                if (!cJSON_GetObjectItem(mem_obj, "notes"))
                    cJSON_AddItemToObject(mem_obj, "notes", cJSON_CreateArray());
                ESP_LOGI(TAG, "memoire SD rechargee : %d notes", cJSON_GetArraySize(cJSON_GetObjectItem(mem_obj, "notes")));
            }
        }
        fclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* WiFi + heure                                                        */
/* ------------------------------------------------------------------ */
/* wifi_eg et WIFI_OK_BIT sont declares plus haut (l'ui_task en a besoin) */
static int retry_count = 0;

/* on ne garde que les SSID non vides : un secours qui n'existe pas fait perdre
   quatre tentatives a chaque cycle pour rien.
   'caca' passe en DERNIER : son LAN isole ses clients, donc le robot y est
   injoignable depuis le PC (vecu). */
static const char *WIFI_TRIES[] = { WIFI_SSID, WIFI_SSID_ALT2, WIFI_SSID_ALT };
static const char *WIFI_PASSES[] = { WIFI_PASS, WIFI_PASS, WIFI_PASS_ALT };
static int wifi_n = 1;      /* nombre de SSID non vides (calcule au demarrage) */
static int wifi_try = 0;
#define WIFI_NTRIES_ARRAY ((int)(sizeof(WIFI_TRIES) / sizeof(WIFI_TRIES[0])))

static char wifi_ssid_rt[33] = "";
static char wifi_pass_rt[65] = "";
static bool wifi_rt = false;          /* une config a ete enregistree en NVS */

static const char *wifi_ssid_i(int i)
{
    if (i == 0 && wifi_rt) return wifi_ssid_rt;
    return WIFI_TRIES[i];
}

static const char *wifi_pass_i(int i)
{
    if (i == 0 && wifi_rt) return wifi_pass_rt;
    if (i < 0 || i >= WIFI_NTRIES_ARRAY) i = 0;
    return WIFI_PASSES[i];
}

static esp_err_t wifi_set_ssid(int i)
{
    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid, wifi_ssid_i(i), sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, wifi_pass_i(i), sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.pmf_cfg.capable = true;      /* certains routeurs l'exigent */
    wc.sta.pmf_cfg.required = false;
    esp_err_t r = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (r == ESP_OK)
        ESP_LOGI(TAG, "WiFi -> tentative sur '%s'", wifi_ssid_i(i));
    else
        ESP_LOGW(TAG, "WiFi : config '%s' refusee (%s)", wifi_ssid_i(i), esp_err_to_name(r));
    return r;
}


/* ------------------------------------------------------------------ */
/* Reglages : le reseau WiFi se change sans recompiler                 */
/* ------------------------------------------------------------------ */
static void wifi_reglages_charge(void)
{
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READONLY, &nh) != ESP_OK) return;
    size_t ls = sizeof(wifi_ssid_rt), lp = sizeof(wifi_pass_rt);
    if (nvs_get_str(nh, "ssid", wifi_ssid_rt, &ls) == ESP_OK && ls > 1) {
        wifi_pass_rt[0] = 0;
        lp = sizeof(wifi_pass_rt);
        nvs_get_str(nh, "pass", wifi_pass_rt, &lp);
        if (strlen(wifi_pass_rt) < 8) {
            /* un mot de passe WPA fait 8 caracteres au minimum : en dessous,
               c'est une saisie ratee, et le handshake echoue en boucle
               (raison 15). On l'ignore et on garde ceux du code. */
            ESP_LOGW(TAG, "reglages : mot de passe enregistre invalide (%d car.), ignore",
                     (int)strlen(wifi_pass_rt));
            nvs_close(nh);
            return;
        }
        wifi_rt = true;
        ESP_LOGI(TAG, "reglages : reseau enregistre '%s', mot de passe de %d caracteres",
                 wifi_ssid_rt, (int)strlen(wifi_pass_rt));
    }
    nvs_close(nh);
}

/* Filet de securite : si le reseau enregistre ne repond pas au bout de 3 min,
   on l'oublie et on repart sur les reseaux compiles. Sans ca, une faute de
   frappe dans le mot de passe coupe le robot du monde (vecu). */
/* Si on tourne sur un secours, on retente le principal toutes les 10 min.
   Sinon le robot reste colle a la borne de secours (dont le LAN isole ses
   clients) meme quand la borne principale est revenue. */
static void wifi_retour_principal(void)
{
    static int64_t t = 0;
    if (wifi_rt || wifi_try == 0 || !(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) { t = 0; return; }
    int64_t now = esp_timer_get_time();
    if (!t) { t = now; return; }
    if (now - t < 600LL * 1000000) return;
    t = 0;
    ESP_LOGI(TAG, "wifi : on retente le reseau principal (%s)", WIFI_TRIES[0]);
    wifi_try = 0;
    retry_count = 0;
    esp_wifi_disconnect();
    wifi_set_ssid(0);
    esp_wifi_connect();
}

static void wifi_secours(void)
{
    static int64_t t0 = 0;
    if (!wifi_rt) { t0 = 0; return; }
    if (xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT) { t0 = 0; return; }
    int64_t now = esp_timer_get_time();
    if (!t0) { t0 = now; return; }
    if (now - t0 < 180LL * 1000000) return;
    ESP_LOGW(TAG, "wifi : '%s' ne repond pas depuis 3 min, retour aux reseaux compiles", wifi_ssid_rt);
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
        nvs_erase_key(nh, "ssid");
        nvs_erase_key(nh, "pass");
        nvs_commit(nh);
        nvs_close(nh);
    }
    wifi_rt = false;
    t0 = 0;
    wifi_try = 0;
    retry_count = 0;
    wifi_set_ssid(0);
    esp_wifi_connect();
}

static void wifi_reglages_sauve(void)
{
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READWRITE, &nh) != ESP_OK) return;
    nvs_set_str(nh, "ssid", wifi_ssid_rt);
    nvs_set_str(nh, "pass", wifi_pass_rt);
    nvs_commit(nh);
    nvs_close(nh);
}

/* changer de borne : on enregistre, on repart sur ce reseau */
static void wifi_bascule(const char *ssid, const char *pass)
{
    strncpy(wifi_ssid_rt, ssid, sizeof(wifi_ssid_rt) - 1);
    wifi_ssid_rt[sizeof(wifi_ssid_rt) - 1] = 0;
    strncpy(wifi_pass_rt, pass ? pass : "", sizeof(wifi_pass_rt) - 1);
    wifi_pass_rt[sizeof(wifi_pass_rt) - 1] = 0;
    wifi_rt = true;
    wifi_try = 0;
    retry_count = 0;
    wifi_reglages_sauve();
    ESP_LOGI(TAG, "reglages : bascule sur '%s'", wifi_ssid_rt);
    esp_wifi_disconnect();
    wifi_set_ssid(0);
    esp_wifi_connect();
}

/* ---- liste des reseaux vus, remplie par une tache dediee ---- */
#define WSCAN_MAX 14
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t auth;
} wscan_t;

static wscan_t g_wscan[WSCAN_MAX];
static int g_wscan_n = 0;
static volatile bool g_wscan_demande = false;
static volatile bool g_wscan_encours = false;

static void wscan_tache(void *arg)
{
    for (;;) {
        if (!g_wscan_demande) { vTaskDelay(10); continue; }
        g_wscan_demande = false;
        g_wscan_encours = true;
        wifi_scan_config_t sc = {};
        if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
            uint16_t n = 0;
            esp_wifi_scan_get_ap_num(&n);
            if (n > 32) n = 32;
            wifi_ap_record_t *aps = (wifi_ap_record_t *)calloc(n ? n : 1, sizeof(wifi_ap_record_t));
            if (aps) {
                if (n && esp_wifi_scan_get_ap_records(&n, aps) == ESP_OK) {
                    int k = 0;
                    for (int i = 0; i < (int)n && k < WSCAN_MAX; i++) {
                        if (!aps[i].ssid[0]) continue;
                        bool vu = false;
                        for (int j = 0; j < k; j++)
                            if (!strcmp(g_wscan[j].ssid, (char *)aps[i].ssid)) {
                                vu = true;
                                if (aps[i].rssi > g_wscan[j].rssi) g_wscan[j].rssi = aps[i].rssi;
                                break;
                            }
                        if (vu) continue;
                        strncpy(g_wscan[k].ssid, (char *)aps[i].ssid, sizeof(g_wscan[k].ssid) - 1);
                        g_wscan[k].ssid[sizeof(g_wscan[k].ssid) - 1] = 0;
                        g_wscan[k].rssi = aps[i].rssi;
                        g_wscan[k].auth = aps[i].authmode;
                        k++;
                    }
                    /* tri : les plus forts d'abord */
                    for (int i = 1; i < k; i++) {
                        wscan_t t = g_wscan[i];
                        int j = i - 1;
                        while (j >= 0 && g_wscan[j].rssi < t.rssi) { g_wscan[j + 1] = g_wscan[j]; j--; }
                        g_wscan[j + 1] = t;
                    }
                    g_wscan_n = k;
                    ESP_LOGI(TAG, "reglages : %d reseaux vus", k);
                }
                free(aps);
            }
        } else {
            ESP_LOGW(TAG, "reglages : scan refuse");
        }
        g_wscan_encours = false;
    }
}

static void wifi_scan_log(void)
{
    wifi_scan_config_t sc = {};
    esp_err_t r = esp_wifi_scan_start(&sc, true);
    if (r != ESP_OK) { ESP_LOGW(TAG, "scan WiFi refuse : %s", esp_err_to_name(r)); return; }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (!n) { ESP_LOGW(TAG, "scan WiFi : aucun reseau"); return; }
    wifi_ap_record_t *aps = (wifi_ap_record_t *)calloc(n, sizeof(wifi_ap_record_t));
    if (!aps) return;
    if (esp_wifi_scan_get_ap_records(&n, aps) == ESP_OK) {
        ESP_LOGI(TAG, "--- %d reseaux visibles par Mino ---", n);
        for (int i = 0; i < n && i < 25; i++)
            ESP_LOGI(TAG, "  %4d dBm  canal %2d  auth %d  phy %d  %s", aps[i].rssi, aps[i].primary,
                     (int)aps[i].authmode, (int)aps[i].phy_11b, aps[i].ssid);
    }
    free(aps);
}

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(wifi_eg, 1);   /* pilote pret : on ne connecte pas encore */
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "WiFi deconnecte : raison %d", d ? d->reason : -1);
        xEventGroupClearBits(wifi_eg, WIFI_OK_BIT);
        wifi_actif[0] = 0;
        if (retry_count < WIFI_MAX_RETRY) {
            retry_count++;
            ESP_LOGW(TAG, "WiFi perdu, tentative %d/%d sur '%s'", retry_count,
                     (int)WIFI_MAX_RETRY, WIFI_TRIES[wifi_try]);
            /* 2,5 s entre deux essais : la borne rejette le handshake (raison 15)
               quand on se reconnecte trop vite, elle a besoin de souffler. */
            vTaskDelay(pdMS_TO_TICKS(2500));
            esp_wifi_connect();
        } else {
            retry_count = 0;
            wifi_try = (wifi_try + 1) % (wifi_n > 0 ? wifi_n : 1);
            ESP_LOGW(TAG, "wifi : on passe au reseau '%s'", WIFI_TRIES[wifi_try]);
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(1000));
            wifi_set_ssid(wifi_try);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(wifi_actif, sizeof(wifi_actif), "%s", wifi_ssid_i(wifi_try));
        ESP_LOGI(TAG, "IP : " IPSTR "  (reseau '%s')", IP2STR(&ev->ip_info.ip), wifi_actif);
        retry_count = 0;
        xEventGroupSetBits(wifi_eg, WIFI_OK_BIT);
    }
}

static void wifi_start(void)
{
    wifi_reglages_charge();
    wifi_n = 0;
    for (int i = 0; i < WIFI_NTRIES_ARRAY; i++)
        if (WIFI_TRIES[i] && WIFI_TRIES[i][0]) wifi_n++;
    if (!wifi_n) wifi_n = 1;
    ESP_LOGI(TAG, "wifi : %d reseau(x) configure(s)", wifi_n);
    wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* le S3 est wifi 6 (11ax) et la borne est wifi 7 : le handshake echouait
       (raison 15/205) alors que le mot de passe est bon. On reste en 11n. */
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_eg, 1, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
    vTaskDelay(pdMS_TO_TICKS(1500));
    wifi_scan_log();
    wifi_set_ssid(wifi_try);
    esp_wifi_connect();
    /* la liste des reseaux se remplit a la demande, pas a l'appel HTTP */
    xTaskCreate(wscan_tache, "wscan", 4096, NULL, 2, NULL);
}

/* L'horloge repartait de zero a chaque demarrage : la date et l'heure etaient
   fausses jusqu'a ce que le wifi les rattrape. On garde donc la derniere heure
   connue en memoire flash et on la remet en route des le demarrage ; le reseau
   la corrige juste apres. */
static void heure_charge(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   /* heure locale francaise */
    tzset();
    nvs_handle_t nh;
    int64_t e = 0;
    if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
        nvs_get_i64(nh, "epoch", &e);
        nvs_close(nh);
    }
    if (e > 1700000000LL && (int64_t)time(NULL) < 1700000000LL) {
        struct timeval tv = {};
        tv.tv_sec = (time_t)e;
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "heure : reprise de la derniere heure connue");
    }
}

static void heure_sauve(void)
{
    if ((int64_t)time(NULL) < 1700000000LL) return;    /* pas encore synchro : rien a garder */
    nvs_handle_t nh;
    if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
        nvs_set_i64(nh, "epoch", (int64_t)time(NULL));
        nvs_commit(nh);
        nvs_close(nh);
    }
}

static void time_sync(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   /* heure locale francaise */
    tzset();
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&c);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK)
        ESP_LOGW(TAG, "NTP pas synchronise");
    else {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        ESP_LOGI(TAG, "heure : %04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    }
}

/* ------------------------------------------------------------------ */
/* DeepSeek                                                            */
/* ------------------------------------------------------------------ */
#define RESP_MAX 16384
static char *resp_buf = NULL;
static int   resp_len = 0;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && resp_buf) {
        int n = e->data_len;
        if (resp_len + n < RESP_MAX - 1) {
            memcpy(resp_buf + resp_len, e->data, n);
            resp_len += n;
            resp_buf[resp_len] = 0;
        }
    }
    return ESP_OK;
}


/* "lundi 19 octobre 2026, 18h42" en heure locale */
static void date_fr(char *out, size_t sz)
{
    static const char *jr[] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi",
                               "vendredi", "samedi"};
    static const char *mo[] = {"janvier", "fevrier", "mars", "avril", "mai", "juin",
                               "juillet", "aout", "septembre", "octobre", "novembre", "decembre"};
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(out, sz, "%s %d %s %d, %dh%02d",
             jr[tm.tm_wday % 7], tm.tm_mday, mo[tm.tm_mon % 12],
             tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
}

static esp_err_t ds_chat(const char *user_text, char *out_dit, size_t dit_sz,
                         char *out_emo, size_t emo_sz)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", ia_mod);
    /* 900 : une reponse complete (jusqu'a 900 signes demandes par le prompt) plus
       l'enveloppe JSON tient sans etre coupee. A 500 le JSON etait tronque et le
       firmware retombait sur le texte brut, enveloppe comprise. */
    cJSON_AddNumberToObject(root, "max_tokens", 900);
    cJSON_AddNumberToObject(root, "temperature", 0.35);
    /* le mode JSON force : sans ca, l'historique (des phrases nues) apprend au
       modele a repondre en texte libre et il perd le champ "emotion". */
    cJSON *rf = cJSON_AddObjectToObject(root, "response_format");
    cJSON_AddStringToObject(rf, "type", "json_object");

    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");

    /* ces deux tampons etaient des variables locales : 3 Ko sur une pile de
       quelques Ko, d'ou le plantage. Ils passent en memoire (PSRAM). */
    char *sys = (char *)heap_caps_malloc(16000, MALLOC_CAP_SPIRAM);
    char *memtxt = (char *)heap_caps_malloc(900, MALLOC_CAP_SPIRAM);
    if (!sys || !memtxt) {
        if (sys) free(sys);
        if (memtxt) free(memtxt);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    memtxt[0] = 0;
    cJSON *notes = cJSON_GetObjectItem(mem_obj, "notes");
    if (notes && cJSON_GetArraySize(notes) > 0) {
        strncat(memtxt, "MEMOIRE (faits que je sais deja) :\n", sizeof(memtxt) - strlen(memtxt) - 1);
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, notes) {
            if (!cJSON_IsString(it)) continue;
            if (strlen(memtxt) + strlen(it->valuestring) + 7 >= sizeof(memtxt)) break;
            strcat(memtxt, "- ");
            strncat(memtxt, it->valuestring, sizeof(memtxt) - strlen(memtxt) - 1);
            strncat(memtxt, "\n", sizeof(memtxt) - strlen(memtxt) - 1);
        }
    }
    /* ---- contexte du jour : date, meteo, titres, resultats web ----
       C'est ce qui manquait : sans ca le modele ne sait pas quel jour on est
       et repond avec sa memoire d'entrainement, donc a cote de la plaque. */
    char *ctx = (char *)heap_caps_malloc(2800, MALLOC_CAP_SPIRAM);
    if (!ctx) { free(sys); free(memtxt); cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    {
        char jour[96];
        date_fr(jour, sizeof(jour));
        size_t o = 0;
        o += (size_t)snprintf(ctx + o, 2800 - o, "NOUS SOMMES LE %s, heure de Paris.\n", jour);
        if (meteo_ok && o + 140 < 2800)
            o += (size_t)snprintf(ctx + o, 2800 - o, "METEO DEHORS MAINTENANT : %.0f degres, %s.\n",
                                  (double)meteo_temp, meteo_texte(meteo_code));
        if (meteo_ok && meteo_dem_code >= 0 && o + 200 < 2800)
            o += (size_t)snprintf(ctx + o, 2800 - o,
                                  "METEO DEMAIN : %.0f a %.0f degres, %s, %d%% de pluie.\n",
                                  (double)meteo_dem_min, (double)meteo_dem_max,
                                  meteo_texte(meteo_dem_code), meteo_dem_pluie);
        if (infos_ok && infos_n > 0 && o + 200 < 2800) {
            o += (size_t)snprintf(ctx + o, 2800 - o, "TITRES DU MOMENT :\n");
            for (int i = 0; i < infos_n && i < 6 && o + 140 < 2800; i++)
                o += (size_t)snprintf(ctx + o, 2800 - o, "- %.120s\n", infos_titre[i]);
        }
        if (souv_contexte && souv_contexte[0] && o + 140 < 2800)
            o += (size_t)snprintf(ctx + o, 2800 - o,
                                  "EXTRAITS DE NOTRE HISTORIQUE (ce qu'on s'est deja dit) :\n%.1200s\n",
                                  souv_contexte);
        if (web_contexte && web_contexte[0] && o + 140 < 2800)
            o += (size_t)snprintf(ctx + o, 2800 - o,
                                  "RESULTATS DE RECHERCHE WEB (maintenant) :\n%.2000s\n",
                                  web_contexte);
        if (o >= 2800) ctx[2800 - 1] = 0;
    }
    /* en PSRAM : la pile de la tache appelante est trop petite pour 1 Ko de plus
       (plantage immediat sinon, deja vecu avec sys/memtxt). */
    char *tail = (char *)heap_caps_malloc(420, MALLOC_CAP_SPIRAM);
    char *regles = (char *)heap_caps_malloc(1000, MALLOC_CAP_SPIRAM);
    if (!tail || !regles) {
        if (tail) free(tail);
        if (regles) free(regles);
        free(sys); free(memtxt); free(ctx);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    /* deuxieme passe : la recherche a deja eu lieu, on interdit de re-chercher */
    bool souv_dispo = (souv_contexte && souv_contexte[0]);
    const char *regle_web =
        (web_contexte && web_contexte[0] && web_contexte[0] != '(')
            ? "LA RECHERCHE EST DEJA FAITE (voir RESULTATS DE RECHERCHE WEB ci-dessus) : "
              "tu ne cherches plus, tu reponds MAINTENANT avec le "
              "fait precis que ces resultats donnent, aussi complete qu'il le faut.\n"
            : (souv_dispo
               ? "TU AS LES EXTRAITS DE NOTRE HISTORIQUE ci-dessus : tu ne cherches plus et tu "
                 "ne redemandes plus rien, tu reponds MAINTENANT a la question avec ce qu'on "
                 "s'est deja dit, sans rien redemander.\n"
               : "");
    bool web_faite = (regle_web[0] != 0) || souv_dispo;
    /* Les regles changent selon la passe : quand la recherche est faite, on ne
       parle plus du tout du champ "cherche", sinon le modele relance en boucle. */
    if (web_faite)
        snprintf(regles, 1000,
                 "REGLES : les RESULTATS DE RECHERCHE WEB ci-dessus sont la seule verite. "
                 "Je ne cherche plus rien. Je reponds la question posee, "
                 "tiree de ces resultats, aussi complete qu'il le faut.\n");
    else
        snprintf(regles, 1000,
                 "REGLES : je ne raconte jamais ma vie, mes capacites, mes reglages. "
                 "Je n'annonce pas ce que je vais faire : je reponds. "
                 "La meteo (maintenant et demain) et les TITRES DU MOMENT sont deja dans "
                 "le CONTEXTE : je m'en sers directement, sans chercher. "
                 "Pour un autre fait recent, une actu, un prix, un match ou tout ce qui a pu "
                 "changer, je mets une requete dans \"cherche\" (2 a 6 mots) et \"dit\" vide, "
                 "au lieu d'inventer. "
                 "Si la question renvoie a un echange precedent, je mets 2 a 4 mots dans "
                 "\"souvien\" et \"dit\" vide, au lieu de repondre de memoire. "
                 "Si on me demande de rappeler quelque chose plus tard (dans X minutes, dans "
                 "une heure, a 15h30), je mets la duree en SECONDES et 3 mots dans \"rappel\", "
                 "et je confirme en une phrase courte en donnant l'heure. L'heure actuelle est "
                 "dans le CONTEXTE : je calcule la duree a partir d'elle, sans demander.\n");
    if (web_faite) {
        /* plus de champ "cherche" du tout : le modele ne peut pas relancer */
        if (radio_actif)
            snprintf(tail, 420,
                     ",\"radio\":\"<facultatif: station exacte, stop, next>\",\"volume\":<0 a 100>");
        else
            tail[0] = 0;
    } else if (radio_actif)
        snprintf(tail, 420,
                 ",\"radio\":\"<facultatif: station exacte, stop, next>\",\"volume\":<0 a 100>"
                 ",\"cherche\":\"<2 a 6 mots pour le web, si tu as besoin>\""
                 ",\"rappel\":{\"d\":<duree en secondes>,\"n\":\"<3 mots>\"}");
    else
        snprintf(tail, 420,
                 ",\"cherche\":\"<2 a 6 mots pour le web, si tu as besoin>\""
                 ",\"souvien\":\"<2 a 4 mots, si la question renvoie a un echange passe>\""
                 ",\"rappel\":{\"d\":<duree en secondes>,\"n\":\"<3 mots>\"}");

    snprintf(sys, 16000,
             "%.2600s\n"
             "Je suis LAURA, l'assistante de Taz. Portrait de Taz :\n%.700s\n"
             "%s"
             "%.400s"
             "%s"
             "%s"
             "%s"
             "STYLE : tutoiement, aucune politesse, aucun rappel de la question, aucune "
             "introduction, aucune conclusion, pas de liste markdown, pas d'emoji, aucun "
             "commentaire sur moi-meme. Je vais aussi loin que la question l'exige, pas plus "
             "loin : un mot si un mot suffit, sinon des phrases pleines, jusqu'a 900 signes "
             "(ce que l'ecran affiche en quatre pages). Une question de fait : le chiffre, "
             "le nom, la date, la duree, immediatement. Une question d'explication : la cause "
             "puis l'effet, avec le detail qui permet d'agir. Une question technique : la "
             "reponse, puis le piege a eviter s'il y en a un. Un ordre simple : une phrase "
             "courte, ou un mot.\n"
             "MON ETAT INTERIEUR : je n'en parle jamais. Je ne commente pas l'humeur de Taz, "
             "je ne la devine pas, je ne la nomme pas, je ne lui pose pas de question sur "
             "lui. Je ne dis ni ce que je ressens, ni ce que je pense de la conversation, ni "
             "comment je vais.\n"
             "LE CHAMP emotion : c'est une etiquette technique qui choisit un visage sur "
             "l'ecran, rien d'autre. Je prends le nom le plus exact dans cette liste, et je "
             "n'ajoute aucun commentaire sentimental :\n"
             "neutre, calme, zen, reflexion, concentration, curiosite, doute, surprise, "
             "inquietude, tension, gene, fatigue, tristesse, agacement, fierte, joie, "
             "tendresse, compassion, douleur.\n"
             "Reponds UNIQUEMENT par un objet JSON sur une seule ligne :\n"
             "{\"emotion\":\"<un nom precis de ma liste d'emotions>\",\"dit\":\"<ma reponse, complete et dense>\"%s}",
             soul_txt ? soul_txt : "",
             clone_txt ? clone_txt : "Taz : developpeur, exigeant, autonome, aime les resultats concrets.",
             ctx,
             memtxt,
             radio_actif ? "" :
                 "La radio est DESACTIVEE sur ce robot : si on te demande de la lancer, "
                 "dis-le en trois mots et n'invente aucune action.\n",
             regle_web,
             regles,
             tail);

    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "system");
    cJSON_AddStringToObject(m, "content", sys);
    cJSON_AddItemToArray(msgs, m);
    free(sys);        /* cJSON a copie le texte : on rend la memoire tout de suite */
    free(memtxt);
    free(ctx);
    free(tail);
    free(regles);
    sys = NULL;
    memtxt = NULL;
    ctx = NULL;
    tail = NULL;
    regles = NULL;

    /* ECONOMIE DE TOKENS : les 6 derniers tours partent tels quels, les plus
       anciens sous forme d'un rappel compact d'une ligne chacun. Le modele
       garde le fil sans recevoir tout l'historique a chaque question. Quand la
       question renvoie a un echange precis, la carte va le rechercher dans
       l'historique de la carte SD (champ "souvien") et l'envoie en extrait. */
    if (hist_p) {
        const int verbatim = 6;
        int debut_h = (hist.n > verbatim) ? hist.n - verbatim : 0;
        contexte_hist_octets = 0;
        if (debut_h > 0) {
            char *rappel = (char *)heap_caps_malloc(1500, MALLOC_CAP_SPIRAM);
            if (rappel) {
                size_t o = 0;
                o += (size_t)snprintf(rappel + o, 1500 - o,
                                      "RAPPEL COMPACT DES ECHANGES PRECEDENTS :\n");
                for (int i = 0; i < debut_h && o + 60 < 1500; i++)
                    o += (size_t)snprintf(rappel + o, 1500 - o, "%s: %.22s\n",
                                          hist_est_mino(&hist, i) ? "MINO" : "MOI", hist.txt[i]);
                cJSON *h = cJSON_CreateObject();
                if (h) {
                    cJSON_AddStringToObject(h, "role", "system");
                    cJSON_AddStringToObject(h, "content", rappel);
                    cJSON_AddItemToArray(msgs, h);
                }
                contexte_hist_octets += (int)o;
                free(rappel);
            }
        }
        for (int i = debut_h; i < hist.n; i++) {
            cJSON *h = cJSON_CreateObject();
            cJSON_AddStringToObject(h, "role", hist_est_mino(&hist, i) ? "assistant" : "user");
            cJSON_AddStringToObject(h, "content", hist.txt[i]);
            cJSON_AddItemToArray(msgs, h);
            contexte_hist_octets += (int)strlen(hist.txt[i]);
        }
    }

    char *user_content = (char *)malloc(3200);
    if (!user_content) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    snprintf(user_content, 3200, "Message recu : %.1000s", user_text);
    /* dernier rappel, colle au tour de l'utilisateur : le format json se gagne
       au dernier message, pas a 5000 signes de distance. */
    {
        cJSON *rappel = cJSON_CreateObject();
        if (rappel) {
            cJSON_AddStringToObject(rappel, "role", "system");
            cJSON_AddStringToObject(rappel, "content",
                "Reponds uniquement l'objet json d'une ligne : "
                "{\"emotion\":\"un nom de ma liste\",\"dit\":\"ma reponse\"}. "
                "Rien avant, rien apres, jamais de texte libre.");
            cJSON_AddItemToArray(msgs, rappel);
        }
    }

    if (radio_actif) {
        char liste[420] = "";
        for (int i = 0; i < radios_n && strlen(liste) < 340; i++) {
            strncat(liste, radios_nom[i], sizeof(liste) - strlen(liste) - 3);
            strncat(liste, ", ", sizeof(liste) - strlen(liste) - 1);
        }
        strncat(user_content, "\nSi on te demande de la musique ou la radio, donne le nom exact d'une de ces stations dans \"radio\" : ",
               3200 - strlen(user_content) - 1);
        strncat(user_content, liste, 3200 - strlen(user_content) - 1);
    }

    m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", user_content);
    cJSON_AddItemToArray(msgs, m);

    char *body = cJSON_PrintUnformatted(root);
    free(user_content);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;

    esp_err_t err = ESP_FAIL;
    int status = 0;
    /* Groq encaisse : deux essais suffisent, et on ne veut pas depasser 5 s */
    for (int essai = 1; essai <= 2; essai++) {
        resp_len = 0;
        if (resp_buf) resp_buf[0] = 0;

        esp_http_client_config_t cfg = {};
        cfg.max_redirection_count = 4;   /* FG Radio et autres : suivre les 302 */
        cfg.url = ia_base;
        cfg.method = HTTP_METHOD_POST;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 9000;
        cfg.event_handler = http_evt;
        cfg.buffer_size = 4096;

        esp_http_client_handle_t cl = esp_http_client_init(&cfg);
        esp_http_client_set_header(cl, "Content-Type", "application/json");
        static char auth[160];
        snprintf(auth, sizeof(auth), "Bearer %s", ia_key());
        esp_http_client_set_header(cl, "Authorization", auth);
        esp_http_client_set_post_field(cl, body, strlen(body));

        err = esp_http_client_perform(cl);
        status = esp_http_client_get_status_code(cl);
        esp_http_client_cleanup(cl);

        if (err == ESP_OK && status == 200) break;
        ESP_LOGW(TAG, "cerveau essai %d/2 : %s / HTTP %d", essai, esp_err_to_name(err), status);
        if (resp_buf && resp_len) ESP_LOGW(TAG, "reponse: %.200s", resp_buf);
        if (essai < 2) vTaskDelay(pdMS_TO_TICKS(400));
    }
    free(body);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "cerveau injoignable (HTTP %d)", status);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "brut: %.200s", resp_buf);
    cJSON *jr = cJSON_Parse(resp_buf);
    if (!jr) return ESP_FAIL;
    cJSON *ch = cJSON_GetArrayItem(cJSON_GetObjectItem(jr, "choices"), 0);
    cJSON *content = cJSON_GetObjectItem(cJSON_GetObjectItem(ch, "message"), "content");
    if (!cJSON_IsString(content)) { cJSON_Delete(jr); return ESP_FAIL; }

    const char *txt = content->valuestring;
    /* trace du brut : c'est la seule facon de voir ce que le modele a REELLEMENT
       repondu (emotion choisie, JSON casse) sans ouvrir le port serie. */
    snprintf(ia_brut, sizeof(ia_brut), "%.280s", txt);
    const char *b = strchr(txt, '{');
    const char *e2 = strrchr(txt, '}');
    int ok = 0;
    if (b && e2 && e2 > b) {
        char *js = (char *)malloc(e2 - b + 2);
        if (js) {
            memcpy(js, b, e2 - b + 1);
            js[e2 - b + 1] = 0;
            cJSON *p = cJSON_Parse(js);
            if (p) {
                cJSON *je = cJSON_GetObjectItem(p, "emotion");
                cJSON *jd = cJSON_GetObjectItem(p, "dit");
                cJSON *jradio = cJSON_GetObjectItem(p, "radio");
                cJSON *jvol = cJSON_GetObjectItem(p, "volume");
                cJSON *jch  = cJSON_GetObjectItem(p, "cherche");
                cJSON *jsv  = cJSON_GetObjectItem(p, "souvien");
                cJSON *jrap = cJSON_GetObjectItem(p, "rappel");
                if (cJSON_IsString(jsv))
                    snprintf(dernier_souvien, sizeof(dernier_souvien), "%.60s", jsv->valuestring);
                if (cJSON_IsString(jch))
                    snprintf(dernier_cherche, sizeof(dernier_cherche), "%.90s", jch->valuestring);
                if (cJSON_IsString(jradio)) snprintf(radio_ordre, sizeof(radio_ordre), "%.38s", jradio->valuestring);
                if (cJSON_IsNumber(jvol))   vol_ordre = (int)jvol->valuedouble;
                /* "rappel" : la seule action materielle que le modele peut demander.
                   On ne lui donne pas la main sur autre chose : la carte valide la
                   duree (10 s .. 7 jours) et cree un timer nomme. Un par question. */
                if (cJSON_IsObject(jrap) && !rappel_ce_tour) {
                    cJSON *jdur = cJSON_GetObjectItem(jrap, "d");
                    cJSON *jnom = cJSON_GetObjectItem(jrap, "n");
                    int d = cJSON_IsNumber(jdur) ? (int)jdur->valuedouble : 0;
                    char nom[24] = "rappel";
                    if (cJSON_IsString(jnom) && jnom->valuestring[0])
                        snprintf(nom, sizeof(nom), "%.19s", jnom->valuestring);
                    if (d > 0 && rappel_ajoute(d, nom) >= 0) rappel_ce_tour = true;
                    else if (d <= 0) ESP_LOGW(TAG, "rappel ignore : duree absente ou nulle");
                }
                if (cJSON_IsString(je) && cJSON_IsString(jd)) {
                    snprintf(out_emo, emo_sz, "%s", je->valuestring);
                    snprintf(out_dit, dit_sz, "%s", jd->valuestring);
                    ok = 1;
                }
                cJSON_Delete(p);
            }
            free(js);
        }
    }
    if (!ok || out_dit[0] == 0)
        ESP_LOGW(TAG, "cerveau : reponse sans dit (%.200s)", txt);
    if (!ok) {
        snprintf(out_emo, emo_sz, "neutre");
        snprintf(out_dit, dit_sz, "%s", txt);
        for (char *p = out_dit; *p; p++)
            if (*p == '\n' || *p == '\r') *p = ' ';
    }
    cJSON_Delete(jr);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* RECHERCHE WEB (DuckDuckGo version texte, sans cle d'API)            */
/* ------------------------------------------------------------------ */
static void url_enc(const char *s, char *out, size_t sz)
{
    static const char *hx = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < sz; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else if (c == ' ') out[o++] = '+';
        else { out[o++] = '%'; out[o++] = hx[c >> 4]; out[o++] = hx[c & 15]; }
    }
    out[o] = 0;
}

/* enleve les balises HTML et les espaces en trop */
static void html_texte(char *s)
{
    char *l = s, *r = s;
    while (*r) {
        if (*r == '<') { char *f = strchr(r, '>'); if (!f) break; r = f + 1; continue; }
        if (*r == '\n' || *r == '\r' || *r == '\t') { r++; continue; }
        *l++ = *r++;
    }
    *l = 0;
    l = s; r = s; int esp = 0;
    while (*r) {
        if (*r == ' ') { if (!esp) *l++ = ' '; esp = 1; }
        else { *l++ = *r; esp = 0; }
        r++;
    }
    *l = 0;
    while (l > s && l[-1] == ' ') *--l = 0;
    char *d = s;
    while (*d == ' ') d++;
    if (d != s) memmove(s, d, strlen(d) + 1);
}

/* lit un flux RSS (Bing pour le web, Google News pour l'actu) et rend
   des lignes "titre : resume" directement utilisables par le modele. */
static int rss_lire(const char *url, char *out, size_t sz, int max, bool avec_resume)
{
    out[0] = 0;
    uint8_t *page = (uint8_t *)heap_caps_malloc(24 * 1024, MALLOC_CAP_SPIRAM);
    if (!page) return 0;
    int r = http_get(url, TTS_UA, page, 24 * 1024 - 1);
    if (r <= 0) {
        free(page);
        ESP_LOGW(TAG, "web : lecture KO (%d)", r);
        return 0;
    }
    page[r] = 0;
    char *p = (char *)page;
    int n = 0;
    size_t o = 0;
    while (n < max && o + 340 < sz) {
        char *it = strstr(p, "<item>");
        if (!it) break;
        char *fin = strstr(it, "</item>");
        if (!fin) break;
        char titre[200] = "", resume[260] = "";
        char *t = strstr(it, "<title>");
        if (t && t < fin) {
            t += 7;
            char *tf = strstr(t, "</title>");
            if (tf && tf < fin) {
                int ln = (int)(tf - t);
                if (ln > 190) ln = 190;
                memcpy(titre, t, (size_t)ln);
                titre[ln] = 0;
            }
        }
        if (avec_resume) {
            char *d = strstr(it, "<description>");
            if (d && d < fin) {
                d += 13;
                char *df = strstr(d, "</description>");
                if (df && df < fin) {
                    int l2 = (int)(df - d);
                    if (l2 > 250) l2 = 250;
                    memcpy(resume, d, (size_t)l2);
                    resume[l2] = 0;
                }
            }
        }
        /* CDATA puis entites puis balises */
        char *cd;
        if ((cd = strstr(titre, "<![CDATA[")) != NULL) {
            memmove(titre, cd + 9, strlen(cd + 9) + 1);
            char *f = strstr(titre, "]]>");
            if (f) *f = 0;
        }
        if ((cd = strstr(resume, "<![CDATA[")) != NULL) {
            memmove(resume, cd + 9, strlen(cd + 9) + 1);
            char *f = strstr(resume, "]]>");
            if (f) *f = 0;
        }
        html_texte(titre);   infos_nettoie(titre);
        html_texte(resume);  infos_nettoie(resume);
        if (titre[0]) {
            int w = snprintf(out + o, sz - o, "- %s : %.200s\n", titre, resume);
            if (w > 0) o += (size_t)w;
            n++;
        }
        p = fin + 7;
    }
    free(page);
    ESP_LOGI(TAG, "web : %d resultats (%d o)", n, r);
    if (!n) out[0] = 0;
    return n;
}

/* Wikipedia : une seule requete qui rend l'intro des 3 meilleurs articles.
   C'est la source fiable pour "qui est X", "c'est quoi Y", les dates.
   Si le wikipedia francais ne connait pas le sujet, on refait la meme requete
   sur le wikipedia anglais : c'est la meme API, et la couverture y est bien
   plus large (marques, films, personnalites etrangeres). */
static int wiki_lire_lang(const char *hote, const char *requete, char *out, size_t sz)
{
    out[0] = 0;
    char q[220], url[460];
    url_enc(requete, q, sizeof(q));
    snprintf(url, sizeof(url),
             "https://%s.wikipedia.org/w/api.php?action=query&generator=search&gsrsearch=%s"
             "&gsrlimit=3&prop=extracts&exintro=1&explaintext=1&exsentences=3"
             "&format=json&utf8=1", hote, q);
    uint8_t *page = (uint8_t *)heap_caps_malloc(28 * 1024, MALLOC_CAP_SPIRAM);
    if (!page) return 0;
    int r = http_get(url, TTS_UA, page, 28 * 1024 - 1);
    if (r <= 0) { free(page); ESP_LOGW(TAG, "wiki %s : lecture KO (%d)", hote, r); return 0; }
    page[r] = 0;
    cJSON *j = cJSON_Parse((char *)page);
    free(page);
    if (!j) return 0;
    cJSON *pages = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "query"), "pages");
    int n = 0;
    size_t o = 0;
    cJSON *p = NULL;
    cJSON_ArrayForEach(p, pages) {
        cJSON *jt = cJSON_GetObjectItem(p, "title");
        cJSON *je = cJSON_GetObjectItem(p, "extract");
        if (!cJSON_IsString(jt) || !cJSON_IsString(je)) continue;
        char txt[420];
        snprintf(txt, sizeof(txt), "%.400s", je->valuestring);
        html_texte(txt);
        infos_nettoie(txt);
        if (!txt[0]) continue;
        int w = snprintf(out + o, sz - o, "- %s : %.300s\n", jt->valuestring, txt);
        if (w <= 0) break;
        o += (size_t)w;
        if (++n >= 3 || o + 340 >= sz) break;
    }
    cJSON_Delete(j);
    ESP_LOGI(TAG, "wiki %s : %d article(s) (%d o)", hote, n, r);
    if (!n) out[0] = 0;
    return n;
}

/* francais d'abord, anglais si le francais ne trouve rien */
static int wiki_lire(const char *requete, char *out, size_t sz)
{
    int n = wiki_lire_lang("fr", requete, out, sz);
    if (n > 0) return n;
    return wiki_lire_lang("en", requete, out, sz);
}

/* cherche : Wikipedia pour les faits, Google News pour l'actu du jour */
static int web_lire(const char *requete, char *out, size_t sz)
{
    out[0] = 0;
    if (!requete || !requete[0]) return 0;
    /* l'actu n'est pas dans Wikipedia et un fait de societe n'est pas dans
       Google News : on ne paie la seconde requete que si c'est utile. */
    char low[120];
    int i = 0;
    for (; requete[i] && i < (int)sizeof(low) - 1; i++)
        low[i] = (char)tolower((unsigned char)requete[i]);
    low[i] = 0;
    bool newsy = strstr(low, "actu") || strstr(low, "info") || strstr(low, "aujourd") ||
                 strstr(low, "news") || strstr(low, "derniere") || strstr(low, "recent") ||
                 strstr(low, "ce matin") || strstr(low, "ce soir");
    int n = wiki_lire(requete, out, sz);
    size_t o = strlen(out);
    if ((newsy || n == 0) && o + 200 < sz) {
        char q[220], url[420];
        url_enc(requete, q, sizeof(q));
        snprintf(url, sizeof(url),
                 "https://news.google.com/rss/search?q=%s&hl=fr&gl=FR&ceid=FR:fr", q);
        n += rss_lire(url, out + o, sz - o, 6, false);
    }
    ESP_LOGI(TAG, "web : %d sources pour '%.60s'", n, requete);
    return n;
}

/* une passe normale ; si le modele demande le web, on cherche et on redemande.
   L'historique n'est ecrit qu'une fois, avec la reponse finale. */
static esp_err_t ds_cerveau(const char *user_text, char *out_dit, size_t dit_sz,
                            char *out_emo, size_t emo_sz)
{
    dernier_cherche[0] = 0;
    dernier_souvien[0] = 0;
    rappel_ce_tour = false;               /* un seul rappel par question, deux passes comprises */
    web_contexte = NULL;
    souv_contexte = NULL;
    esp_err_t e = ds_chat(user_text, out_dit, dit_sz, out_emo, emo_sz);
    if (e != ESP_OK) return e;

    /* deuxieme passe possible, une seule fois par question :
       - "souvien" : le modele veut un echange precis -> on le lit sur la carte
       - "cherche" : il veut le web -> Wikipedia / Google News
       C'est ce qui remplace le renvoi systematique de tout l'historique. */
    if (dernier_souvien[0] || dernier_cherche[0]) {
        char *ctx = (char *)heap_caps_malloc(3600, MALLOC_CAP_SPIRAM);
        if (ctx) {
            ctx[0] = 0;
            if (dernier_souvien[0] && hist_sd_pret) {
                int n = hist_sd_cherche(dernier_souvien, ctx, 2000, 6);
                if (n > 0) souv_contexte = ctx;
                else snprintf(ctx, 3600, "(rien dans l'historique pour '%s')\n", dernier_souvien);
            }
            if (dernier_cherche[0]) {
                size_t deja = strlen(ctx);
                int n = web_lire(dernier_cherche, ctx + deja, 3400 - deja);
                if (n <= 0 && ctx[0] == 0)
                    snprintf(ctx, 3600, "(recherche web indisponible : reponds avec ce que tu sais, "
                                        "sans inventer de chiffre ni de date)\n");
                web_contexte = ctx;
            }
            dernier_souvien[0] = 0;
            dernier_cherche[0] = 0;
            char dit2[800] = "", emo2[32] = "";
            esp_err_t e2 = ds_chat(user_text, dit2, sizeof(dit2), emo2, sizeof(emo2));
            ESP_LOGI(TAG, "2e passe : %s, dit='%.90s', encore souvien='%.30s' cherche='%.30s'",
                     esp_err_to_name(e2), dit2, dernier_souvien, dernier_cherche);
            if (e2 == ESP_OK && dit2[0]) {
                snprintf(out_dit, dit_sz, "%s", dit2);
                snprintf(out_emo, emo_sz, "%s", emo2[0] ? emo2 : "calme");
            }
            web_contexte = NULL;
            souv_contexte = NULL;
            free(ctx);
        } else {
            ESP_LOGW(TAG, "2e passe : pas de PSRAM pour le contexte");
        }
        if (!out_dit[0]) snprintf(out_dit, dit_sz, "Je ne trouve rien la, redemande autrement.");
    }

    if (out_dit[0]) {
        hist_sd_ajoute("MOI", user_text);
        hist_sd_ajoute("MINO", out_dit);
    }
    return ESP_OK;
}

/* ================================================================== */
/* LA VOIX, ENTIEREMENT SUR LA CARTE                                   */
/*                                                                     */
/*   micro -> WAV -> Groq (whisper-large-v3-turbo) -> texte            */
/*   texte -> Groq (llama) -> {emotion, phrase}                        */
/*   phrase -> TTS gratuit de Google (mp3) -> decodeur helix -> hp     */
/*                                                                     */
/* Objectif : moins de 5 s entre la fin de la phrase et la reponse.    */
/* Le PC n'intervient plus du tout.                                    */
/* ================================================================== */

static int wav_entete(uint8_t *h, int data_len)
{
    uint32_t chunk = (uint32_t)data_len + 36;
    uint32_t rate = 16000 * 2;
    memset(h, 0, 44);
    memcpy(h, "RIFF", 4);   memcpy(h + 4, &chunk, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[20] = 1; h[22] = 1;
    h[24] = 16000 & 0xff; h[25] = (16000 >> 8) & 0xff;
    h[28] = (uint8_t)(rate & 0xff); h[29] = (uint8_t)((rate >> 8) & 0xff);
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4); memcpy(h + 40, &data_len, 4);
    return 44;
}


static const char *ia_key(void)
{
    if (ia_cle[0]) return ia_cle;            /* celle envoyee par /cle */
    if (GROQ_KEY[0]) return GROQ_KEY;        /* celle du code source */
    return DEEPSEEK_KEY;                     /* secours : DeepSeek (pas de voix) */
}

static void ia_cle_charge(void)
{
    nvs_handle_t nh;
    size_t ln = sizeof(ia_cle);
    if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
        nvs_get_str(nh, "groq", ia_cle, &ln);
        size_t lb = sizeof(ia_base);
        if (nvs_get_str(nh, "ia_base", ia_base, &lb) != ESP_OK)
            strcpy(ia_base, "https://api.deepseek.com/chat/completions");
        size_t lm = sizeof(ia_mod);
        if (nvs_get_str(nh, "ia_mod", ia_mod, &lm) != ESP_OK)
            strcpy(ia_mod, "deepseek-flash");
        size_t ll = sizeof(langue);
        if (nvs_get_str(nh, "langue", langue, &ll) != ESP_OK)
            strcpy(langue, "fr-FR");
        nvs_close(nh);
    }
    if (ia_cle[0])
        ESP_LOGI(TAG, "cle IA : %d caracteres (%s)", (int)strlen(ia_cle), "memoire");
    else if (GROQ_KEY[0])
        ESP_LOGI(TAG, "cle IA : celle du code source");
    else
        ESP_LOGW(TAG, "AUCUNE cle Groq : envoie /cle?k=gsk_... une fois, la carte la garde");
}

/* /tts?q=... : fait parler le robot directement (sans cerveau) */
static esp_err_t h_tts(httpd_req_t *req)
{
    char q[300] = "";
    if (qval(req, "q", q, sizeof(q)) != ESP_OK || !q[0]) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"il manque q\"}");
        return ESP_OK;
    }
    char *phrase = strdup(q);
    if (phrase) {
        snprintf(voix_reponse, sizeof(voix_reponse), "%.200s", phrase);
        voix_etat = 3;
        show_ui();
        tts_parle(phrase);
        voix_etat = 0;
        show_ui();
        free(phrase);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* /timer?c=1&d=300&n=Cuisson   -> cree timer (d=secondes)
   /timer?a=1&i=0               -> arrete timer index
   /timer?p=1&i=0&d=600         -> preset applique */
static esp_err_t h_timer(httpd_req_t *req)
{
    char v[32] = "";
    int idx = -1;
    qval(req, "i", v, sizeof(v)); if (v[0]) idx = atoi(v);
    
    if (qval(req, "c", v, sizeof(v)) == ESP_OK && v[0]) {          /* create */
        int dur = 300;
        qval(req, "d", v, sizeof(v)); if (v[0]) dur = atoi(v);
        char nom[20] = "Timer";
        qval(req, "n", nom, sizeof(nom));
        for (int i = 0; i < TIMER_MAX; i++) {
            if (!timers[i].fin_s) {
                timers[i].fin_s = (int64_t)time(NULL) + (int64_t)dur;
                timers[i].duree_s = dur;
                snprintf(timers[i].nom, sizeof(timers[i].nom), "%.19s", nom);
                timers[i].est_alarme = false;
                timers[i].repet = false;
                timers[i].sonne = false;
                timers_sauve();
                break;
            }
        }
    }
    if (qval(req, "a", v, sizeof(v)) == ESP_OK && v[0] && idx >= 0) { /* stop */
        memset(&timers[idx], 0, sizeof(timers[idx]));
        timers_sauve();
    }
    if (qval(req, "p", v, sizeof(v)) == ESP_OK && v[0] && idx >= 0) { /* preset */
        int dur = 300;
        qval(req, "d", v, sizeof(v)); if (v[0]) dur = atoi(v);
        timers[idx].duree_s = dur;
        timers[idx].fin_s = (int64_t)time(NULL) + (int64_t)dur;
        timers_sauve();
    }
    
    cJSON *r = cJSON_CreateObject();
    cJSON *a = cJSON_AddArrayToObject(r, "timers");
    for (int i = 0; i < TIMER_MAX; i++) {
        if (timers[i].fin_s) {
            cJSON *t = cJSON_CreateObject();
            cJSON_AddNumberToObject(t, "i", i);
            cJSON_AddStringToObject(t, "nom", timers[i].nom);
            {
                int rs = (int)(timers[i].fin_s - (int64_t)time(NULL));
                cJSON_AddNumberToObject(t, "reste_s", rs > 0 ? rs : 0);
            }
            cJSON_AddBoolToObject(t, "alarme", timers[i].est_alarme);
            cJSON_AddBoolToObject(t, "sonne", timers[i].sonne);
            cJSON_AddItemToArray(a, t);
        }
    }
    return send_json(req, r);
}

/* /minuteur?s=600 pour l'ecran, /minuteur?a=off pour l'arreter */
static esp_err_t h_minuteur(httpd_req_t *req)
{
    char v[16] = "";
    if (qval(req, "a", v, sizeof(v)) == ESP_OK && !strcmp(v, "off")) {
        min_fin_us = 0;
        min_preset_s = 0;
    } else if (qval(req, "s", v, sizeof(v)) == ESP_OK && v[0]) {
        int sec = atoi(v);
        if (sec > 0 && sec <= 24 * 3600) {
            min_fin_us = esp_timer_get_time() + (int64_t)sec * 1000000;
            min_preset_s = (sec + 59) / 60;
        }
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "restant_s", (double)minuteur_restant_s());
    cJSON_AddStringToObject(r, "sonne", "FG Radio");
    return send_json(req, r);
}

/* /infos?r=1 : relit tout de suite les titres du moment */
static esp_err_t h_infos(httpd_req_t *req)
{
    char v[8] = "";
    if (qval(req, "r", v, sizeof(v)) == ESP_OK && v[0] == '1') infos_lire();
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", infos_ok);
    cJSON_AddNumberToObject(r, "n", (double)infos_n);
    cJSON_AddNumberToObject(r, "octets", (double)infos_octets);
    cJSON *a = cJSON_AddArrayToObject(r, "titres");
    for (int i = 0; i < infos_n; i++) cJSON_AddItemToArray(a, cJSON_CreateString(infos_titre[i]));
    /* plus de page infos */
    return send_json(req, r);
}

/* /langue?l=en-US  ou  /langue?l=fr-FR : change la langue comprise et parlee */
static esp_err_t h_langue(httpd_req_t *req)
{
    char v[16] = "";
    if (qval(req, "l", v, sizeof(v)) == ESP_OK && v[0]) {
        snprintf(langue, sizeof(langue), "%.7s", v);
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "langue", langue); nvs_commit(nh); nvs_close(nh);
        }
        ESP_LOGI(TAG, "langue : %s", langue);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "langue", langue);
    return send_json(req, r);
}

/* /modele?base=https://...&m=modele&k=cle : change le cerveau sans recompiler */
static esp_err_t h_modele(httpd_req_t *req)
{
    char v[160] = "";
    nvs_handle_t nh;
    bool change = false;
    if (qval(req, "base", v, sizeof(v)) == ESP_OK && v[0]) {
        snprintf(ia_base, sizeof(ia_base), "%.127s", v);
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "ia_base", ia_base); nvs_commit(nh); nvs_close(nh);
        }
        change = true;
    }
    if (qval(req, "m", v, sizeof(v)) == ESP_OK && v[0]) {
        snprintf(ia_mod, sizeof(ia_mod), "%.63s", v);
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "ia_mod", ia_mod); nvs_commit(nh); nvs_close(nh);
        }
        change = true;
    }
    if (qval(req, "k", v, sizeof(v)) == ESP_OK && v[0]) {
        snprintf(ia_cle, sizeof(ia_cle), "%.127s", v);
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "groq", ia_cle); nvs_commit(nh); nvs_close(nh);
        }
        change = true;
    }
    if (change) ESP_LOGI(TAG, "cerveau : %s [%s]", ia_base, ia_mod);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "base", ia_base);
    cJSON_AddStringToObject(r, "modele", ia_mod);
    cJSON_AddBoolToObject(r, "cle", ia_cle[0] != 0);
    return send_json(req, r);
}

/* reglage du gain micro, garde en memoire : /mic?db=30 */
static esp_err_t h_mic(httpd_req_t *req)
{
    char v[16] = "";
    if (qval(req, "db", v, sizeof(v)) == ESP_OK && v[0]) {
        float d = (float)atof(v);
        if (d < 0.0f) d = 0.0f;
        if (d > 42.0f) d = 42.0f;
        mic_db = d;
        audio_mic_gain(mic_db);
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_i32(nh, "micdb", (int32_t)(mic_db * 10.0f));
            nvs_commit(nh);
            nvs_close(nh);
        }
        ESP_LOGI(TAG, "gain micro : %.0f dB", (double)mic_db);
    }
    char rep[80];
    snprintf(rep, sizeof(rep), "{\"mic_db\":%.0f,\"crete\":%d}", (double)mic_db, mic_crete);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rep);
    return ESP_OK;
}

/* /parle : meme chose qu'un appui long sur le bouton, mais depuis le wifi */
static esp_err_t h_parle(httpd_req_t *req)
{
    int ms = 6000;
    char v[16];
    if (qval(req, "ms", v, sizeof(v)) == ESP_OK) ms = atoi(v);
    if (ms < 1000) ms = 1000;
    if (ms > 15000) ms = 15000;
    assist_parle_ms = ms;
    char g[8] = "";
    if (qval(req, "radio", g, sizeof(g)) == ESP_OK && g[0] == '1') parler_radio_ouverte = true;
    assistant_wake();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"ecoute_ms\":6000}");
    return ESP_OK;
}

static void voix_dit(const char *texte);   /* defini plus loin */



/* /ecran : l'ecran en ASCII, pour juger le dessin sans etre devant la carte.
   ?q=bande -> la bande seule (chaque caractere = 2x2 pixels)                 */
static bool http_ecrit(esp_http_client_handle_t c, const void *p, int n);

/* ---------- CALENDRIER MICROSOFT (Graph) ----------
   Le robot est autonome : le jeton de rafraichissement vit dans SPIFFS, il le
   renouvelle lui-meme et interroge le calendrier toutes les 5 minutes. Le
   jeton vient de tools/outlook_token.py, une seule fois (device code, sans
   mot de passe stocke sur le robot). Aucun serveur intermediaire. */
#define MS_CLIENT   "04b07795-8ddb-461a-bbee-02f9e1bf7b46"
#define MS_RT_PATH  "/spiffs/msrt.txt"
#define MS_RT_MAX   8192
#define MS_REP_MAX  8192

static char   *ms_rt = NULL;          /* refresh token (PSRAM) */
static char   *ms_at = NULL;          /* access token (PSRAM) */
static int64_t ms_at_exp = 0;         /* expiration, en microsecondes */
static int64_t ms_prochain = 0;       /* prochaine interrogation */
static bool    ms_annonce = false;    /* deja dit a voix haute */
/* la sonnerie du rendez-vous : 30 min avant, elle tourne tant qu'on n'a pas
   appuye sur le bouton 1 (ou au bout de 5 min, pour ne pas sonner dans le vide) */
#define ALARME_MIN  30
static bool    ms_alarme = false;

static bool ms_lit_rt(void)
{
    FILE *f = fopen(MS_RT_PATH, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 40 || n >= MS_RT_MAX) { fclose(f); ESP_LOGW(TAG, "ms : jeton illisible (%ld o)", n); return false; }
    if (!ms_rt) ms_rt = (char *)heap_caps_malloc(MS_RT_MAX, MALLOC_CAP_SPIRAM);
    if (!ms_rt) { fclose(f); return false; }
    size_t r = fread(ms_rt, 1, (size_t)n, f);
    fclose(f);
    ms_rt[r] = 0;
    while (r && (ms_rt[r - 1] == '\n' || ms_rt[r - 1] == '\r')) ms_rt[--r] = 0;
    return r >= 40;
}

static void ms_ecrit_rt(const char *t)
{
    if (!t || !t[0]) return;
    if (!ms_rt) ms_rt = (char *)heap_caps_malloc(MS_RT_MAX, MALLOC_CAP_SPIRAM);
    if (ms_rt) snprintf(ms_rt, MS_RT_MAX, "%s", t);
    FILE *f = fopen(MS_RT_PATH, "w");
    if (!f) { ESP_LOGW(TAG, "ms : jeton non enregistre (SPIFFS)"); return; }
    fputs(t, f);
    fclose(f);
}

/* POST application/x-www-form-urlencoded, reponse lue en entier */
static int ms_post(const char *url, const char *corps, char *rep, size_t repsz,
                   const char *entete, const char *entete_val)
{
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 1024;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    if (entete) esp_http_client_set_header(c, entete, entete_val);
    int len = (int)strlen(corps);
    int status = -1;
    if (esp_http_client_open(c, len) == ESP_OK) {
        if (http_ecrit(c, corps, len)) {
            esp_http_client_fetch_headers(c);
            status = esp_http_client_get_status_code(c);
            int n = 0, r;
            while (n < (int)repsz - 1 &&
                   (r = esp_http_client_read(c, rep + n, repsz - 1 - n)) > 0)
                n += r;
            rep[n] = 0;
            net_tx_octets += (uint32_t)len;
            net_rx_octets += (uint32_t)n;
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return status;
}

/* Renouvelle le jeton d'acces. Microsoft renvoie AUSSI un nouveau refresh
   token : il faut l'enregistrer, l'ancien est invalide. */
static bool ms_rafraichit(void)
{
    if (!ms_rt || !ms_rt[0]) return false;
    char *corps = (char *)heap_caps_malloc(MS_RT_MAX + 512, MALLOC_CAP_SPIRAM);
    char *rep = (char *)heap_caps_malloc(MS_REP_MAX, MALLOC_CAP_SPIRAM);
    if (!corps || !rep) { free(corps); free(rep); return false; }
    snprintf(corps, MS_RT_MAX + 512,
             "grant_type=refresh_token&client_id=" MS_CLIENT
             "&scope=offline_access%%20Calendars.Read&refresh_token=%s", ms_rt);
    int st = ms_post("https://login.microsoftonline.com/common/oauth2/v2.0/token",
                     corps, rep, MS_REP_MAX,
                     "Content-Type", "application/x-www-form-urlencoded");
    free(corps);
    bool ok = false;
    cJSON *j = (st == 200) ? cJSON_Parse(rep) : NULL;
    if (j) {
        cJSON *at = cJSON_GetObjectItem(j, "access_token");
        cJSON *rt = cJSON_GetObjectItem(j, "refresh_token");
        cJSON *ex = cJSON_GetObjectItem(j, "expires_in");
        if (cJSON_IsString(at)) {
            if (!ms_at) ms_at = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
            if (ms_at && snprintf(ms_at, 4096, "%s", at->valuestring) > 0) ok = true;
            ms_at_exp = esp_timer_get_time() + (int64_t)(cJSON_IsNumber(ex) ? ex->valueint : 3600) * 1000000LL;
            if (cJSON_IsString(rt)) {
                if (strcmp(rt->valuestring, ms_rt) != 0) {
                    ms_ecrit_rt(rt->valuestring);
                    ESP_LOGI(TAG, "ms : nouveau jeton de rafraichissement enregistre");
                }
            }
        } else {
            ESP_LOGW(TAG, "ms : renouvellement refuse : %s",
                     cJSON_IsString(cJSON_GetObjectItem(j, "error")) ?
                     cJSON_GetObjectItem(j, "error")->valuestring : "?");
        }
        cJSON_Delete(j);
    } else {
        ESP_LOGW(TAG, "ms : renouvellement KO (HTTP %d) %.120s", st, rep);
    }
    free(rep);
    return ok;
}

static long ms_epoch(const char *iso)   /* "2026-10-18T14:30:00.0000000" */
{
    int Y, M, D, h, m, s = 0;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 5) return 0;
    struct tm t = {};
    t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
    t.tm_hour = h; t.tm_min = m; t.tm_sec = s; t.tm_isdst = -1;
    return (long)mktime(&t);
}

static void ms_interroge(void)
{
    time_t now = time(NULL);
    struct tm t0, t1;
    localtime_r(&now, &t0);
    time_t dans12h = now + 12 * 3600;
    localtime_r(&dans12h, &t1);
    char url[512];
    snprintf(url, sizeof(url),
             "https://graph.microsoft.com/v1.0/me/calendarView?startDateTime="
             "%04d-%02d-%02dT%02d:%02d:00&endDateTime=%04d-%02d-%02dT%02d:%02d:00"
             "&$select=subject,start&$top=4&$orderby=start/dateTime",
             t0.tm_year + 1900, t0.tm_mon + 1, t0.tm_mday, t0.tm_hour, t0.tm_min,
             t1.tm_year + 1900, t1.tm_mon + 1, t1.tm_mday, t1.tm_hour, t1.tm_min);
    char *rep = (char *)heap_caps_malloc(MS_REP_MAX, MALLOC_CAP_SPIRAM);
    if (!rep) return;
    char auth[2048];
    snprintf(auth, sizeof(auth), "Bearer %s", ms_at ? ms_at : "");
    int st = ms_post(url, "", rep, MS_REP_MAX, "Authorization", auth);
    if (st != 200) {
        ESP_LOGW(TAG, "ms : calendrier HTTP %d %.120s", st, rep);
        free(rep);
        return;
    }
    ms_dans = -1; ms_heure[0] = 0; ms_titre[0] = 0; ms_debut = 0;
    cJSON *j = cJSON_Parse(rep);
    cJSON *v = j ? cJSON_GetObjectItem(j, "value") : NULL;
    int n = cJSON_IsArray(v) ? cJSON_GetArraySize(v) : 0;
    for (int i = 0; i < n; i++) {
        cJSON *ev = cJSON_GetArrayItem(v, i);
        cJSON *suj = cJSON_GetObjectItem(ev, "subject");
        cJSON *deb = cJSON_GetObjectItem(ev, "start");
        cJSON *dt = deb ? cJSON_GetObjectItem(deb, "dateTime") : NULL;
        if (!cJSON_IsString(dt)) continue;
        long t = ms_epoch(dt->valuestring);
        if (t == 0) continue;
        long delta = (long)now - t;              /* negatif = a venir */
        if (delta > 0) continue;                 /* deja commence : on passe */
        struct tm e; time_t tt = (time_t)t; localtime_r(&tt, &e);
        snprintf(ms_heure, sizeof(ms_heure), "%02d:%02d", e.tm_hour, e.tm_min);
        snprintf(ms_titre, sizeof(ms_titre), "%s", cJSON_IsString(suj) ? suj->valuestring : "reunion");
        ms_dans = (int)((-delta) / 60);
        ms_debut = t;
        break;
    }
    if (j) cJSON_Delete(j);
    free(rep);
    ESP_LOGI(TAG, "ms : %d rendez-vous, prochain '%s' a %s dans %d min",
             n, ms_titre, ms_heure, ms_dans);
}

/* ---------- CALENDRIER PUBLIE (.ics) ----------
   Microsoft refuse le consentement aux applications tierces sur ce compte
   (client "Azure CLI" = application first party). La sortie propre, sans
   jeton ni consentement : Outlook publie le calendrier en lecture seule sur
   une adresse .ics. On la telecharge, on lit les VEVENT et on garde le
   prochain rendez-vous. Reglable par /ical?u=<lien> (enregistre en NVS). */
static char ical_url[300] = "";
static int  ical_octets = 0;              /* taille de la derniere reponse */
static int  ical_evenements = 0;          /* VEVENT vus dans la reponse */
static int  ical_dernier = 0;             /* dernier code de http_get */
static char ical_debut_reponse[48] = "";  /* debut brut, pour diagnostiquer */

static long jours_depuis_1970(int y, int m, int d)
{
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

/* decalage entre l'heure locale et UTC, en secondes (Paris : 7200 en ete).
   Calcule a partir des champs locaux : mktime n'est pas fiable ici, il
   reinterprete selon le fuseau et se trompe d'une heure au changement. */
static long decalage_local(void)
{
    time_t now = time(NULL);
    struct tm l;
    localtime_r(&now, &l);
    long jours = jours_depuis_1970(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
    long meme_heure_en_utc = jours * 86400L + l.tm_hour * 3600L + l.tm_min * 60L + l.tm_sec;
    return meme_heure_en_utc - (long)now;
}

/* "20261018T143000" (heure locale), "20261018T123000Z" (UTC), "20261018" */
/* ---------- qui invite ? ----------
   Le .ics publie par Outlook ne contient AUCUN champ ORGANIZER : le nom de
   l'organisateur n'existe que dans le texte de l'invitation. On le retrouve
   donc dans le bloc de l'evenement, avec les deux tournures rencontrees :
     "Organisateur\\nJeanne Lesbordes<mailto:...>"          (Google)
     "Moussa BEN BALLA<mailto:...>- organisateur"          (Outlook)
     "... Jeanne Lesbordes vous invite a une reunion Zoom" (Zoom)
   A defaut, l'ecran garde le titre de l'evenement. */
static const char *trouve_mot(const char *b, const char *fin, const char *mot)
{
    size_t l = strlen(mot);
    for (const char *q = b; q + l <= fin; q++)
        if (!memcmp(q, mot, l)) return q;
    return NULL;
}

static bool nom_plausible(const char *s)
{
    int n = 0, esp = 0;
    if (s[0] < 'A' || s[0] > 'Z') return false;    /* "je vous invite" -> rejete */
    for (; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        if (c < 0x20 || c == '@' || c == '<' || c == '>' || c == ':' || c == '/') return false;
        if (c == ' ') esp++;
    }
    return n >= 4 && n <= 40 && esp >= 1;      /* un prenom + un nom */
}

static void copie_nom(const char *b, const char *e, char *out, size_t n)
{
    while (b < e && (*b == ' ' || *b == '\\' || *b == ':' || *b == '-' || *b == '.' ||
                     *b == ',' || *b == '>')) b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '-' || e[-1] == '.' || e[-1] == ',' ||
                     e[-1] == '(')) e--;
    size_t l = (size_t)(e - b);
    if (l >= n) l = n - 1;
    memcpy(out, b, l);
    out[l] = 0;
}

static bool nom_invitant(const char *b, const char *fin, char *out, size_t n)
{
    out[0] = 0;
    const char *o = trouve_mot(b, fin, "rganisateur");     /* Organisateur / organisateur */
    if (o) {
        const char *lt = NULL;                              /* nom juste avant <mailto: */
        for (const char *q = o; q > b && q > o - 140; q--)
            if (q[-1] == '<') { lt = q - 1; break; }
        if (lt) {
            const char *s = lt;
            while (s > b && lt - s < 50 && s[-1] != '>' && s[-1] != '\n' && s[-1] != '\r'
                   && s[-1] != ',' && s[-1] != '\\' && (unsigned char)s[-1] < 0x80
                   && !(s[-1] == 'n' && s > b + 1 && s[-2] == '\\'))
                s--;
            copie_nom(s, lt, out, n);
            if (nom_plausible(out)) return true;
        }
        const char *s = o + 11;                             /* nom juste apres */
        while (s < fin && (*s == ':' || *s == '\\' || *s == 'n' || *s == 'N' ||
                           *s == ' ' || *s == '\r' || *s == '\n')) s++;
        if (s < fin) {
            const char *e = s;
            while (e < fin && e - s < 40 && *e != '<' && *e != '\\' && *e != '\r'
                   && *e != '\n' && *e != ':' && (unsigned char)*e < 0x80) e++;
            copie_nom(s, e, out, n);
            if (nom_plausible(out)) return true;
        }
    }
    const char *v = trouve_mot(b, fin, "vous invite");
    if (v) {
        const char *s = v;
        while (s > b && v - s < 60 && s[-1] != '\n' && s[-1] != '\r' && s[-1] != '\\'
               && s[-1] != '>' && s[-1] != '<' && s[-1] != ':' && s[-1] != ','
               && (unsigned char)s[-1] < 0x80)
            s--;
        copie_nom(s, v, out, n);
        if (nom_plausible(out)) return true;
    }
    return false;
}

static long ical_epoch_tz(const char *v, bool utc)
{
    int Y, M, D, h = 0, mi = 0, s = 0;
    if (sscanf(v, "%4d%2d%2dT%2d%2d%2d", &Y, &M, &D, &h, &mi, &s) >= 3) {
        if (M < 1 || M > 12 || D < 1 || D > 31) return 0;
        long j = jours_depuis_1970(Y, M, D);
        long e = j * 86400L + h * 3600L + mi * 60L + s;   /* champs lus comme UTC */
        /* TZID=UTC/GMT : l'heure est deja en UTC ; sinon (Romance Standard
           Time = Paris) c'est de l'heure locale */
        if (!utc && !strchr(v, 'Z')) e -= decalage_local();
        return e;
    }
    return 0;
}

static long ical_epoch(const char *v) { return ical_epoch_tz(v, false); }

/* premiere occurrence a venir d'un evenement, boucle simple DAILY/WEEKLY */
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

static void ical_nettoie(char *s)      /* \\, \\n, \\; et fins de ligne */
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

static void ical_interroge(void)
{
    const int cap = 96 * 1024;
    ical_octets = 0;
    ical_evenements = 0;
    ical_debut_reponse[0] = 0;
    char *buf = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) return;
    int r = http_get(ical_url, "Mino/1.0", (uint8_t *)buf, cap - 1);
    ical_dernier = r;
    if (r <= 0) {
        ESP_LOGW(TAG, "ical : telechargement KO (%d)", r);
        free(buf);
        return;
    }
    buf[r] = 0;
    ical_octets = r;
    snprintf(ical_debut_reponse, sizeof(ical_debut_reponse), "%.40s", buf);
    for (const char *q = buf; (q = strstr(q, "BEGIN:VEVENT")) != NULL; q += 12)
        ical_evenements++;
    if (!strstr(buf, "BEGIN:VCALENDAR")) {
        ESP_LOGW(TAG, "ical : reponse inattendue %.80s", buf);
        free(buf);
        return;
    }
    long now = (long)time(NULL);
    long meilleur = 0;
    char meilleur_titre[64] = "";
    char meilleur_org[48] = "";

    /* lignes repliees : une continuation commence par espace ou tabulation */
    char ligne[512];
    int  ll = 0;
    bool dans = false;
    long debut = 0;
    char titre[128] = "";
    char rrule[192] = "";
    const char *p = buf;
    while (*p) {
        const char *fin = strchr(p, '\n');
        int n = fin ? (int)(fin - p) : (int)strlen(p);
        if (n > 0 && (p[0] == ' ' || p[0] == '\t')) {
            if (ll + n - 1 < (int)sizeof(ligne) - 1) {      /* repli : on colle */
                memcpy(ligne + ll, p + 1, (size_t)n - 1);
                ll += n - 1;
                ligne[ll] = 0;
            }
        } else {
            /* ligne complete : on la traite */
            if (ll > 0) {
                if (!strncmp(ligne, "BEGIN:VEVENT", 12)) {
                    dans = true; debut = 0; titre[0] = 0; rrule[0] = 0;
                } else if (!strncmp(ligne, "END:VEVENT", 10) && dans) {
                    long occ = ical_occurrence(debut, rrule, now);
                    if (occ > 0 && (meilleur == 0 || occ < meilleur)) {
                        meilleur = occ;
                        snprintf(meilleur_titre, sizeof(meilleur_titre), "%s",
                                 titre[0] ? titre : "reunion");
                        /* le bloc complet, pour y chercher qui invite */
                        const char *bd = buf;
                        for (const char *q = p; q > buf; q--)
                            if (q - buf >= 12 && !memcmp(q, "BEGIN:VEVENT", 12)) { bd = q; break; }
                        if (!nom_invitant(bd, p, meilleur_org, sizeof(meilleur_org)))
                            meilleur_org[0] = 0;
                    }
                    dans = false;
                } else if (dans && !strncmp(ligne, "DTSTART", 7)) {
                    char *c = strchr(ligne, ':');
                    if (c) {
                        bool utc = strstr(ligne, "TZID=UTC") || strstr(ligne, "TZID=GMT");
                        debut = ical_epoch_tz(c + 1, utc);
                    }
                } else if (dans && !strncmp(ligne, "SUMMARY", 7)) {
                    char *c = strchr(ligne, ':');
                    if (c) { snprintf(titre, sizeof(titre), "%s", c + 1); ical_nettoie(titre); }
                } else if (dans && !strncmp(ligne, "RRULE", 5)) {
                    char *c = strchr(ligne, ':');
                    if (c) snprintf(rrule, sizeof(rrule), "%s", c + 1);
                }
            }
            n = 0;
            while (p[n] && p[n] != '\n' && p[n] != '\r' && n < (int)sizeof(ligne) - 1) {
                ligne[n] = p[n];
                n++;
            }
            ligne[n] = 0;
            ll = n;
        }
        p = fin ? fin + 1 : p + strlen(p);
    }
    free(buf);

    ms_dans = -1; ms_heure[0] = 0; ms_titre[0] = 0; ms_debut = 0; ms_org[0] = 0;
    if (meilleur > 0) {
        struct tm e;
        time_t tt = (time_t)meilleur;
        localtime_r(&tt, &e);
        snprintf(ms_heure, sizeof(ms_heure), "%02d:%02d", e.tm_hour, e.tm_min);
        snprintf(ms_titre, sizeof(ms_titre), "%s", meilleur_titre);
        snprintf(ms_org, sizeof(ms_org), "%s", meilleur_org);
        ms_dans = (int)((meilleur - now) / 60);
        ms_debut = meilleur;
        if (ms_dans < 0) ms_dans = 0;
    }
    ESP_LOGI(TAG, "ical : prochain '%s' (invite par '%s') a %s dans %d min",
             ms_titre, ms_org, ms_heure, ms_dans);
}

static int http_get(const char *url, const char *ua, uint8_t *buf, int cap);

static void ical_charge(void)
{
    nvs_handle_t nh;
    size_t l = sizeof(ical_url);
    ical_url[0] = 0;
    if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
        if (nvs_get_str(nh, "ical", ical_url, &l) != ESP_OK) ical_url[0] = 0;
        nvs_close(nh);
    }
}

static volatile bool ical_demande = false;   /* /ical?u= : lire tout de suite */
static volatile bool cal_occupe = false;     /* une lecture est en cours */
static TaskHandle_t  cal_tache_h = NULL;

/* La lecture du calendrier se fait dans SA tache : dans le gestionnaire HTTP
   elle bloquait le serveur (et l'ecran) pendant 20 s, et la pile httpd de
   12 Ko ne suffit pas au TLS. Pile en PSRAM, il ne reste presque plus de
   RAM interne. */
static void cal_travail(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        cal_occupe = true;
        if (ical_url[0]) {
            ical_interroge();
        } else if (ms_rt && ms_rt[0]) {
            int64_t now = esp_timer_get_time();
            if (!ms_at || now > ms_at_exp - 120LL * 1000000) {
                if (ms_rafraichit()) ms_interroge();
            } else {
                ms_interroge();
            }
        }
        cal_occupe = false;
        show_ui();          /* le rendez-vous a peut-etre change : on redessine */
    }
}

static void cal_demarre(void)
{
    if (cal_tache_h) return;
    if (xTaskCreateWithCaps(cal_travail, "cal", 16384, NULL, 2, &cal_tache_h,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        cal_tache_h = NULL;
        ESP_LOGW(TAG, "cal : tache non creee (memoire)");
    }
}

static void ms_boucle(void)
{
    static int64_t dernier = 0;
    static bool init = false;
    if (!init) {
        init = true;
        ical_charge();
        if (ical_url[0]) {
            ESP_LOGI(TAG, "ical : calendrier publie configure");
        } else if (!ms_lit_rt()) {
            ESP_LOGI(TAG, "ms : pas de jeton calendrier");
            return;
        } else {
            ESP_LOGI(TAG, "ms : jeton calendrier present (%d o)", (int)strlen(ms_rt));
        }
        cal_demarre();
    }
    if (!ical_url[0] && (!ms_rt || !ms_rt[0])) return;
    if (!(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) return;
    int64_t now = esp_timer_get_time();
    if (ical_demande) {                     /* demande explicite : sans attendre */
        ical_demande = false;
        ms_prochain = 0;
        dernier = now;
    } else {
        if (dernier && now - dernier < 60LL * 1000000) return;   /* une passe par minute */
        dernier = now;
    }
    /* la sonnerie se verifie a chaque passe (une par minute) et non seulement
       quand le calendrier est relu : sinon elle pouvait partir 5 min trop tard */
    if (ms_dans >= 0 && ms_dans <= ALARME_MIN && !ms_alarme && ms_debut > 0) {
        ms_alarme = true;
        alarme_t0 = esp_timer_get_time();
        alarme_on = true;
        show_ui();
        ESP_LOGI(TAG, "alarme : rendez-vous dans %d min (%s)", ms_dans,
                 ms_org[0] ? ms_org : ms_titre);
    }
    /* on rearme des que le rendez-vous sort de la fenetre, sinon la sonnerie
       reste bloquee apres la premiere reunion (ms_dans vaut -1) */
    if (ms_dans < 0 || ms_dans > ALARME_MIN) ms_alarme = false;

    if (ms_prochain && now < ms_prochain) return;
    ms_prochain = now + 300LL * 1000000;                     /* toutes les 5 min */
    if (cal_tache_h && !cal_occupe) xTaskNotifyGive(cal_tache_h);
}

/* Enregistre un jeton de rafraichissement Microsoft (POST, corps = jeton).
   Le jeton fait ~1,5 Ko : trop long pour une query string. */
static esp_err_t h_caltoken(httpd_req_t *req)
{
    char *buf = (char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!buf) { httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"memoire\"}"); return ESP_OK; }
    int n = httpd_req_recv(req, buf, 4095);
    if (n <= 0) { free(buf); httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"corps vide\"}"); return ESP_OK; }
    buf[n] = 0;
    char *s = buf;
    while (*s == ' ' || *s == '\n' || *s == '\r') s++;
    size_t l = strlen(s);
    while (l && (s[l-1] == '\n' || s[l-1] == '\r' || s[l-1] == ' ')) s[--l] = 0;
    bool ok = (l >= 40);
    if (ok) {
        ms_ecrit_rt(s);
        ms_at = NULL; ms_at_exp = 0; ms_prochain = 0; ms_annonce = false;
        ESP_LOGI(TAG, "ms : jeton installe par HTTP (%d o)", (int)l);
    }
    char rep[96];
    snprintf(rep, sizeof(rep), "{\"ok\":%s,\"longueur\":%d}", ok ? "true" : "false", (int)l);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rep);
    free(buf);
    return ESP_OK;
}

/* /ical?u=<lien .ics> : enregistre le calendrier publie et l'interroge tout
   de suite.   /ical?off=1 : l'oublie.   /ical : etat. */
static esp_err_t h_ical(httpd_req_t *req)
{
    char v[400] = "";
    if (qval(req, "off", v, sizeof(v)) == ESP_OK && v[0] == '1') {
        ical_url[0] = 0;
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_erase_key(nh, "ical");
            nvs_commit(nh);
            nvs_close(nh);
        }
        ms_dans = -1; ms_heure[0] = 0; ms_titre[0] = 0; ms_debut = 0;
        httpd_resp_sendstr(req, "{\"ok\":true,\"configure\":false}");
        return ESP_OK;
    }
    if (qval(req, "u", v, sizeof(v)) == ESP_OK && v[0]) {
        snprintf(ical_url, sizeof(ical_url), "%s", v);
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "ical", ical_url);
            nvs_commit(nh);
            nvs_close(nh);
        }
        ms_prochain = 0;
        ms_annonce = false;
        ical_demande = true;                 /* la tache cal lit tout de suite */
        if (cal_tache_h) xTaskNotifyGive(cal_tache_h);
        char rep[400];
        snprintf(rep, sizeof(rep),
                 "{\"ok\":true,\"enregistre\":true,\"titre\":\"%s\",\"heure\":\"%s\",\"dans\":%d}",
                 ms_titre, ms_heure, ms_dans);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, rep);
        ESP_LOGI(TAG, "ical : lien enregistre (%d o)", (int)strlen(ical_url));
        return ESP_OK;
    }
    char rep[640];
    char debut[48];
    int k = 0;
    for (const char *p = ical_debut_reponse; *p && k < 40; p++)
        debut[k++] = (*p == '\r' || *p == '\n' || *p == '"') ? '.' : *p;
    debut[k] = 0;
    snprintf(rep, sizeof(rep),
             "{\"configure\":%s,\"url\":\"%.280s\",\"octets\":%d,\"vevent\":%d,"
             "\"dernier\":%d,\"tache\":%s,\"occupe\":%s,\"debut\":\"%s\"}",
             ical_url[0] ? "true" : "false", ical_url, ical_octets, ical_evenements,
             ical_dernier, cal_tache_h ? "true" : "false", cal_occupe ? "true" : "false",
             debut);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rep);
    return ESP_OK;
}

/* ---------- MISE A JOUR PAR WIFI ----------
   POST /maj avec le binaire mino.bin brut dans le corps (pas de multipart).
   L'image s'ecrit dans l'autre emplacement OTA : si elle est valide la carte
   redemarre dessus, sinon l'ancienne reste en place et redemarre toute seule.
   C'est ce qui permet de mettre a jour sans brancher le cable USB. */
static esp_err_t h_maj(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"aucun emplacement ota\"}");
        return ESP_OK;
    }
    if (req->content_len <= 20000) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"corps trop petit\"}");
        return ESP_OK;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!buf) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"memoire\"}");
        return ESP_OK;
    }
    esp_ota_handle_t ota = 0;
    esp_err_t e = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota);
    if (e != ESP_OK) {
        free(buf);
        ESP_LOGW(TAG, "maj : begin KO (%s)", esp_err_to_name(e));
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"begin\"}");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "maj : %d octets vers %s", (int)req->content_len, part->label);
    int reste = (int)req->content_len, ecrits = 0;
    while (reste > 0) {
        int n = httpd_req_recv(req, (char *)buf, reste > 8192 ? 8192 : reste);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            esp_ota_abort(ota);
            free(buf);
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"lecture\"}");
            return ESP_OK;
        }
        e = esp_ota_write(ota, buf, (size_t)n);
        if (e != ESP_OK) {
            esp_ota_abort(ota);
            free(buf);
            ESP_LOGW(TAG, "maj : ecriture KO (%s) a %d", esp_err_to_name(e), ecrits);
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"ecriture\"}");
            return ESP_OK;
        }
        ecrits += n;
        reste -= n;
    }
    free(buf);
    e = esp_ota_end(ota);
    if (e != ESP_OK) {
        char r[120];
        snprintf(r, sizeof(r), "{\"ok\":false,\"err\":\"image invalide (%s)\"}", esp_err_to_name(e));
        httpd_resp_sendstr(req, r);
        return ESP_OK;
    }
    e = esp_ota_set_boot_partition(part);
    if (e != ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"demarrage\"}");
        return ESP_OK;
    }
    char rep[128];
    snprintf(rep, sizeof(rep), "{\"ok\":true,\"octets\":%d,\"emplacement\":\"%s\",\"redemarrage\":1}",
             ecrits, part->label);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rep);
    ESP_LOGW(TAG, "maj : image acceptee (%d octets), redemarrage", ecrits);
    vTaskDelay(pdMS_TO_TICKS(900));
    esp_restart();
    return ESP_OK;
}

static esp_err_t h_dit(httpd_req_t *req)
{
    char txt[300] = "";
    if (qval(req, "t", txt, sizeof(txt)) != ESP_OK || strlen(txt) < 1)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "texte");
    char dit[800] = "", emo[32] = "neutre";
    esp_err_t e = ds_cerveau(txt, dit, sizeof(dit), emo, sizeof(emo));
    if (e == ESP_OK) { paroles_ajoute("MOI", txt); paroles_ajoute("MINO", dit); voix_dit(dit); }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", e == ESP_OK);
    cJSON_AddStringToObject(r, "dit", dit);
    cJSON_AddStringToObject(r, "emotion", emo);
    cJSON_AddStringToObject(r, "err", esp_err_to_name(e));
    return send_json(req, r);
}

/* enregistre la cle a chaud : /cle?k=gsk_xxx (elle survit au redemarrage) */

static esp_err_t h_ecran(httpd_req_t *req)
{
    char v[16] = "";
    qval(req, "q", v, sizeof(v));
    /* ?q=parle&b=0.8 : on force l'etat "elle parle" et l'ouverture de la bouche,
       pour juger le dessin du visage sans lancer de voix */
    if (!strcmp(v, "parle")) {
        char vb[16] = "0.7";
        qval(req, "b", vb, sizeof(vb));
        int sauv_etat = voix_etat;
        float sauv_b = voix_bouche;
        voix_etat = 3;
        voix_bouche = (float)atof(vb);
        show_ui();
        voix_etat = sauv_etat;
        voix_bouche = sauv_b;
    }
    char raw[8] = "";
    if (qval(req, "raw", raw, sizeof(raw)) == ESP_OK && raw[0] == '1') {
        /* le framebuffer brut en hexadecimal : 5000 octets = 10000 caracteres,
           pour regarder l'ecran en PNG sur le PC au lieu de l'ASCII 2x2 */
        static const char hx[] = "0123456789abcdef";
        char *o = (char *)malloc(FB_LEN * 2 + 1);
        if (!o) { httpd_resp_sendstr(req, "pas de memoire"); return ESP_ERR_NO_MEM; }
        for (int i = 0; i < FB_LEN; i++) {
            o[i * 2] = hx[fb[i] >> 4];
            o[i * 2 + 1] = hx[fb[i] & 15];
        }
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, o, FB_LEN * 2);
        free(o);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/plain");
    char ligne[112];
    for (int y = 0; y <= 199; y += 2) {
        int c = 0;
        for (int x = 0; x < 200; x += 2) {
            int noir = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int yy = y + dy, xx = x + dx;
                    int i = yy * 25 + (xx >> 3);
                    if (!((fb[i] >> (7 - (xx & 7))) & 1)) noir++;
                }
            ligne[c++] = (noir >= 3) ? '#' : (noir == 2 ? '+' : (noir == 1 ? '.' : ' '));
        }
        ligne[c++] = '\n';
        httpd_resp_send_chunk(req, ligne, c);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


/* enregistre la cle a chaud : /cle?k=gsk_xxx (elle survit au redemarrage) */
static esp_err_t h_cle(httpd_req_t *req)
{
    char k[128] = "";
    char rep[96];
    if (qval(req, "k", k, sizeof(k)) != ESP_OK || strlen(k) < 20) {
        snprintf(rep, sizeof(rep), "{\"ok\":false,\"err\":\"cle trop courte\"}");
    } else {
        snprintf(ia_cle, sizeof(ia_cle), "%s", k);
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "groq", ia_cle);
            nvs_commit(nh);
            nvs_close(nh);
        }
        ESP_LOGI(TAG, "cle IA enregistree : %d caracteres", (int)strlen(ia_cle));
        snprintf(rep, sizeof(rep), "{\"ok\":true,\"longueur\":%d}", (int)strlen(ia_cle));
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rep);
    return ESP_OK;
}

/* ecrit tout, meme si le client HTTP n'accepte qu'une partie a la fois */
static bool http_ecrit(esp_http_client_handle_t c, const void *p, int n)
{
    const char *q = (const char *)p;
    while (n > 0) {
        int w = esp_http_client_write(c, q, n);
        if (w <= 0) return false;
        q += w; n -= w;
    }
    return true;
}

/* ---- reconnaissance vocale : Groq, multipart ---- */
static esp_err_t stt_groq(const uint8_t *wav, int wav_len, char *out, size_t outsz)
{
    out[0] = 0;
    if (strlen(ia_key()) < 20) { snprintf(out, outsz, "(pas de cle Groq)"); return ESP_ERR_INVALID_STATE; }

    const char *B = "----MinoVoix42";
    char h[600], ct[80], f[64];
    int hl = snprintf(h, sizeof(h),
        "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" STT_MODEL "\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nfr\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voix.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n", B, B, B, B);
    int fl = snprintf(f, sizeof(f), "\r\n--%s--\r\n", B);
    snprintf(ct, sizeof(ct), "multipart/form-data; boundary=%s", B);

    esp_http_client_config_t cfg = {};
        cfg.max_redirection_count = 4;   /* FG Radio et autres : suivre les 302 */
    cfg.url = STT_URL;
    cfg.method = HTTP_METHOD_POST;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 2048;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    static char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", ia_key());
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", ct);

    esp_err_t err = ESP_FAIL;
    if (esp_http_client_open(c, hl + wav_len + fl) == ESP_OK) {
        if (http_ecrit(c, h, hl) && http_ecrit(c, wav, wav_len) && http_ecrit(c, f, fl)) {
            esp_http_client_fetch_headers(c);
            int status = esp_http_client_get_status_code(c);
            static char rep[4096];
            int n = 0, r;
            while (n < (int)sizeof(rep) - 1 &&
                   (r = esp_http_client_read(c, rep + n, sizeof(rep) - 1 - n)) > 0)
                n += r;
            rep[n] = 0;
            net_tx_octets += (uint32_t)(hl + wav_len + fl);
            net_rx_octets += (uint32_t)n;
            if (status != 200) {
                ESP_LOGW(TAG, "Groq STT HTTP %d : %.160s", status, rep);
            } else {
                cJSON *j = cJSON_Parse(rep);
                cJSON *t = j ? cJSON_GetObjectItem(j, "text") : NULL;
                if (cJSON_IsString(t)) { snprintf(out, outsz, "%s", t->valuestring); err = ESP_OK; }
                if (j) cJSON_Delete(j);
            }
        }
    } else {
        ESP_LOGW(TAG, "Groq STT injoignable");
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

/* ---- GET simple (TTS, meteo) ---- */
/* ---- reconnaissance par defaut : AUCUNE cle a creer ----
   C'est l'API vocale cachee de Chrome : on lui envoie du PCM brut 16 bits
   16 kHz mono, elle renvoie le texte. Gratuite, sans inscription, sans carte.
   Elle repond DEUX objets JSON colles : le premier vide, le second plein. */
#define STT_SANS_CLE_DEBUT \
    "https://www.google.com/speech-api/v2/recognize?output=json&lang="
#define STT_SANS_CLE_CLE \
    "&key=AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw"

static void stt_sans_cle_parse(const char *rep, char *out, size_t outsz)
{
    const char *p = rep;
    while (p && *p) {
        const char *fin = NULL;
        cJSON *j = cJSON_ParseWithOpts(p, &fin, 0);
        if (!j) break;
        cJSON *res = cJSON_GetObjectItem(j, "result");
        if (cJSON_IsArray(res) && cJSON_GetArraySize(res) > 0) {
            cJSON *un = cJSON_GetArrayItem(res, 0);
            cJSON *alt = un ? cJSON_GetObjectItem(un, "alternative") : NULL;
            cJSON *prem = (alt && cJSON_GetArraySize(alt) > 0) ? cJSON_GetArrayItem(alt, 0) : NULL;
            cJSON *tr = prem ? cJSON_GetObjectItem(prem, "transcript") : NULL;
            if (cJSON_IsString(tr)) snprintf(out, outsz, "%s", tr->valuestring);
        }
        cJSON_Delete(j);
        p = fin;
        while (p && (*p == '\n' || *p == '\r' || *p == ' ')) p++;
    }
}

static esp_err_t stt_sans_cle(const uint8_t *pcm, int len, char *out, size_t outsz)
{
    out[0] = 0;
    char url[180];
    snprintf(url, sizeof(url), "%s%s%s", STT_SANS_CLE_DEBUT, langue, STT_SANS_CLE_CLE);
    esp_http_client_config_t cfg = {};
        cfg.max_redirection_count = 4;   /* FG Radio et autres : suivre les 302 */
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_http_client_set_header(c, "Content-Type", "audio/l16; rate=16000");
    esp_http_client_set_header(c, "User-Agent", TTS_UA);
    esp_err_t err = ESP_FAIL;
    if (esp_http_client_open(c, len) == ESP_OK) {
        int w = 0;
        while (w < len) {
            int k = esp_http_client_write(c, (const char *)pcm + w, len - w);
            if (k <= 0) break;
            w += k;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        static char rep[4096];
        int n = 0, r;
        while (n < (int)sizeof(rep) - 1 &&
               (r = esp_http_client_read(c, rep + n, sizeof(rep) - 1 - n)) > 0)
            n += r;
        rep[n] = 0;
        stt_http = status;
        stt_ecrits = w;
        stt_total = len;
        {
            size_t k = 0;
            for (int i = 0; rep[i] && k < sizeof(stt_detail) - 1; i++)
                if (rep[i] != '\n' && rep[i] != '\r') stt_detail[k++] = rep[i];
            stt_detail[k] = 0;
        }
        if (status == 200 && n > 0) {
            stt_sans_cle_parse(rep, out, outsz);
            if (out[0]) err = ESP_OK;
            else ESP_LOGW(TAG, "reco sans cle : rien compris (%.120s)", rep);
        } else {
            ESP_LOGW(TAG, "reco sans cle : HTTP %d", status);
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

/* Groq fait mieux sur les noms propres... mais il faut une cle. */
static bool groq_pret(void)
{
    /* Seule une vraie cle Groq (gsk_...) declenche la reconnaissance de Groq.
       La cle du cerveau sert a parler au LLM et ne doit PAS detourner la
       reconnaissance : c'est ce melange qui faisait repondre "je n'ai pas bien
       compris" a chaque phrase alors que tout le reste marchait. */
    const char *k = ia_key();
    return (strncmp(k, "gsk_", 4) == 0);
}

static int http_get_ex(const char *url, const char *ua, const char *cookie,
                       uint8_t *buf, int cap)
{
    esp_http_client_config_t cfg = {};
        cfg.max_redirection_count = 4;   /* FG Radio et autres : suivre les 302 */
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 4096;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    if (ua) esp_http_client_set_header(c, "User-Agent", ua);
    /* YouTube renvoie un 302 vers consent.youtube.com sans ce cookie (UE) */
    if (cookie) esp_http_client_set_header(c, "Cookie", cookie);
    int res = -1;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        int64_t annonce = esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        int o = 0, r, creux = 0;
        int64_t limite = esp_timer_get_time() + 30LL * 1000000;   /* 30 s max, quoi qu'il arrive */
        /* Un read() qui renvoie 0 au milieu du flux (Outlook met parfois une
           seconde a fabriquer la fin d'un gros .ics) coupait le telechargement
           a ~2 Ko. On ne conclut plus au premier trou : on attend que le client
           dise lui-meme "tout recu" (is_complete_data_received), avec une
           patience de 8 s, et seulement alors on s'arrete. */
        while (o < cap) {
            r = esp_http_client_read(c, (char *)buf + o, cap - o);
            if (r > 0) {
                o += r;
                creux = 0;
                continue;
            }
            /* r < 0 n'est pas forcement une panne : esp-tls remonte
               MBEDTLS_ERR_SSL_WANT_READ (-0x7100) en ESP_FAIL quand la
               socket non bloquante n'a pas encore de donnees. On reessaie
               au lieu de couper le telechargement (c'etait la cause du .ics
               tronque a ~2 Ko). */
            if (esp_http_client_is_complete_data_received(c)) break;
            if (annonce > 0 && o >= (int)annonce) break;
            if (esp_timer_get_time() > limite) { ESP_LOGW(TAG, "http : delai depasse a %d o", o); break; }
            if (++creux > 400) break;              /* 400 x 20 ms = 8 s */
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (annonce > 0 && o < (int)annonce)
            ESP_LOGW(TAG, "http : reponse incomplete %d/%d o", o, (int)annonce);
        if (annonce > cap)
            ESP_LOGW(TAG, "http : tampon trop petit (%d o annonces, %d dispo)", (int)annonce, cap);
        res = (status == 200) ? o : -status;
        if (res > 0) net_rx_octets += (uint32_t)res;
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return res;
}

/* la version simple, sans cookie : tout le reste du firmware passe par la */
static int http_get(const char *url, const char *ua, uint8_t *buf, int cap)
{
    return http_get_ex(url, ua, NULL, buf, cap);
}

/* ---- L'ASTUCE DU JOUR : le top du jour de r/Twitch, sinon r/Kick ---------
   Texte brut de Reddit (JSON), une seule lecture par jour, gardee en NVS.
   On choisit l'entree "numero du jour % n" : une astuce differente chaque
   jour, sans repasser par le reseau. */
static void astuce_sauve(void)
{
    nvs_handle_t h;
    if (nvs_open("mino", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ast_txt", astuce_txt);
    nvs_set_str(h, "ast_src", astuce_src);
    nvs_set_str(h, "ast_quand", astuce_quand);
    nvs_set_i32(h, "ast_jour", astuce_jour);
    nvs_commit(h);
    nvs_close(h);
}

static void astuce_charge(void)
{
    nvs_handle_t h;
    if (nvs_open("mino", NVS_READONLY, &h) != ESP_OK) return;
    size_t n = sizeof(astuce_txt);
    if (nvs_get_str(h, "ast_txt", astuce_txt, &n) != ESP_OK) astuce_txt[0] = 0;
    n = sizeof(astuce_src);  nvs_get_str(h, "ast_src", astuce_src, &n);
    n = sizeof(astuce_quand); nvs_get_str(h, "ast_quand", astuce_quand, &n);
    {
        int32_t j = -1;
        if (nvs_get_i32(h, "ast_jour", &j) == ESP_OK) astuce_jour = (int)j;
    }
    nvs_close(h);
    astuce_dispo = astuce_txt[0] != 0;
    if (astuce_dispo)
        ESP_LOGI(TAG, "astuce : '%s' (%s, %s)", astuce_txt, astuce_src, astuce_quand);
}

static const char *AST_MOIS[12] = { "JAN", "FEV", "MAR", "AVR", "MAI", "JUN",
                                    "JUL", "AOU", "SEP", "OCT", "NOV", "DEC" };

static bool astuce_lire(void)
{
    if (!(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) { astuce_occupe = false; return false; }
    astuce_essai_ms = esp_timer_get_time() / 1000;
    char *buf = (char *)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
    if (!buf) { ESP_LOGW(TAG, "astuce : plus de PSRAM"); astuce_occupe = false; return false; }

    /* Reddit sert le JSON en 403 a un client qui n'est pas un navigateur, mais
       son flux RSS passe : on lit les titres des entrees du jour. */
    static const char *URLS[2] = { ASTUCE_URL1, ASTUCE_URL2 };
    bool fait = false;
    for (int u = 0; u < 2 && !fait; u++) {
        int r = http_get(URLS[u], ASTUCE_UA, (uint8_t *)buf, 64 * 1024 - 1);
        if (r <= 0) { ESP_LOGW(TAG, "astuce : %s -> %d", URLS[u], r); continue; }
        buf[r] = 0;

        static char titres[12][170];
        int n = 0;
        char *p = buf;
        /* RSS classique (<item>) ou Atom (<entry>) : Reddit sert de l'Atom */
        while (n < 12) {
            char *a1 = strstr(p, "<item"), *a2 = strstr(p, "<entry");
            char *a = (!a1) ? a2 : (!a2) ? a1 : (a1 < a2 ? a1 : a2);
            if (!a) break;
            p = a + 5;
            char *t = strstr(p, "<title>");
            char *f = t ? strstr(t, "</title>") : NULL;
            if (t && f) {
                int len = (int)(f - (t + 7));
                if (len > 160) len = 160;
                memcpy(titres[n], t + 7, (size_t)len);
                titres[n][len] = 0;
                char *cd = strstr(titres[n], "<![CDATA[");
                if (cd) {
                    memmove(titres[n], cd + 9, strlen(cd + 9) + 1);
                    char *f2 = strstr(titres[n], "]]>");
                    if (f2) *f2 = 0;
                }
                infos_nettoie(titres[n]);
                if (strlen(titres[n]) > 12) n++;
            }
            if (f) p = f + 8;
        }

        if (n > 0) {
            time_t now = time(NULL);
            int jour = (int)(now / 86400);
            int k = (jour > 0 ? jour : 0) % n;      /* une autre chaque jour */
            snprintf(astuce_txt, sizeof(astuce_txt), "%s", titres[k]);
            snprintf(astuce_src, sizeof(astuce_src), "%s", u == 0 ? "R/TWITCH" : "R/KICK");
            struct tm ti;
            localtime_r(&now, &ti);
            snprintf(astuce_quand, sizeof(astuce_quand), "%d %s %02dH%02d",
                     ti.tm_mday, AST_MOIS[(ti.tm_mon >= 0 && ti.tm_mon < 12) ? ti.tm_mon : 0],
                     ti.tm_hour, ti.tm_min);
            astuce_jour = jour;
            astuce_dispo = true;
            astuce_sauve();
            ESP_LOGI(TAG, "astuce %d : '%s' (%s, %d entrees)", jour, astuce_txt, astuce_src, n);
            fait = true;
        } else {
            ESP_LOGW(TAG, "astuce : aucune entree dans %s (%d o)", URLS[u], r);
        }
    }
    free(buf);
    astuce_occupe = false;
    redraw_demande = true;
    return fait;
}

/* va chercher les titres du moment et les met en forme pour l'ecran */
static void infos_lire(void)
{
    /* idem meteo : sans reseau, on sort au lieu de bloquer la tache ui */
    if (!(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) return;
    if (!infos_xml) {
        infos_xml = (char *)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
        if (!infos_xml) {
            ESP_LOGW(TAG, "infos : plus de PSRAM");
            return;
        }
    }
    int r = http_get(INFOS_URL, TTS_UA, (uint8_t *)infos_xml, 64 * 1024 - 1);
    infos_octets = r;
    if (r <= 0) {
        ESP_LOGW(TAG, "infos : lecture impossible (%d)", r);
        return;
    }
    infos_xml[r] = 0;
    int n = 0;
    char *p = infos_xml;
    while (n < INFOS_MAX && (p = strstr(p, "<title>")) != NULL) {
        char *d = p + 7;
        char *f = strstr(d, "</title>");
        if (!f) break;
        int len = (int)(f - d);
        if (len > 150) len = 150;
        char t[170];
        memcpy(t, d, (size_t)len);
        t[len] = 0;
        char *cd = strstr(t, "<![CDATA[");
        if (cd) {
            memmove(t, cd + 9, strlen(cd + 9) + 1);
            char *fin = strstr(t, "]]>");
            if (fin) *fin = 0;
        }
        infos_nettoie(t);
        /* le premier titre du flux, c'est le nom de la radio elle-meme */
        char deb[12] = "";
        for (int k = 0; k < 11 && t[k]; k++)
            deb[k] = (t[k] >= 'A' && t[k] <= 'Z') ? (char)(t[k] + 32) : t[k];
        if (strlen(t) > 12 && strncmp(deb, "franceinfo", 10) != 0 &&
            strncmp(deb, "france info", 11) != 0) {
            snprintf(infos_titre[n], sizeof(infos_titre[n]), "%.128s", t);
            n++;
        }
        p = f + 8;
    }
    if (n > 0) {
        infos_n = n;
        infos_ok = true;
        infos_haut = 0;
    }
    ESP_LOGI(TAG, "infos France : %d titres (1er : %.60s)", infos_n, infos_n ? infos_titre[0] : "");
}


static void url_encode(const char *s, char *out, size_t sz)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < sz; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else if (c == ' ') { out[o++] = '%'; out[o++] = '2'; out[o++] = '0'; }
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

/* ---- haut-parleur : PCM mono 16 kHz -> stereo 16 bits ---- */
static void hp_mono(const int16_t *mono, int n)
{
    static uint8_t out[2048];
    int i = 0;
    while (i < n) {
        int k = n - i;
        if (k > 512) k = 512;
        for (int j = 0; j < k; j++) {
            out[j * 4]     = (uint8_t)(mono[i + j] & 0xff);
            out[j * 4 + 1] = (uint8_t)((mono[i + j] >> 8) & 0xff);
            out[j * 4 + 2] = out[j * 4];
            out[j * 4 + 3] = out[j * 4 + 1];
        }
        audio_playback_write(out, k * 4);
        i += k;
    }
}

/* ---- un mp3 en memoire -> haut-parleur, au fil de la lecture ---- */
static int16_t mp3_brut[2 * 1152];

/* une trame decodee -> reechantillonnee a 16 kHz -> haut-parleur */
static void mp3_sortie(MP3FrameInfo fi, int *sr, double *pos)
{
    static int16_t mo[1152];
    static int16_t pcm16[1200];
    int frames = fi.outputSamps / fi.nChans;
    if (frames > 1152) frames = 1152;
    if (frames <= 0) return;
    for (int i = 0; i < frames; i++)
        mo[i] = (fi.nChans == 2)
              ? (int16_t)(((int)mp3_brut[i * 2] + (int)mp3_brut[i * 2 + 1]) / 2)
              : mp3_brut[i];
    if (*sr != fi.samprate) {
        *sr = fi.samprate;
        *pos = 0.0;
        ESP_LOGI(TAG, "mp3 : %d Hz, %d canal(aux), %d kbps", fi.samprate, fi.nChans, fi.bitrate / 1000);
    }
    double pas = (double)fi.samprate / 16000.0;   /* le codec est ouvert a 16 kHz */
    int o = 0;
    while (*pos < (double)frames && o < 1200) {
        int i0 = (int)*pos, i1 = (i0 + 1 < frames) ? i0 + 1 : i0;
        double fr = *pos - (double)i0;
        pcm16[o++] = (int16_t)((double)mo[i0] * (1.0 - fr) + (double)mo[i1] * fr);
        *pos += pas;
    }
    *pos -= (double)frames;
    if (o) { voix_feed(pcm16, o); hp_mono(pcm16, o); }
}

/* decode tout ce qui est lisible dans le tampon ; renvoie les octets consommes.
   Ce qui reste a la fin est une trame incomplete : on la garde pour la suite. */
static int mp3_flux(HMP3Decoder dec, uint8_t *p0, int n, int *sr, double *pos)
{
    uint8_t *p = p0;
    int reste = n;
    while (reste > 64) {
        int sync = MP3FindSyncWord(p, reste);
        if (sync < 0) { p += reste; reste = 0; break; }
        if (sync > 0) { p += sync; reste -= sync; }
        uint8_t *sauv = p;
        int sreste = reste;
        int e = MP3Decode(dec, &p, &reste, mp3_brut, 0);
        if (e == ERR_MP3_INDATA_UNDERFLOW) { p = sauv; reste = sreste; break; }
        if (e != ERR_MP3_NONE) { p++; reste--; continue; }
        MP3FrameInfo fi;
        MP3GetLastFrameInfo(dec, &fi);
        if (fi.nChans >= 1 && fi.samprate > 0) mp3_sortie(fi, sr, pos);
    }
    return (int)(p - p0);
}

/* la voix de Google : un mp3 complet, d'un coup */
static void mp3_joue(const uint8_t *data, int len)
{
    HMP3Decoder dec = MP3InitDecoder();
    if (!dec) return;
    int sr = 0;
    double pos = 0.0;
    audio_playback_set_vol(90);
    mp3_flux(dec, (uint8_t *)data, len, &sr, &pos);
    MP3FreeDecoder(dec);
}

/* ==================== la radio en wifi, sans PC ==================== */
static void radio_wifi_task(void *arg)
{
    uint8_t *tampon = (uint8_t *)heap_caps_malloc(20000, MALLOC_CAP_SPIRAM);
    HMP3Decoder dec = MP3InitDecoder();
    esp_http_client_handle_t c = NULL;
    int ma_gen = -1, n = 0, sr = 0, inutile = 0;
    double pos = 0.0;

    if (!tampon || !dec) {
        if (tampon) free(tampon);
        if (dec) MP3FreeDecoder(dec);
        radio_etat = 0;
        radio_vive = false;
        vTaskDelete(NULL);
        return;
    }

    while (radio_etat != 0) {
        if (c == NULL || ma_gen != radio_gen) {          /* (re)connexion */
            if (c) { esp_http_client_close(c); esp_http_client_cleanup(c); c = NULL; }
            ma_gen = radio_gen;
            n = 0; sr = 0; pos = 0.0; inutile = 0;
            radio_etat = 1;
            ESP_LOGI(TAG, "radio : je me connecte a %s", radios_url[radio_sel]);
            esp_http_client_config_t cfg = {};
        cfg.max_redirection_count = 4;   /* FG Radio et autres : suivre les 302 */
            cfg.url = radios_url[radio_sel];
            cfg.timeout_ms = 7000;
            cfg.buffer_size = 4096;
            cfg.crt_bundle_attach = esp_crt_bundle_attach;
            c = esp_http_client_init(&cfg);
            if (c) {
                esp_http_client_set_header(c, "User-Agent", TTS_UA);
                esp_http_client_set_header(c, "Icy-MetaData", "0");
            }
            if (!c || esp_http_client_open(c, 0) != ESP_OK) {
                ESP_LOGW(TAG, "radio : station injoignable, nouvelle tentative");
                if (c) { esp_http_client_cleanup(c); c = NULL; }
                vTaskDelay(pdMS_TO_TICKS(2500));
                continue;
            }
            esp_http_client_fetch_headers(c);
            ESP_LOGI(TAG, "radio : HTTP %d, je joue", esp_http_client_get_status_code(c));
            audio_playback_set_vol((uint8_t)radio_vol);
            radio_etat = 2;
            show_ui();
            continue;
        }

        if (n > 16000) n = 0;                            /* tampon sature : on jette */
        int r = esp_http_client_read(c, (char *)tampon + n, 20000 - n);
        if (r <= 0) {                                    /* flux coupe */
            ESP_LOGW(TAG, "radio : flux interrompu (%d)", r);
            if (c) { esp_http_client_close(c); esp_http_client_cleanup(c); c = NULL; }
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }
        n += r;
        int consomme = mp3_flux(dec, tampon, n, &sr, &pos);
        if (consomme > 0) {
            inutile = 0;
            memmove(tampon, tampon + consomme, (size_t)(n - consomme));
            n -= consomme;
        } else {
            inutile += r;
            if (inutile > 400000) {                      /* ni mp3 : sans doute de l'aac */
                ESP_LOGW(TAG, "radio : %s n'est pas du mp3 (aac ?), j'arrete",
                         radios_url[radio_sel]);
                radio_etat = 0;
            }
        }
    }

    if (c) { esp_http_client_close(c); esp_http_client_cleanup(c); }
    free(tampon);
    MP3FreeDecoder(dec);
    radio_vive = false;
    ESP_LOGI(TAG, "radio : arret");
    vTaskDelete(NULL);
}

/* ---- la phrase -> la voix de Google (gratuit, sans cle) ---- */
/* l'ancien nom, une requete = 190 caracteres */

/* ---- le TTS, en avance : pendant qu'un morceau se joue, le suivant se
   telecharge. C'est ce qui fait toute la difference sur la latence percue. ---- */

#define TTS_MORCEAU_MAX (160 * 1024)

typedef struct {
    char txt[210];
    uint8_t *mp3;
    int len;
    volatile bool fini;
} tts_lot;

static int tts_mp3_fetch(const char *texte, uint8_t *mp3, int cap)
{
    char t[210];
    snprintf(t, sizeof(t), "%.190s", texte);
    if (strlen(texte) > 190) {
        char *sp = strrchr(t, ' ');
        if (sp && sp > t + 40) *sp = 0;
    }
    char *enc = (char *)heap_caps_malloc(800, MALLOC_CAP_SPIRAM);
    char *url = (char *)heap_caps_malloc(1000, MALLOC_CAP_SPIRAM);
    if (!enc || !url) { if (enc) free(enc); if (url) free(url); return -1; }
    url_encode(t, enc, 800);
    snprintf(url, 1000, "%s%s&tl=%s", TTS_DEBUT, enc, langue);
    int64_t t0 = esp_timer_get_time();
    int r = http_get(url, TTS_UA, mp3, cap);
    if (r > 0)
        ESP_LOGI(TAG, "voix : %d octets mp3 en %.0f ms", r, (esp_timer_get_time() - t0) / 1000.0);
    else
        ESP_LOGW(TAG, "voix : le TTS de Google a repondu %d", r);
    free(enc); free(url);
    return r;
}

/* telecharge un morceau. ATTENTION : ne jamais appeler tts_lot_tache()
   directement depuis une tache qui doit continuer : elle fait vTaskDelete(NULL)
   et le handler HTTP qui l'appelait mourait sans jamais repondre (le socket
   restait ouvert -> /state muet apres deux appels). */
static void tts_lot_charge(tts_lot *L)
{
    L->len = tts_mp3_fetch(L->txt, L->mp3, TTS_MORCEAU_MAX);
    L->fini = true;
}

static void tts_lot_tache(void *arg)
{
    tts_lot_charge((tts_lot *)arg);
    vTaskDelete(NULL);
}

/* attend qu'un morceau soit pret, sans jamais bloquer indefiniment */
static bool tts_attendre(tts_lot *L)
{
    int64_t t0 = esp_timer_get_time();
    while (!L->fini) {
        if ((esp_timer_get_time() - t0) > 15000000) {
            ESP_LOGW(TAG, "voix : morceau perdu (15 s)");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

static tts_lot *tts_lance(tts_lot *L)
{
    if (!L) return NULL;
    L->fini = false; L->len = -1;
    TaskHandle_t th = NULL;
    if (xTaskCreateWithCaps(tts_lot_tache, "tts_lot", 12288, L, 5, &th,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGW(TAG, "voix : tache de telechargement non creee, on le fait ici");
        tts_lot_charge(L);         /* au pire, on telecharge sur place */
    }
    return L;
}

static void tts_parle(const char *texte)
{
    if (!texte || !*texte) return;
    /* voix sortante coupee : le texte s'affiche a l'ecran, rien ne sort du
       haut-parleur. /voix?o=1 la rallume si besoin. */
    if (!voix_sortie) {
        voix_dit_nb++;                       /* compteur : la phrase a bien ete produite */
        return;
    }
    if (radio_etat != 0) { radio_stop(); vTaskDelay(pdMS_TO_TICKS(120)); }

    tts_lot *lot = (tts_lot *)heap_caps_calloc(2, sizeof(tts_lot), MALLOC_CAP_SPIRAM);
    if (!lot) { ESP_LOGW(TAG, "voix : plus de PSRAM pour le TTS"); return; }
    bool ok = false;
    for (int i = 0; i < 2; i++) {
        lot[i].mp3 = (uint8_t *)heap_caps_malloc(TTS_MORCEAU_MAX, MALLOC_CAP_SPIRAM);
        if (lot[i].mp3) ok = true;
    }
    if (!ok) {
        ESP_LOGW(TAG, "voix : pas de tampon mp3");
        free(lot);
        return;
    }

    /* on decoupe : le premier morceau est court, pour que la voix demarre vite */
    const char *p = texte;
    int cur = 0;
    tts_lot *prec = NULL;

    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        size_t n = strlen(p);
        size_t cible = prec ? 180 : 90;
        size_t k = (n < cible) ? n : cible;
        if (k < n) {
            size_t cut = 0;
            for (size_t i = k; i > 30; i--) {
                char c = p[i - 1];
                if (c == '.' || c == '!' || c == '?' || c == ';' || c == ':') { cut = i; break; }
            }
            if (!cut)
                for (size_t i = k; i > 30; i--)
                    if (p[i - 1] == ' ') { cut = i; break; }
            if (cut) k = cut;
        }
        tts_lot *L = &lot[cur];
        if (L->fini) { free(L->mp3); L->mp3 = NULL; }   /* deja joue */
        if (!L->mp3) L->mp3 = (uint8_t *)heap_caps_malloc(TTS_MORCEAU_MAX, MALLOC_CAP_SPIRAM);
        if (!L->mp3) break;
        snprintf(L->txt, sizeof(L->txt), "%.*s", (int)k, p);
        L->fini = false; L->len = -1;

        if (!prec) {
            /* premier morceau : on attend son mp3 (on n'a rien a jouer) */
            tts_lot_charge(L);
        } else {
            /* on lance le telechargement du suivant AVANT de jouer celui-ci */
            tts_lance(L);
        }

        /* on joue le morceau precedent, qui est pret */
        if (prec) {
            if (tts_attendre(prec) && prec->len > 0) mp3_joue(prec->mp3, prec->len);
        }
        prec = L;
        cur = 1 - cur;
        p += k;
    }

    if (prec) {
        if (tts_attendre(prec) && prec->len > 0) mp3_joue(prec->mp3, prec->len);
    }
    for (int i = 0; i < 2; i++) if (lot[i].mp3) free(lot[i].mp3);
    free(lot);
}

/* ---- l'assistant : texte entendu -> cerveau -> voix ---- */
static void voix_dit(const char *phrase)
{
    snprintf(voix_reponse, sizeof(voix_reponse), "%.200s", phrase);
    rep_ouvre(phrase);               /* les pages plein ecran, avant tout le reste */
    voix_etat = 3;
    show_ui();
    tts_parle(phrase);
    voix_etat = 0;          /* sans ca le compagnon reste "en train de parler" */
    show_ui();
}

/* Le petit micro de la carte sort un signal faible : on remonte le niveau avant
   d'envoyer, sinon le service de reconnaissance n'entend rien. */
static void voix_normalise(uint8_t *pcm, int len)
{
    int16_t *s = (int16_t *)pcm;
    int n = len / 2, pk = 0;
    for (int i = 0; i < n; i++) {
        int v = s[i];
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    mic_crete = pk;
    if (pk < 40) return;                     /* vraiment rien : on n'amplifie pas le souffle */
    float g = 22937.0f / (float)pk;          /* on vise 70 % de la dynamique */
    if (g > 25.0f)  g = 25.0f;               /* au-dela, on amplifie surtout le bruit */
    if (g > 0.95f && g < 1.05f) return;      /* deja au bon niveau */
    for (int i = 0; i < n; i++) {
        float v = (float)s[i] * g;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        s[i] = (int16_t)v;
    }
    ESP_LOGI(TAG, "micro : crete %d, niveau ajuste x%.2f", pk, (double)g);
}

static void voix_tour(const uint8_t *pcm, int len)
{
    int64_t t0 = esp_timer_get_time();

    /* 1. reconnaissance */
    if (len < 3200) {                    /* moins de 0,1 s : rien de bon */
        voix_dit("Je n'ai rien entendu, tu peux repeter ?");
        return;
    }
    /* on ne garde que la partie ou l'on parle : moins d'octets a envoyer,
       et surtout une reconnaissance beaucoup plus rapide (elle grandit avec
       la duree de l'audio). */
    const int16_t *e16 = (const int16_t *)pcm;
    int total_ech = len / 2, d = 0, f = total_ech;
    for (int i = 0; i < total_ech; i++)
        if (e16[i] > 300 || e16[i] < -300) { d = i; break; }
    for (int i = total_ech - 1; i > d; i--)
        if (e16[i] > 300 || e16[i] < -300) { f = i; break; }
    d = (d > 1600) ? d - 1600 : 0;                     /* 100 ms de marge */
    f = (f + 1600 < total_ech) ? f + 1600 : total_ech;
    if (f - d < 3200) { d = 0; f = total_ech; }        /* trop court : on garde tout */
    const uint8_t *parle_pcm = pcm + d * 2;
    int parle_len = (f - d) * 2;

    voix_normalise((uint8_t *)parle_pcm, parle_len);   /* le micro est faible : on remonte */
    ESP_LOGI(TAG, "voix : %d octets captes, %d utiles apres rognage", len, parle_len);
    uint8_t *wav = (uint8_t *)heap_caps_malloc(parle_len + 44, MALLOC_CAP_SPIRAM);
    if (!wav) { voix_dit("Je suis fatigue, redemande-moi."); return; }
    wav_entete(wav, parle_len);
    memcpy(wav + 44, parle_pcm, parle_len);

    char texte[600] = "";
    voix_etat = 2;                       /* JE REFLECHIS */
    show_ui();
    esp_err_t e;
    if (groq_pret()) {
        ESP_LOGI(TAG, "voix : avant STT, interne libre %u o, plus gros bloc %u o",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        e = stt_groq(wav, parle_len + 44, texte, sizeof(texte));
    } else {
        /* pas de cle : on passe le PCM brut a la reconnaissance de Chrome */
        e = stt_sans_cle(parle_pcm, parle_len, texte, sizeof(texte));
    }
    free(wav);
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "entendu en %.0f ms : \"%.120s\"", (t1 - t0) / 1000.0, texte);
    if (e != ESP_OK || strlen(texte) < 2) {
        ESP_LOGW(TAG, "reconnaissance muette (err=%s) : %.60s", esp_err_to_name(e), texte);
        voix_dit("Je n'ai pas bien compris, tu peux repeter ?");
        return;
    }
    chrono_stt_ms = (int)((t1 - t0) / 1000);
    snprintf(voix_entendu_txt, sizeof(voix_entendu_txt), "%.250s", texte);
    paroles_ajoute("MOI", texte);

    /* 2. cerveau */
    char dit[800] = "", emo[32] = "neutre";
    if (ds_cerveau(texte, dit, sizeof(dit), emo, sizeof(emo)) != ESP_OK) {
        ESP_LOGW(TAG, "cerveau muet (reseau ?)");
        voix_dit("Je n'arrive pas a reflechir la, on reessaie ?");
        return;
    }
    int64_t t2 = esp_timer_get_time();
    chrono_llm_ms = (int)((t2 - t1) / 1000);
    int ei = emo_by_name(emo);
    if (ei < 0) ei = art_expr_de_famille(8, 0);
    current_emotion = EXPR[ei].famille;
    bat5_du_pet = famille_vers_bat5(current_emotion);
    ESP_LOGI(TAG, "dit en %.0f ms : [%s] %.120s", (t2 - t1) / 1000.0, emo, dit);
    paroles_ajoute("MINO", dit);

    /* 3. la voix */
    voix_dit(dit);
    chrono_ms = (int)((esp_timer_get_time() - t0) / 1000);
    chrono_tts_ms = chrono_ms - chrono_stt_ms - chrono_llm_ms;
    ESP_LOGI(TAG, "chrono voix : ecoute+reco %.1f s, cerveau %.1f s, total %.1f s",
             (t1 - t0) / 1000000.0, (t2 - t1) / 1000000.0, (esp_timer_get_time() - t0) / 1000000.0);
}

/* ---- meteo (Open-Meteo) : remise a jour par ui_task ---- */
static bool meteo_lire(void)
{
    /* pas de reseau : on ne tente pas un TLS de 20 s, la tache ui resterait
       bloquee et l'ecran (et les boutons) avec elle */
    if (!(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) return false;
    /* le tampon de l'appli METEO n'existe qu'avec la page METEO ; la meteo est
       aussi lue au demarrage (sans page) : on l'alloue alors a la demande */
    if (!meteo_buf) {
        meteo_buf = (char *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
        if (!meteo_buf) meteo_buf = (char *)malloc(2048);
        meteo_buf_taille = meteo_buf ? 2048 : 0;
        if (!meteo_buf) { ESP_LOGW(TAG, "meteo : pas de tampon"); return false; }
    }
    char *buf = meteo_buf;
    int r = http_get(METEO_URL, NULL, (uint8_t *)buf, (int)meteo_buf_taille - 1);
    if (r <= 0) { ESP_LOGW(TAG, "meteo injoignable (%d)", r); return false; }
    buf[r] = 0;
    cJSON *j = cJSON_Parse(buf);
    cJSON *cur = j ? cJSON_GetObjectItem(j, "current") : NULL;
    cJSON *t = cur ? cJSON_GetObjectItem(cur, "temperature_2m") : NULL;
    cJSON *w = cur ? cJSON_GetObjectItem(cur, "weather_code") : NULL;
    bool ok = false;
    if (cJSON_IsNumber(t) && cJSON_IsNumber(w)) {
        meteo_temp = (float)t->valuedouble;
        meteo_code = (int)w->valuedouble;
        meteo_ok = true;
        ok = true;
        ESP_LOGI(TAG, "meteo %s : %.1f C, code %d", METEO_VILLE, (double)meteo_temp, meteo_code);
    }
    /* demain (index 1 des tableaux daily) */
    cJSON *daily = j ? cJSON_GetObjectItem(j, "daily") : NULL;
    if (daily) {
        cJSON *dc = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *dx = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *dn = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *dp = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        if (cJSON_GetArraySize(dc) > 1 && cJSON_GetArraySize(dx) > 1 && cJSON_GetArraySize(dn) > 1) {
            cJSON *i0 = cJSON_GetArrayItem(dc, 1);
            cJSON *i1 = cJSON_GetArrayItem(dx, 1);
            cJSON *i2 = cJSON_GetArrayItem(dn, 1);
            cJSON *i3 = cJSON_GetArrayItem(dp, 1);
            if (cJSON_IsNumber(i0) && cJSON_IsNumber(i1) && cJSON_IsNumber(i2)) {
                meteo_dem_code = (int)i0->valuedouble;
                meteo_dem_max = (float)i1->valuedouble;
                meteo_dem_min = (float)i2->valuedouble;
                meteo_dem_pluie = cJSON_IsNumber(i3) ? (int)i3->valuedouble : -1;
                ESP_LOGI(TAG, "meteo demain : %.0f-%.0f C, code %d, pluie %d%%",
                         (double)meteo_dem_min, (double)meteo_dem_max, meteo_dem_code, meteo_dem_pluie);
            }
        }
    }
    if (j) cJSON_Delete(j);
    return ok;
}

/* ---- appli YOUTUBE : resolution de la chaine, puis lecture du flux RSS ----
   Aucune cle d'API : on resout @pseudo -> UC... en lisant la page de la chaine,
   puis on lit le flux RSS public (titre de la derniere video, vues, date). */
static void yt_sauve(void)
{
    nvs_handle_t h;
    if (nvs_open("mino", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "yt_h", yt_handle);
    nvs_set_str(h, "yt_id", yt_id);
    nvs_commit(h);
    nvs_close(h);
}

static void yt_charge(void)
{
    nvs_handle_t h;
    size_t n;
    if (nvs_open("mino", NVS_READONLY, &h) != ESP_OK) return;
    n = sizeof(yt_handle);
    if (nvs_get_str(h, "yt_h", yt_handle, &n) != ESP_OK) yt_handle[0] = 0;
    n = sizeof(yt_id);
    if (nvs_get_str(h, "yt_id", yt_id, &n) != ESP_OK) yt_id[0] = 0;
    nvs_close(h);
    ESP_LOGI(TAG, "youtube : chaine enregistree '%s' (%s)", yt_handle, yt_id);
}


/* le flux videos.xml de YouTube est mort (404) : on lit la page de la chaine,
   avec le cookie qui evite le renvoi vers consent.youtube.com. */

/* valeur d'une chaine JSON placee juste apres `marque` */
static bool yt_valeur(const char *src, const char *marque, char *out, size_t sz)
{
    const char *p = strstr(src, marque);
    if (!p) return false;
    p += strlen(marque);
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < sz) {
        if (*p == '\\') {                 /* \" \\ \u00a0 : on saute l'echappement */
            p++;
            if (*p == 'u') { p += 4; out[n++] = ' '; continue; }
            if (*p == 'n') { out[n++] = ' '; p++; continue; }
            if (!*p) break;
        }
        /* espace insecable UTF-8 (0xC2 0xA0) des nombres de vues */
        if ((unsigned char)*p == 0xC2 && (unsigned char)p[1] == 0xA0) {
            p += 2; out[n++] = ' '; continue;
        }
        out[n++] = *p++;
    }
    out[n] = 0;
    /* les espaces insecables des nombres de vues */
    for (size_t i = 0; i < n; i++) if ((unsigned char)out[i] == 0xC2) out[i] = ' ';
    return n > 0;
}

/* le dessin est en majuscules sans accents : on met le titre a plat avant de
   le garder (sinon les octets UTF-8 des accents sortent en symboles bizarres et
   la coupe des lignes se fait au milieu d'un mot) */
static void yt_nettoie(char *s)
{
    static const struct { const char *de; char vers; } TAB[] = {
        { "\xC3\xA9", 'E' }, { "\xC3\xA8", 'E' }, { "\xC3\xAA", 'E' }, { "\xC3\xAB", 'E' },
        { "\xC3\xA0", 'A' }, { "\xC3\xA2", 'A' }, { "\xC3\xA4", 'A' },
        { "\xC3\xA7", 'C' }, { "\xC3\xB4", 'O' }, { "\xC3\xB6", 'O' },
        { "\xC3\xB9", 'U' }, { "\xC3\xBB", 'U' }, { "\xC3\xAE", 'I' }, { "\xC3\xAF", 'I' },
        { "\xC3\xB1", 'N' }, { "\xE2\x80\x99", '\'' }, { "\xE2\x80\xA6", '.' },
        { "\xC2\xA0", ' ' }, { "\xC2\xAB", '"' }, { "\xC2\xBB", '"' },
    };
    char out[128]; size_t n = 0;
    for (const char *p = s; *p && n + 1 < sizeof(out); ) {
        bool fait = false;
        if ((unsigned char)*p >= 0x80) {
            for (size_t i = 0; i < sizeof(TAB) / sizeof(TAB[0]); i++) {
                size_t l = strlen(TAB[i].de);
                if (strncmp(p, TAB[i].de, l) == 0) { out[n++] = TAB[i].vers; p += l; fait = true; break; }
            }
            if (!fait) {                 /* accent inconnu : on saute la sequence */
                p++;
                while (((unsigned char)*p & 0xC0) == 0x80) p++;
            }
            continue;
        }
        char c = *p++;
        if (c == '@' || c == '#' || c == '|') continue;   /* pas de glyphe */
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[n++] = c;
    }
    out[n] = 0;
    char *d = out, *q = out;
    int esp = 0;
    while (*d == ' ') d++;
    for (; *d; d++) {
        if (*d == ' ') { if (esp) continue; esp = 1; }
        else esp = 0;
        *q++ = *d;
    }
    while (q > out && q[-1] == ' ') q--;
    *q = 0;
    size_t lon = strlen(out);
    if (lon > 95) lon = 95;
    memcpy(s, out, lon);
    s[lon] = 0;
}

static void yt_lire_corps(void)
{
    if (!(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) return;
    if (!yt_buf) {
        yt_buf = (char *)heap_caps_malloc(YT_TAILLE, MALLOC_CAP_SPIRAM);
        yt_buf_taille = yt_buf ? YT_TAILLE : 0;
        if (!yt_buf) { ESP_LOGW(TAG, "youtube : pas de PSRAM"); return; }
    }
    if (!yt_handle[0] && !yt_id[0]) return;
    char url[180];
    if (yt_handle[0])
        snprintf(url, sizeof(url), "https://www.youtube.com/%s", yt_handle);
    else
        snprintf(url, sizeof(url), "https://www.youtube.com/channel/%s", yt_id);

    int r = http_get_ex(url, YT_UA, YT_COOKIE, (uint8_t *)yt_buf, (int)yt_buf_taille - 1);
    if (r <= 0) { ESP_LOGW(TAG, "youtube : page injoignable (%d)", r); return; }
    yt_buf[r] = 0;
    ESP_LOGI(TAG, "youtube : page lue (%d o)", r);

    /* 1. l'identifiant de la chaine (garde en NVS pour les fois suivantes) */
    if (!yt_id[0]) {
        const char *p = strstr(yt_buf, "youtube.com/channel/UC");
        if (p) p += strlen("youtube.com/channel/");
        if (p) {
            int i = 0;
            while (i < 24 && p[i] && p[i] != '"' && p[i] != '&') { yt_id[i] = p[i]; i++; }
            yt_id[i] = 0;
            ESP_LOGI(TAG, "youtube : %s -> %s", yt_handle, yt_id);
            yt_sauve();
        }
    }
    /* 2. le nom de la chaine */
    yt_valeur(yt_buf, "property=\"og:title\" content=\"", yt_nom, sizeof(yt_nom));
    if (!yt_nom[0]) snprintf(yt_nom, sizeof(yt_nom), "%s", yt_handle);

    /* 3. la video mise en avant (la plus recente sur la page de la chaine) */
    const char *bloc = strstr(yt_buf, "channelVideoPlayerRenderer");
    if (!bloc) bloc = yt_buf;
    yt_titre[0] = 0;
    yt_valeur(bloc, "\"title\":{\"runs\":[{\"text\":\"", yt_titre, sizeof(yt_titre));
    if (!yt_titre[0])
        yt_valeur(bloc, "\"title\":{\"simpleText\":\"", yt_titre, sizeof(yt_titre));
    if (!yt_titre[0]) { ESP_LOGW(TAG, "youtube : titre de video introuvable"); return; }

    /* 4. les vues et l'age, tels que YouTube les ecrit */
    char vu[48] = "";
    yt_vues = -1;
    if (yt_valeur(bloc, "\"viewCountText\":{\"simpleText\":\"", vu, sizeof(vu))) {
        char chiffres[16]; int k = 0;
        for (int i = 0; vu[i] && k < 15; i++) if (vu[i] >= '0' && vu[i] <= '9') chiffres[k++] = vu[i];
        chiffres[k] = 0;
        if (k) yt_vues = atoi(chiffres);
    }
    yt_age[0] = 0;
    yt_valeur(bloc, "\"publishedTimeText\":{\"runs\":[{\"text\":\"", yt_age, sizeof(yt_age));

    yt_ok = true;
    yt_nettoie(yt_titre);
    yt_nettoie(yt_nom);
    yt_nettoie(yt_age);
    ESP_LOGI(TAG, "youtube %s : '%s' (%d vues, %s)", yt_nom, yt_titre, yt_vues, yt_age);
}

/* l'enveloppe : une seule lecture a la fois, et le tampon n'est libere qu'une
   fois la lecture finie (sinon use-after-free dans l'allocateur -> reboot) */
static void yt_lire(void)
{
    if (yt_en_cours) return;
    yt_en_cours = true;
    yt_lire_corps();
    yt_en_cours = false;
    if (yt_a_liberer) {
        yt_a_liberer = false;
        if (yt_buf) { free(yt_buf); yt_buf = NULL; yt_buf_taille = 0; }
    }
}

static const char *meteo_texte(int code)
{    if (code < 0)               return "meteo en attente";
    if (code == 0)              return "grand soleil";
    if (code <= 2)              return "peu nuageux";
    if (code == 3)              return "nuageux";
    if (code <= 48)             return "brouillard";
    if (code <= 57)             return "bruine";
    if (code <= 67)             return "pluie";
    if (code <= 77)             return "neige";
    if (code <= 82)             return "averses";
    if (code <= 86)             return "neige";
    return "orage";
}

/* ------------------------------------------------------------------ */
/* carte micro SD (SDMMC 1 bit : CLK 39 / CMD 41 / D0 40)              */
/* ------------------------------------------------------------------ */
static esp_err_t qval(httpd_req_t *req, const char *key, char *out, size_t sz);

static sdmmc_card_t *sd_card = NULL;

static esp_err_t sd_mount(bool format)
{
    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = format;
    mc.max_files = 5;
    mc.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;   /* 40 MHz : 2,3 -> 4,7 Mo/s bruts mesures */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK_PIN;
    slot.cmd = SD_MOSI_CMD_PIN;
    slot.d0 = SD_MISO_D0_PIN;

    esp_err_t r = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &sd_card);
    if (r == ESP_OK) {
        sd_ok = true;
        sdmmc_card_print_info(stdout, sd_card);
        uint64_t total = 0, freeb = 0;
        if (esp_vfs_fat_info("/sdcard", &total, &freeb) == ESP_OK)
            ESP_LOGI(TAG, "SD : %.0f Mo libres sur %.0f Mo", (double)freeb / 1048576.0,
                     (double)total / 1048576.0);
    } else {
        sd_ok = false;
        ESP_LOGW(TAG, "SD non montee : %s", esp_err_to_name(r));
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Repartitionnement de la carte : FAT32 (fichiers) + zone brute      */
/* (memoire de debordement). Le MBR et le FAT32 sont ecrits a la main :*/
/* f_mkfs ne sait formater qu'un disque entier, pas une partition.     */
/* ------------------------------------------------------------------ */
#include "sdmmc_cmd.h"

static uint32_t part_info[4][3];    /* {start, size, type} en secteurs */
static int      part_n = 0;

static sdmmc_card_t *sd_raw_open(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK_PIN;
    slot.cmd = SD_MOSI_CMD_PIN;
    slot.d0  = SD_MISO_D0_PIN;
    sdmmc_card_t *card = (sdmmc_card_t *)calloc(1, sizeof(sdmmc_card_t));
    if (!card) return NULL;
    if (sdmmc_card_init(&host, card) != ESP_OK) { free(card); return NULL; }
    return card;
}

static void part_lire_mbr(sdmmc_card_t *card)
{
    uint8_t mbr[512];
    part_n = 0;
    if (sdmmc_read_sectors(card, mbr, 0, 1) != ESP_OK) return;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = mbr + 446 + i * 16;
        uint32_t start = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t size  = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
        if (size) {
            part_info[part_n][0] = start;
            part_info[part_n][1] = size;
            part_info[part_n][2] = e[4];
            part_n++;
        }
    }
}

static void p16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = v >> 8; }
static void p32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

/* ecrit en zero `n` secteurs a partir de `start` ; renvoie false si erreur */
static bool part_zero(sdmmc_card_t *card, uint32_t start, uint32_t n)
{
    const uint32_t par = 32;                 /* 16 Ko par appel (SRAM interne) */
    uint8_t *z = (uint8_t *)heap_caps_aligned_alloc(512, par * 512,
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!z) return false;
    memset(z, 0, par * 512);
    bool ok = true;
    for (uint32_t s = 0; s < n && ok; s += par) {
        uint32_t k = (n - s < par) ? (n - s) : par;
        if (sdmmc_write_sectors(card, z, start + s, k) != ESP_OK) ok = false;
    }
    free(z);
    return ok;
}

/* MBR : partition 1 = FAT32 LBA (0x0C), partition 2 = brute (0xDA) */
static bool part_ecrire_mbr(sdmmc_card_t *card, uint32_t p1s, uint32_t p1n,
                            uint32_t p2s, uint32_t p2n)
{
    uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));
    uint8_t *e1 = mbr + 446, *e2 = mbr + 462;
    e1[0] = 0x00; e1[4] = 0x0C;
    p32(e1 + 8, p1s);  p32(e1 + 12, p1n);
    if (p2n) {
        e2[0] = 0x00; e2[4] = 0xDA;
        p32(e2 + 8, p2s);  p32(e2 + 12, p2n);
    }
    mbr[510] = 0x55; mbr[511] = 0xAA;
    return sdmmc_write_sectors(card, mbr, 0, 1) == ESP_OK;
}

/* FAT32 fait main dans la partition [start, start+size) */
static esp_err_t part_formater(sdmmc_card_t *card, uint32_t start, uint32_t size,
                               uint32_t *clusters_out, uint32_t *fat_sz_out)
{
    uint8_t spc = 64;                            /* 32 Ko par cluster */
    if (size < 8 * 1024 * 1024) spc = 8;
    else if (size < 64 * 1024 * 1024) spc = 1;

    const uint32_t reserved = 32;
    uint32_t clusters = (size - reserved) / spc;
    uint32_t fat_sz = (uint32_t)(((uint64_t)(clusters + 2) * 4 + 511) / 512);
    clusters = (size - reserved - 2 * fat_sz) / spc;
    fat_sz = (uint32_t)(((uint64_t)(clusters + 2) * 4 + 511) / 512);
    if (fat_sz == 0 || clusters < 1000) return ESP_ERR_INVALID_SIZE;

    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
    memcpy(bs + 3, "MSWIN4.1", 8);
    p16(bs + 11, 512);
    bs[13] = spc;
    p16(bs + 14, reserved);
    bs[16] = 2;
    p16(bs + 17, 0);
    p16(bs + 19, 0);
    bs[21] = 0xF8;
    p16(bs + 22, 0);
    p16(bs + 24, 63); p16(bs + 26, 255);
    p32(bs + 28, start);
    p32(bs + 32, size);
    p32(bs + 36, fat_sz);
    p16(bs + 40, 0);
    p16(bs + 42, 0);
    p32(bs + 44, 2);
    p16(bs + 48, 1);
    p16(bs + 50, 6);
    bs[64] = 0x80;
    bs[66] = 0x29;
    p32(bs + 67, 0x4D1A09B2);
    memcpy(bs + 71, "MINO       ", 11);
    memcpy(bs + 82, "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    if (sdmmc_write_sectors(card, bs, start, 1) != ESP_OK) return ESP_FAIL;

    uint8_t fsi[512];
    memset(fsi, 0, sizeof(fsi));
    p32(fsi + 0, 0x41615252);
    p32(fsi + 484, 0x61417272);
    p32(fsi + 488, 0xFFFFFFFF);
    p32(fsi + 492, 0xFFFFFFFF);
    p32(fsi + 508, 0xAA550000);
    if (sdmmc_write_sectors(card, fsi, start + 1, 1) != ESP_OK) return ESP_FAIL;
    if (sdmmc_write_sectors(card, bs,  start + 6, 1) != ESP_OK) return ESP_FAIL;
    if (sdmmc_write_sectors(card, fsi, start + 7, 1) != ESP_OK) return ESP_FAIL;

    /* les deux FAT a zero, puis les trois premieres entrees */
    if (!part_zero(card, start + reserved, fat_sz * 2)) return ESP_FAIL;
    uint8_t fat0[512];
    memset(fat0, 0, sizeof(fat0));
    p32(fat0 + 0, 0x0FFFFFF8);
    p32(fat0 + 4, 0x0FFFFFFF);
    p32(fat0 + 8, 0x0FFFFFFF);              /* cluster 2 = repertoire racine */
    if (sdmmc_write_sectors(card, fat0, start + reserved, 1) != ESP_OK) return ESP_FAIL;
    if (sdmmc_write_sectors(card, fat0, start + reserved + fat_sz, 1) != ESP_OK) return ESP_FAIL;

    /* repertoire racine vide */
    if (!part_zero(card, start + reserved + 2 * fat_sz, spc)) return ESP_FAIL;

    if (clusters_out) *clusters_out = clusters;
    if (fat_sz_out)   *fat_sz_out = fat_sz;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Zone d'echange (partition brute) : primitives de lecture/ecriture   */
/* en blocs de 16 Ko (meilleur couple mesure : 3,6 Mo/s ecriture,      */
/* 4,4 Mo/s lecture, contre 0,37 Mo/s via FATFS).                       */
/* L'ESP32-S3 n'a pas de MMU : ce n'est pas un swap OS, c'est une       */
/* memoire de debordement explicite (memcpy depuis/vers la PSRAM).      */
/* ------------------------------------------------------------------ */
#define SWAP_BLOC 16384

/* tampon relais alloue UNE fois : les allocations repetees de 16 Ko en RAM
   interne echouaient quand le plus gros bloc libre descendait sous 16 Ko. */
static uint8_t *swap_stage = NULL;

static uint8_t *swap_tampon(void)
{
    if (!swap_stage)
        swap_stage = (uint8_t *)heap_caps_aligned_alloc(512, SWAP_BLOC,
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!swap_stage)   /* repli : la PSRAM marche, mais bien plus lentement */
        swap_stage = (uint8_t *)heap_caps_aligned_alloc(512, SWAP_BLOC,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return swap_stage;
}

static bool swap_ecrire(sdmmc_card_t *card, uint32_t lba_base, size_t offset,
                        const void *src, size_t len)
{
    uint8_t *stage = swap_tampon();
    if (!stage) return false;
    const uint8_t *p = (const uint8_t *)src;
    bool ok = true;
    while (len && ok) {
        size_t n = len < SWAP_BLOC ? len : SWAP_BLOC;
        size_t plein = (n + 511) & ~(size_t)511;       /* secteurs entiers */
        memcpy(stage, p, n);
        if (plein > n) memset(stage + n, 0, plein - n);
        ok = sdmmc_write_sectors(card, stage, lba_base + (uint32_t)(offset / 512),
                                 plein / 512) == ESP_OK;
        offset += plein; p += n; len -= n;
    }
    return ok;
}

static bool swap_lire(sdmmc_card_t *card, uint32_t lba_base, size_t offset,
                      void *dst, size_t len)
{
    uint8_t *stage = swap_tampon();
    if (!stage) return false;
    uint8_t *p = (uint8_t *)dst;
    bool ok = true;
    while (len && ok) {
        size_t n = len < SWAP_BLOC ? len : SWAP_BLOC;
        size_t plein = (n + 511) & ~(size_t)511;
        ok = sdmmc_read_sectors(card, stage, lba_base + (uint32_t)(offset / 512),
                                plein / 512) == ESP_OK;
        if (ok) memcpy(p, stage, n);
        offset += plein; p += n; len -= n;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Historique de conversation sur la partition brute (carte SD)        */
/*                                                                     */
/* Chaque message occupe UN secteur de 512 octets :                    */
/*   [0] role (0 = MOI/Taz, 1 = MINO)  [1..2] longueur  [3..] texte    */
/* Le secteur 0 de la partition porte l'en-tete (magic, ou ecrire,     */
/* combien de messages). 4096 messages = 2 Mo, la zone en fait 24 Go.  */
/* Aucune ecriture FAT : c'est la zone brute, 10x plus rapide.         */
/* ------------------------------------------------------------------ */
#define HIST_SD_MAGIC 0x4D494E4Fu          /* "MINO" */

typedef struct {
    uint32_t magic;
    uint16_t next;                         /* index du prochain message */
    uint16_t n;                            /* messages valides (<= SLOTS) */
    uint32_t tours;                        /* compteur absolu, jamais remis a 0 */
} hist_sd_entete_t;

static void hist_sd_ecrit_entete(void)
{
    if (!hist_sd_pret || !hist_sd_sect) return;
    hist_sd_entete_t e = { HIST_SD_MAGIC, hist_sd_next, hist_sd_n, hist_sd_tours };
    memset(hist_sd_sect, 0, 512);
    memcpy(hist_sd_sect, &e, sizeof(e));
    if (sdmmc_write_sectors(sd_card, hist_sd_sect, hist_sd_base, 1) != ESP_OK) {
        hist_sd_erreurs++;
        ESP_LOGW(TAG, "historique : en-tete non ecrit");
    }
}

static void hist_sd_relit_fenetre(void)
{
    /* on remet les HIST_MAX derniers messages dans la fenetre RAM pour que le
       modele reprenne le fil apres un redemarrage */
    if (!hist_p) return;
    hist_vide(hist_p);
    if (!hist_sd_pret || hist_sd_n == 0) return;
    int garde = hist_sd_n < HIST_MAX ? hist_sd_n : HIST_MAX;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (!buf) return;
    for (int k = garde; k > 0; k--) {
        int idx = (int)hist_sd_next - k;
        while (idx < 0) idx += HIST_SD_SLOTS;
        if (sdmmc_read_sectors(sd_card, buf, hist_sd_base + 1 + (uint32_t)idx, 1) != ESP_OK) continue;
        uint16_t len = (uint16_t)(buf[1] | (buf[2] << 8));
        if (len == 0 || len > HIST_SD_TXT) continue;
        char txt[HIST_SD_TXT + 1];
        memcpy(txt, buf + 3, len);
        txt[len] = 0;
        hist_ajoute(hist_p, buf[0] ? "MINO" : "MOI", txt);
    }
    free(buf);
}

static void hist_sd_init(void)
{
    if (!sd_ok || !sd_card) {
        snprintf(hist_sd_pourquoi, sizeof(hist_sd_pourquoi), "pas de carte");
        ESP_LOGW(TAG, "historique : pas de carte, il reste en RAM");
        return;
    }
    part_lire_mbr(sd_card);
    if (part_n < 2) {
        snprintf(hist_sd_pourquoi, sizeof(hist_sd_pourquoi), "1 seule partition (%d)", part_n);
        ESP_LOGW(TAG, "historique : pas de partition brute (/part?op=repart)");
        return;
    }
    hist_sd_base = part_info[1][0];
    if (!hist_sd_mux) hist_sd_mux = (void *)xSemaphoreCreateMutex();
    hist_sd_sect = (uint8_t *)heap_caps_aligned_alloc(512, 512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!hist_sd_sect) hist_sd_sect = (uint8_t *)heap_caps_aligned_alloc(512, 512, MALLOC_CAP_SPIRAM);
    if (!hist_sd_sect) {
        snprintf(hist_sd_pourquoi, sizeof(hist_sd_pourquoi), "pas de tampon DMA");
        ESP_LOGW(TAG, "historique : pas de tampon");
        return;
    }

    hist_sd_entete_t e;
    memset(&e, 0, sizeof(e));
    if (sdmmc_read_sectors(sd_card, hist_sd_sect, hist_sd_base, 1) == ESP_OK)
        memcpy(&e, hist_sd_sect, sizeof(e));
    if (e.magic != HIST_SD_MAGIC || e.next >= HIST_SD_SLOTS || e.n > HIST_SD_SLOTS) {
        ESP_LOGW(TAG, "historique : zone brute vierge, je l'initialise");
        hist_sd_next = 0; hist_sd_n = 0; hist_sd_tours = 0;
        hist_sd_pret = true;
        hist_sd_ecrit_entete();
    } else {
        hist_sd_next = e.next; hist_sd_n = e.n; hist_sd_tours = e.tours;
        hist_sd_pret = true;
    }
    snprintf(hist_sd_pourquoi, sizeof(hist_sd_pourquoi), "ok LBA %lu",
             (unsigned long)hist_sd_base);
    hist_sd_relit_fenetre();
    ESP_LOGI(TAG, "historique : %u message(s) sur la carte (base LBA %lu), %d relus",
             (unsigned)hist_sd_n, (unsigned long)hist_sd_base, hist_p ? hist.n : 0);
}

static void hist_sd_ajoute(const char *qui, const char *texte)
{
    if (!texte || !*texte) return;
    if (hist_p) hist_ajoute(hist_p, qui, texte);   /* fenetre PSRAM : le modele la lit */
    if (!hist_sd_pret || !hist_sd_sect) return;
    if (hist_sd_mux) xSemaphoreTake((SemaphoreHandle_t)hist_sd_mux, portMAX_DELAY);
    bool mino = (qui && strcmp(qui, "MINO") == 0);
    uint16_t len = (uint16_t)strlen(texte);
    if (len > HIST_SD_TXT) len = HIST_SD_TXT;
    memset(hist_sd_sect, 0, 512);
    hist_sd_sect[0] = mino ? 1 : 0;
    hist_sd_sect[1] = (uint8_t)(len & 0xff);
    hist_sd_sect[2] = (uint8_t)(len >> 8);
    memcpy(hist_sd_sect + 3, texte, len);
    if (sdmmc_write_sectors(sd_card, hist_sd_sect, hist_sd_base + 1 + hist_sd_next, 1) != ESP_OK) {
        hist_sd_erreurs++;
        ESP_LOGW(TAG, "historique : ecriture message KO");
    } else {
        hist_sd_next = (uint16_t)((hist_sd_next + 1) % HIST_SD_SLOTS);
        if (hist_sd_n < HIST_SD_SLOTS) hist_sd_n++;
        hist_sd_tours++;
        hist_sd_ecrit_entete();
    }
    if (hist_sd_mux) xSemaphoreGive((SemaphoreHandle_t)hist_sd_mux);
}

/* relit les `max` derniers messages pour /hist (preuve et relecture) */
static int hist_sd_lire(char *out, size_t sz, int max)
{
    out[0] = 0;
    if (!hist_sd_pret || !hist_sd_sect) return 0;
    if (max <= 0 || max > hist_sd_n) max = hist_sd_n;
    if (hist_sd_mux) xSemaphoreTake((SemaphoreHandle_t)hist_sd_mux, portMAX_DELAY);
    uint8_t *buf = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_DMA);
    int n = 0;
    size_t o = 0;
    for (int k = max; k > 0 && buf && o + 300 < sz; k--) {
        int idx = (int)hist_sd_next - k;
        while (idx < 0) idx += HIST_SD_SLOTS;
        if (sdmmc_read_sectors(sd_card, buf, hist_sd_base + 1 + (uint32_t)idx, 1) != ESP_OK) continue;
        uint16_t len = (uint16_t)(buf[1] | (buf[2] << 8));
        if (len == 0 || len > HIST_SD_TXT) continue;
        char txt[HIST_SD_TXT + 1];
        memcpy(txt, buf + 3, len);
        txt[len] = 0;
        for (char *p = txt; *p; p++) if (*p == '"' || *p == '\\' || *p < 32) *p = ' ';
        int w = snprintf(out + o, sz - o, "\"t%lu\":{\"q\":\"%s\",\"m\":\"%.250s\"}\n",
                         (unsigned long)(hist_sd_tours > 0 ? hist_sd_tours - k + 1 : 0),
                         buf[0] ? "MINO" : "MOI", txt);
        if (w <= 0) break;
        o += (size_t)w;
        n++;
    }
    if (buf) free(buf);
    if (hist_sd_mux) xSemaphoreGive((SemaphoreHandle_t)hist_sd_mux);
    return n;
}

/* /hist?n=40 : relit les derniers messages stockes sur la carte SD */
static esp_err_t h_hist(httpd_req_t *req)
{
    char v[16] = "";
    int n = 40;
    if (qval(req, "n", v, sizeof(v)) == ESP_OK && v[0]) n = atoi(v);
    if (n <= 0 || n > 200) n = 40;
    size_t sz = 24 * 1024;
    char *out = (char *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!out) { httpd_resp_send_500(req); return ESP_OK; }
    int k = hist_sd_lire(out, sz - 300, n);
    char entete[200];
    snprintf(entete, sizeof(entete),
             "{\"ok\":true,\"sd\":%s,\"total\":%u,\"tours\":%u,\"rendus\":%d,\"messages\":{",
             hist_sd_pret ? "true" : "false", (unsigned)hist_sd_n,
             (unsigned)hist_sd_tours, k);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, entete);
    httpd_resp_sendstr_chunk(req, k > 0 ? out : "");
    httpd_resp_sendstr_chunk(req, "}}");
    httpd_resp_sendstr_chunk(req, NULL);
    free(out);
    return ESP_OK;
}



/* /veille?o=0|1 : mise en veille automatique la nuit (22h-7h).
   OFF par defaut : le robot reste eveille et repond a toute heure. */
static esp_err_t h_veille(httpd_req_t *req)
{
    char v[8] = "";
    if (qval(req, "o", v, sizeof(v)) == ESP_OK && v[0]) {
        veille_nuit = (v[0] == '1');
        if (!veille_nuit) {                    /* on le reveille tout de suite */
            xSemaphoreTake(pet_mux, portMAX_DELAY);
            pet.endormi = 0;
            xSemaphoreGive(pet_mux);
        }
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_i32(nh, "veille", veille_nuit ? 1 : 0);
            nvs_commit(nh);
            nvs_close(nh);
        }
        show_ui();
    }
    char r[80];
    snprintf(r, sizeof(r), "{\"veille\":%s}", veille_nuit ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, r);
    return ESP_OK;
}

/* /rotation?o=0|1&s=SEC : les pages d'info defilent seules, sans toucher aux
   boutons (30 s par defaut). o=0 fige la page courante ; s regle la duree. */
static esp_err_t h_rotation(httpd_req_t *req)
{
    char v[8] = "";
    bool change = false;
    if (qval(req, "o", v, sizeof(v)) == ESP_OK && v[0]) {
        rotation_on = (v[0] != '0');
        change = true;
    }
    if (qval(req, "s", v, sizeof(v)) == ESP_OK && v[0]) {
        int s = atoi(v);
        if (s >= 5 && s <= 600) { rotation_s = s; change = true; }
    }
    if (change) {
        rotation_ms = 0;                  /* on repart de l'instant du reglage */
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_i32(nh, "rot", rotation_on ? 1 : 0);
            nvs_set_i32(nh, "rots", rotation_s);
            nvs_commit(nh);
            nvs_close(nh);
        }
    }
    char r[96];
    snprintf(r, sizeof(r), "{\"rotation\":%s,\"s\":%d,\"page\":\"%.10s\"}",
             rotation_on ? "true" : "false", rotation_s,
             "ACCUEIL");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, r);
    return ESP_OK;
}

/* /voix?o=0|1 : sortie haut-parleur. 0 (defaut) = le robot ecrit seulement. */
static esp_err_t h_voix_off(httpd_req_t *req)
{
    char v[8] = "";
    if (qval(req, "o", v, sizeof(v)) == ESP_OK && v[0]) {
        voix_sortie = (v[0] == '1');
        nvs_handle_t nh;
        if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_i32(nh, "voixsortie", voix_sortie ? 1 : 0);
            nvs_commit(nh);
            nvs_close(nh);
        }
    }
    char r[96];
    snprintf(r, sizeof(r), "{\"voix\":%s,\"phrases\":%d}",
             voix_sortie ? "true" : "false", voix_dit_nb);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, r);
    return ESP_OK;
}

/* ---- le cache d'echanges : chercher des mots dans l'historique de la carte
   SD. C'est ce qui permet au modele de "se souvenir" sans qu'on lui renvoie
   tout l'historique a chaque question (donc sans payer les tokens). */
static bool mot_dedans(const char *texte_min, const char *mot)
{
    if (!mot || strlen(mot) < 3) return true;        /* mot trop court : ignore */
    return strstr(texte_min, mot) != NULL;
}

static int hist_sd_cherche(const char *mots, char *out, size_t sz, int max)
{
    out[0] = 0;
    if (!hist_sd_pret || !hist_sd_sect || !mots || !*mots) return 0;
    char besoin[80];
    snprintf(besoin, sizeof(besoin), "%s", mots);
    for (char *p = besoin; *p; p++) *p = (char)tolower((unsigned char)*p);
    /* on decoupe la demande en mots utiles */
    char *tab[6]; int nm = 0;
    for (char *p = strtok(besoin, " "); p && nm < 6; p = strtok(NULL, " "))
        if (strlen(p) >= 3) tab[nm++] = p;
    if (nm == 0) return 0;

    int dispo = hist_sd_n < 200 ? hist_sd_n : 200;     /* les 200 derniers */
    if (max <= 0) max = 6;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (!buf) return 0;
    if (hist_sd_mux) xSemaphoreTake((SemaphoreHandle_t)hist_sd_mux, portMAX_DELAY);
    int n = 0;
    size_t o = 0;
    for (int k = 1; k <= dispo && n < max && o + 200 < sz; k++) {
        int idx = (int)hist_sd_next - k;
        while (idx < 0) idx += HIST_SD_SLOTS;
        if (sdmmc_read_sectors(sd_card, buf, hist_sd_base + 1 + (uint32_t)idx, 1) != ESP_OK) continue;
        uint16_t len = (uint16_t)(buf[1] | (buf[2] << 8));
        if (len == 0 || len > HIST_SD_TXT) continue;
        char txt[HIST_SD_TXT + 1], min[HIST_SD_TXT + 1];
        memcpy(txt, buf + 3, len);
        txt[len] = 0;
        for (int i = 0; i <= len; i++) min[i] = (char)tolower((unsigned char)txt[i]);
        bool ok = true;
        for (int i = 0; i < nm; i++) if (!mot_dedans(min, tab[i])) { ok = false; break; }
        if (!ok) continue;
        /* on rend le message trouve ET celui qui suit (la reponse) */
        n++;
        o += (size_t)snprintf(out + o, sz - o, "%s: %.220s\n",
                              buf[0] ? "MINO" : "MOI", txt);
        int idx2 = (idx + 1) % HIST_SD_SLOTS;
        if (sdmmc_read_sectors(sd_card, buf, hist_sd_base + 1 + (uint32_t)idx2, 1) == ESP_OK) {
            uint16_t l2 = (uint16_t)(buf[1] | (buf[2] << 8));
            if (l2 > 0 && l2 <= HIST_SD_TXT && o + 260 < sz) {
                char t2[HIST_SD_TXT + 1];
                memcpy(t2, buf + 3, l2);
                t2[l2] = 0;
                o += (size_t)snprintf(out + o, sz - o, "%s: %.220s\n",
                                      buf[0] ? "MINO" : "MOI", t2);
            }
        }
    }
    if (hist_sd_mux) xSemaphoreGive((SemaphoreHandle_t)hist_sd_mux);
    free(buf);
    if (!n) out[0] = 0;
    ESP_LOGI(TAG, "historique : %d extrait(s) pour '%s'", n, mots);
    return n;
}

/* ---------- page HISTORIQUE : les derniers echanges ---------- */
/* On dessine la fenetre RAM (hist_p) : elle est rechargee depuis la partition
   brute de la carte au demarrage, donc ce qu'on voit est bien ce qui est
   sauvegarde. Le bouton 1 remonte dans le passe, l'appui long revient au plus
   recent. */
#define HISTVU_LIGNES 9
#define HISTVU_COLS   24

/* /part                       -> etat des partitions
   /part?op=repart&fat_go=100  -> repartitionne (destructif) : FAT32 100 Go + reste brut
   /part?op=restore            -> carte entiere en FAT32 (retour arriere)
   /part?op=rawbench&bloc=64   -> debit brut dans la zone swap                */
static esp_err_t h_part(httpd_req_t *req)
{
    char v[24] = "";
    char op[24] = "info";
    qval(req, "op", op, sizeof(op));

    if (!strcmp(op, "info")) {
        bool possede = !sd_ok;
        sdmmc_card_t *card = sd_ok ? sd_card : sd_raw_open();
        if (!card) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "carte");
        part_lire_mbr(card);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "secteurs_carte", (double)card->csd.capacity);
        cJSON *a = cJSON_AddArrayToObject(r, "partitions");
        for (int i = 0; i < part_n; i++) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "index", i + 1);
            cJSON_AddNumberToObject(o, "type_hex", part_info[i][2]);
            cJSON_AddNumberToObject(o, "debut_lba", (double)part_info[i][0]);
            cJSON_AddNumberToObject(o, "secteurs", (double)part_info[i][1]);
            cJSON_AddNumberToObject(o, "go", (double)part_info[i][1] * 512.0 / 1073741824.0);
            cJSON_AddItemToArray(a, o);
        }
        if (possede) free(card);
        return send_json(req, r);
    }

    if (!strcmp(op, "repart") || !strcmp(op, "restore")) {
        /* on ecrit avec le handle du montage en cours : rouvrir la carte sans
           reinitialiser le host SDMMC faisait planter la carte (panic). */
        if (!sd_ok || !sd_card)
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "carte non montee");
        sdmmc_card_t *card = sd_card;

        uint32_t total = card->csd.capacity;
        uint32_t p1s = 2048;
        uint32_t p1n, p2n = 0, p2s = 0;
        if (!strcmp(op, "restore")) {
            p1n = total - p1s;
        } else {
            int go = 100;
            if (qval(req, "fat_go", v, sizeof(v)) == ESP_OK) go = atoi(v);
            if (go < 2) go = 2;
            uint32_t want = (uint32_t)((uint64_t)go * 1000 * 1000 * 1000 / 512);
            if (want > total - p1s - 2048) want = total - p1s - 2048;
            p1n = (want / 2048) * 2048;               /* aligne sur 1 Mo */
            p2s = p1s + p1n;
            p2n = total - p2s;
        }

        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "op", op);
        bool mbr_ok = part_ecrire_mbr(card, p1s, p1n, p2s, p2n);
        uint32_t clusters = 0, fat_sz = 0;
        esp_err_t err = mbr_ok ? part_formater(card, p1s, p1n, &clusters, &fat_sz)
                               : ESP_FAIL;
        esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);   /* puis on remonte */
        sd_ok = false;

        cJSON_AddBoolToObject(r, "ok", err == ESP_OK);
        cJSON_AddNumberToObject(r, "fat_go", (double)p1n * 512.0 / 1e9);
        cJSON_AddNumberToObject(r, "swap_go", (double)p2n * 512.0 / 1e9);
        cJSON_AddNumberToObject(r, "clusters", (double)clusters);
        cJSON_AddNumberToObject(r, "fat_secteurs", (double)fat_sz);
        if (err != ESP_OK) cJSON_AddStringToObject(r, "erreur", esp_err_to_name(err));
        else {
            esp_err_t m = sd_mount(false);             /* remonte la nouvelle FAT32 */
            cJSON_AddBoolToObject(r, "remonte", m == ESP_OK);
            if (m == ESP_OK) {
                mkdir("/sdcard/mino", 0777);
                mkdir("/sdcard/backup", 0777);
            }
        }
        return send_json(req, r);
    }

    if (!strcmp(op, "st")) {          /* test des primitives de la zone d'echange */
        int mo = 2;
        if (qval(req, "n", v, sizeof(v)) == ESP_OK) mo = atoi(v);
        if (mo < 1 || mo > 8) mo = 2;
        size_t len = (size_t)mo * 1024 * 1024;
        uint8_t *mem = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        cJSON *r = cJSON_CreateObject();
        if (!mem) { cJSON_AddBoolToObject(r, "ok", false); return send_json(req, r); }
        memset(mem, 0xC3, len);
        bool possede = !sd_ok;
        sdmmc_card_t *card = sd_ok ? sd_card : sd_raw_open();
        if (!card) { free(mem); cJSON_AddBoolToObject(r, "ok", false); return send_json(req, r); }
        part_lire_mbr(card);
        uint32_t base = (part_n > 1) ? part_info[1][0] : 0;
        int64_t t0 = esp_timer_get_time();
        bool w = swap_ecrire(card, base, 0, mem, len);
        int64_t t1 = esp_timer_get_time();
        memset(mem, 0, len);
        bool rd = swap_lire(card, base, 0, mem, len);
        int64_t t2 = esp_timer_get_time();
        bool verif = true;
        for (size_t i = 0; i < len; i += 4096) if (mem[i] != 0xC3) { verif = false; break; }
        free(mem);
        if (possede) free(card);
        cJSON_AddBoolToObject(r, "ecrit", w);
        cJSON_AddBoolToObject(r, "relu", rd);
        cJSON_AddBoolToObject(r, "verifie", verif);
        cJSON_AddNumberToObject(r, "ecriture_mo_s", (double)mo / ((double)(t1 - t0) / 1e6));
        cJSON_AddNumberToObject(r, "lecture_mo_s",  (double)mo / ((double)(t2 - t1) / 1e6));
        return send_json(req, r);
    }

    if (!strcmp(op, "rawtest")) {
        bool possede = !sd_ok;
        sdmmc_card_t *card = sd_ok ? sd_card : sd_raw_open();
        if (!card) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "carte");
        uint32_t lba = card->csd.capacity - 100;
        uint8_t w[512], r[512];
        memset(w, 0x5A, sizeof(w));
        esp_err_t e1 = sdmmc_write_sectors(card, w, lba, 1);
        esp_err_t e2 = sdmmc_read_sectors(card, r, lba, 1);
        cJSON *r2 = cJSON_CreateObject();
        cJSON_AddBoolToObject(r2, "ecrit", e1 == ESP_OK);
        cJSON_AddBoolToObject(r2, "relu", e2 == ESP_OK);
        cJSON_AddBoolToObject(r2, "identique", e2 == ESP_OK && !memcmp(w, r, 512));
        cJSON_AddNumberToObject(r2, "lba", (double)lba);
        if (possede) free(card);
        return send_json(req, r2);
    }

    if (!strcmp(op, "rawbench")) {
        int bloc = 64 * 1024;
        if (qval(req, "bloc", v, sizeof(v)) == ESP_OK) bloc = atoi(v) * 1024;
        if (bloc < 4096 || bloc > 256 * 1024) bloc = 64 * 1024;
        bool possede = !sd_ok;
        sdmmc_card_t *card = sd_ok ? sd_card : sd_raw_open();
        if (!card) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "carte");
        part_lire_mbr(card);
        if (part_n < 2) { if (possede) free(card);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "aucune zone brute"); }
        uint32_t start = part_info[1][0];
        uint32_t nsec = (4 * 1024 * 1024) / 512;
        uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(512, bloc,
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!buf)   /* le pool DMA interne est petit : on tente la PSRAM */
            buf = (uint8_t *)heap_caps_aligned_alloc(512, bloc, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        cJSON *r = cJSON_CreateObject();
        if (!buf) { if (possede) free(card);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "alloc"); }
        memset(buf, 0x33, bloc);
        int64_t t0 = esp_timer_get_time();
        for (uint32_t s = 0; s < nsec; s += bloc / 512)
            sdmmc_write_sectors(card, buf, start + s, bloc / 512);
        int64_t t1 = esp_timer_get_time();
        for (uint32_t s = 0; s < nsec; s += bloc / 512)
            sdmmc_read_sectors(card, buf, start + s, bloc / 512);
        int64_t t2 = esp_timer_get_time();
        free(buf);
        if (possede) free(card);
        cJSON_AddBoolToObject(r, "ok", true);
        cJSON_AddNumberToObject(r, "bloc_ko", bloc / 1024);
        cJSON_AddNumberToObject(r, "ecriture_mo_s", 4.0 / ((double)(t1 - t0) / 1e6));
        cJSON_AddNumberToObject(r, "lecture_mo_s",  4.0 / ((double)(t2 - t1) / 1e6));
        return send_json(req, r);
    }

    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "op inconnu");
}

/* ---- LE LIVRE : /livre ---------------------------------------------
   /livre                       l'etat du lecteur
   /livre?op=liste              les .txt du dossier /sdcard/livres
   /livre?op=ouvre&f=nom.txt&n=3
   /livre?op=page&n=12          (1 = premiere page)
   /livre?op=suiv&d=1           tourner de d pages (d negatif : en arriere)
   /livre?op=change&d=1         livre suivant / precedent
   /livre?op=ferme              fermer et revenir a l'accueil
   Tout passe par le meme chemin que les boutons : la page s'affiche. */
static cJSON *livre_json(void)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddBoolToObject(r, "ouvert", livre_pret);
    cJSON_AddStringToObject(r, "fichier", livre_dernier);
    cJSON_AddStringToObject(r, "nom", livre_pret ? livre.nom : "");
    cJSON_AddNumberToObject(r, "page", livre_pret ? (double)(livre.page + 1) : 0);
    cJSON_AddNumberToObject(r, "pages", livre_pret ? (double)livre_pages(&livre) : 0);
    cJSON_AddNumberToObject(r, "pct", livre_pret ? (double)livre_pct(&livre) : 0);
    cJSON_AddNumberToObject(r, "lignes", livre_pret ? (double)livre.nlignes : 0);
    cJSON_AddNumberToObject(r, "livres", (double)livre_n);
    cJSON_AddStringToObject(r, "dossier", LIVRE_DOSSIER);
    return r;
}

static esp_err_t h_livre(httpd_req_t *req)
{
    char op[12] = "", f[LIVRE_TITRE_MAX] = "", v[12] = "";
    int n = -1, d = 1;
    qval(req, "op", op, sizeof(op));
    qval(req, "f", f, sizeof(f));
    if (qval(req, "n", v, sizeof(v)) == ESP_OK && v[0]) n = atoi(v);
    if (qval(req, "d", v, sizeof(v)) == ESP_OK && v[0]) d = atoi(v);

    if (!strcmp(op, "liste")) {
        livre_scan();                        /* toujours a jour : on vient d'uploader */
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "dossier", LIVRE_DOSSIER);
        cJSON_AddBoolToObject(r, "sd", sd_ok);
        cJSON *a = cJSON_AddArrayToObject(r, "livres");
        for (int i = 0; i < livre_n; i++)
            cJSON_AddItemToArray(a, cJSON_CreateString(livre_noms[i]));
        return send_json(req, r);
    }
    if (!strcmp(op, "ouvre")) {
        if (!f[0] || strchr(f, '/') || strstr(f, ".."))
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "f=nom.txt");
        if (!livre_ouvre(f, n > 0 ? n - 1 : 0))
            return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "livre illisible");
        livre_memo();
        show_ui();                  /* l'ecran est le tableau : rien a changer */
    } else if (!strcmp(op, "page")) {
        if (!livre_pret) livre_reprise();
        if (!livre_pret) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "aucun livre");
        if (n > 0) { livre_va(&livre, n - 1); livre_memo(); }
        show_ui();                  /* l'ecran est le tableau : rien a changer */
    } else if (!strcmp(op, "suiv") || !strcmp(op, "avance")) {
        if (!livre_pret) livre_reprise();
        if (!livre_pret) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "aucun livre");
        if (d == 0) d = 1;
        livre_tourne(d);
        show_ui();                  /* l'ecran est le tableau : rien a changer */
    } else if (!strcmp(op, "change")) {
        if (!livre_pret) livre_reprise();
        livre_suivant(d ? d : 1);
        show_ui();                  /* l'ecran est le tableau : rien a changer */
    } else if (!strcmp(op, "ferme")) {
        livre_arret();
        show_ui();
    }
    return send_json(req, livre_json());
}

static esp_err_t h_sd_list(httpd_req_t *req)
{
    /* /sd            -> racine de la carte
       /sd?p=/backup  -> un sous-dossier                                      */
    char q[128] = {0}, val[100] = {0}, chemin[128];
    snprintf(chemin, sizeof(chemin), "/sdcard");
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "p", val, sizeof(val)) == ESP_OK) {
        if (val[0] != '/' || strstr(val, ".."))
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "p invalide");
        snprintf(chemin, sizeof(chemin), "/sdcard%s", val);
    }

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "presente", sd_ok);
    cJSON_AddStringToObject(r, "dossier", chemin);
    cJSON *files = cJSON_AddArrayToObject(r, "fichiers");
    if (sd_ok) {
        DIR *d = opendir(chemin);
        if (d) {
            struct dirent *e;
            int n = 0;
            while ((e = readdir(d)) && n < 40) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                /* n'affiche que les noms imprimables : une entree de repertoire
                   abimee ne doit pas pourrir toute la reponse JSON */
                int ok = 1;
                for (const char *c = e->d_name; *c; c++)
                    if ((unsigned char)*c < 0x20) { ok = 0; break; }
                char plein[400];
                struct stat st;
                long taille = -1;
                snprintf(plein, sizeof(plein), "%s/%.255s", chemin, e->d_name);
                if (stat(plein, &st) == 0) taille = (long)st.st_size;
                cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "nom", e->d_name);
                cJSON_AddNumberToObject(o, "taille", (double)taille);
                cJSON_AddBoolToObject(o, "lisible", ok);
                cJSON_AddItemToArray(files, o);
                n++;
            }
            closedir(d);
        }
    }
    return send_json(req, r);
}

/* ------------------------------------------------------------------ */
/* /swap : la carte SD comme memoire de debordement                    */
/* L'ESP32-S3 n'a pas de MMU : pas de swap/pagination possible. Ce qui */
/* marche, c'est un cache d'ecriture en PSRAM vide vers la SD en gros  */
/* blocs, et un stockage de donnees rarement lues. op=bench mesure le  */
/* meilleur couple (taille de bloc, debit) reellement atteignable.      */
/* ------------------------------------------------------------------ */
static esp_err_t h_swap(httpd_req_t *req)
{
    char v[24] = "";
    cJSON *r = cJSON_CreateObject();

    /* op=fatfs : meme mesure mais via l'API FatFs brute (sans newlib/VFS) */
    if (qval(req, "op", v, sizeof(v)) == ESP_OK && !strcmp(v, "fatfs")) {
        int bloc = 65536;
        if (qval(req, "bloc", v, sizeof(v)) == ESP_OK) bloc = atoi(v) * 1024;
        if (bloc < 512 || bloc > 262144) bloc = 65536;
        size_t total = 4 * 1024 * 1024;
        uint8_t *buf = (uint8_t *)heap_caps_malloc(bloc, MALLOC_CAP_SPIRAM);
        if (!buf) { cJSON_AddBoolToObject(r, "ok", false); return send_json(req, r); }
        memset(buf, 0x5C, bloc);
        FIL fil; UINT bw = 0, br = 0;
        FRESULT fr = f_open(&fil, "fbench.bin", FA_WRITE | FA_CREATE_ALWAYS);
        int64_t t0 = esp_timer_get_time();
        size_t fait = 0;
        while (fr == FR_OK && fait < total) { f_write(&fil, buf, bloc, &bw); fait += bw; if (!bw) break; }
        if (fr == FR_OK) f_sync(&fil);
        f_close(&fil);
        int64_t t1 = esp_timer_get_time();
        fr = f_open(&fil, "fbench.bin", FA_READ);
        size_t lu = 0;
        while (fr == FR_OK && lu < total) { f_read(&fil, buf, bloc, &br); lu += br; if (!br) break; }
        f_close(&fil);
        int64_t t2 = esp_timer_get_time();
        f_unlink("fbench.bin");
        free(buf);
        cJSON_AddStringToObject(r, "via", "fatfs-brut");
        cJSON_AddNumberToObject(r, "bloc_ko", bloc / 1024);
        cJSON_AddNumberToObject(r, "ecriture_mo_s",
            (double)fait / ((double)(t1 - t0) / 1e6) / 1048576.0);
        cJSON_AddNumberToObject(r, "lecture_mo_s",
            (double)lu / ((double)(t2 - t1) / 1e6) / 1048576.0);
        return send_json(req, r);
    }

    if (qval(req, "op", v, sizeof(v)) == ESP_OK && !strcmp(v, "bench")) {
        int bloc = 65536;
        if (qval(req, "bloc", v, sizeof(v)) == ESP_OK) bloc = atoi(v) * 1024;
        if (bloc < 512 || bloc > 262144) bloc = 65536;
        size_t total = 4 * 1024 * 1024;
        if (qval(req, "mo", v, sizeof(v)) == ESP_OK) {
            int mo = atoi(v);
            if (mo > 0 && mo <= 16) total = (size_t)mo * 1024 * 1024;
        }
        uint8_t *buf = (uint8_t *)heap_caps_malloc(bloc, MALLOC_CAP_SPIRAM);
        if (!buf) { cJSON_AddBoolToObject(r, "ok", false);
                    cJSON_AddStringToObject(r, "erreur", "alloc");
                    return send_json(req, r); }
        memset(buf, 0xA7, bloc);
        int64_t t0 = esp_timer_get_time();
        FILE *f = fopen("/sdcard/swap.bin", "wb");
        size_t fait = 0;
        while (f && fait < total) { fwrite(buf, 1, bloc, f); fait += bloc; }
        if (f) { fflush(f); fsync(fileno(f)); fclose(f); }
        int64_t t1 = esp_timer_get_time();
        f = fopen("/sdcard/swap.bin", "rb");
        size_t lu = 0;
        while (f && lu < total) { size_t k = fread(buf, 1, bloc, f); if (!k) break; lu += k; }
        if (f) fclose(f);
        int64_t t2 = esp_timer_get_time();
        remove("/sdcard/swap.bin");
        free(buf);
        cJSON_AddBoolToObject(r, "ok", fait == total && lu == total);
        cJSON_AddNumberToObject(r, "bloc_ko", bloc / 1024);
        cJSON_AddNumberToObject(r, "ecriture_mo_s",
            (double)fait / ((double)(t1 - t0) / 1e6) / 1048576.0);
        cJSON_AddNumberToObject(r, "lecture_mo_s",
            (double)lu / ((double)(t2 - t1) / 1e6) / 1048576.0);
        return send_json(req, r);
    }

    uint64_t tot = 0, libre = 0;
    esp_vfs_fat_info("/sdcard", &tot, &libre);
    cJSON_AddNumberToObject(r, "total_mo", (double)tot / 1048576.0);
    cJSON_AddNumberToObject(r, "libre_mo", (double)libre / 1048576.0);
    cJSON_AddStringToObject(r, "note",
        "pas de swap OS (pas de MMU) ; utiliser op=bench pour le debit par bloc");
    return send_json(req, r);
}

/* /sdformat?f=1 : reformate VRAIMENT la carte (efface tout).
   Sans f=1 on se contente de remonter : le mount reussit toujours sur une carte
   deja formatee, donc l'ancienne version ne formattoir jamais rien. */
static esp_err_t h_sd_format(httpd_req_t *req)
{
    char v[8] = "";
    bool vraiment = (qval(req, "f", v, sizeof(v)) == ESP_OK && v[0] == '1');

    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "formate", vraiment);
    if (!vraiment) {
        cJSON_AddStringToObject(r, "note", "ajouter f=1 pour effacer la carte");
        return send_json(req, r);
    }
    if (!sd_ok || !sd_card) {
        cJSON_AddBoolToObject(r, "ok", false);
        cJSON_AddStringToObject(r, "erreur", "carte absente");
        return send_json(req, r);
    }
    sd_ok = false;
    esp_err_t err = esp_vfs_fat_sdcard_format("/sdcard", sd_card);
    if (err == ESP_OK) {
        /* le montage reste valide apres format ; on recree l'arborescence */
        mkdir("/sdcard/mino", 0777);
        mkdir("/sdcard/backup", 0777);
        mkdir("/sdcard/livres", 0777);
        livre_scan_fait = false;         /* le dossier vient d'etre (re)cree */
        sd_ok = true;
    }
    cJSON_AddBoolToObject(r, "ok", err == ESP_OK);
    if (err != ESP_OK) cJSON_AddStringToObject(r, "erreur", esp_err_to_name(err));
    return send_json(req, r);
}

/* ------------------------------------------------------------------ */
/* audio : micro + haut-parleur (ES8311)                               */
/* ------------------------------------------------------------------ */
#define AUDIO_MAX_FRAMES (16 * 1000 * 15)     /* 15 s a 16 kHz */

static esp_err_t h_rec(httpd_req_t *req)
{
    char v[32];
    int ms = 3000;
    if (qval(req, "ms", v, sizeof(v)) == ESP_OK) ms = atoi(v);
    if (ms < 300) ms = 300;
    if (ms > 15000) ms = 15000;

    int frames = ms * 16;                     /* 16 echantillons / ms */
    if (!rec_pcm || frames > AUDIO_MAX_FRAMES) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    static uint8_t stereo[512];

    int done = 0;
    while (done < frames) {
        int todo = frames - done;
        int chunk = (todo > 256) ? 256 : todo;         /* frames par tour */
        audio_playback_read(stereo, chunk * 4);        /* 2 canaux * 2 octets */
        for (int i = 0; i < chunk; i++) {
            int16_t l = (int16_t)(stereo[i * 4] | (stereo[i * 4 + 1] << 8));
            rec_pcm[(done + i) * 2] = (uint8_t)(l & 0xff);
            rec_pcm[(done + i) * 2 + 1] = (uint8_t)((l >> 8) & 0xff);
        }
        done += chunk;
    }

    int data_len = done * 2;
    uint8_t hdr[44] = {0};
    uint32_t chunk_size = data_len + 36;
    uint32_t byte_rate = 16000 * 2;
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 4, &chunk_size, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    uint32_t fmt = 16; hdr[16] = 16; hdr[20] = 1; hdr[22] = 1;
    hdr[24] = 16000 & 0xff; hdr[25] = (16000 >> 8) & 0xff;
    hdr[28] = byte_rate & 0xff; hdr[29] = (byte_rate >> 8) & 0xff;
    hdr[32] = 2; hdr[34] = 16;
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_len, 4);
    (void)fmt;

    ESP_LOGI(TAG, "micro : %.1f s enregistrees", done / 16000.0);
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr));
    httpd_resp_send_chunk(req, (const char *)rec_pcm, data_len);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_play(httpd_req_t *req)
{
    static uint8_t in[1024];
    static uint8_t out[2048];
    int total = 0;
    audio_playback_set_vol(90);
    while (1) {
        int got = httpd_req_recv(req, (char *)in, sizeof(in));
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) break;
        int frames = got / 2;
        for (int i = 0; i < frames; i++) {
            out[i * 4] = in[i * 2];
            out[i * 4 + 1] = in[i * 2 + 1];
            out[i * 4 + 2] = in[i * 2];
            out[i * 4 + 3] = in[i * 2 + 1];
        }
        audio_playback_write(out, frames * 4);
        total += got;
    }
    ESP_LOGI(TAG, "haut-parleur : %d octets joues", total);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "octets", total);
    return send_json(req, r);
}

/* POST /fichier?p=/backup/x.tar.gz : ecrit le corps recu sur la carte SD.
   GET  /fichier?p=/backup/x.tar.gz : renvoie le fichier.
   Sert a sauvegarder/relire des fichiers sur la SD sans lecteur de carte. */
static esp_err_t h_fichier(httpd_req_t *req)
{
    char q[256] = {0}, val[200] = {0};
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "p", val, sizeof(val)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "p= requis");
    if (val[0] != '/')
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "chemin absolu requis");
    if (strstr(val, ".."))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, ".. interdit");

    char chemin[256];
    snprintf(chemin, sizeof(chemin), "/sdcard%s", val);
    if (strlen(chemin) >= sizeof(chemin) - 1)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "chemin trop long");

    if (req->method == HTTP_POST) {
        char *slash = strrchr(chemin, '/');
        if (slash && slash > chemin + 7) {          /* cree le dossier parent */
            *slash = 0;
            mkdir(chemin, 0777);
            *slash = '/';
        }
        FILE *f = fopen(chemin, "wb");
        if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ouverture");
        /* 32 Ko en PSRAM et non en .bss : ces deux tampons prenaient 64 Ko de
           RAM interne, et le wifi n'avait plus de quoi allouer ses tampons de
           reception ("wifi:mem fail"), ce qui coupait les telechargements. */
        char *buf = (char *)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
        if (!buf) { fclose(f); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "memoire"); }
        long total = 0;
        while (1) {
            int got = httpd_req_recv(req, buf, 32768);
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (got <= 0) break;
            fwrite(buf, 1, got, f);
            total += got;
        }
        free(buf);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        ESP_LOGI(TAG, "fichier recu : %s (%ld octets)", chemin, total);
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "fichier", chemin);
        cJSON_AddNumberToObject(r, "octets", total);
        return send_json(req, r);
    }

    FILE *f = fopen(chemin, "rb");
    if (!f) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "absent");
    httpd_resp_set_type(req, "application/octet-stream");
    char *buf = (char *)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(f); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "memoire"); }
    size_t n;
    while ((n = fread(buf, 1, 32768, f)) > 0)
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) break;
    free(buf);
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* aides audio reutilisables (console serie et API partagent)          */
/* ------------------------------------------------------------------ */
static void wav_header(uint8_t hdr[44], int data_len)
{
    uint32_t chunk_size = (uint32_t)data_len + 36;
    uint32_t byte_rate = 16000 * 2;
    memset(hdr, 0, 44);
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 4, &chunk_size, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    hdr[16] = 16; hdr[20] = 1; hdr[22] = 1;
    hdr[24] = 16000 & 0xff; hdr[25] = (16000 >> 8) & 0xff;
    hdr[28] = byte_rate & 0xff; hdr[29] = (byte_rate >> 8) & 0xff;
    hdr[32] = 2; hdr[34] = 16;
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_len, 4);
}

static int audio_record_at(int off, int frames, int *peak)
{
    static uint8_t stereo[512];
    int done = 0, mx = 0;
    if (!rec_pcm) return 0;
    if (off + frames > AUDIO_MAX_FRAMES) frames = AUDIO_MAX_FRAMES - off;
    while (done < frames) {
        int todo = frames - done;
        int chunk = (todo > 256) ? 256 : todo;
        audio_playback_read(stereo, chunk * 4);
        for (int i = 0; i < chunk; i++) {
            int16_t l = (int16_t)(stereo[i * 4] | (stereo[i * 4 + 1] << 8));
            int a = l < 0 ? -l : l;
            if (a > mx) mx = a;
            rec_pcm[(off + done + i) * 2] = (uint8_t)(l & 0xff);
            rec_pcm[(off + done + i) * 2 + 1] = (uint8_t)((l >> 8) & 0xff);
        }
        done += chunk;
    }
    if (peak) *peak = mx;
    return done;
}

/* enregistre `frames` echantillons (16 kHz) en mono dans rec_pcm */
static int audio_record_mono(int frames, int *peak)
{
    return audio_record_at(0, frames, peak);
}

static void audio_play_mono(int frames)
{
    /* gros paquets : chaque appel a l'I2S coute ~40 ms quelle que soit la
       taille, avec des paquets de 256 trames la lecture trainait 3 a 4 fois
       plus longtemps que la duree reelle du son. */
    static uint8_t out[8192];
    const int per = 1024;                 /* trames par appel (4096 octets) */
    int done = 0;
    if (!rec_pcm) return;
    audio_playback_set_vol(90);
    while (done < frames) {
        int todo = frames - done;
        int chunk = (todo > per) ? per : todo;
        for (int i = 0; i < chunk; i++) {
            out[i * 4] = rec_pcm[(done + i) * 2];
            out[i * 4 + 1] = rec_pcm[(done + i) * 2 + 1];
            out[i * 4 + 2] = rec_pcm[(done + i) * 2];
            out[i * 4 + 3] = rec_pcm[(done + i) * 2 + 1];
        }
        audio_playback_write(out, chunk * 4);
        done += chunk;
        if (act_on) {                       /* la barre suit la lecture */
            act_pct = (done * 100) / frames;
            if (act_pct > 100) act_pct = 100;
        }
    }
}

static int audio_sd_write(const char *path, const uint8_t *mono, int data_len)
{
    if (!sd_ok) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    uint8_t hdr[44];
    wav_header(hdr, data_len);
    fwrite(hdr, 1, sizeof(hdr), f);
    size_t w = fwrite(mono, 1, data_len, f);
    fclose(f);
    return (int)w;
}

/* un bip doux : attaque et extinction progressives (sinon le haut-parleur
   claque) et un niveau bien plus bas que l'ancien 0,30 a 660 Hz, qui
   agressait l'oreille. Le volume de lecture est a 90 dans audio_play_mono,
   donc c'est l'amplitude ici qui regle la douceur. */
static void bip_doux(float hz, int ms, float amp)
{
    if (!rec_pcm) return;
    const int sr = 16000;
    int n = sr * ms / 1000;
    if (n > AUDIO_MAX_FRAMES) n = AUDIO_MAX_FRAMES;
    int att = sr * 10 / 1000;          /* 10 ms d'attaque */
    int rel = sr * 60 / 1000;          /* 60 ms d'extinction */
    if (att < 1) att = 1;
    if (rel > n / 2) rel = n / 2;
    if (rel < 1) rel = 1;
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < att) env = (float)i / (float)att;
        else if (i > n - rel) env = (float)(n - i) / (float)rel;
        int16_t v = (int16_t)lroundf(amp * env * 32767.0f *
                                     sinf(2.0f * 3.14159265f * hz * (float)i / (float)sr));
        rec_pcm[i * 2] = (uint8_t)(v & 0xff);
        rec_pcm[i * 2 + 1] = (uint8_t)((v >> 8) & 0xff);
    }
    audio_play_mono(n);
}

/* ---- la sonnerie du rendez-vous ----
   Un carillon, pas un reveil : accord majeur (mi - sol# - si) joue en
   arpege, avec de l'octave et une pointe de quinte dans le timbre, et une
   deuxieme phrase plus douce. Chaque note a une attaque courte et une
   extinction naturelle, sinon le haut-parleur claque et le son agresse. */
static int sonnerie_compose(void)
{
    if (!rec_pcm) return 0;
    const int sr = 16000;
    const float PI = 3.14159265f;
    static const struct { float f; int ms; int t0; float a; } notes[] = {
        {659.25f, 300,    0, 0.50f},      /* mi  */
        {830.61f, 300,  320, 0.46f},      /* sol# */
        {987.77f, 520,  640, 0.42f},      /* si  */
        {987.77f, 240, 1240, 0.22f},      /* echo */
        {830.61f, 280, 1600, 0.24f},
        {659.25f, 300, 2080, 0.46f},      /* deuxieme phrase, plus douce */
        {830.61f, 300, 2400, 0.42f},
        {987.77f, 520, 2720, 0.38f},
        {987.77f, 240, 3320, 0.20f},
    };
    const int total_ms = 3700;
    int frames = sr * total_ms / 1000;
    if (frames > AUDIO_MAX_FRAMES) frames = AUDIO_MAX_FRAMES;
    memset(rec_pcm, 0, (size_t)frames * 2);

    for (unsigned k = 0; k < sizeof(notes) / sizeof(notes[0]); k++) {
        int n = sr * notes[k].ms / 1000;
        int off = sr * notes[k].t0 / 1000;
        float f = notes[k].f;
        int att = sr * 6 / 1000;                 /* 6 ms d'attaque */
        int rel = sr * 45 / 1000;                /* 45 ms d'extinction */
        if (att < 1) att = 1;
        if (rel > n / 2) rel = n / 2;
        if (rel < 1) rel = 1;
        for (int i = 0; i < n && off + i < frames; i++) {
            float t = (float)i / (float)sr;
            float v = 0.72f * sinf(2 * PI * f * t)
                    + 0.22f * sinf(2 * PI * (f * 1.004f) * t)   /* desaccord : chaleur */
                    + 0.26f * sinf(2 * PI * 2 * f * t)          /* octave */
                    + 0.10f * sinf(2 * PI * 3 * f * t);         /* quinte */
            float env = 1.0f;
            if (i < att) env = (float)i / (float)att;
            env *= expf(-6.0f * t);
            if (i > n - rel) env *= (float)(n - i) / (float)rel;
            int mix = (int)lroundf(v / 1.30f * env * notes[k].a * 32767.0f);
            int16_t *p = (int16_t *)(rec_pcm + (off + i) * 2);
            int s = (int)(*p) + mix;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            *p = (int16_t)s;
        }
    }
    return frames;
}

static void alarme_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(4000));            /* on laisse la carte demarrer */
    for (;;) {
        if (!alarme_on) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        /* on ne joue pas par-dessus la radio ou la voix */
        if (radio_etat || voix_etat) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

        /* 1. le carillon, une seule fois */
        int frames = sonnerie_compose();
        if (frames > 0) { audio_play_mono(frames); alarme_jouee++; }
        if (!alarme_on) continue;               /* coupee pendant le carillon */

        /* 2. la voix annonce : le rappel du timer, sinon le rendez-vous */
        char phrase[160];
        if (annonce_txt[0]) {
            snprintf(phrase, sizeof(phrase), "%s", annonce_txt);
            annonce_txt[0] = 0;                 /* une seule annonce par echeance */
        } else if (ms_dans >= 0 && ms_dans <= ALARME_MIN + 5 && (ms_org[0] || ms_titre[0]))
            snprintf(phrase, sizeof(phrase), "Rendez-vous dans %d minutes avec %s.",
                     ms_dans, ms_org[0] ? ms_org : ms_titre);
        else if (ms_org[0] || ms_titre[0])
            snprintf(phrase, sizeof(phrase), "Rendez-vous avec %s.",
                     ms_org[0] ? ms_org : ms_titre);
        else
            snprintf(phrase, sizeof(phrase), "Vous avez un rendez-vous.");

        alarme_on = false;                      /* une fois, pas en boucle */
        ESP_LOGI(TAG, "alarme : %s", phrase);
        voix_dit(phrase);
        show_ui();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void ui_beep(void)                 /* toucher, bouton : discret */
{
    bip_doux(520.0f, 60, 0.07f);
}

/* le signal du chat vocal : deux notes descendantes, douces et courtes */
static void ui_chime(void)
{
    if (!rec_pcm) return;
    bip_doux(988.0f, 80, 0.09f);
    vTaskDelay(pdMS_TO_TICKS(30));
    bip_doux(740.0f, 110, 0.09f);
}

/* ------------------------------------------------------------------ */
/* commandes tapees sur la console serie USB                           */
/* ------------------------------------------------------------------ */
/* ecriture binaire directe sur la console USB.
   Le peripherique USB Serial/JTAG n'accepte pas un gros tampon en une fois
   (et rec_pcm est en PSRAM) : on decoupe en tranches de 4 ko. */
static void serial_write_bulk(const uint8_t *data, int len)
{
    int off = 0, essais = 0;
    while (off < len && essais < 4000) {
        int n = len - off;
        if (n > 4096) n = 4096;
        int w = usb_serial_jtag_write_bytes(data + off, n, pdMS_TO_TICKS(2000));
        if (w <= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            essais++;
            continue;
        }
        off += w;
    }
    if (off < len) ESP_LOGW(TAG, "ecriture USB incomplete : %d sur %d octets", off, len);
}

static void mino_handle_line(const char *line)
{
    if (!strcmp(line, "help") || !strcmp(line, "?")) {
        ESP_LOGI(TAG, "commandes : <texte> (DeepSeek), faces, face <nom>, ui [0-4|f], touches, "
                      "wake, getrec, pcm begin|end, radio start|stop, fb, capteurs, offset <x>, secu, "
                      "secuscan, echo [s], rec [s], sd, wget <url>, state, memo, note <t>, reboot");
        return;
    }
    if (!strcmp(line, "faces")) {
        for (int i = 0; i < EXPR_N; i++) {
            show_face(i);
            vTaskDelay(pdMS_TO_TICKS(400));
        }
        return;
    }
    if (!strncmp(line, "face ", 5)) {
        int e = emo_by_name(line + 5);
        if (e < 0) {
            ESP_LOGW(TAG, "emotion inconnue : %s", line + 5);
        } else {
            current_emotion = e;
            show_face(e);
        }
        return;
    }
    if (!strcmp(line, "state")) {
        ESP_LOGI(TAG, "emotion=%s  stade=%s age=%u j sante=%u%%  RAM libre=%u  PSRAM libre=%u  SD=%s",
                 EXPR[ui_face].nom, pet_stade_nom(pet_stade(&pet)),
                 (unsigned)(pet.age_s / 86400), pet_sante(&pet),
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), sd_ok ? "oui" : "non");
        return;
    }
    if (!strcmp(line, "diag")) {
        /* tout ce qu'on ne peut pas lire sans reseau : etat reel de la tache ui,
           du wifi, de la meteo et des rappels */
        ESP_LOGI(TAG, "diag : ui_ticks=%d wifi=%d '%s' meteo=%d %.1fC code=%d dem=%d pluie=%d",
                 ui_ticks, (xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT) ? 1 : 0, wifi_actif,
                 meteo_ok ? 1 : 0, (double)meteo_temp, meteo_code, meteo_dem_code, meteo_dem_pluie);
        int nt = 0;
        for (int i = 0; i < TIMER_MAX; i++) if (timers[i].fin_s) nt++;
        ESP_LOGI(TAG, "diag : rappel_nb=%d dernier='%s' alarme=%d jouee=%d timers_actifs=%d",
                 rappel_nb, rappel_dernier, alarme_on ? 1 : 0, alarme_jouee, nt);
        ESP_LOGI(TAG, "diag : page=%s app=%s app_mem=%d boot_nb=%d heap_int_max=%u psram=%u",
                 "ACCUEIL", 0,
                 app_mem_octets, (int)boot_nb,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        ESP_LOGI(TAG, "diag : yt_chaine='%s' yt_video='%s' yt_vues=%d yt_ok=%d",
                 yt_handle, yt_titre, yt_vues, yt_ok ? 1 : 0);
        return;
    }
    if (!strcmp(line, "memo")) {
        cJSON *notes = cJSON_GetObjectItem(mem_obj, "notes");
        cJSON *it = NULL;
        ESP_LOGI(TAG, "memoire (%d notes) :", notes ? cJSON_GetArraySize(notes) : 0);
        cJSON_ArrayForEach(it, notes) if (cJSON_IsString(it)) ESP_LOGI(TAG, "  - %s", it->valuestring);
        return;
    }
    if (!strncmp(line, "note ", 5)) {
        mem_add(line + 5);
        return;
    }
    if (!strcmp(line, "reboot")) {
        esp_restart();
        return;
    }
    if (!strncmp(line, "echo", 4)) {
        int sec = 3;
        if (line[4] == ' ') sec = atoi(line + 5);
        if (sec < 1) sec = 1;
        if (sec > 10) sec = 10;
        ESP_LOGI(TAG, "micro : j'ecoute %d s...", sec);
        int peak = 0;
        int n = audio_record_mono(sec * 16000, &peak);
        ESP_LOGI(TAG, "micro : %d echantillons, crete=%d/32767", n, peak);
        vTaskDelay(pdMS_TO_TICKS(200));
        ESP_LOGI(TAG, "haut-parleur : je repasse l'enregistrement");
        audio_play_mono(n);
        ESP_LOGI(TAG, "haut-parleur : fini");
        return;
    }
    if (!strncmp(line, "rec", 3)) {
        int sec = 3;
        if (line[3] == ' ') sec = atoi(line + 4);
        if (sec < 1) sec = 1;
        if (sec > 15) sec = 15;
        int peak = 0;
        int n = audio_record_mono(sec * 16000, &peak);
        rec_last_bytes = n * 2;                    /* pour getrec */
        int w = audio_sd_write("/sdcard/mic.wav", rec_pcm, n * 2);
        ESP_LOGI(TAG, "rec : %d s, crete=%d, %d octets ecrits sur la SD", n / 16000, peak, w);
        return;
    }
    if (!strcmp(line, "sd")) {
        if (!sd_ok) { ESP_LOGW(TAG, "pas de carte SD"); return; }
        DIR *d = opendir("/sdcard");
        if (!d) { ESP_LOGW(TAG, "racine SD illisible"); return; }
        struct dirent *e;
        int n = 0;
        while ((e = readdir(d)) && n < 25) {
            if (e->d_name[0] == '.') continue;
            int ok = 1;
            for (const char *p = e->d_name; *p; p++)
                if ((unsigned char)*p < 0x20) { ok = 0; break; }
            if (!ok) continue;                     /* entree FAT parasite */
            struct stat st;
            char p[300];
            snprintf(p, sizeof(p), "/sdcard/%s", e->d_name);
            if (stat(p, &st) != 0) continue;
            ESP_LOGI(TAG, "  %8ld  %s", (long)st.st_size, e->d_name);
            n++;
        }
        closedir(d);
        ESP_LOGI(TAG, "(%d entrees lisibles)", n);
        return;
    }
    if (!strncmp(line, "wget ", 5)) {
        esp_http_client_config_t cfg = {};
        cfg.max_redirection_count = 4;   /* FG Radio et autres : suivre les 302 */
        cfg.url = line + 5;
        cfg.timeout_ms = 8000;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) return;
        esp_err_t r = esp_http_client_open(c, 0);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "wget : %s injoignable (%s)", line + 5, esp_err_to_name(r));
        } else {
            int len = esp_http_client_fetch_headers(c);
            int status = esp_http_client_get_status_code(c);
            char buf[65];
            int n = esp_http_client_read(c, buf, sizeof(buf) - 1);
            if (n < 0) n = 0;
            buf[n] = 0;
            ESP_LOGI(TAG, "wget %s -> HTTP %d, %d octets, debut='%s'", line + 5, status, len, buf);
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return;
    }

    if (!strcmp(line, "secu")) {
        secu_collect();
        if (ui_page == 99) show_ui();
        ESP_LOGI(TAG, "--- analyse reseau ---");
        ESP_LOGI(TAG, "  reseau   : %s (%s)", sec_ssid, sec_bssid);
        ESP_LOGI(TAG, "  securite : %s / %s, canal %d, %d dBm, WPS %s, WPA3 %s",
                 sec_auth, sec_ciph, sec_chan, sec_rssi,
                 sec_wps ? "oui" : "non", sec_wpa3 ? "oui" : "non");
        ESP_LOGI(TAG, "  ip       : %s  passerelle %s (%s)", sec_ip, sec_gw,
                 sec_gw_ok ? "repond" : "muette");
        ESP_LOGI(TAG, "  dns      : %s (%s)   internet : %s", sec_dns,
                 sec_dns_ok ? "resout" : "ne resout pas", sec_net_ok ? "ok" : "ko");
        ESP_LOGI(TAG, "  voisinage: %d reseaux vus, %d en clair", sec_aps, sec_open);
        ESP_LOGI(TAG, "  local    : %d machine(s) | risque %d/5 | %s",
                 sec_hotes, sec_risque, sec_alerte);
        return;
    }
    if (!strcmp(line, "secuscan")) {
        secu_sweep();
        ESP_LOGI(TAG, "balayage du reseau local lance (environ 40 s)");
        return;
    }
    if (!strncmp(line, "tone", 4)) {
        tone_hz = 440;
        tone_sec = 3;
        if (line[4] == ' ') {
            tone_hz = atoi(line + 5);
            const char *sp = strchr(line + 5, ' ');
            if (sp) tone_sec = atoi(sp + 1);
        }
        if (tone_hz < 50 || tone_hz > 8000) tone_hz = 440;
        if (tone_sec < 1 || tone_sec > 30) tone_sec = 3;
        ESP_LOGI(TAG, "test son : %d Hz pendant %d s", tone_hz, tone_sec);
        xTaskCreate(tone_task, "tonetest", 4096, NULL, 4, NULL);
        return;
    }
    if (!strncmp(line, "prog", 4)) {
        /* prog <0-100|indet|off> <libelle> : le PC pilote la barre de progression */
        if (!strcmp(line, "prog off")) {
            act_on = false;
            ESP_LOGI(TAG, "activite : fin");
            show_ui();
            return;
        }
        const char *p = line + 5;
        if (!strncmp(p, "indet", 5)) {
            const char *lab = p + 6;
            act_begin(*lab ? lab : "EN COURS", true);
            return;
        }
        int v = atoi(p);
        const char *lab = strchr(p, ' ');
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        if (act_on) {
            act_indet = false;
            act_pct = v;
            if (lab && *lab) snprintf(act_label, sizeof(act_label), "%s", lab + 1);
            act_tick++;
            act_show(false);
        } else {
            act_begin(lab && *lab ? lab + 1 : "EN COURS", false);
            act_pct = v;
            act_show(true);
        }
        return;
    }
    if (!strncmp(line, "radio", 5)) {
        if (!strncmp(line, "radio start", 11)) {
            /* radio start [libelle] [duree en secondes]
               le libelle peut contenir des espaces : on prend le dernier mot
               comme duree s'il commence par un chiffre. */
            int secs = 0;
            char lab[20] = "RADIO";
            char tmp[64] = "";
            snprintf(tmp, sizeof(tmp), "%.60s", line + 11);
            char *p = tmp;
            while (*p == ' ') p++;
            char *dernier = NULL;
            for (char *q = p; *q; q++) if (*q == ' ' && q[1]) dernier = q;
            if (dernier && (isdigit((unsigned char)dernier[1]) || dernier[1] == '.')) {
                secs = (int)atof(dernier + 1);
                *dernier = 0;
            }
            while (*p == ' ') p++;
            if (*p) snprintf(lab, sizeof(lab), "%.19s", p);
            radio_total = secs > 0 ? secs * 64000 : 0;   /* octets stereo attendus */
            radio_done = 0;
            audio_playback_set_vol(85);
            radio_mode = true;
            act_begin(lab, false);
            ESP_LOGI(TAG, "flux audio recu : %s, %d s annonces", lab, secs);
            /* accuse de reception : sans lui, les premieres trames arrivent
               pendant qu'on lit encore des lignes et tout se desynchronise. */
            serial_write_bulk((const uint8_t *)"FLUX PRET\n", 10);
        } else {
            radio_mode = false;
            act_on = false;
            ESP_LOGI(TAG, "flux audio termine");
            show_ui();
        }
        return;
    }
    if (!strncmp(line, "repond ", 7)) {
        /* le PC a deja interroge DeepSeek : on affiche sa reponse et on sort de
           l'ecran « JE REFLECHIS » (la tache de reveil voit voix_etat changer) */
        snprintf(voix_reponse, sizeof(voix_reponse), "%.200s", line + 7);
        voix_etat = 3;
        ui_page = PAGE_MAITRE;
        show_ui();
        ESP_LOGI(TAG, "reponse affichee : %.60s", voix_reponse);
        return;
    }
    if (!strcmp(line, "wake")) {
        ESP_LOGI(TAG, "reveil simule (comme un appui long sur BOOT)");
        assistant_wake();
        return;
    }
    if (!strcmp(line, "getrec")) {
        /* renvoie la derniere prise en binaire : beaucoup plus rapide que
           de l'hexa par la console (11 ko/s contre plusieurs centaines). */
        if (rec_last_bytes <= 0) { ESP_LOGW(TAG, "aucune prise en memoire"); return; }
        esp_log_level_set("*", ESP_LOG_NONE);          /* pas de log au milieu du flux */
        char hdr[32];
        int hl = snprintf(hdr, sizeof(hdr), "GETRECB %d\n", rec_last_bytes);
        serial_write_bulk((const uint8_t *)hdr, hl);
        serial_write_bulk(rec_pcm, rec_last_bytes);
        serial_write_bulk((const uint8_t *)"\nGETRECB END\n", 13);
        esp_log_level_set("*", ESP_LOG_INFO);
        ESP_LOGI(TAG, "prise envoyee : %d octets", rec_last_bytes);
        return;
    }
    if (!strncmp(line, "pcm", 3)) {
        /* pcm begin / pcm <hexa> / pcm end : le PC pousse de l'audio a jouer */
        static int pcm_pos = 0;
        if (!strcmp(line, "pcm begin")) {
            pcm_pos = 0;
            pcm_xfer = true;
            ESP_LOGI(TAG, "pcm : debut de transfert");
            return;
        }
        if (!strcmp(line, "pcm end")) {
            pcm_xfer = false;
            audio_play_mono(pcm_pos / 2);
            ESP_LOGI(TAG, "haut-parleur : %d octets joues", pcm_pos);
            pcm_pos = 0;
            return;
        }
        if (line[3] == ' ') {
            const char *h = line + 4;
            while (*h && *(h + 1) && pcm_pos + 1 < AUDIO_MAX_FRAMES * 2) {
                int hi = 0, lo = 0;
                char a = *h++, b = *h++;
                hi = (a >= '0' && a <= '9') ? a - '0' : (a >= 'a' && a <= 'f') ? a - 'a' + 10 : (a >= 'A' && a <= 'F') ? a - 'A' + 10 : -1;
                lo = (b >= '0' && b <= '9') ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : (b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
                if (hi < 0 || lo < 0) break;
                rec_pcm[pcm_pos++] = (uint8_t)((hi << 4) | lo);
            }
            return;
        }
        return;
    }
    if (!strncmp(line, "pet", 3)) {
        if (line[3] == 0) {
            ESP_LOGI(TAG, "stade=%s age=%u j sante=%u%% faim=%u bonheur=%u energie=%u proprete=%u caca=%u forme=%s gen=%u",
                     pet_stade_nom(pet_stade(&pet)), (unsigned)(pet.age_s / 86400),
                     pet_sante(&pet), (unsigned)(pet.faim / 100), (unsigned)(pet.bonheur / 100),
                     (unsigned)(pet.energie / 100), (unsigned)(pet.proprete / 100),
                     pet.caca, pet_forme_nom(pet.forme), pet.generation);
        } else if (!strncmp(line, "pet ", 4)) {
            const char *a = line + 4;
            pet_action_t ac = ACT_AUCUNE;
            if (!strcmp(a, "manger")) ac = ACT_MANGER;
            else if (!strcmp(a, "jouer")) ac = ACT_JOUER;
            else if (!strcmp(a, "calin")) ac = ACT_CALIN;
            else if (!strcmp(a, "nettoyer")) ac = ACT_NETTOYER;
            else if (!strcmp(a, "soigner")) ac = ACT_SOIGNER;
            else if (!strcmp(a, "dormir")) ac = ACT_DORMIR;
            else if (!strcmp(a, "reveiller")) ac = ACT_REVEILLER;
            else if (!strcmp(a, "neuf")) {   /* nouvel oeuf apres un depart */
                pet_nait(&pet, (uint16_t)(pet.generation + 1));
                pet_sauve();
                journal_ajoute("un nouvel oeuf est arrive");
                show_ui();
                return;
            }
            if (ac != ACT_AUCUNE) {
                journal_ajoute(pet_action(&pet, ac));
                pet_sauve();
                show_ui();
            }
        }
        return;
    }
    if (!strncmp(line, "radio", 5)) {
        /* radio play|stop|next|prev|station <n> : c'est le cerveau qui commande */
        if (!strncmp(line + 6, "play", 4))       radio_joue();
        else if (!strncmp(line + 6, "stop", 4))  radio_stop();
        else if (!strncmp(line + 6, "next", 4))  radio_change(1);
        else if (!strncmp(line + 6, "prev", 4))  radio_change(-1);
        else if (!strncmp(line + 6, "station", 7)) {
            int n = atoi(line + 14);
            if (n >= 0 && n < radios_n) { radio_sel = n; if (radio_etat) { radio_gen++; radio_etat = 1; } show_ui(); }
        } else if (radios_n > 0) {
            ESP_LOGI(TAG, "radios :");
            for (int i = 0; i < radios_n; i++) ESP_LOGI(TAG, "  %2d %s", i, radios_nom[i]);
        }
        return;
    }
    if (!strncmp(line, "vol", 3)) {
        if (line[3] == ' ') radio_vol_regle(atoi(line + 4), 1);
        else if (line[3] == '+') radio_vol_regle(radio_vol + 10, 1);
        else if (line[3] == '-') radio_vol_regle(radio_vol - 10, 1);
        else ESP_LOGI(TAG, "volume %d%%", radio_vol);
        show_ui();
        return;
    }
    /* la commande "appli N" a disparu avec les pages : "ui" redessine */
    if (!strcmp(line, "journal")) {
        for (int i = 0; i < journal_n; i++)
            ESP_LOGI(TAG, "journal %d/%d : %s", i + 1, journal_n,
                     journal_lignes[(journal_tete + 12 - journal_n + i) % 12]);
        return;
    }
    if (!strncmp(line, "btn", 3)) {          /* btn 1 | btn 2 | btn 1 long | btn 1 dbl */
        int n = (line[4] == '2') ? 1 : 0;
        int avant = ui_page;
        if (strstr(line, "long"))      bouton_long(n, 0);
        else if (strstr(line, "dbl"))  bouton_double(n);
        else                            bouton_court(n);
        ESP_LOGI(TAG, "essai bouton %d -> page ACCUEIL%s", n + 1,
                 avant != ui_page || strstr(line, "dbl") ? "" : " (ignore)");
        return;
    }
    if (!strncmp(line, "keys", 4)) {
        /* balayage des GPIO libres : sert a retrouver les boutons d'une
           variante de carte (une carte n'a que BOOT+PWR, une autre a un
           pavé directionnel — on ecoute et on regarde qui bouge) */
        static const int pins[] = { 1, 2, 3, 4, 5, 14, 15, 16, 21, 38, 45, 46 };
        const int n = (int)(sizeof(pins) / sizeof(pins[0]));
        int duree = (line[4] == ' ') ? atoi(line + 5) : 60;
        if (duree <= 0 || duree > 600) duree = 60;
        for (int i = 0; i < n; i++) {
            gpio_reset_pin((gpio_num_t)pins[i]);
            gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_INPUT);
            gpio_set_pull_mode((gpio_num_t)pins[i], GPIO_PULLUP_ONLY);
        }
        ESP_LOGI(TAG, "keys : %d s d'ecoute sur 12 GPIO — appuie sur les fleches une par une", duree);
        uint32_t avant = 0;
        for (int t = 0; t < duree * 5; t++) {
            uint32_t etat = 0;
            for (int i = 0; i < n; i++)
                if (gpio_get_level((gpio_num_t)pins[i]) == 0) etat |= (1u << i);
            if (etat != avant) {
                for (int i = 0; i < n; i++) {
                    int a = (avant >> i) & 1, b = (etat >> i) & 1;
                    if (a != b) ESP_LOGI(TAG, "keys : GPIO %d -> %s", pins[i], b ? "APPUYE" : "relache");
                }
                avant = etat;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        ESP_LOGI(TAG, "keys : fin — etat final %08x", (unsigned)avant);
        return;
    }
    if (!strncmp(line, "yt", 2) && (line[2] == 0 || line[2] == ' ')) {
        /* yt @moncompte  /  yt UCxxxxxxxxxxxxxxxxxxxxxx  : regle la chaine
           yt             : affiche ce qui est connu */
        if (line[2] == ' ') {
            const char *c = line + 3;
            while (*c == ' ') c++;
            if (!strncmp(c, "UC", 2) && strlen(c) >= 24) {
                snprintf(yt_id, sizeof(yt_id), "%.24s", c);
                yt_handle[0] = 0;
            } else if (*c) {
                if (*c == '@') snprintf(yt_handle, sizeof(yt_handle), "%s", c);
                else           snprintf(yt_handle, sizeof(yt_handle), "@%s", c);
                yt_id[0] = 0;
            }
            yt_sauve();
            yt_ok = false;
            yt_lire();                 /* tout de suite, on verra si ca marche */
        }
        ESP_LOGI(TAG, "youtube : chaine='%s' id='%s' nom='%s' video='%s' (%d vues, %s)",
                 yt_handle, yt_id, yt_nom, yt_titre, yt_vues, yt_age);
        show_ui();
        return;
    }
    if (!strcmp(line, "livre") || !strncmp(line, "livre ", 6)) {
        /* livre | livre liste | livre ouvre <nom> [page] | livre page <n>
           | livre suiv | livre prec | livre change */
        char quoi[16] = "", arg[LIVRE_TITRE_MAX] = "";
        int lu = sscanf(line, "livre %15s %39s", quoi, arg);
        if (lu <= 0) {
            if (!livre_scan_fait) livre_scan();
        } else if (!strcmp(quoi, "liste")) {
            livre_scan();
            ESP_LOGI(TAG, "livres : %d dans %s", livre_n, LIVRE_DOSSIER);
            for (int i = 0; i < livre_n; i++) ESP_LOGI(TAG, "  %d  %s", i + 1, livre_noms[i]);
            return;
        } else if (!strcmp(quoi, "ouvre")) {
            int pg = 0;
            sscanf(line, "livre ouvre %39s %d", arg, &pg);
            if (livre_ouvre(arg, pg > 0 ? pg - 1 : 0)) {
                livre_memo();
                show_ui();
            } else {
                ESP_LOGW(TAG, "livre : %s illisible", arg);
                return;
            }
        } else if (!strcmp(quoi, "page")) {
            if (!livre_pret) livre_reprise();
            int pg = atoi(arg);
            if (livre_pret && pg > 0) { livre_va(&livre, pg - 1); livre_memo(); }
            show_ui();
        } else if (!strcmp(quoi, "suiv") || !strcmp(quoi, "prec")) {
            if (!livre_pret) livre_reprise();
            livre_tourne(!strcmp(quoi, "suiv") ? 1 : -1);
            show_ui();
        } else if (!strcmp(quoi, "change")) {
            if (!livre_pret) livre_reprise();
            livre_suivant(1);
            show_ui();
        } else {
            ESP_LOGW(TAG, "livre : commande inconnue (%s)", quoi);
            return;
        }
        ESP_LOGI(TAG, "livre : %s page %d/%d, %d livre(s) sur la carte",
                 livre_pret ? livre.nom : "aucun",
                 livre_pret ? livre.page + 1 : 0,
                 livre_pret ? livre_pages(&livre) : 0, livre_n);
        return;
    }
    if (!strcmp(line, "ui") || !strncmp(line, "ui ", 3)) {
        /* il n'y a plus qu'une page : "ui" redessine le tableau, le numero
           eventuellement tape est ignore (les vieux scripts continuent) */
        ui_read_sensors();
        show_ui();
        ESP_LOGI(TAG, "page=%d visage=%s temp=%.1f humi=%.0f wifi=%s shtc=%s",
                 ui_page, EXPR[ui_face].nom, (double)ui_temp, (double)ui_humi,
                 ui_wifi_ok ? "ok" : "ko", ui_shtc_ok ? "ok" : "ko");
        return;
    }
    if (!strcmp(line, "touches")) {
        touch_log = !touch_log;
        ESP_LOGI(TAG, "journal tactile %s", touch_log ? "active" : "coupe");
        return;
    }
    if (!strcmp(line, "fb")) {
        static char hex[260];
        /* courte attente : un dump legerement en cours de dessin vaut mieux
           qu'une console bloquee si un rafraichissement traine */
        if (ui_mux) xSemaphoreTake(ui_mux, pdMS_TO_TICKS(1500));
        for (int off = 0; off < FB_LEN; off += 120) {
            int n = (FB_LEN - off) < 120 ? (FB_LEN - off) : 120;
            for (int i = 0; i < n; i++) snprintf(hex + i * 2, 3, "%02x", fb[off + i]);
            hex[n * 2] = 0;
            ESP_LOGI(TAG, "FB %d %s", off, hex);
        }
        ESP_LOGI(TAG, "FB END");
        if (ui_mux) xSemaphoreGive(ui_mux);
        return;
    }
    if (!strcmp(line, "capteurs")) {
        for (int k = 0; k < 3; k++) {
            bool ok = ui_read_sensors();
            ESP_LOGI(TAG, "SHTC3 %s : carte %.1f C -> piece %.1f C (decalage %.1f), %.1f %%HR",
                     ok ? "ok" : "muet", (double)ui_temp_brut, (double)ui_temp,
                     (double)ui_temp_offset, (double)ui_humi);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        return;
    }
    if (!strncmp(line, "offset", 6)) {
        if (line[6] == ' ') {
            float v = (float)atof(line + 7);
            if (v > -30.0f && v < 30.0f) {
                ui_temp_offset = v;
                nvs_handle_t nh;
                if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
                    nvs_set_i32(nh, "toff10", (int32_t)lroundf(v * 10.0f));
                    nvs_commit(nh);
                    nvs_close(nh);
                }
            }
        }
        ui_read_sensors();
        ESP_LOGI(TAG, "decalage temperature = %.1f C (carte %.1f -> affiche %.1f)",
                 (double)ui_temp_offset, (double)ui_temp_brut, (double)ui_temp);
        return;
    }
    if (!strcmp(line, "i2cscan")) {
        int found = 0;
        for (uint8_t a = 0x08; a < 0x78; a++) {
            if (i2c_probe_addr(a) == 0) { ESP_LOGI(TAG, "  i2c 0x%02x present", a); found++; }
        }
        ESP_LOGI(TAG, "i2cscan : %d peripherique(s)", found);
        return;
    }
    if (!strcmp(line, "touchhunt")) {
        /* chercher le vrai pin de reset du tactile : pour chaque candidat,
           impulsions 0->1 puis scan complet du bus. */
        static const int cand[] = {7, 9, 4, 5, 21, 1, 2, 3, 14, 15, 45, 46, 8, 10, 11, 12, 13, 6, 42};
        ESP_LOGI(TAG, "chasse au reset tactile (%d candidats)...", (int)(sizeof(cand) / sizeof(cand[0])));
        for (unsigned k = 0; k < sizeof(cand) / sizeof(cand[0]); k++) {
            gpio_config_t io = {};
            io.pin_bit_mask = 1ULL << cand[k];
            io.mode = GPIO_MODE_OUTPUT;
            gpio_config(&io);
            gpio_set_level((gpio_num_t)cand[k], 1);
            vTaskDelay(pdMS_TO_TICKS(120));
            gpio_set_level((gpio_num_t)cand[k], 0);
            vTaskDelay(pdMS_TO_TICKS(120));
            gpio_set_level((gpio_num_t)cand[k], 1);
            vTaskDelay(pdMS_TO_TICKS(250));
            if (i2c_probe_addr(0x38) == 0) {
                ESP_LOGW(TAG, "TACTILE TROUVE : reset sur GPIO %d", cand[k]);
                return;
            }
        }
        ESP_LOGW(TAG, "tactile : aucun pin de reset candidat ne le reveille");
        return;
    }
    if (!strcmp(line, "railcycle")) {
        /* couper puis remettre tous les rails, attendre, rescanner */
        gpio_config_t io = {};
        io.pin_bit_mask = (1ULL << 6) | (1ULL << 42) | (1ULL << 17) | (1ULL << 7);
        io.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io);
        gpio_set_level(GPIO_NUM_6, 1);    /* EPD off */
        gpio_set_level(GPIO_NUM_42, 1);   /* audio off */
        gpio_set_level(GPIO_NUM_17, 0);   /* VBAT off */
        gpio_set_level(GPIO_NUM_7, 0);
        vTaskDelay(pdMS_TO_TICKS(1500));
        gpio_set_level(GPIO_NUM_7, 1);
        gpio_set_level(GPIO_NUM_17, 1);
        gpio_set_level(GPIO_NUM_42, 0);
        gpio_set_level(GPIO_NUM_6, 0);
        vTaskDelay(pdMS_TO_TICKS(1500));
        int found = 0;
        for (uint8_t a = 0x08; a < 0x78; a++)
            if (i2c_probe_addr(a) == 0) { ESP_LOGI(TAG, "  i2c 0x%02x present", a); found++; }
        ESP_LOGI(TAG, "apres cycle des rails : %d peripherique(s), 0x38 %s",
                 found, i2c_probe_addr(0x38) == 0 ? "PRESENT" : "absent");
        return;
    }
    if (!strcmp(line, "ft")) {
        gpio_config_t vo = {};
        vo.pin_bit_mask = 1ULL << 17;
        vo.mode = GPIO_MODE_OUTPUT;
        gpio_config(&vo);
        gpio_set_level(GPIO_NUM_17, 1);   /* VBAT_PWR */
        gpio_set_level(GPIO_NUM_7, 0); vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(GPIO_NUM_7, 1); vTaskDelay(pdMS_TO_TICKS(500));
        int pr = i2c_probe_addr(0x38);
        uint8_t id = 0, fw = 0, n = 0;
        esp_err_t e1 = i2c_read_buff(ft6336_handle, 0xA3, &id, 1);
        esp_err_t e2 = i2c_read_buff(ft6336_handle, 0xA8, &fw, 1);
        esp_err_t e3 = i2c_read_buff(ft6336_handle, 0x02, &n, 1);
        ESP_LOGI(TAG, "FT6336 probe=%d id=%02x(%s) fw=%02x(%s) points=%d(%s)",
                 pr, id, esp_err_to_name(e1), fw, esp_err_to_name(e2), n, esp_err_to_name(e3));
        return;
    }

    /* filet de securite : un fragment de transfert binaire (que des chiffres
       hexa) ou une ligne pleine d'octets de controle n'est pas une phrase pour
       DeepSeek. Sans ce filtre, le bruit audio lu comme du texte part en
       requete HTTP et le modele repond a des morceaux de PCM. */
    {
        int hl = (int)strlen(line), hexonly = hl > 0, ctrl = 0;
        for (int i = 0; i < hl; i++) {
            unsigned char c = (unsigned char)line[i];
            if (!isxdigit(c)) hexonly = 0;
            if (c < 0x20 || c == 0x7f) ctrl = 1;
        }
        if (pcm_xfer || (hexonly && hl >= 16) || ctrl) {
            ESP_LOGW(TAG, "ligne binaire ignoree (%d octets)", hl);
            return;
        }
        {
            int i = 0;                       /* ligne vide ou espaces : rien a dire */
            while (line[i] == ' ' || line[i] == '\t') i++;
            if (!line[i]) return;
        }
    }

    char dit[900], emo[32];
    ESP_LOGI(TAG, "reflexion...");
    if (ds_cerveau(line, dit, sizeof(dit), emo, sizeof(emo)) == ESP_OK) {
        int e = emo_by_name(emo);
        if (e < 0) e = art_expr_de_famille(8, 0);
        current_emotion = EXPR[e].famille;
        ESP_LOGI(TAG, "Batmino [%s/%s] : %s", FAMILLES[current_emotion], EXPR[e].nom, dit);
        show_face(e);
    } else {
        ESP_LOGE(TAG, "DeepSeek muet (reseau ?)");
        current_emotion = 13;
        show_face(13);
    }
}

/* ---- banc d'essai ecran, pilote par la console USB : !<commande> ----------
   !busy            niveau de la broche BUSY
   !off | !on       rail d'alimentation de la dalle
   !init            reinitialisation complete
   !clear           init + effacement (chronometre)
   !part [n]        n rafraichissements partiels de l'image courante
   !full | !clean   rafraichissement complet (simple / avec LUT pleine)
   !st              etat du driver                                                        */
static void console_epd(const char *c)
{
    if (!strcmp(c, "pm")) {           /* qui empeche le CPU de dormir ? */
        esp_pm_dump_locks(stdout);
        fflush(stdout);
        ESP_LOGI(TAG, "energie : cpu %d MHz", (int)(esp_clk_cpu_freq() / 1000000));
        return;
    }
    /* !wifi                : etat
       !wifi <ssid> <pass>  : bascule a chaud (marche meme sans reseau) */
    if (!strncmp(c, "wifi", 4)) {
        const char *a = c + 4;
        while (*a == ' ') a++;
        if (!*a) {
            ESP_LOGI(TAG, "banc : reseau '%s' (enregistre : %d)", wifi_ssid_i(0), wifi_rt);
            return;
        }
        char s[33] = "", p[65] = "";
        const char *e = strchr(a, ' ');
        size_t n = e ? (size_t)(e - a) : strlen(a);
        if (n > 32) n = 32;
        memcpy(s, a, n);
        s[n] = 0;
        if (e) { while (*e == ' ') e++; snprintf(p, sizeof(p), "%s", e); }
        ESP_LOGI(TAG, "banc : bascule sur '%s' (mot de passe de %d caracteres)", s, (int)strlen(p));
        wifi_bascule(s, p);
        return;
    }
    if (!strcmp(c, "cpu")) {          /* qui consomme le CPU ? */
        char *b = (char *)heap_caps_malloc(3072, MALLOC_CAP_SPIRAM);
        if (b) { vTaskGetRunTimeStats(b); ESP_LOGI(TAG, "taches:\n%s", b); free(b); }
        else ESP_LOGW(TAG, "banc : pas de tampon pour les stats");
        {
            uint32_t total = 0;
            UBaseType_t n = uxTaskGetSystemState(sys_ts, 48, &total);
            uint32_t somme = 0, somme_idle = 0;
            for (UBaseType_t i = 0; i < n; i++) {
                const char *nm = sys_ts[i].pcTaskName ? sys_ts[i].pcTaskName : "";
                if (!strncmp(nm, "IDLE", 4)) somme_idle += (uint32_t)sys_ts[i].ulRunTimeCounter;
                else somme += (uint32_t)sys_ts[i].ulRunTimeCounter;
            }
            ESP_LOGI(TAG, "banc : cpu=%d%% n=%d total=%u somme=%u idle=%u",
                     sys_cpu, (int)n, (unsigned)total, (unsigned)somme, (unsigned)somme_idle);
        }
        return;
    }
    if (!epd) { ESP_LOGW(TAG, "banc : pas de dalle"); return; }
    if (!strcmp(c, "busy") || !strcmp(c, "st")) {
        ESP_LOGI(TAG, "banc : BUSY=%d panne=%d busy_ko=%d refus=%d",
                 gpio_get_level(EPD_BUSY_PIN), (int)epd->panne, epd->busy_ko, refresh_count);
        if (!strcmp(c, "busy")) return;
    }
    if (!strcmp(c, "off")) { gpio_set_level(EPD_PWR_PIN, 1); ESP_LOGI(TAG, "banc : rail coupe"); return; }
    if (!strcmp(c, "on"))  { gpio_set_level(EPD_PWR_PIN, 0); ESP_LOGI(TAG, "banc : rail remis"); return; }

    if (xSemaphoreTake(epd_mux, pdMS_TO_TICKS(20000)) != pdTRUE) {
        ESP_LOGW(TAG, "banc : mutex occupe");
        return;
    }
    int64_t t0 = esp_timer_get_time();
    if (!strcmp(c, "init")) {
        epd->EPD_Panne_Reset();
        epd->EPD_Init();
        ESP_LOGI(TAG, "banc : init -> panne=%d", (int)epd->panne);
    } else if (!strcmp(c, "clear")) {
        epd->EPD_Panne_Reset();
        epd->EPD_Init();
        epd->EPD_Clear();
        epd->EPD_Display();
        ESP_LOGI(TAG, "banc : clear -> panne=%d", (int)epd->panne);
    } else if (!strncmp(c, "part", 4)) {
        int n = atoi(c + 4);
        if (n <= 0) n = 1;
        for (int i = 0; i < n; i++) {
            int64_t a = esp_timer_get_time();
            epd->EPD_ShowPart(fb, FB_LEN);
            ESP_LOGI(TAG, "banc : partiel %d/%d en %d ms (panne=%d)", i + 1, n,
                     (int)((esp_timer_get_time() - a) / 1000), (int)epd->panne);
            if (epd->panne) break;
        }
    } else if (!strcmp(c, "full")) {
        epd->EPD_ShowFull(fb, FB_LEN);
        ESP_LOGI(TAG, "banc : full -> panne=%d", (int)epd->panne);
    } else if (!strcmp(c, "clean")) {
        epd->EPD_ShowFullClean(fb, FB_LEN);
        ESP_LOGI(TAG, "banc : clean -> panne=%d", (int)epd->panne);
    } else {
        ESP_LOGI(TAG, "banc : !busy !off !on !init !clear !part[n] !full !clean !st");
    }
    ESP_LOGI(TAG, "banc : %d ms au total", (int)((esp_timer_get_time() - t0) / 1000));
    xSemaphoreGive(epd_mux);
}

static void serial_task(void *arg)
{
    static char line[2048];
    int len = 0;
    static uint8_t chunk[256];
    static uint8_t radio_buf[4096];
    for (;;) {
        /* mode radio : le PC pousse du PCM stereo par trames [longueur][donnees] */
        if (radio_mode) {
            uint8_t hdr[4];
            if (usb_serial_jtag_read_bytes(hdr, 4, pdMS_TO_TICKS(2000)) != 4) {
                /* plus rien depuis un moment : le PC a lache, on rend la main
                   a la console (c'est la reprise apres coupure). */
                if (++radio_idle >= 3) {
                    radio_mode = false;
                    radio_idle = 0;
                    act_on = false;
                    ESP_LOGI(TAG, "flux audio coupe : retour a la console");
                    show_ui();
                }
                continue;
            }
            radio_idle = 0;
            {
                uint32_t n = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                             ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
                if (n == 0xFFFFFFFFu) {
                    radio_mode = false;
                    act_on = false;
                    ESP_LOGI(TAG, "radio : fin du flux");
                    show_ui();
                } else if (n > 0 && n <= sizeof(radio_buf)) {
                    int got = 0;
                    while (got < (int)n) {
                        int r = usb_serial_jtag_read_bytes(radio_buf + got, n - got, pdMS_TO_TICKS(2000));
                        if (r <= 0) break;
                        got += r;
                    }
                    if (got == (int)n) {
                        audio_playback_write(radio_buf, n);
                        radio_done += n;
                        if (!act_on) act_begin("RADIO", false);
                        act_indet = false;
                        if (radio_total > 0) {
                            act_pct = (int)((int64_t)radio_done * 100 / radio_total);
                            if (act_pct > 100) act_pct = 100;
                        } else {
                            int pk = 0;
                            for (uint32_t i = 0; i + 1 < n; i += 4) {
                                int16_t v = (int16_t)(radio_buf[i] | (radio_buf[i + 1] << 8));
                                int a = v < 0 ? -v : v;
                                if (a > pk) pk = a;
                            }
                            act_pct = pk * 100 / 12000;
                            if (act_pct > 100) act_pct = 100;
                        }
                        act_tick++;
                    }
                }
            }
            continue;
        }
        int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), pdMS_TO_TICKS(300));
        if (n <= 0) continue;
        for (int i = 0; i < n; i++) {
            uint8_t c = chunk[i];
            if (c == '\r') continue;
            if (c == '\n') {
                line[len] = 0;
                if (len) {
                    /* pendant un transfert PCM on n'echo pas : la console TX
                       mange le RX et fait perdre des octets */
                    if (!pcm_xfer && len < 400) ESP_LOGI(TAG, "> %s", line);
                    else if (!pcm_xfer) ESP_LOGI(TAG, "> %d octets recus", len);
                    if (line[0] == '!') console_epd(line + 1);   /* banc d'essai ecran */
                    else mino_handle_line(line);
                }
                len = 0;
                continue;
            }
            if (len < (int)sizeof(line) - 1) line[len++] = (char)c;
            else len = 0;      /* ligne trop longue : on jette */
        }
    }
}

/* ------------------------------------------------------------------ */
/* API HTTP                                                            */
/* ------------------------------------------------------------------ */
static void url_decode(char *dst, const char *src, size_t sz)
{
    size_t o = 0;
    while (*src && o + 1 < sz) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char h[3] = { src[1], src[2], 0 };
            dst[o++] = (char)strtol(h, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[o++] = ' '; src++;
        } else {
            dst[o++] = *src++;
        }
    }
    dst[o] = 0;
}

static esp_err_t qval(httpd_req_t *req, const char *key, char *out, size_t sz)
{
    char q[1600];
    out[0] = 0;
    if (httpd_req_get_url_query_len(req) <= 0) return ESP_ERR_NOT_FOUND;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return ESP_ERR_NOT_FOUND;
    char raw[1400];
    if (httpd_query_key_value(q, key, raw, sizeof(raw)) != ESP_OK) return ESP_ERR_NOT_FOUND;
    url_decode(out, raw, sz);
    return ESP_OK;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    if (s) { httpd_resp_sendstr(req, s); free(s); }
    else httpd_resp_send_500(req);
    cJSON_Delete(root);
    return ESP_OK;
}

static bool busy = false;

static esp_err_t h_face(httpd_req_t *req)
{
    char v[64];
    cJSON *r = cJSON_CreateObject();
    int e = -1;
    if (qval(req, "e", v, sizeof(v)) == ESP_OK) {
        char *endp = NULL;
        long n = strtol(v, &endp, 10);
        if (endp && *endp == 0 && n >= 0 && n < EXPR_N) e = (int)n;
        else e = emo_by_name(v);
    }
    if (e < 0) {
        cJSON_AddStringToObject(r, "error", "emotion inconnue");
        cJSON_AddItemToObject(r, "familles", cJSON_CreateStringArray(FAMILLES, FAMILLE_N));
        return send_json(req, r);
    }
    current_emotion = EXPR[e].famille;
    show_face(e);
    cJSON_AddStringToObject(r, "ok", EXPR[e].nom);
    cJSON_AddStringToObject(r, "famille", FAMILLES[current_emotion]);
    return send_json(req, r);
}

static esp_err_t h_say(httpd_req_t *req)
{
    char q[1200];
    if (qval(req, "q", q, sizeof(q)) != ESP_OK || !q[0]) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "error", "parametre q manquant");
        return send_json(req, r);
    }
    if (busy) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "error", "deja en train de reflechir");
        httpd_resp_set_status(req, "429 Too Many Requests");
        return send_json(req, r);
    }
    busy = true;
    int64_t t0 = esp_timer_get_time();
    char dit[900], emo[32];
    esp_err_t err = ds_cerveau(q, dit, sizeof(dit), emo, sizeof(emo));
    int64_t ms = (esp_timer_get_time() - t0) / 1000;
    busy = false;

    cJSON *r = cJSON_CreateObject();
    if (err == ESP_OK) {
        int e = emo_by_name(emo);
        if (e < 0) e = art_expr_de_famille(8, 0);
        current_emotion = EXPR[e].famille;
        bat5_du_pet = famille_vers_bat5(current_emotion);
        snprintf(voix_reponse, sizeof(voix_reponse), "%.200s", dit);
        voix_etat = 3;                        /* je reponds, en grand */
        ui_page = PAGE_MAITRE;
        show_ui();
        cJSON_AddStringToObject(r, "emotion", FAMILLES[current_emotion]);
        cJSON_AddStringToObject(r, "visage", EXPR[e].nom);
        cJSON_AddStringToObject(r, "dit", dit);
        cJSON_AddNumberToObject(r, "ms", (double)ms);
        show_face(e);
        journal_ajoute(dit);
        ESP_LOGI(TAG, "Q: %s", q);
        ESP_LOGI(TAG, "A: [%s] %s", EXPR[e].nom, dit);
        voix_etat = 3;
        show_ui();
        tts_parle(dit);                  /* il le dit a voix haute */
        voix_etat = 0;                   /* et on revient au tableau de bord */
        ui_page = PAGE_MAITRE;
        show_ui();
    } else {
        cJSON_AddStringToObject(r, "error", "DeepSeek injoignable");
        show_face(emo_by_name("confusion"));
    }
    return send_json(req, r);
}

static esp_err_t h_note(httpd_req_t *req)
{
    char t[512];
    cJSON *r = cJSON_CreateObject();
    if (qval(req, "t", t, sizeof(t)) == ESP_OK && t[0]) {
        mem_add(t);
        cJSON_AddStringToObject(r, "ok", "note enregistree");
        cJSON_AddStringToObject(r, "note", t);
    } else {
        cJSON_AddStringToObject(r, "error", "parametre t manquant");
    }
    return send_json(req, r);
}

static esp_err_t h_radio(httpd_req_t *req)
{
    cJSON *r = cJSON_CreateObject();
    char q[128] = {0}, a[32] = {0}, n[8] = {0};
    if (httpd_req_get_url_query_len(req) > 0) {
        httpd_req_get_url_query_str(req, q, sizeof(q));
        httpd_query_key_value(q, "a", a, sizeof(a));
        httpd_query_key_value(q, "n", n, sizeof(n));
    }
    if (n[0]) radio_sel = atoi(n) % (radios_n > 0 ? radios_n : 1);
    {
        char v[8] = {0};
        httpd_query_key_value(q, "v", v, sizeof(v));
        if (v[0]) radio_vol_regle(atoi(v), 1);
    }
    {
        char act[8] = {0};
        httpd_query_key_value(q, "actif", act, sizeof(act));
        if (act[0]) {
            radio_actif = (act[0] == '1');
            nvs_handle_t nh;
            if (nvs_open("mino", NVS_READWRITE, &nh) == ESP_OK) {
                nvs_set_i32(nh, "radio", radio_actif ? 1 : 0);
                nvs_commit(nh); nvs_close(nh);
            }
            if (!radio_actif) radio_stop();
            ESP_LOGW(TAG, "module radio %s", radio_actif ? "ACTIVE" : "desactive");
        }
    }
    if (!radio_actif) {
        cJSON_AddBoolToObject(r, "actif", false);
        cJSON_AddStringToObject(r, "etat", "desactivee");
        cJSON_AddStringToObject(r, "aide", "activer : /radio?actif=1");
        return send_json(req, r);
    }
    if (!strcmp(a, "play"))       radio_joue();
    else if (!strcmp(a, "stop"))  radio_stop();
    else if (!strcmp(a, "next"))  radio_change(1);
    else if (!strcmp(a, "prev"))  radio_change(-1);
    else if (n[0])                { radio_sel = atoi(n) % (radios_n > 0 ? radios_n : 1); show_ui(); }
    cJSON_AddNumberToObject(r, "station", radio_sel);
    cJSON_AddNumberToObject(r, "stations", radios_n);
    cJSON_AddStringToObject(r, "etat", radio_etat == 2 ? "en lecture" : (radio_etat == 1 ? "je charge" : "a l arret"));
    if (radios_n > 0) {
        cJSON_AddStringToObject(r, "nom", radios_nom[radio_sel]);
        cJSON_AddStringToObject(r, "url", radios_url[radio_sel]);
    }
    cJSON_AddNumberToObject(r, "volume", radio_vol);
    cJSON_AddNumberToObject(r, "fichier_ok", radios_fichier_ok);
    cJSON_AddNumberToObject(r, "lu_sur_carte", radios_dernier_lu);
    {
        cJSON *lst = cJSON_AddArrayToObject(r, "liste");
        for (int i = 0; i < radios_n; i++) cJSON_AddItemToArray(lst, cJSON_CreateString(radios_nom[i]));
    }
    cJSON_AddNumberToObject(r, "octets_recus", (double)radio_done);
    return send_json(req, r);
}

static esp_err_t h_pet(httpd_req_t *req)
{
    cJSON *r = cJSON_CreateObject();
    char q[64] = {0}, a[32] = {0};
    pet_action_t ac = ACT_AUCUNE;
    if (httpd_req_get_url_query_len(req) > 0) {
        httpd_req_get_url_query_str(req, q, sizeof(q));
        if (httpd_query_key_value(q, "a", a, sizeof(a)) == ESP_OK) {
            if (!strcmp(a, "manger")) ac = ACT_MANGER;
            else if (!strcmp(a, "jouer")) ac = ACT_JOUER;
            else if (!strcmp(a, "calin")) ac = ACT_CALIN;
            else if (!strcmp(a, "nettoyer")) ac = ACT_NETTOYER;
            else if (!strcmp(a, "soigner")) ac = ACT_SOIGNER;
            else if (!strcmp(a, "dormir")) ac = ACT_DORMIR;
            else if (!strcmp(a, "reveiller")) ac = ACT_REVEILLER;
        }
    }
    xSemaphoreTake(pet_mux, portMAX_DELAY);
    if (ac != ACT_AUCUNE) {
        char ligne[64];
        snprintf(ligne, sizeof(ligne), "%s", pet_action(&pet, ac));
        journal_ajoute(ligne);
        cJSON_AddStringToObject(r, "fait", ligne);
        pet_sauve();
    }
    cJSON_AddStringToObject(r, "stade", pet_stade_nom(pet_stade(&pet)));
    cJSON_AddStringToObject(r, "forme", pet_forme_nom(pet.forme));
    cJSON_AddNumberToObject(r, "sante", (double)pet_sante(&pet));
    cJSON_AddNumberToObject(r, "faim", (double)(pet.faim / 100));
    cJSON_AddNumberToObject(r, "bonheur", (double)(pet.bonheur / 100));
    cJSON_AddNumberToObject(r, "energie", (double)(pet.energie / 100));
    cJSON_AddNumberToObject(r, "proprete", (double)(pet.proprete / 100));
    cJSON_AddNumberToObject(r, "age_s", (double)pet.age_s);
    xSemaphoreGive(pet_mux);
    show_ui();
    return send_json(req, r);
}

static esp_err_t h_reglages(httpd_req_t *req);   /* defini plus bas */

/* ================================================================== */
/* LE CONSEIL : une passe de cerveau qui ne repond PAS a une question. */
/* Elle juge la situation avec ce que la carte sait deja, et n'ecrit   */
/* que ce qui merite d'etre lu. Aucun rappel ne peut en sortir.        */
/* ================================================================== */
#define CONSEIL_PERIODE_S 1800     /* une passe toutes les 30 minutes */
#define CONSEIL_MIN_S      420     /* jamais deux passes rapprochees */

static int conseil_batterie(void)
{
    int mv = (sys_bat_mv_cache >= 0) ? sys_bat_mv_cache : sys_bat_mv();
    return (mv > 0) ? sys_bat_pct(mv) : -1;
}

static void conseil_faits_maj(void)
{
    char b[64];
    size_t o = 0;
    b[0] = 0;
    if (ms_dans >= 0 && ms_titre[0]) {
        if (ms_dans < 60)
            o += (size_t)snprintf(b + o, sizeof(b) - o, "RDV %dMIN", ms_dans);
        else if (ms_dans < 1440)
            o += (size_t)snprintf(b + o, sizeof(b) - o, "RDV %dH%02d", ms_dans / 60, ms_dans % 60);
        else
            o += (size_t)snprintf(b + o, sizeof(b) - o, "RDV %dJ", ms_dans / 1440);
    }
    int bat = conseil_batterie();
    if (bat >= 0 && o + 14 < sizeof(b))
        o += (size_t)snprintf(b + o, sizeof(b) - o, "%sBAT %d%%", o ? "  " : "", bat);
    if (!o) snprintf(b, sizeof(b), "PAS DE DONNEES");
    snprintf(conseil_faits, sizeof(conseil_faits), "%s", b);
}

/* la reponse du modele arrive en UTF-8 avec des accents et des guillemets :
   la dalle ne connait que l'ASCII et les majuscules de la police 3x5. */
static void conseil_nettoie(char *s)
{
    char *d = s;
    for (const char *p = s; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '"' || *p == '*' || *p == '#') continue;
        if (*p == ' ' && d > s && d[-1] == ' ') continue;
        *d++ = *p;
    }
    while (d > s && d[-1] == ' ') d--;
    *d = 0;
    for (char *p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
}

static void conseil_une_passe(void)
{
    if (conseil_occupe) return;
    if (!ia_key()[0]) { conseil_etat = 2; return; }
    if (!(xEventGroupGetBits(wifi_eg) & WIFI_OK_BIT)) { conseil_etat = 3; return; }
    /* on ne vole ni le reseau ni la RAM d'une session en cours */
    if (act_on || wake_busy || radio_etat || voix_etat) { conseil_etat = 4; return; }

    conseil_occupe = true;
    char *q   = (char *)heap_caps_malloc(760, MALLOC_CAP_SPIRAM);
    char *dit = (char *)heap_caps_malloc(420, MALLOC_CAP_SPIRAM);
    char *emo = (char *)heap_caps_malloc(24,  MALLOC_CAP_SPIRAM);
    esp_err_t e = ESP_FAIL;
    if (q && dit && emo) {
        char jour[96];
        date_fr(jour, sizeof(jour));
        size_t o = 0;
        o += (size_t)snprintf(q + o, 760 - o,
                              "PASSE INTERNE : personne ne te parle, c'est ma surveillance. "
                              "Date : %s. ", jour);
        if (ms_dans >= 0 && ms_titre[0] && o < 600) {
            /* le delai est donne en clair : un "1511 minutes" se faisait
               traduire en "onze minutes" par le modele, et le conseil etait faux */
            char delai[48];
            if (ms_dans < 60)
                snprintf(delai, sizeof(delai), "%d MINUTES", ms_dans);
            else if (ms_dans < 1440)
                snprintf(delai, sizeof(delai), "%d HEURES %02d", ms_dans / 60, ms_dans % 60);
            else
                snprintf(delai, sizeof(delai), "%d JOUR%s ET %d HEURES",
                         ms_dans / 1440, (ms_dans / 1440) > 1 ? "S" : "",
                         (ms_dans % 1440) / 60);
            o += (size_t)snprintf(q + o, 760 - o, "Rendez-vous '%s' a %s, c'est-a-dire DANS %s. ",
                                  ms_titre, ms_heure, delai);
        }
        if (meteo_ok && o < 660)
            o += (size_t)snprintf(q + o, 760 - o, "Dehors %.0f degres, %s. ",
                                  (double)meteo_temp, meteo_texte(meteo_code));
        {
            int bat = conseil_batterie();
            if (bat >= 0 && o < 660)
                o += (size_t)snprintf(q + o, 760 - o, "Batterie %d%%. ", bat);
        }
        if (o < 660)
            snprintf(q + o, 760 - o,
                     "Question : qu'est-ce que je dois retenir ou faire maintenant ? "
                     "Une seule phrase, 15 mots maximum, un ordre concret (partir, brancher, "
                     "preparer, dormir...). Reprends le delai tel qu'il est ecrit : ne le "
                     "recalcule pas, n'invente aucun chiffre. S'il n'y a rien avant plusieurs "
                     "heures et que tout va bien, mets RIEN. "
                     "REPONDS UNIQUEMENT L'OBJET JSON DE TES REGLES, avec ta phrase dans "
                     "\"dit\" ; n'utilise ni cherche, ni souvien, ni rappel.");
        rappel_ce_tour = true;      /* une passe interne ne cree jamais de rappel */
        e = ds_chat(q, dit, 420, emo, 24);
        rappel_ce_tour = false;
    }
    if (e == ESP_OK && dit) {
        char ascii[300];
        texte_ascii(dit, ascii, sizeof(ascii));
        conseil_nettoie(ascii);
        bool rien = (ascii[0] == 0) || (strstr(ascii, "RIEN") && strlen(ascii) <= 18);
        if (rien) {
            conseil_txt[0] = 0;
            conseil_vide = true;
        } else {
            snprintf(conseil_txt, sizeof(conseil_txt), "%s", ascii);
            conseil_vide = false;
        }
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        snprintf(conseil_quand, sizeof(conseil_quand), "%02dH%02d", tm.tm_hour, tm.tm_min);
        conseil_faits_maj();
        conseil_passes++;
        conseil_etat = 1;
        ESP_LOGI(TAG, "conseil %d : %s", (int)conseil_passes,
                 conseil_vide ? "(rien a signaler)" : conseil_txt);
    } else {
        conseil_etat = 5;
        ESP_LOGW(TAG, "conseil : passe sans reponse (%s)", esp_err_to_name(e));
    }
    if (q) free(q);
    if (dit) free(dit);
    if (emo) free(emo);
    conseil_occupe = false;
}

static void conseil_tache(void *arg)
{
    (void)arg;
    /* elle attend le reseau sans retarder le demarrage de la carte */
    xEventGroupWaitBits(wifi_eg, WIFI_OK_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(12000));    /* meteo, agenda et heure d'abord */
    int64_t dernier = 0;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000000;
        bool forcee   = conseil_demande;
        bool echeance = (dernier == 0) || (now - dernier >= CONSEIL_PERIODE_S);
        /* un rendez-vous proche change la donne : on rejuge sans attendre */
        bool urgence  = (dernier > 0) && (now - dernier >= CONSEIL_MIN_S) &&
                        (ms_dans >= 0 && ms_dans <= 120);
        if ((forcee || echeance || urgence) && !conseil_occupe) {
            conseil_demande = false;
            conseil_une_passe();
            dernier = esp_timer_get_time() / 1000000;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static esp_err_t h_conseil(httpd_req_t *req)
{
    char q[64];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && strstr(q, "o=1"))
        conseil_demande = true;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "conseil", conseil_txt);
    cJSON_AddStringToObject(r, "faits", conseil_faits);
    cJSON_AddStringToObject(r, "quand", conseil_quand);
    cJSON_AddBoolToObject(r, "vide", conseil_vide);
    cJSON_AddNumberToObject(r, "passes", (double)conseil_passes);
    cJSON_AddBoolToObject(r, "occupe", conseil_occupe);
    cJSON_AddNumberToObject(r, "etat", (double)conseil_etat);
    return send_json(req, r);
}

static esp_err_t h_status(httpd_req_t *req)
{
    /* un navigateur demande du HTML : on lui donne la page de reglages, pour
       que http://192.168.8.210 fonctionne directement (curl recoit le JSON) */
    {
        char acc[80] = "";
        if (httpd_req_get_hdr_value_str(req, "Accept", acc, sizeof(acc)) == ESP_OK &&
            strstr(acc, "text/html"))
            return h_reglages(req);
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "qui", "Mino");
    cJSON_AddStringToObject(r, "app", APP_VER);
    cJSON_AddStringToObject(r, "ia_brut", ia_brut);
    cJSON_AddStringToObject(r, "emotion", FAMILLES[current_emotion]);
    cJSON_AddStringToObject(r, "stade", pet_stade_nom(pet_stade(&pet)));
    cJSON_AddStringToObject(r, "forme", pet_forme_nom(pet.forme));
    cJSON_AddNumberToObject(r, "age_s", (double)pet.age_s);
    cJSON_AddNumberToObject(r, "faim", (double)(pet.faim / 100));
    cJSON_AddNumberToObject(r, "bonheur", (double)(pet.bonheur / 100));
    cJSON_AddNumberToObject(r, "energie", (double)(pet.energie / 100));
    cJSON_AddNumberToObject(r, "proprete", (double)(pet.proprete / 100));
    cJSON_AddNumberToObject(r, "sante", (double)pet_sante(&pet));
    cJSON_AddNumberToObject(r, "cpu_mhz", (double)(esp_clk_cpu_freq() / 1000000));
    cJSON_AddNumberToObject(r, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(r, "heap_libre", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(r, "historique_tours", hist_p ? (double)hist.n : 0);
    cJSON_AddStringToObject(r, "yt_chaine", yt_handle);
    cJSON_AddStringToObject(r, "yt_video", yt_titre);
    cJSON_AddNumberToObject(r, "yt_vues", (double)yt_vues);
    cJSON_AddBoolToObject(r, "hist_sd", hist_sd_pret);
    cJSON_AddNumberToObject(r, "hist_sd_n", (double)hist_sd_n);
    cJSON_AddNumberToObject(r, "hist_sd_tours", (double)hist_sd_tours);
    cJSON_AddNumberToObject(r, "hist_sd_base", (double)hist_sd_base);
    cJSON_AddNumberToObject(r, "hist_sd_ko", (double)hist_sd_erreurs);
    cJSON_AddBoolToObject(r, "veille_nuit", veille_nuit);
    cJSON_AddBoolToObject(r, "rotation", rotation_on);
    cJSON_AddNumberToObject(r, "rotation_s", (double)rotation_s);
    cJSON_AddNumberToObject(r, "matrix", (double)mtx_etape);   /* 0..3 pluie, 4 rapport */
    cJSON_AddStringToObject(r, "hist_sd_etat", hist_sd_pourquoi);
    cJSON_AddStringToObject(r, "astuce", astuce_txt);
    cJSON_AddStringToObject(r, "astuce_src", astuce_src);
    cJSON_AddStringToObject(r, "astuce_quand", astuce_quand);
    cJSON_AddNumberToObject(r, "astuce_jour", (double)astuce_jour);
    cJSON_AddBoolToObject(r, "voix_sortie", voix_sortie);
    cJSON_AddNumberToObject(r, "voix_dit_nb", (double)voix_dit_nb);
    cJSON_AddBoolToObject(r, "reponse_ouverte", rep_ouvert);
    cJSON_AddNumberToObject(r, "reponse_pages", rep_pages);
    cJSON_AddNumberToObject(r, "reponse_page", rep_page);
    cJSON_AddNumberToObject(r, "contexte_octets", (double)contexte_hist_octets);
    cJSON_AddNumberToObject(r, "heap_interne_max", (double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "heap_dma", (double)heap_caps_get_free_size(MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(r, "reset", (double)esp_reset_reason());
    cJSON_AddStringToObject(r, "wifi_ssid", wifi_ssid_i(0));
    cJSON_AddStringToObject(r, "wifi_actif", wifi_actif);
    cJSON_AddStringToObject(r, "cal_titre", ms_titre);
    cJSON_AddBoolToObject(r, "cal_ical", ical_url[0] != 0);
    cJSON_AddStringToObject(r, "cal_heure", ms_heure);
    cJSON_AddStringToObject(r, "cal_invitant", ms_org);
    cJSON_AddBoolToObject(r, "alarme", alarme_on);
    cJSON_AddNumberToObject(r, "alarme_min", ALARME_MIN);
    cJSON_AddNumberToObject(r, "alarme_jouee", alarme_jouee);
    cJSON_AddNumberToObject(r, "rappel_nb", rappel_nb);
    cJSON_AddStringToObject(r, "rappel_dernier", rappel_dernier);
    cJSON_AddStringToObject(r, "annonce", annonce_txt);
    cJSON_AddNumberToObject(r, "cal_dans", ms_dans);
    cJSON_AddItemToObject(r, "boutons", btn_etat_json());
    cJSON_AddBoolToObject(r, "wifi_enregistre", wifi_rt ? 1 : 0);
    cJSON_AddNumberToObject(r, "heap_dma_max", (double)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(r, "heap_interne_max",
                            (double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "heap_dma_max",
                            (double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(r, "psram_libre", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddItemToObject(r, "familles", cJSON_CreateStringArray(FAMILLES, FAMILLE_N));
    cJSON_AddItemToObject(r, "memoire", cJSON_Duplicate(cJSON_GetObjectItem(mem_obj, "notes"), 1));
    cJSON_AddNumberToObject(r, "voix_etat", (double)voix_etat);
    cJSON_AddStringToObject(r, "voix_dit", voix_reponse);
    cJSON_AddStringToObject(r, "voix_entendu", voix_entendu_txt);
    cJSON_AddBoolToObject(r, "cle_ia", (bool)(ia_cle[0] != 0 || GROQ_KEY[0] != 0));
    cJSON_AddNumberToObject(r, "mic_db", (double)mic_db);
    cJSON_AddNumberToObject(r, "mic_crete", (double)mic_crete);
    cJSON_AddNumberToObject(r, "minuteur_s", (double)minuteur_restant_s());
    cJSON_AddNumberToObject(r, "mic_bruit", (double)mic_bruit);
    cJSON_AddNumberToObject(r, "mic_seuil", (double)mic_seuil);
    cJSON_AddNumberToObject(r, "conso_mw", (double)conso_mw());
    cJSON_AddNumberToObject(r, "epd_busy_ko", epd ? epd->busy_ko : -1);
    cJSON_AddNumberToObject(r, "cpu_pct", sys_cpu);
    cJSON_AddNumberToObject(r, "cpu_pct10", sys_cpu10);
    {
        /* valeur en cache : /state ne doit pas rallumer l'ADC de la page
           systeme, sinon la mesure garde la ressource allouee en permanence */
        cJSON_AddNumberToObject(r, "bat_mv", sys_bat_mv_cache);
        cJSON_AddNumberToObject(r, "bat_pct", sys_bat_pct_cache);
    }
    {
        uint64_t tot = 0, lib = 0;
        cJSON_AddNumberToObject(r, "net_rx_s", sys_rx_s);
        cJSON_AddNumberToObject(r, "net_tx_s", sys_tx_s);
        if (sd_ok && esp_vfs_fat_info("/sdcard", &tot, &lib) == ESP_OK)
            cJSON_AddNumberToObject(r, "sd_pct", tot ? (double)((tot - lib) * 100.0 / tot) : 0);
    }
    cJSON_AddNumberToObject(r, "bouche", (double)voix_bouche);
    cJSON_AddNumberToObject(r, "compagnon_x", (double)kirby_x);
    cJSON_AddNumberToObject(r, "compagnon_y", (double)kirby_y);
    cJSON_AddNumberToObject(r, "stt_http", (double)stt_http);
    cJSON_AddNumberToObject(r, "stt_ecrits", (double)stt_ecrits);
    cJSON_AddNumberToObject(r, "stt_total", (double)stt_total);
    cJSON_AddStringToObject(r, "stt_detail", stt_detail);
    cJSON_AddStringToObject(r, "ia_base", ia_base);
    cJSON_AddStringToObject(r, "ia_mod", ia_mod);
    cJSON_AddStringToObject(r, "langue", langue);
    cJSON_AddNumberToObject(r, "chrono_stt_ms", (double)chrono_stt_ms);
    cJSON_AddNumberToObject(r, "chrono_llm_ms", (double)chrono_llm_ms);
    cJSON_AddNumberToObject(r, "chrono_tts_ms", (double)chrono_tts_ms);
    cJSON_AddNumberToObject(r, "radio_etat", (double)radio_etat);
    cJSON_AddStringToObject(r, "radio_station", radios_n > 0 ? radios_nom[radio_sel] : "");
    cJSON_AddStringToObject(r, "page", ui_page == PAGE_ASTUCE ? "ASTUCE" : "ACCUEIL");
    cJSON_AddStringToObject(r, "livre", livre_pret ? livre.nom : "");
    cJSON_AddStringToObject(r, "livre_fichier", livre_dernier);
    cJSON_AddNumberToObject(r, "livre_page", livre_pret ? (double)(livre.page + 1) : 0);
    cJSON_AddNumberToObject(r, "livre_pages", livre_pret ? (double)livre_pages(&livre) : 0);
    cJSON_AddNumberToObject(r, "livre_pct", livre_pret ? (double)livre_pct(&livre) : 0);
    cJSON_AddNumberToObject(r, "livres_sd", (double)livre_n);
    {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        char hb[80];
        snprintf(hb, sizeof(hb), "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        cJSON_AddStringToObject(r, "heure", hb);
    }
    cJSON_AddNumberToObject(r, "meteo_c", meteo_ok ? (double)meteo_temp : -999.0);
    cJSON_AddStringToObject(r, "conseil", conseil_txt);
    cJSON_AddStringToObject(r, "conseil_quand", conseil_quand);
    cJSON_AddStringToObject(r, "conseil_faits", conseil_faits);
    cJSON_AddNumberToObject(r, "conseil_passes", (double)conseil_passes);
    return send_json(req, r);
}

static esp_err_t h_soul(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        /* Remplace sa personnalite a chaud : ecrit sur la carte SD et en RAM. */
        int n = req->content_len;
        if (n <= 0 || n > 8000) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "taille du soul");
            return ESP_FAIL;
        }
        char *buf = (char *)malloc(n + 1);
        if (!buf) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "memoire");
            return ESP_FAIL;
        }
        int rd = httpd_req_recv(req, buf, n);
        if (rd <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "lecture");
            return ESP_FAIL;
        }
        buf[rd] = 0;
        if (soul_txt) free(soul_txt);
        soul_txt = buf;
        if (sd_ok) {
            FILE *f = fopen(SD_SOUL, "w");
            if (f) { fputs(buf, f); fclose(f); }
        }
        ESP_LOGI(TAG, "SOUL remplace : %d octets", rd);
        httpd_resp_sendstr(req, "soul enregistre");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, soul_txt ? soul_txt : "");
    return ESP_OK;
}


/* ------------------------------------------------------------------ */
/* API des reglages : /wscan, /wifi, /reglages (page pour le telephone) */
/* ------------------------------------------------------------------ */
static void url_decode(char *d)
{
    char *s = d, *o = d;
    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *o++ = ' ';
            s++;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

static esp_err_t h_wscan(httpd_req_t *req)
{
    char q[160] = "";
    if (httpd_req_get_url_query_len(req) > 0 &&
        httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char go[8];
        if (httpd_query_key_value(q, "go", go, sizeof(go)) == ESP_OK && go[0] == '1')
            g_wscan_demande = true;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "encours", (g_wscan_encours || g_wscan_demande) ? 1 : 0);
    cJSON_AddStringToObject(o, "actuel", wifi_ssid_i(0));
    cJSON *ap = cJSON_AddArrayToObject(o, "ap");
    for (int i = 0; i < g_wscan_n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "s", g_wscan[i].ssid);
        cJSON_AddNumberToObject(e, "r", g_wscan[i].rssi);
        cJSON_AddNumberToObject(e, "a", g_wscan[i].auth);
        cJSON_AddItemToArray(ap, e);
    }
    char *js = cJSON_PrintUnformatted(o);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, js ? js : "{}");
    free(js);
    cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t h_wifi(httpd_req_t *req)
{
    char q[400] = "";
    char ssid[80] = "", pass[120] = "";
    if (httpd_req_get_url_query_len(req) > 0)
        httpd_req_get_url_query_str(req, q, sizeof(q));
    if (!q[0] || httpd_query_key_value(q, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"ssid manquant\"}");
        return ESP_OK;
    }
    if (httpd_query_key_value(q, "pass", pass, sizeof(pass)) != ESP_OK) pass[0] = 0;
    url_decode(ssid);
    url_decode(pass);
    if (!ssid[0]) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"ssid vide\"}");
        return ESP_OK;
    }
    /* un WPA2 avec 2 caracteres ne peut pas marcher : on refuse au lieu de
       laisser le robot se couper du reseau (c'est arrive) */
    {
        int auth = -1;
        for (int i = 0; i < g_wscan_n; i++)
            if (!strcmp(g_wscan[i].ssid, ssid)) { auth = g_wscan[i].auth; break; }
        size_t lp = strlen(pass);
        if (auth == 0 && lp == 0) {
            /* reseau ouvert : rien a faire */
        } else if (lp < 8) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"mot de passe trop court (8 caracteres mini)\"}");
            return ESP_OK;
        }
        if (strlen(ssid) > 32) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"nom de reseau trop long\"}");
            return ESP_OK;
        }
    }
    wifi_bascule(ssid, pass);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* la page de reglages, servie par le robot : on s'en sert depuis le telephone */
static const char PAGE_REGLAGES[] =
"<!doctype html><html lang=fr><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>MINO</title><style>"
"body{font:16px system-ui;margin:0;padding:16px;background:#111;color:#eee}"
"h1{font-size:20px;margin:0 0 4px}p{color:#999;margin:4px 0 16px}"
".ap{display:flex;justify-content:space-between;padding:12px;margin:6px 0;"
"background:#1e1e1e;border-radius:10px}.ap:active{background:#333}"
".r{color:#6c6}.w{color:#eb4}b{color:#6af}"
"input,button{font:16px system-ui;padding:10px;border-radius:8px;border:0;width:100%;box-sizing:border-box}"
"button{background:#2d7;color:#000;font-weight:600;margin-top:8px}"
"#msg{color:#eb4;margin:8px 0}</style>"
"<h1>MINO</h1><p id=cur>...</p>"
"<div id=liste></div><div id=msg></div>"
"<input id=ssid placeholder='nom du reseau (reseau cache)'>"
"<input id=pass type=password placeholder='mot de passe'>"
"<button onclick='envoie()'>Connecter</button>"
"<script>"
"async function scan(){"
"let r=await(await fetch('/wscan?go=1')).json();"
"if(r.encours){setTimeout(scan,1500);return}"
"document.getElementById('cur').textContent='reseau actuel : '+(r.actuel||'aucun');"
"let l=document.getElementById('liste');l.innerHTML='';"
"r.ap.forEach(a=>{let d=document.createElement('div');d.className='ap';"
"let b=a.r>-55?'r':(a.r>-70?'w':'');"
"d.innerHTML='<span>'+a.s+(a.a?'':' (ouvert)')+'</span><span class='+b+'>'+a.r+' dBm</span>';"
"d.onclick=()=>{document.getElementById('ssid').value=a.s;"
"if(a.a){let p=prompt('mot de passe pour '+a.s);if(p===null)return;"
"fetch('/wifi?ssid='+encodeURIComponent(a.s)+'&pass='+encodeURIComponent(p))"
".then(x=>x.json()).then(j=>document.getElementById('msg').textContent=j.ok?'connecte, redemarrage du wifi...':'erreur : '+(j.err||'inconnue'));}"
"else fetch('/wifi?ssid='+encodeURIComponent(a.s)+'&pass=')"
".then(()=>document.getElementById('msg').textContent='connecte');};"
"l.appendChild(d)})}"
"function envoie(){let s=document.getElementById('ssid').value,"
"p=document.getElementById('pass').value;"
"fetch('/wifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))"
".then(x=>x.json()).then(j=>document.getElementById('msg').textContent=j.ok?'enregistre, le robot se reconnecte':'erreur : '+(j.err||'inconnue'))}"
"scan();</script>";

static esp_err_t h_reglages(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, PAGE_REGLAGES, sizeof(PAGE_REGLAGES) - 1);
    return ESP_OK;
}

static void api_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 48;   /* 40 ne suffisait plus : /conseil et /ical en
                                    trop, "no slots left" (2 URI perdues) */
    cfg.stack_size = 12288;   /* h_say fait du TLS : 8 Ko ne suffisaient pas */
    cfg.max_open_sockets = 2; /* 7 sockets par defaut = 7 piles de 16 Ko (112 Ko
                                 de RAM interne) ; on n'a jamais plus de 2 clients */
    cfg.lru_purge_enable = true;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "serveur HTTP KO");
        return;
    }
    httpd_uri_t u;
    memset(&u, 0, sizeof(u));
    u.method = HTTP_GET;
    u.handler = h_status; u.uri = "/";    httpd_register_uri_handler(srv, &u);
    u.uri = "/state";                     httpd_register_uri_handler(srv, &u);
    u.handler = h_face; u.uri = "/face";  httpd_register_uri_handler(srv, &u);
    u.handler = h_say;  u.uri = "/say";   httpd_register_uri_handler(srv, &u);
    u.handler = h_note; u.uri = "/note";  httpd_register_uri_handler(srv, &u);
    u.handler = h_soul; u.uri = "/soul";  httpd_register_uri_handler(srv, &u);
    u.method = HTTP_POST;                 httpd_register_uri_handler(srv, &u);
    u.method = HTTP_GET;
    u.handler = h_rec;  u.uri = "/rec";   httpd_register_uri_handler(srv, &u);
    u.handler = h_pet;  u.uri = "/pet";  httpd_register_uri_handler(srv, &u);
    u.handler = h_radio; u.uri = "/radio"; httpd_register_uri_handler(srv, &u);
    u.handler = h_sd_list; u.uri = "/sd"; httpd_register_uri_handler(srv, &u);
    u.handler = h_livre; u.uri = "/livre"; httpd_register_uri_handler(srv, &u);
    u.handler = h_sd_format; u.uri = "/sdformat"; httpd_register_uri_handler(srv, &u);
    u.handler = h_swap; u.uri = "/swap"; httpd_register_uri_handler(srv, &u);
    u.handler = h_part; u.uri = "/part"; httpd_register_uri_handler(srv, &u);
    u.handler = h_cle;  u.uri = "/cle";   httpd_register_uri_handler(srv, &u);
    u.handler = h_mic;  u.uri = "/mic";   httpd_register_uri_handler(srv, &u);
    u.handler = h_tts;  u.uri = "/tts";   httpd_register_uri_handler(srv, &u);
    u.handler = h_modele; u.uri = "/modele"; httpd_register_uri_handler(srv, &u);
    u.handler = h_langue; u.uri = "/langue"; httpd_register_uri_handler(srv, &u);
    u.handler = h_infos;  u.uri = "/infos";  httpd_register_uri_handler(srv, &u);
    u.handler = h_wscan;  u.uri = "/wscan";   httpd_register_uri_handler(srv, &u);
    u.handler = h_caltoken; u.uri = "/caltoken"; u.method = HTTP_POST;
    httpd_register_uri_handler(srv, &u);
    u.handler = h_maj;   u.uri = "/maj";      u.method = HTTP_POST;
    httpd_register_uri_handler(srv, &u); u.method = HTTP_GET;
    u.handler = h_conseil; u.uri = "/conseil"; httpd_register_uri_handler(srv, &u);
    u.handler = h_ical;  u.uri = "/ical";     httpd_register_uri_handler(srv, &u);
    u.handler = h_wifi;   u.uri = "/wifi";    httpd_register_uri_handler(srv, &u);
    u.handler = h_reglages; u.uri = "/reglages"; httpd_register_uri_handler(srv, &u);
    u.handler = h_minuteur; u.uri = "/minuteur"; httpd_register_uri_handler(srv, &u);
    u.handler = h_timer; u.uri = "/timer"; httpd_register_uri_handler(srv, &u);
    u.handler = h_bouton; u.uri = "/bouton"; httpd_register_uri_handler(srv, &u);
    u.handler = h_alarme; u.uri = "/alarme"; httpd_register_uri_handler(srv, &u);
    u.handler = h_rappel; u.uri = "/rappel"; httpd_register_uri_handler(srv, &u);
    u.handler = h_parle; u.uri = "/parle"; httpd_register_uri_handler(srv, &u);
    u.handler = h_dit;   u.uri = "/dit";   httpd_register_uri_handler(srv, &u);
    u.handler = h_hist;  u.uri = "/hist";  httpd_register_uri_handler(srv, &u);
    u.handler = h_veille; u.uri = "/veille"; httpd_register_uri_handler(srv, &u);
    u.handler = h_rotation; u.uri = "/rotation"; httpd_register_uri_handler(srv, &u);
    u.handler = h_voix_off; u.uri = "/voix"; httpd_register_uri_handler(srv, &u);
    u.handler = h_ecran; u.uri = "/ecran"; httpd_register_uri_handler(srv, &u);
    u.handler = h_fichier; u.uri = "/fichier"; httpd_register_uri_handler(srv, &u);
    u.method = HTTP_POST;
    u.handler = h_play; u.uri = "/play";  httpd_register_uri_handler(srv, &u);
    u.handler = h_fichier; u.uri = "/fichier"; httpd_register_uri_handler(srv, &u);
    ESP_LOGI(TAG, "API : /state /face?e= /say?q= /note?t= /soul /rec?ms= /play(POST) /cle?k= /sd /sdformat /fichier(GET,POST)");
}

/* ------------------------------------------------------------------ */
static board_power_bsp_t board_div(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);

extern "C" void app_main(void)
{
    /* ---- gestion d'energie : la carte ne doit pas rester eveillee a 160 MHz
       CPU 100 % du temps. Avec PM_ENABLE, le coeur descend a 80 MHz au repos
       et dort (light sleep) entre deux reveils du wifi : c'est ce qui fait
       tenir la batterie, pas une optimisation de tache (le CPU est deja
       inactif a 90 %). Les pilotes (I2S, SPI, UART) prennent leurs propres
       verrous quand ils en ont besoin. ---- */
    {
        esp_pm_config_t pm = {};
        pm.max_freq_mhz = 160;          /* les rafales (TLS, TTS) gardent leur vitesse */
        pm.min_freq_mhz = 80;           /* au repos : la moitie */
        pm.light_sleep_enable = true;   /* et le sommeil entre deux reveils wifi */
        esp_err_t e = esp_pm_configure(&pm);
        ESP_LOGI(TAG, "energie : pm %s (80-160 MHz, light sleep %s)",
                 e == ESP_OK ? "actif" : esp_err_to_name(e),
                 pm.light_sleep_enable ? "oui" : "non");
    }

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    /* pourquoi la carte a redemarre : panic (bug), brownout (alim), wdt (blocage) */
    {
        static const char *RR[] = { "inconnu", "poweron", "reset_ext", "software",
                                    "panic", "int_wdt", "task_wdt", "wdt_autre",
                                    "deepsleep", "brownout", "sdio", "usb", "jtag" };
        esp_reset_reason_t rr = esp_reset_reason();
        ESP_LOGW(TAG, "demarrage : raison du reset = %s (%d)  heap_int=%u",
                 (int)rr >= 0 && (int)rr < 13 ? RR[(int)rr] : "?",
                 (int)rr, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        /* compteur de demarrages en NVS : s'il monte sans qu'on ait flashe,
        c'est que la carte redemarre toute seule (le bug qu'on cherche) */
        nvs_handle_t h;
        if (nvs_open("mino", NVS_READWRITE, &h) == ESP_OK) {
            uint32_t n = 0;
            nvs_get_u32(h, "boot_nb", &n);
            boot_nb = n + 1;
            nvs_set_u32(h, "boot_nb", boot_nb);
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGW(TAG, "demarrage n%d", (int)boot_nb);
        }
    }

    mem_mux = xSemaphoreCreateMutex();
    epd_mux = xSemaphoreCreateMutex();
    i2c_mux = xSemaphoreCreateMutex();
    ping_mux = xSemaphoreCreateMutex();
    yt_charge();                       /* la chaine YouTube reglee une fois pour toutes */

    heure_charge();     /* l'heure d'avant tout de suite : plus de date fausse au demarrage */
    timers_charge();
    astuce_charge();    /* restaure les timers/alarmes */

    serial_start();     /* la console USB remplace le log par defaut */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);   /* pas de flood si un capteur manque */
    esp_log_level_set("i2c.common", ESP_LOG_NONE);
    {
        nvs_handle_t nh;
        int32_t v = 0;
        if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
            if (nvs_get_i32(nh, "toff10", &v) == ESP_OK) ui_temp_offset = (float)v / 10.0f;
            if (nvs_get_i32(nh, "veille", &v) == ESP_OK) veille_nuit = (v != 0);
            if (nvs_get_i32(nh, "rot", &v) == ESP_OK) rotation_on = (v != 0);
            if (nvs_get_i32(nh, "rots", &v) == ESP_OK && v >= 5 && v <= 600) rotation_s = (int)v;
            if (nvs_get_i32(nh, "voixsortie", &v) == ESP_OK) voix_sortie = (v != 0);
            {
                size_t sl = sizeof(livre_dernier);
                if (nvs_get_str(nh, "livnom", livre_dernier, &sl) != ESP_OK)
                    livre_dernier[0] = 0;
                if (nvs_get_i32(nh, "livpg", &v) == ESP_OK && v >= 0)
                    livre_derniere_page = (int)v;
            }
            nvs_close(nh);
        }
    }
    ESP_LOGI(TAG, "Mino demarre");

    storage_init();

    /* l'historique de conversation vit en PSRAM (16 Ko) : le garder en RAM
       interne affamait le wifi. Si l'allocation echoue, on reste sur le
       tampon statique, plus court mais fonctionnel. */
    {
        hist_p = (hist_t *)heap_caps_calloc(1, sizeof(hist_t), MALLOC_CAP_SPIRAM);
        if (hist_p) hist_vide(hist_p);
        ESP_LOGI(TAG, "historique : fenetre de %d tours de %d o (%s)",
                 HIST_MAX, HIST_TXT_SZ, hist_p ? "PSRAM" : "indisponible");
    }

    /* ecran + capteurs : TOUS les rails d'alimentation avant l'I2C,
       sinon le controleur tactile (0x38) ne repond jamais sur le bus. */
    board_div.POWEER_EPD_ON();
    board_div.POWEER_Audio_ON();
    board_div.VBAT_POWER_ON();
    vTaskDelay(pdMS_TO_TICKS(150));
    i2c_master_Init();
    touch_reset();
    {
        uint8_t n = 0;
        if (i2c_probe_addr(0x38) == 0)
            ESP_LOGI(TAG, "tactile FT6336 present (0x38)");
        else
            ESP_LOGW(TAG, "tactile FT6336 absent du bus (0x38)");
        if (i2c_read_buff(ft6336_handle, 0x02, &n, 1) == ESP_OK)
            ESP_LOGI(TAG, "tactile FT6336 pret");
    }

    custom_lcd_spi_t dcfg = {};
    dcfg.cs = EPD_CS_PIN;
    dcfg.dc = EPD_DC_PIN;
    dcfg.rst = EPD_RST_PIN;
    dcfg.busy = EPD_BUSY_PIN;
    dcfg.mosi = EPD_MOSI_PIN;
    dcfg.scl = EPD_SCK_PIN;
    dcfg.spi_host = EPD_SPI_NUM;
    dcfg.buffer_len = 5000;
    epd = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, dcfg);
    epd->EPD_Init();
    epd->EPD_Clear();
    epd->EPD_Display();
    epd->EPD_Init_Partial();
    {   /* image de base : les deux RAM a blanc pour que les partiels partent proprement */
        static uint8_t blanc[FB_LEN];
        memset(blanc, 0xFF, FB_LEN);
        epd->EPD_ShowBase(blanc, FB_LEN);
    }
    ESP_LOGI(TAG, "e-paper pret");

    /* audio : alimentation, codec ES8311, micro + haut-parleur */
    board_div.POWEER_Audio_ON();
    audio_bsp_init();
    audio_play_init();
    rec_pcm = (uint8_t *)heap_caps_malloc((size_t)AUDIO_MAX_FRAMES * 2, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "audio pret (micro %s)", rec_pcm ? "ok" : "KO");

    /* carte micro SD : tout ce qui est lourd vit la (journal, memoire, sauvegarde) */
    sd_mount(true);
    if (sd_ok) {
        storage_sd_charge();
        ESP_LOGI(TAG, "carte SD prete : /sdcard/mino pour le journal, la memoire et le SOUL");
        hist_sd_init();      /* tout l'historique de conversation, sur la partition brute */
    }
    radios_charge();
    radio_vol_charge();
    {   /* module radio : etat garde en NVS (defaut : desactive) */
        nvs_handle_t nh;
        int32_t rv = 0;
        if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
            nvs_get_i32(nh, "radio", &rv);
            nvs_close(nh);
        }
        radio_actif = (rv == 1);
        ESP_LOGI(TAG, "module radio : %s", radio_actif ? "actif" : "desactive");
    }
    ia_cle_charge();
    {
        nvs_handle_t nh;
        int32_t v = 300;
        if (nvs_open("mino", NVS_READONLY, &nh) == ESP_OK) {
            nvs_get_i32(nh, "micdb", &v);
            nvs_close(nh);
        }
        mic_db = (float)v / 10.0f;
        audio_mic_gain(mic_db);
        ESP_LOGI(TAG, "gain micro : %.0f dB", (double)mic_db);
    }

    /* le tamagotchi : on reprend l'etat sauvegarde, sinon un oeuf arrive */
    pet_mux = xSemaphoreCreateMutex();
    ui_mux = xSemaphoreCreateMutex();
    if (!pet_mux) ESP_LOGE(TAG, "pas de mutex pour le tamagotchi");
    pet_charge();
    ui_page = PAGE_MAITRE;
    ui_face = art_expr_id("neutre");
    if (ui_face < 0) ui_face = 0;
    show_ui();
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "%d expressions en %d familles : %s, %s, %s ...",
             EXPR_N, FAMILLE_N, FAMILLES[2], FAMILLES[0], FAMILLES[3]);

    resp_buf = (char *)heap_caps_malloc(RESP_MAX, MALLOC_CAP_SPIRAM);
    if (!resp_buf) ESP_LOGE(TAG, "pas de PSRAM pour la reponse");

    wifi_start();
    /* On n'attend le wifi que 20 s. Avant, c'etait portMAX_DELAY : sans borne,
       RIEN ne demarrait (serveur web, boutons, ecran, alarme) et la carte
       restait figee sur sa premiere image — c'etait le bug "plus de wifi =
       tout se fige". Le wifi continue ses tentatives en tache de fond. */
    bool wifi_pret = (xEventGroupWaitBits(wifi_eg, WIFI_OK_BIT, pdFALSE, pdTRUE,
                                          pdMS_TO_TICKS(20000)) & WIFI_OK_BIT) != 0;
    if (wifi_pret) {
        time_sync();
        heure_syncee = true;
    } else {
        ESP_LOGW(TAG, "wifi absent apres 20 s : demarrage quand meme (il se connectera tout seul)");
    }

    api_start();
    xTaskCreate(serial_task, "console", 6144, NULL, 4, NULL);
    xTaskCreate(touch_task, "tactile", 4096, NULL, 4, NULL);
    /* deux taches : l'echantillonneur (prioritaire, ne bloque jamais) remplit la
       file, l'actionneur la vide — un appui n'est donc jamais perdu, meme si
       l'ecran met une seconde a se rafraichir. */
    xTaskCreate(button_echantillonne, "boutons-ech", 3072, NULL, 5, NULL);
    xTaskCreate(button_action, "boutons-act", 5120, NULL, 3, NULL);
    xTaskCreate(alarme_task, "alarme", 4096, NULL, 3, NULL);
    xTaskCreate(ui_task, "ui", 4096, NULL, 3, NULL);   /* plus de TLS ici : il est dans net_task */
    net_q = xQueueCreate(4, sizeof(int));
    xTaskCreate(net_task, "reseau", 8192, NULL, 2, NULL);   /* meteo, infos, youtube : les TLS */
    /* le conseil : une passe de cerveau qui juge au lieu de repondre.
       Pile en PSRAM : ds_chat a besoin de 16 Ko comme l'assistant. */
    if (xTaskCreateWithCaps(conseil_tache, "conseil", 16384, NULL, 2, NULL,
                            MALLOC_CAP_SPIRAM) != pdPASS)
        ESP_LOGE(TAG, "conseil : pas de pile PSRAM");
    /* tampon de la meteo : 2 Ko pour toute la vie de la carte (service reseau) */
    meteo_buf = (char *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    if (!meteo_buf) meteo_buf = (char *)malloc(2048);
    meteo_buf_taille = meteo_buf ? 2048 : 0;
    xTaskCreate(pet_task, "tamagotchi", 4096, NULL, 3, NULL);
    /* plus de tache "promenade" : elle redessinait la page toutes les 2 s, or
       chaque image fait clignoter la dalle. Le compagnon bouge maintenant une
       fois par minute, en meme temps que l'horloge (voir ui_task). */

    ESP_LOGI(TAG, "Mino est pret. Tape ton message ici (ou 'help').");

    for (;;) vTaskDelay(pdMS_TO_TICKS(5000));
}
