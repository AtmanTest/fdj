---
name: batmino-esp32-robot
description: "Use when working on the BATMino ESP32-S3 e-paper robot: build, flash, bench, debug display/audio/SD."
---

# BATMino — robot tamagotchi ESP32-S3 e-paper

Carte : Waveshare ESP32-S3-ePaper-1.54 (ESP32-S3-PICO-1-N8R8, 8 Mo flash, 8 Mo PSRAM octal 80 MHz, e-paper 200x200, micro + ES8311). Firmware autonome (voix Wi-Fi : STT Groq / LLM Groq-DeepSeek / TTS Google, tamagotchi local, API HTTP).

## Emplacements

    /home/taz/mino-fw        firmware (git, README-BATMINO.md, main/main.cpp ~200 Ko)
    /home/taz/mino-bench     projet bench mem/flash/SD (main/bench.c)
    /home/taz/mino-app       appli PC annexe (soul-batmino.md...)
    /home/taz/mino-fw/build/mino.bin    app image a reflasher a 0x10000 (sauvegarde de secours)

Partitions (identiques dans les deux projets) : nvs 0x9000, phy 0xf000, factory app 0x10000 3 Mo, spiffs 0x310000 1 Mo.

## Build / flash

    export IDF_TOOLS_PATH=/opt/espressif-tools && . /opt/esp-idf/export.sh
    idf.py build
    idf.py -p /dev/ttyACM0 -b 460800 flash

L'utilisateur `taz` est dans le groupe `uucp` : pas besoin de sudo pour ttyACM0.

## Lire le port serie (le piege)

`cat /dev/ttyACM0` ne renvoie RIEN si la ligne n'est pas configuree. Il faut `stty -F /dev/ttyACM0 115200 raw -echo` avant, plus un reset pour capturer le boot (esptool `--no-stub --after hard_reset chip_id` en arriere-plan, puis stty+cat). Script pret : `scripts/capture-serial.sh <secondes>`. USB-Serial/JTAG = CDC, le baud est ignore.

Apres un flash `idf.py`, la sortie de boot est deja passee : sans reset on ne capture que la boucle finale.

## Banc d'essai ecran par la console USB

Le firmware accepte des commandes locales (elles ne partent pas au LLM) :

    printf '!st\n' > /dev/ttyACM0     # BUSY, drapeau panne, compteurs
    !off / !on        rail d'alimentation de la dalle
    !init             reinitialisation
    !clear            init + effacement (chronometre)
    !part4            4 rafraichissements partiels de l'image courante
    !full / !clean    rafraichissement complet simple / avec LUT pleine

Chaque attente BUSY > 300 ms est journalisee : c'est la mesure de reference
(1758 ms complet, 573 ms partiel).

## Pieges deja payes

- **`CONFIG_FREERTOS_HZ=100` : `pdMS_TO_TICKS(5)` vaut ZERO.** Tout
  `vTaskDelay(pdMS_TO_TICKS(x))` avec x < 10 ne dort pas, il rend seulement la
  main. Consequence vecue : la boucle d'attente BUSY de l'e-paper tournait a
  plein CPU, son compteur de securite se declenchait en quelques microsecondes
  et la dalle etait declaree en panne PENDANT son rafraichissement normal (puis
  alimentation coupee en plein milieu -> ecran fige). Utiliser `vTaskDelay(1)`
  (= 1 tick = 10 ms) et mesurer les delais avec `esp_timer_get_time()`.
- **Chaque rafraichissement de la dalle clignote ~0,6 s** (partiel 573 ms,
  complet 1758 ms mesures). Il ne faut donc JAMAIS redessiner la page pour une
  valeur qui change toute seule : la RAM libre affichee dans la barre d'etat
  suffisait a rendre chaque image differente, donc a faire clignoter l'ecran
  en boucle. `show_ui()` compare desormais le framebuffer a la derniere image
  envoyee (`memcmp`) et impose 3 s minimum entre deux images.
- **`max_uri_handlers`** : a 28, `httpd_register_uri_handler` refusait les
  derniers URI en silence (`no slots left`), donc /bande et /ecran n'existaient
  pas. Mis a 40.
- **Une tache qui termine par `vTaskDelete(NULL)` ne doit JAMAIS etre appelee
  comme une fonction.** `tts_lot_tache()` etait appelee directement pour le
  premier morceau : la tache httpd se suicidait sans repondre, le socket
  restait ouvert et, avec `max_open_sockets = 2`, toute l'API devenait muette
  apres deux appels (symptome : `/tts` et `/dit` sans reponse, puis `/state`
  aussi). Separer `tts_lot_charge()` (le travail) de `tts_lot_tache()` (tache +
  suicide).
