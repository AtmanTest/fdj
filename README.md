# BATMino — robot tamagotchi e-paper (ESP32-S3)

Firmware autonome pour la carte **Waveshare ESP32-S3-ePaper-1.54** (ESP32-S3,
8 Mo flash, 8 Mo PSRAM, e-paper 200x200, micro + ES8311) : un personnage 1 bit
qui vit sa vie (tamagotchi déterministe calculé sur la carte), parle, écoute et
répond en Wi-Fi — micro → STT (Groq whisper) → LLM (Groq / DeepSeek) → TTS
(Google Traduction), le tout sur la carte, sans PC.

Deux boutons, quatre gestes : appui court du bouton 1 = lancer la tâche de l'écran
courant (chat vocal sur l'accueil, radio, minuteur…), appui court du bouton 2 =
abandonner la tâche et changer de page, appui long = inchangé pour l'instant,
double appui = détecté et réservé pour la suite.

**Conversation continue** : après chaque réponse, le micro se réouvre seul
pendant 10 s — on peut répondre sans rappuyer sur le bouton (10 tours max). Le
fil est conservé (12 derniers échanges). Son rôle et le portrait de Taz
(`/sdcard/mino/clone.md`) partent dans le prompt système.

## Tests

```bash
bash tests/run_unit.sh    # unitaires, sur le PC (historique) : 12/12
bash tests/api.sh         # integration + composant, sur la carte : 13/13
```

Les exigences par fonctionnalité sont dans `docs/us/` (US-001 conversation
continue, US-002 clone et rôle, US-003 réponses longues).

## Démarrer

```bash
cp main/secrets.h.example main/secrets.h   # puis remplir Wi-Fi + cles
export IDF_TOOLS_PATH=/opt/espressif-tools && . /opt/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 -b 460800 flash
```

ESP-IDF 5.5.1, cible `esp32s3`, partition `factory` 3 Mo + SPIFFS 1 Mo
(`partitions.csv`). `main/secrets.h` n'est **jamais** versionné : il contient le
mot de passe Wi-Fi et les clés d'API.

## Documents

* `README-BATMINO.md` — le personnage, le moteur tamagotchi, l'écran, l'API.
* `main/user_config.h` — broches, URLs des services, ville de la météo.
* `tools/` — outillage PC : `preview.cpp` (même code de dessin, rendu PNG),
  `pettest.cpp` (rejoue des jours en secondes), `talk.py`, `ecran.py`,
  `ascii_fb.py`.

```bash
cd tools
g++ -O2 -o preview preview.cpp ../main/art.cpp ../main/petart.cpp ../main/pet.cpp -lm -lz
./preview monde
```

## API HTTP de la carte

```
/state            etat complet (JSON)
/face?e=<famille> 14 familles d'expression
/say?emotion=<famille>&text=...
/pet?a=manger|jouer|calin|nettoyer|soigner|dormir|reveiller
/soul /note /rec /play /radio /infos /mic /tts /timer /minuteur /modele
/langue /cle /bouton?n=0|1&m=court|long|double /parle /spiffs
/sd?p=/dossier    liste JSON {nom, taille, lisible}
/sdformat?f=1     reformate la carte (FAT32 pleine carte)
/fichier?p=/chemin  GET = telecharge, POST = ecrit (sauvegardes par Wi-Fi)
/part?op=info|repart&fat_go=N|restore|rawtest|rawbench&bloc=K|st&n=Mo
/swap             etat de l'espace libre + bench FATFS (op=bench|fatfs&bloc=K)
/dit?t=texte      injecte un texte comme s'il venait du micro (test cerveau+voix)
```

La carte est partitionnee : FAT32 de 93 Go pour les fichiers + 23,9 Go de zone
brute servant de memoire de debordement (3,4 Mo/s contre 0,37 via FATFS).
Voir `README-BATMINO.md`, section « La carte SD ».
