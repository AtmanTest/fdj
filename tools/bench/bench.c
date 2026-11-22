/* BATMino — bench de bande passante memoire / flash / micro SD.
   Affiche les resultats sur le port serie (USB-Serial/JTAG), puis boucle.
   Mesures :
     - SRAM interne : memcpy, memset, lecture sequentielle
     - PSRAM 8 Mo   : memcpy, memset, lecture sequentielle
     - PSRAM acces aleatoire (pointer chase, pas de 64 o) -> latence en ns
     - flash SPI (rodata hors cache) : lecture sequentielle
     - micro SD (SDMMC 1 bit) : ecriture/lecture sequentielle par tailles de bloc,
       latence d'une ecriture de 4 Ko, fsync
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "bench";

#define SD_CLK_PIN  GPIO_NUM_39
#define SD_CMD_PIN  GPIO_NUM_41
#define SD_D0_PIN   GPIO_NUM_40

/* 512 Ko de rodata : bien plus gros que le cache -> on mesure la vraie flash */
static const uint8_t flash_buf[512 * 1024] __attribute__((aligned(64))) = { 1, 2, 3, 4 };

static int64_t us(void) { return esp_timer_get_time(); }

static double mbps(size_t octets, int64_t dt_us)
{
    if (dt_us <= 0) return 0;
    return (double)octets / (dt_us / 1e6) / (1024.0 * 1024.0);
}

static void bench_mem(const char *nom, uint8_t *src, uint8_t *dst, size_t taille, int passes)
{
    /* chauffe + verite */
    memcpy(dst, src, taille);
    int64_t t0 = us();
    for (int i = 0; i < passes; i++) {
        memcpy(dst, src, taille);
        __asm__ __volatile__("" ::: "memory");
    }
    int64_t t1 = us();
    printf("MEM  %-18s memcpy  %6.1f Mo/s  (%zu o x %d passes)\n",
           nom, mbps(taille * (size_t)passes, t1 - t0), taille, passes);

    t0 = us();
    for (int i = 0; i < passes; i++) {
        memset(dst, i & 0xff, taille);
        __asm__ __volatile__("" ::: "memory");
    }
    t1 = us();
    printf("MEM  %-18s memset  %6.1f Mo/s\n", nom, mbps(taille * (size_t)passes, t1 - t0));

    /* lecture seule (le compilateur ne peut pas l'eliminer : somme volatile) */
    volatile uint32_t somme = 0;
    t0 = us();
    for (int i = 0; i < passes; i++) {
        uint32_t s = 0;
        const uint32_t *p = (const uint32_t *)src;
        for (size_t k = 0; k < taille / 4; k++) s += p[k];
        somme += s;
    }
    t1 = us();
    printf("MEM  %-18s lecture %6.1f Mo/s\n", nom, mbps(taille * (size_t)passes, t1 - t0));
    (void)somme;
}

static void bench_latence(uint8_t *base, size_t taille, const char *nom)
{
    /* pointer chase : chaque acces depend du precedent -> latence pure */
    size_t n = taille / 64;
    if (n < 16) return;
    for (size_t i = 0; i < n; i++) *(size_t *)(base + i * 64) = (i + 1) % n;
    size_t idx = 0;
    int64_t t0 = us();
    for (int i = 0; i < 20000; i++) idx = *(size_t *)(base + idx * 64) % n;
    int64_t t1 = us();
    printf("MEM  %-18s acces aleatoire : %.1f ns/acces (%d sauts)\n",
           nom, (double)(t1 - t0) * 1000.0 / 20000.0, 20000);
}