- **Firmware et bench compiles en Debug (`-Og`) par defaut** : `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y`. Pour mesurer du materiel, passer en `CONFIG_COMPILER_OPTIMIZATION_PERF=y` ; c'est aussi le premier levier de perf reel sur mino-fw (pas encore applique la).
- `fsync(fileno(f))` existe (VFS ESP-IDF, via `unistd.h`) ; `f_sync()` attend un `FIL*` (FATFS brut), pas un `FILE*`.
- `build/` herite d'un build interrompu = `objdump: returned non-zero exit status 1` sur une lib : `rm -rf build` puis rebuild.
- Bug bench corrige : `bench_latence(a, 64*1024, ...)` sur des buffers de 32 Ko ecrasait le tas -> `Guru Meditation StoreProhibited` dans `free()`. Toujours passer la taille REELLE du buffer.
- Le pointer-chase de latence est elimine par `-O2` (0.1 ns mesure) : lire via un pointeur `volatile` ou une somme dependante non optimisable.

## Mesures de reference (17/10/2026, CPU 160 MHz, PSRAM octal 80 MHz, -O2)

    SRAM interne   memcpy 243 Mo/s | memset 486 Mo/s
    PSRAM 8 Mo     memcpy  21 Mo/s | memset 470 Mo/s (douteux, > debit du bus)
    flash SPI QIO 80 MHz hors cache : lecture 25 Mo/s
    SD (SDMMC 1 bit, FATFS) : ecriture 0,35 Mo/s | lecture 1,2 Mo/s quel que
       soit le bloc (4-64 Ko) ; 4 Ko + fsync = 8,1 ms ; lecture 4 Ko dispersee 0,23 ms
    SD secteurs BRUTS (sans FATFS), 1 bit : 20 MHz -> 2,34 Mo/s ecriture et lecture ;
       40 MHz -> 4,7 Mo/s (doublement exact, la carte tient 40 MHz) ; 4 Ko brut = 2,3 ms

Diagnostic SD : le debit brut colle au maximum theorique du bus (20 MHz x 1 bit
= 2,5 Mo/s), donc la carte et le cablage ne sont pas le probleme. L'ecart
0,35 vs 2,34 Mo/s vient de FATFS/VFS (facteur ~7 en ecriture, ~2 en lecture).
Deux leviers mesures : `host.max_freq_khz = SDMMC_FREQ_HIGHSPEED` (40 MHz,
+100 % brut) et ecriture par gros blocs / secteurs bruts plutot que fwrite
petit. La carte Waveshare ne cable que CLK/CMD/D0 : le 4 bits est impossible.

Consequences : PSRAM bonne seulement en gros buffers sequentiels (memcpy 21 Mo/s) ; aucun streaming sur la SD (0,35 Mo/s tue l'enregistrement audio) ; gros rodata depuis la flash a 25 Mo/s (canon.pcm 1,9 Mo = ~76 ms).

## API de transfert de fichiers (sauvegarde sur la micro SD)

Depuis v0.3, sans lecteur de carte on passe par le Wi-Fi :

    curl -X POST --data-binary @fichier.tar.gz \
         "http://192.168.8.208/fichier?p=/backup/fichier.tar.gz"   # ecrit sur la SD
    curl "http://192.168.8.208/fichier?p=/backup/fichier.tar.gz" -o copie  # relit

Mesure : 7,3 Mo en 15 s (~0,47 Mo/s, coherent avec les 0,35 Mo/s FATFS).
Toujours verifier par `cmp` apres relecture : l'ecriture SD n'est pas fiable a 100 %.

Piege FATFS : avec `CONFIG_FATFS_LFN_NONE=y` (defaut du projet), fopen echoue
en "ouverture" des qu'un nom a une extension de plus de 3 caracteres ou deux
points (`x.tar.gz`). Passe en `CONFIG_FATFS_LFN_HEAP=y` + `CONFIG_FATFS_CODEPAGE_850=y`.
Le listing `/sd` reste illisible (octets bruts dans les noms) : bug de
h_sd_list, pas un probleme de nommage.

## Depot git

    remote : https://github.com/AtmanTest/batmino.git (branche main, tags v0.2/v0.3)

Le token fine-grained est enregistre dans `~/.git-credentials` (helper `store`,
chmod 600) ET colle en premiere ligne de `LIRE-D-ABORD.md` sur la cle MINO-ESP32.
Ne jamais repondre "il n'y a pas de token" sans avoir teste : `curl -H "Authorization: Bearer <t>" https://api.github.com/user`
renvoie 401 pour un token revoque, ce qui distingue "absent" de "mort".

## Boutons (v0.8)

    bouton 1 (BOOT, GPIO0) DOUBLE appui : LE SEUL geste qui lance l'assistant,
        depuis n'importe quelle page (fenetre 320 ms)
    bouton 1 appui court : page SUIVANTE (page REPONSE : 1 = suite)
    bouton 2 (PWR, GPIO18) appui court : page PRECEDENTE (page REPONSE : 2 = avant)
    appui long (les deux) : retour a l'accueil + arret de la tache
    pendant une tache : un appui simple (1 ou 2) l'arrete et ramene a l'accueil

L'appui court du bouton 1 est retenu `DOUBLE_MS` (320 ms) dans `button_task`
avant d'etre execute, pour tous les ecrans et pas seulement le clavier : sinon
un double appui tournait la page juste avant de lancer l'assistant. Le bouton 2
n'a pas de double appui, il part au relachement.

Sur la page HISTORIQUE le bouton 1 defile vers le passe (l'appui long revient au
plus recent) ; le bouton 2 y sort de la page. La page REGLAGES garde ses gestes
propres (clavier : double 1 = sauter 6 touches, double 2 = abandonner).

L'abandon passe par `assist_abort` (volatile) verifie dans la boucle d'ecoute de
assistant_wake_task : c'est le seul moyen propre de sortir d'une tache de 6 s.
Test a distance : `/bouton?n=0|1&m=court|long|double`, `/bouton?p=0..3`.

## Debit SD : ou est le vrai plafond

- 4, 16, 64, 256 Ko de bloc via FATFS : 0,37 Mo/s ecriture / 1,6 Mo/s lecture,
  identique => la taille de bloc ne change rien.
- secteurs bruts (sdmmc_write_sectors) : 4,65 / 4,70 Mo/s a 40 MHz.
- => le plafond est FATFS + chemin d'ecriture, pas la carte ni le bus.
- Pas de swap possible : ESP32-S3 sans MMU, aucune pagination. Pour utiliser
  vraiment la carte comme memoire (4,6 Mo/s), il faut une zone BRUTE hors FAT32
  (repartition : FAT32 + region reservee, acces par sdmmc_*_sectors).
- Endpoints ajoutes : `/swap` (etat + `op=bench&bloc=K&mo=N`), `/sdformat?f=1`
  (reformate vraiment), `/sd?p=/dossier` (listing JSON nom/taille/lisible).

## Laura, et la radio desactivee (v0.8)

L'assistante s'appelle **Laura**. Son SOUL vit sur la carte
(`/sdcard/mino/soul.md`, ~1,6 Ko) et `DEFAULT_SOUL` sert de repli : les deux
parlent d'une adulte experte, plus d'un enfant de six ans. Le prompt systeme
n'envoie que `%.1500s` du SOUL — un SOUL plus long est tronque en silence (c'est
ce qui a fait disparaitre la consigne radio).

Module radio : `radio_actif` (NVS, cle `radio`, defaut desactive). Garde dans
`radio_joue()`, interrupteur `/radio?actif=1|0`, et le prompt perd la liste des
stations + le champ `radio` du JSON quand c'est coupe. Sans cette consigne
explicite, le modele inventait des actions (« je lance la radio… »).

## Conversation continue, clone et historique (v0.7)

`assistant_wake_task()` boucle : un appui lance le 1er tour, puis le micro se
reouvre seul `CONV_FENETRE_MS` (10 s) apres chaque reponse ; silence = fin,
`CONV_MAX_TOURS` (10) borne la boucle. Le bouton 2 (`assist_abort`) coupe tout.

Historique : `main/historique.h` (header seul, compile cote carte ET cote PC).
`hist_t` garde `HIST_MAX` 12 tours de `HIST_TXT_SZ` 320 octets, envoyes au modele
en vrais messages `user`/`assistant`. `/state` expose `historique_tours`.