static void bench_flash(void)
{
    const size_t taille = 256 * 1024;   /* au-dela du cache */
    uint8_t *dst = (uint8_t *)heap_caps_malloc(taille, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!dst) return;
    /* on touche le rodata : la premiere passe chauffe le cache, la deuxieme mesure */
    memcpy(dst, flash_buf, taille);
    int64_t t0 = us();
    for (int i = 0; i < 4; i++) {
        memcpy(dst, flash_buf + (i % 2 ? 256 * 1024 : 0), taille);
        __asm__ __volatile__("" ::: "memory");
    }
    int64_t t1 = us();
    printf("MEM  %-18s lecture %6.1f Mo/s  (SPI QIO 80 MHz, hors cache)\n",
           "flash", mbps(taille * 4, t1 - t0));
    free(dst);
}

static sdmmc_card_t *sd_mount(int freq_khz)
{
    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = false;
    mc.max_files = 5;
    mc.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = freq_khz;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK_PIN;
    slot.cmd = SD_CMD_PIN;
    slot.d0  = SD_D0_PIN;

    sdmmc_card_t *card = NULL;
    esp_err_t r = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &card);
    if (r != ESP_OK) {
        printf("SD   montage impossible : %s\n", esp_err_to_name(r));
        return NULL;
    }
    sdmmc_card_print_info(stdout, card);
    return card;
}

static uint8_t *sd_buf;

static void bench_sd(const char *chemin, size_t bloc, size_t total)
{
    FILE *f = fopen(chemin, "wb");
    if (!f) { printf("SD   ouverture %s impossible\n", chemin); return; }
    int64_t t0 = us();
    size_t ecrit = 0;
    while (ecrit < total) {
        fwrite(sd_buf, 1, bloc, f);
        ecrit += bloc;
    }
    fflush(f);
    fsync(fileno(f));   /* VFS ESP-IDF (unistd.h) */
    int64_t t1 = us();
    double w = mbps(ecrit, t1 - t0);
    fclose(f);

    f = fopen(chemin, "rb");
    if (!f) return;
    int64_t t2 = us();
    size_t lu = 0;
    while (lu < total) {
        size_t r = fread(sd_buf, 1, bloc, f);
        if (r == 0) break;
        lu += r;
    }
    int64_t t3 = us();
    fclose(f);
    printf("SD   bloc %2zu Ko : ecriture %5.2f Mo/s | lecture %5.2f Mo/s (%zu o)\n",
           bloc / 1024, w, mbps(lu, t3 - t2), total);
}

static void bench_sd_latence(const char *chemin)
{
    /* 50 petites ecritures de 4 Ko, chacune suivie d'un fsync */
    FILE *f = fopen(chemin, "wb");
    if (!f) return;
    int64_t t0 = us();
    for (int i = 0; i < 50; i++) {
        fwrite(sd_buf, 1, 4096, f);
        fflush(f);
        fsync(fileno(f));   /* VFS ESP-IDF (unistd.h) */
    }
    int64_t t1 = us();
    fclose(f);
    printf("SD   4 Ko + fsync : %.2f ms par ecriture\n", (double)(t1 - t0) / 1000.0 / 50.0);

    f = fopen(chemin, "rb");
    if (!f) return;
    t0 = us();
    for (int i = 0; i < 200; i++) {
        fread(sd_buf, 1, 4096, f);
        fseek(f, (long)(i % 100) * 65536, SEEK_SET);
    }
    t1 = us();
    fclose(f);
    printf("SD   lecture 4 Ko dispersee : %.2f ms par lecture\n", (double)(t1 - t0) / 1000.0 / 200.0);
}

/* Ecriture/lecture BRUTES de secteurs : separe la carte du systeme de
   fichiers (FATFS + VFS) du debit reel du bus SDMMC. */
static void bench_raw(sdmmc_card_t *card, const char *etiquette)
{
    const size_t nsect = 512;                  /* 256 Ko : tient en SRAM interne */
    uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(512, nsect * 512,
                                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) { printf("RAW  alloc impossible\n"); return; }
    memset(buf, 0x5A, nsect * 512);

    int64_t t0 = us();
    esp_err_t r = sdmmc_write_sectors(card, buf, 8192, nsect);
    int64_t t1 = us();
    if (r == ESP_OK)
        printf("RAW  %s  256 Ko en blocs de 512 : ecriture %5.2f Mo/s\n",
               etiquette, mbps(nsect * 512, t1 - t0));
    else
        printf("RAW  %s  ecriture : %s\n", etiquette, esp_err_to_name(r));

    t0 = us();
    r = sdmmc_read_sectors(card, buf, 8192, nsect);
    t1 = us();
    if (r == ESP_OK)
        printf("RAW  %s  256 Ko en blocs de 512 : lecture  %5.2f Mo/s\n",
               etiquette, mbps(nsect * 512, t1 - t0));
    else
        printf("RAW  %s  lecture : %s\n", etiquette, esp_err_to_name(r));

    /* latence d'une ecriture de 4 Ko (8 secteurs), 50 fois */
    t0 = us();
    for (int i = 0; i < 50; i++) sdmmc_write_sectors(card, buf, 8192 + i * 8, 8);
    t1 = us();
    printf("RAW  %s  4 Ko : %.2f ms par ecriture brute\n",
           etiquette, (double)(t1 - t0) / 1000.0 / 50.0);
    free(buf);
}

static void bench_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    printf("\n\n========== BENCH BATMino ==========\n");
    printf("CPU %d MHz, PSRAM %d o libres, SRAM interne %d o libres\n",
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    const size_t n = 32 * 1024;
    uint8_t *a = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *b = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *pa = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    uint8_t *pb = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!a || !b || !pa || !pb) { printf("BENCH: alloc impossible\n"); return; }

    bench_mem("SRAM interne", a, b, n, 2000);
    bench_mem("PSRAM (oct 80M)", pa, pb, n, 500);

    /* latence : on reste DANS le buffer alloue (n octets), sinon on ecrase le
       tas voisin -> panic StoreProhibited dans free() (bug du 1er jet) */
    bench_latence(a, n, "SRAM interne");
    bench_latence(pa, n, "PSRAM (oct 80M)");
    free(a); free(b); free(pa); free(pb);

    bench_flash();

    /* --- SD : trois etages de mesure ---
       1) FATFS/VFS a 20 MHz (ce que fait le firmware aujourd'hui)
       2) secteurs bruts a 20 MHz (debit pur de la carte, sans FATFS)
       3) secteurs bruts a 40 MHz (montre si le bus peut monter)      */
    sdmmc_card_t *card = sd_mount(SDMMC_FREQ_DEFAULT);
    if (card) {
        sd_buf = (uint8_t *)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
        if (sd_buf) {
            memset(sd_buf, 0xA5, 64 * 1024);
            bench_sd("/sdcard/bench.bin", 4 * 1024, 4 * 1024 * 1024);
            bench_sd("/sdcard/bench.bin", 64 * 1024, 4 * 1024 * 1024);
            bench_sd_latence("/sdcard/bench2.bin");
            remove("/sdcard/bench.bin");
            remove("/sdcard/bench2.bin");
            free(sd_buf);
        }
        bench_raw(card, "20 MHz");
        esp_vfs_fat_sdcard_unmount("/sdcard", card);

        card = sd_mount(SDMMC_FREQ_HIGHSPEED);
        if (card) {
            bench_raw(card, "40 MHz");
            sd_buf = (uint8_t *)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);
            if (sd_buf) {
                memset(sd_buf, 0xA5, 64 * 1024);
                bench_sd("/sdcard/bench.bin", 64 * 1024, 4 * 1024 * 1024);
                remove("/sdcard/bench.bin");
                free(sd_buf);
            }
            esp_vfs_fat_sdcard_unmount("/sdcard", card);
        }
    }
    printf("========== FIN BENCH ==========\n\n");
    ESP_LOGI(TAG, "bench termine, boucle");
    while (1) { vTaskDelay(pdMS_TO_TICKS(5000)); printf("."); fflush(stdout); }
}

void app_main(void)
{
    printf("\n\n### bench demarre ###\n");
    xTaskCreatePinnedToCore(bench_task, "bench", 8192, NULL, 5, NULL, 1);
}