Clone de Taz : `/sdcard/mino/clone.md`, relu au demarrage (`CLONE lu sur la
carte SD`), injecte dans le prompt avec le role (expert IT, IA, technicien
superieur, developpeur, coach de vie, reflechir avant d'agir). Depose par
`POST /fichier?p=/mino/clone.md` — pas besoin de reflasher.

Reponses longues : `max_tokens` 700, temperature 0,7, prompt impose 3-6 phrases.
`tts_parle()` decoupe en morceaux de 180 caracteres (limite Google) coupes sur
la ponctuation et appelle `tts_parle_un()` — sinon la voix s'arretait a 190
caracteres.

Tests (`bash tests/run_unit.sh`, `bash tests/api.sh`) : 12 unitaires + 13
d'integration. Le test `zone d'echange` attrape un piege reel : le tampon relais
16 Ko doit etre alloue UNE fois (`swap_tampon()`), sinon l'allocation echoue des
que le plus gros bloc interne passe sous 16 Ko.

Sauvegarde : `~/mino-backup-usb.sh` + timer systemd utilisateur
`mino-backup.timer` (30 min, OnUnitActiveSec), archive datee dans
`/run/media/taz/MINO-ESP32/backup`, journal `~/mino-backup.log`. Le cron Hermes
n'est pas fiable ici : il exige la passerelle locale demarree.

## Boutons : une gestuelle, une action

Regles valables partout (implementees dans `button_task`) :

- **appui simple** = action principale, tout de suite (sauf au clavier, ou
  l'action attend 320 ms pour laisser la place au double appui).
- **appui long = 600 ms, et il part PENDANT l'appui**, pas au relachement :
  avant, rien ne se passait tant que le doigt restait, donc on rappuyait.
- **double appui = 320 ms d'ecart, seulement au clavier** (sauter 6 touches,
  abandonner la saisie). Avant, le premier appui executait deja son action :
  un double faisait "touche + saut".
- Un appui de moins de 40 ms est ignore (anti-rebond).

Sur l'accueil : 1 = parler, 2 = page suivante, **2 long = retour accueil et
arret** (avant, 2 long ne faisait rien hors reglages). Dans les reglages :
1 = suivant, 2 = choisir, 1 long = effacer (clavier), 2 long = valider /
remonter / sortir.

Pour verifier ce que la carte a *compris* (et pas ce qu'on croit avoir
appuye) : `GET /state` donne `boutons` = compteurs b1/b2 court/long/double
depuis le demarrage, plus les seuils.

## Accueil : le prochain rendez-vous a une place fixe

La bande du bas (sous la ligne y=112) est reservee au rendez-vous :
libelle (`PROCHAIN RDV`, ou `DANS n MIN` a moins de 90 min), puis
`22 OCT 16:30` en corps 3 (le jour du mois, pas le nom du jour), puis le nom
de la personne qui invite en petit. Le nom de la borne wifi a
ete retire de l'accueil (il reste dans les reglages et dans `/state`) pour
laisser la place. Le compagnon marche sous la ligne y=168, rayon 8.

## Les pages : accueil, systeme, reglages

Trois pages : `PAGE_MAITRE` (0), `PAGE_SYS` (1), `PAGE_REG` (2).

`PAGE_REG` a trois vues : menu (WIFI / ECRAN / INFOS), liste des reseaux (scan
en tache separee, cache trie par RSSI) et un clavier 6x5 pour le mot de passe.

- bouton 1 = suivant, bouton 2 = choisir, long 1 = effacer, long 2 = valider
  ou remonter, **double appui 1 = sauter 6 touches**, double appui 2 = abandonner
  la saisie. Dans le menu, appui long 2 quitte les reglages.
- **Un appui long, c'est 600 ms.** Le seuil etait a 18 x 60 ms = 1,08 s : tout
  le monde croyait que l'appui long ne marchait pas. Ne pas y toucher sans
  mesurer, et ecrire la commande en bas de l'ecran (les boutons n'ont pas
  d'etiquette).
- Le reseau change a chaud : SSID et mot de passe ecrits en NVS (namespace
  `mino`, cles `ssid`/`pass`). Si la connexion echoue, `wifi_try` repasse aux
  SSID compiles dans `secrets.h` : on ne se coupe jamais les pieds.
- Taper un mot de passe au clavier, c'est 300 appuis a 600 ms. La vraie voie
  c'est le telephone : `http://<ip>/reglages` (page de 2 Ko servie par le
  robot). API : `/wscan[?go=1]`, `/wifi?ssid=..&pass=..`.
- Pour juger un dessin sans photographier la dalle : `/ecran?raw=1` renvoie le
  framebuffer en hexadecimal, `python3 tools/shot.py sortie.png` en fait un PNG
  400x400.

Les tests : `bash tests/api.sh <ip>` est rapide (0,2 s, sans voix ni cerveau) ;
`bash tests/api.sh <ip> complet` ajoute /dit, /tts et la zone d'echange.

## Les pages (v0.9) : accueil + systeme

Il n'y a plus que DEUX pages : `PAGE_MAITRE` (0) et `PAGE_SYS` (1). Les pages
PARLER, RADIO et TIMER ont ete supprimees (le moteur radio et les timers sont
toujours dans le code, simplement plus accessibles par l'ecran).

La conversation vit sur l'accueil : `voix_etat` 1/2/3 affiche ECOUTE /
REFLEXION / JE PARLE sous le compagnon en grand, sans changer de page.

`PAGE_SYS` est un mini btop : CPU (+ graphe), RAM, PSRAM, GPU (aucun sur
l'ESP32-S3), watts, batterie (ADC1 = GPIO4, pont 200K/200K, VBAT = 2 x VADC),
reseau, SD et SPIFFS. Une image toutes les 5 s.

Une page ne laisse rien en memoire : le tableau `TaskStatus_t` (1,8 Ko, en
PSRAM) et l'unite ADC sont alloues A L'ENTREE sur PAGE_SYS et liberes a la
sortie, et l'echantillonnage s'arrete. `/state` lit la batterie en cache, sinon
un simple appel HTTP rallumerait l'ADC. Le framebuffer est commun aux pages :
dessiner une page ecrase la precedente, il n'y a pas de tampon par page.

### Mesurer le CPU

    CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
    CONFIG_FREERTOS_USE_TRACE_FACILITY=y
    CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y

puis `uxTaskGetSystemState()` et **exclure les taches dont le nom commence par
IDLE** (sinon la somme vaut toujours 100 % des deux coeurs). Un hook idle ne
marche pas ici : a 100 Hz de tick il n'est pas appele assez souvent (il donnait
93 % sur un CPU au repos). Le vrai chiffre au repos est ~0,7 %.

`CONFIG_LWIP_MIB2` n'existe pas dans ESP-IDF : `netif->mib2_counters` ne
compile pas. Pour un debit reseau, compter les octets applicatifs.

### Le WiFi qui refuse le mot de passe (raison 15/205)

Symptome : le robot se deconnecte en boucle avec `raison 15` (4-way handshake
timeout) puis `205`, alors que le mot de passe est bon (un autre poste se
connecte avec le meme, verifie a la main), que la borne est a -30 dBm et que le
scan la voit parfaitement. Tout ce qu'on peut changer cote code (PMF, 11n
force, autre MAC, seuil d'authentification) ne change rien.

Cause : les donnees de calibration PHY stockees en NVS sont corrompues. La
reception marche (le scan voit tout), l'emission ne passe plus. **Remede :
`idf.py -p /dev/ttyACM0 erase-flash` puis reflasher.** Le robot se reconnecte
immediatement.

Effet de bord : `erase-flash` vide la NVS, donc la cle d'API et le SSID
enregistre reviennent aux valeurs compilees dans `secrets.h`. C'est sans danger,
mais a savoir avant de le faire.

### Les reseaux de secours

`secrets.h` contient maintenant deux secours et chacun sa cle :

    WIFI_SSID      "MON_RESEAU_SECOURS"     WIFI_PASS      "lavieestbellE$1"
    WIFI_SSID_ALT  "caca"               WIFI_PASS_ALT  "goodlife"
    WIFI_SSID_ALT2 "MON_RESEAU_SECOURS_2"

- Le SSID `GL-MT3000-633-5G` est en **5 GHz : l'ESP32-S3 ne le voit pas**. Son
  equivalent 2,4 GHz est `caca` (meme routeur, autre LAN : 192.168.1.x).
- `caca` **isole ses clients** : le robot y prend une adresse (192.168.1.208
  verifie) et garde internet, mais on ne peut pas le joindre depuis un autre
  poste du meme reseau. C'est un secours pour rester vivant, pas pour l'admin.
- Les SSID vides dans `WIFI_TRIES` sont ignores (`wifi_n`).
- **Retour automatique** : quand il tourne sur un secours, toutes les 10 min il
  retente le principal. Sinon il resterait colle a la borne de secours meme
  apres le retour de la principale.

Exercice de bascule (verifie) : `tools/test_secours.py` envoie un SSID bidon au
robot et lit le port serie. Resultat mesure : ~10 s pour abandonner le faux
SSID, ~25 s pour etre connecte sur `caca`.

### Se debloquer sans cable reseau

Le robot n'a pas d'ecran de secours : une faute de frappe dans le mot de passe
le coupe du monde. Trois garde-fous sont en place, dans cet ordre :

1. `/wifi` refuse un mot de passe de moins de 8 caracteres sur un reseau ferme
   (un WPA2 a 2 caracteres ne peut pas marcher) et le dit a la page web.
2. Si le reseau enregistre ne repond pas pendant 3 min, le robot efface
   `ssid`/`pass` de la NVS et repart sur les SSID compiles (`wifi_secours()`).
3. Sinon, par USB, sans reflasher :
   `esptool.py -p /dev/ttyACM0 erase_region 0x9000 0x6000` puis
   `esptool.py -p /dev/ttyACM0 run` — efface la NVS seule (config, cle d'API,
   etat du compagnon) et garde le firmware.

`/state` expose `wifi_ssid` et `wifi_enregistre` : c'est ce qui a permis de voir
que la NVS contenait un mot de passe de 2 caracteres.

### Le TLS qui echoue sans rien dire

`esp-aes: Failed to allocate memory` en pleine requete HTTPS (le LLM ou le TTS
sortent en `ESP_FAIL` / HTTP 0, sans autre indice) : l'AES **materiel** du S3
reclame un tampon DMA interne continu et le pool DMA est minuscule (~10 Ko
libres, plus gros bloc ~5,8 Ko). Desactiver `CONFIG_MBEDTLS_HARDWARE_AES` :
l'AES logiciel suffit pour des requetes de quelques Ko. Ajouter aussi
`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096` (au lieu de 16 Ko) et
`CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`. Symptome typique : les tests passent une
fois sur deux, le premier appel marche puis le suivant non.


## RAM interne : le piege qui casse la voix (v0.6)

Symptomes : le bouton ne fait rien, ou "je n'ai pas bien compris", ou TTS -1.
Journal : `mbedtls_ssl_handshake returned -0x3000` / `mbedtls_ssl_setup
returned -0x7F00` = MBEDTLS_ERR_SSL_ALLOC_FAILED (PAS un probleme de certificat,
ne pas desactiver la verification).

Cause : RAM interne saturee (~11 Ko libres, plus gros bloc 4 Ko). Le serveur
HTTP reservait 7 sockets x 16 Ko de pile = 112 Ko, et `xTaskCreate(16384)` du
reveil echouait en SILENCE (retour non teste -> bouton muet).

Regles a garder :
- `cfg.max_open_sockets = 2` et pile httpd 12 Ko (TLS tient dedans).
- piles de taches lourdes en PSRAM : `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`
  + `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y`, et TOUJOURS tester le retour.
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (pas 16384) : les tampons TLS de
  16 Ko peuvent ainsi aller en PSRAM.
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` et `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`.
- Diagnostiquer avec `/state` : `heap_interne`, `heap_interne_max`, `heap_dma_max`
  (sain : >30 Ko libres, plus gros bloc >16 Ko).
- Tester cerveau+voix sans parler : `/dit?t=texte` (ni micro ni STT).

## Carte repartitionnee (v0.5) : FAT32 93 Go + zone brute 23,9 Go

Etat actuel de la carte : MBR 2 partitions, p1 = FAT32 (0x0C, LBA 2048, 93 Go,
montee sur /sdcard), p2 = brute (0xDA, LBA 195313664, 23,9 Go).

    /part?op=info                      etat des partitions (lu sur la carte)
    /part?op=repart&fat_go=100         repartitionne + formate (DESTRUCTIF)
    /part?op=restore                   retour a une carte entiere en FAT32
    /part?op=rawtest | rawbench&bloc=K verif et debit de la zone brute
    /part?op=st&n=4                    ecrit/relit n Mo via swap_ecrire/swap_lire

Le MBR et le FAT32 sont ecrits A LA MAIN : f_mkfs ne formate qu'un disque entier,
jamais une partition (il recreerait un MBR pleine carte). Formule FAT32 utilisee :
reserved 32, 2 FAT, clusters 32 Ko, root cluster 2, FSInfo + backup boot.

Piege paye : rouvrir la carte avec sdmmc_card_init sans rappeler host->init() ni
sdmmc_host_init_slot() => panic au premier acces. Pour ecrire des secteurs bruts,
reutiliser le handle du montage en cours (sd_card) puis demonter/remonter.

Debits mesures : FATFS 0,37 Mo/s ecriture / 1,6 lecture ; zone brute via tampon
relais 16 Ko en SRAM interne : 3,4 Mo/s ecriture / 3,5 lecture (verifie).
Un tampon de 64-256 Ko en PSRAM fait CHUTER le debit a 0,4-0,6 Mo/s.

Ce n'est PAS un swap OS : pas de MMU sur ESP32-S3, aucune pagination. C'est un
stockage explicite (memcpy PSRAM <-> zone brute).

## Calendrier Outlook publie (.ics) : trois pieges payes

Le compte refuse le consentement aux applis tierces (Graph/device code), donc la
voie retenue est le calendrier **publie** en lecture seule (`/ical?u=<lien>`,
lien garde en NVS cles `mino`/`ical`, diagnostic dans `GET /ical`).
`tools/outlook_attente.py` et `tools/cal_installe.py` ne servent plus qu'a la
voie Graph, gardee en secours.

1. **Un corps TLS de plus de 4 Ko etait tronque a ~2 Ko.** Symptome : `/ical`
   annoncait `octets: 2001` (variable) et `vevent: 0`, journal
   `esp-tls-mbedtls: read error :-0x7100` (WANT_READ) en boucle. Cause :
   `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096` alors qu'Outlook envoie des
   enregistrements de 16 Ko. Regle : **16384** (les tampons vont en PSRAM via
   `MBEDTLS_EXTERNAL_MEM_ALLOC`, aucune RAM interne perdue). Verifier avec
   `GET /ical` : `octets` doit valoir la taille complete (~70 Ko ici).
2. **Jamais de requete reseau dans un gestionnaire httpd** : la tache httpd a
   12 Ko de pile (pas assez pour TLS) et se bloquait 20 s, ce qui tuait aussi
   `/state` et la page de reglages. Le telechargement vit dans une tache
   dediee (`cal_travail`, pile 16 Ko en PSRAM, reveillee par
   `xTaskNotifyGive`) ; `/ical?u=` repond en 40 ms et la lecture se fait
   derriere.
3. **Les tampons `static` des gestionnaires mangent la RAM interne** : deux
   `static char buf[32768]` dans `/fichier` prenaient 64 Ko, et le wifi ne
   pouvait plus allouer ses tampons de reception (`wifi:mem fail` dans le
   journal, `heap_interne_max` tombe a 5 Ko). Ils sont maintenant en PSRAM.
   Sante : `heap_interne_max` >= 30 Ko.

L'annonce vocale part une seule fois quand il reste <= 15 min (`voix_dit`), et
l'accueil affiche `HH:MM titre` quand c'est <= 90 min. Les heures `TZID=UTC`
sont converties (avant, elles etaient lues comme heure de Paris, 2 h de trop).

**Qui invite** : le .ics publie n'a aucun champ `ORGANIZER`, le nom n'existe
que dans le texte de l'invitation. `nom_invitant()` le retrouve dans le bloc
de l'evenement, trois tournures : `Organisateur\nNom Prenom<mailto:...>`
(Google), `Nom Prenom<mailto:...>- organisateur` (Outlook), `Nom Prenom vous
invite` (Zoom). Il faut un prenom + un nom, une majuscule initiale, pas de
`@` : sinon on garde le titre (un texte comme "je vous invite" est rejete).
`/state` donne `cal_invitant`.

Verifier de bout en bout sans internet : deposer un .ics sur la carte
(`POST /fichier?p=/test.ics`) puis pointer `/ical?u=http://127.0.0.1/fichier?p=/test.ics`.

## Restaurer le firmware apres un flash de bench

    cd /home/taz/mino-fw && python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
        write_flash 0x10000 build/mino.bin

Verifier au serie : `mino: SOUL lu sur la carte SD`, `carte SD prete : /sdcard/mino`.

## Accueil Mino : la mise en page (200x200)

Hierarchie verticale, en haut ce qui se lit de loin, en bas ce qui se consulte :

    4..39   heure, corps 7
    46..61  date, corps 3, nom du jour en gras, mois abrege (MERCREDI 30 SEP = 177 px)
    64      filet
    70..85  meteo sur UNE ligne : icone + "8.0C NUAGEUX" (corps 3, centre ensemble)
    86..96  ville en gras a gauche, nom de la borne wifi a droite (corps 2)
    100..180 cadre fin (1 px) autour du rendez-vous
            104 label corps 2 gras ("PROCHAIN RDV" ou "DANS n MIN")
            118 date + heure corps 3 gras
            137 nom de l'invitant corps 3 gras (2 lignes coupees au dernier
                espace si plus de 14 signes)
    186..192 le compagnon se promene dans la bande libre

Regles de dessin apprises ici :

- La police 3x5 n'a pas de graisse : `a_text_gras()` dessine deux fois a
  1 px d'ecart. Ca reste lisible a partir de s=2 (3 px de trait, 2 px de
  blanc) ; a s=1 les lettres se collent, ne jamais graisser du corps 1.
- Toujours calculer la largeur avec `a_text_w()` avant de centrer : 12 px
  par signe en corps 3, 8 px en corps 2, 4 px en corps 1. Au-dela de 14
  signes en corps 3, ca sort du cadre : couper au dernier espace.
- Le compagnon doit etre dessine dans une bande qui ne contient aucun texte,
  sinon il passe par-dessus les noms pendant sa promenade.

## La sonnerie du rendez-vous

30 min avant (`ALARME_MIN`), une fois par rendez-vous : le carillon, puis la
voix qui annonce « Rendez-vous dans 30 minutes avec <nom>. ». Pas de boucle,
donc plus besoin de la faire taire — mais le bouton 1 coupe quand meme le
carillon (et annule la voix si on appuie pendant). `bouton_court()` teste
`alarme_on` AVANT tout le reste, sinon l'appui ouvrait aussi l'assistant.

- La melodie est synthetisee dans `sonnerie_compose()` : arpege mi - sol# - si
  (accord majeur), timbre avec octave, quinte et une deuxieme voix desaccorde
  a +0,4 %, attaque 6 ms et extinction exponentielle. Un sinus pur ou une
  enveloppe carree donne un bip de reveil, pas un carillon.
- Elle est ecrite dans `rec_pcm` (le tampon du micro) et jouee par
  `audio_play_mono()` : 3,7 s par passage, un passage toutes les 1,3 s.
- La tache `alarme_task` ne joue pas par-dessus la radio ou la voix
  (`radio_etat`, `voix_etat`).
- La phrase est verifiee par `/state` -> `voix_dit` : c'est la derniere phrase
  envoyee au service de synthese, donc la preuve que l'annonce est bien partie.
- `/alarme?t=1` lance la sonnerie a la demande (pour l'ecouter) ; sans
  parametre elle s'arrete. `/state` donne `alarme`, `alarme_min`,
  `alarme_jouee` (compteur de passages reellement joues — c'est ce qui prouve
  que le haut-parleur a bien sonne, sans oreille sur place).
- Le controle de la fenetre des 30 min doit se faire a chaque passe de
  `ms_boucle` (une par minute), PAS apres le test `ms_prochain` : place la
  bas, il n'etait evalue qu'une fois toutes les 5 min et la sonnerie partait
  en retard.

## Le livre (v1.0) : un e-reader sur 200x200

Page LIVRE, 7e du cycle. Un `.txt` par livre dans `/sdcard/livres` (le dossier
est cree au premier scan).

- **Un octet par caractere affiche.** `livre_encode()` convertit l'UTF-8 en
  indices de glyphe au chargement. La pagination compte alors des colonnes et
  jamais des octets : plus aucune conversion pendant la tourne de page, et le
  rendu lit directement `FONT5[code-1]`.
- `0xFF` = fin de ligne, `0xFE` = fin de paragraphe (ligne vide). Un seul octet
  de controle suffit a distinguer un vers d'un paragraphe ; les confondre
  donnait une ligne vide entre chaque vers (les fleurs du mal : 633 pages au
  lieu de 456).
- Police `font5.cpp` : 5x10, generee par `tools/gen_font5.py` depuis la 5x7
  d'Adafruit (`tools/glcdfont.c`, domaine public). Les accents occupent les
  lignes 1-2 au-dessus de la hauteur d'x ; les majuscules accentuees n'ont
  qu'une ligne (0) et se decalent d'un pixel vers le bas pour le cedille.
  **Ne pas editer font5.cpp a la main** : relancer le generateur.
- `livre_dessine()` vit dans `livre.cpp`, pas dans `main.cpp` : `tools/preview
  livre fichier.txt 35` rend exactement la page de la carte, y compris
  l'en-tete et le pied.
- Tourner une page = un rafraichissement **partiel** (865 ms). Enchainer deux
  appuis plus vite que 1 s met la dalle en BUSY : `bouton_court()` attend le
  reste du delai plutot que de jeter l'appui. Refresh complet tous les 10
  partiels, comme partout.
- La position (fichier + page) va en NVS (`livnom`, `livpg`) a chaque tour de
  page ; quitter la page rend la PSRAM (`livre_arret()`), y revenir relit le
  fichier. Verifier la reprise : `reset` puis `/bouton?p=6`.
- La rotation automatique est coupee sur cette page (`rotation_tick`), sinon
  elle volerait la lecture au bout de 30 s.
- EPUB -> TXT sur le PC : `tools/epub2txt.py` (ordre du spine, entites,
  cesures recollees, vers serres, paragraphes detaches par un blanc quand le
  bloc depasse 110 caracteres), puis
  `curl --data-binary @livre.txt 'http://192.168.8.209/fichier?p=/livres/x.txt'`.
- Preuve visuelle sans USB : `/ecran?raw=1` rend le framebuffer en hexa,
  5000 octets, et `tools/shot.py` en fait un PNG.
