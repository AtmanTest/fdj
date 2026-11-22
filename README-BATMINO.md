# ADV card computer — le robot e-paper (Waveshare ESP32-S3-ePaper-1.54)

Carte : ESP32-S3, 8 Mo PSRAM, écran e-paper 200x200 monochrome, micro + ES8311,
carte micro SD 128 Go. MAC `70:04:1d:d7:aa:28`, port série `/dev/ttyACM0`
(USB-Serial/JTAG). Ce dossier est la copie de travail : la copie d'origine
`/home/taz/robot-ia` appartient à root.

## Le projet

**ADV card computer** (le robot s'appelle BATMino). Une appli par page, chaque
appli tuée en mémoire quand on la quitte, et seulement le Wi-Fi, l'heure, les
boutons, l'écran, les rappels et l'assistant qui tournent en permanence.

- Une fiche par fonctionnalité, avec ses tests : `docs/features/`
- Test global sur la carte, par l'USB : `tools/test_global.py`
- Lancer les tests : `source tools/env.sh && $IDF_PYTHON_ENV_PATH/bin/python tools/test_global.py /dev/ttyACM1`

## Ce qu'il est devenu

Il s'appelait Mino, un cochon. C'est maintenant **BATMino** : une petite
chauve-souris à cagoule et cape, qui vit une vie de tamagotchi dans une ville
en noir et blanc, et qui parle.

## Les fichiers

    main/main.cpp        firmware : WiFi/API, audio, écran, boutons, menu, tâches
    main/art.h/.cpp      le personnage 1 bit (73 expressions, 14 familles) + primitives
    main/pet.h/.cpp      le moteur du tamagotchi : jauges, évolution, maladie, mort
    main/petart.h/.cpp   le monde (ville, bat-signal, sol), le sprite qui évolue, le bandeau
    main/livre.h/.cpp    le lecteur de livres : encodage, pagination, rendu 5x10
    main/font5.h/.cpp    la police 5x10 du lecteur (accents compris), générée
    tools/preview.cpp    aperçu PNG sur le PC : le même code de dessin que la carte
    tools/pettest.cpp    vérifie le moteur en accéléré (jours simulés en secondes)
    tools/talk.py        dialogue série avec la carte
    tools/ecran.py       photo de l'écran réel -> PNG (attend que la carte soit prête)
    tools/ascii_fb.py    le framebuffer de la carte en ASCII, pour trancher un doute
    tools/gen_font5.py   fabrique la police 5x10 du lecteur (tools/glcdfont.c = base)
    tools/epub2txt.py    EPUB -> texte simple, à poser dans /livres sur la carte

Le rendu est **partagé** : `art.cpp`/`petart.cpp` se compilent côté carte et côté
PC. Ce qu'on voit dans `./preview` est exactement ce que l'e-paper affiche.

## Interface : deux boutons, cinq gestes

    DOUBLE APPUI bouton 1 : LE SEUL geste qui lance l'assistant (chat vocal),
                            depuis n'importe quelle page. La fenêtre pour
                            enchaîner les deux appuis est de 320 ms.
    APPUI COURT bouton 1  : page SUIVANTE (1 = suite dans la page REPONSE)
    APPUI COURT bouton 2  : page PRÉCÉDENTE (2 = avant dans la page REPONSE)
    APPUI LONG (1 ou 2)   : retour à l'accueil et arrêt de ce qui tourne
    MAINTIEN (après le long) : répète l'action de la page toutes les 400 ms —
                            le seul moyen de faire défiler avec deux boutons

La lecture des broches est séparée de l'exécution (`main/geste.h`, deux tâches
`button_echantillonne` / `button_action`) : un appui n'est jamais perdu, même
quand la dalle met une seconde à se rafraîchir. Voir `docs/features/01-boutons.md`.

    Sur la page LIVRE, les deux boutons tournent les pages du livre et le
    double appui 2 passe au livre suivant (voir docs/features/09-livre.md).

    Pendant une tâche (écoute, radio, voix), un appui simple — l'un ou l'autre
    bouton — l'arrête et ramène à l'accueil au lieu de tourner la page.
    La page HISTORIQUE garde son défilement sur le bouton 1 (messages plus
    anciens) ; l'appui long y revient au plus récent.

L'appui simple du bouton 1 est retenu 320 ms avant d'être exécuté : c'est la
seule façon de ne pas tourner la page juste avant de lancer l'assistant. Le
bouton 2, lui, part tout de suite (il n'a pas de double appui).

Testable à distance : `/bouton?n=0|1&m=court|long|double`.

Pages du cycle, une appli par page (comme les premières versions) :

    ACCUEIL    l'heure, la date, le prochain rendez-vous, le compagnon
    METEO      le ciel maintenant (température en gros, état) et demain
               (min/max, pluie) ; « PAS DE DONNEES » s'il n'y a pas de wifi
    SYSTEM     cpu, ram, psram, watts, réseau (nom de la borne en haut à
               droite), carte SD et flash
    HISTORIQUE les derniers échanges lus sur la carte
    YOUTUBE    la chaîne et sa dernière vidéo (vues, âge). Se règle une fois :
               `yt @monpseudo` dans la console, ou `yt UCxxxx...`
    MATRIX     l'analyse du systeme : une sequence de pluie de code (4 images,
               pas une animation — la dalle clignote a chaque rafraichissement)
               puis un rapport fixe sur les mesures reelles : cpu, ram, psram,
               carte, wifi, batterie, modele du cerveau. Appui LONG 1 = rejouer.
    LIVRE      le lecteur : un .txt par livre dans /livres sur la carte, la
               page courante en grand, position retenue entre deux sessions

La grille des applis : **double appui du bouton 2**, depuis n'importe quelle page.
Huit tuiles (ACCUEIL, METEO, SYSTEM, HISTO, YOUTUBE, CONSEIL, MATRIX, LIVRE),
bouton 1 = tuile suivante, bouton 2 = ouvrir, appui long = accueil.

La page RÉGLAGES existe encore dans le code mais n'est plus dans le cycle
(jugée mal faite) : elle reste joignable par `/reglages` et `/wifi` (et garde
ses gestes propres pour le clavier).

## Le conseil : la page qui ne repond pas, elle juge

Cinq pages affichent une donnee. La sixieme, **CONSEIL**, affiche un jugement.
Une tache de fond (`conseil`) appelle le cerveau en dehors de toute
conversation : elle lui donne la date, le prochain rendez-vous avec son delai
en clair, la meteo, l'etat du compagnon et la batterie, et lui demande une
seule chose — l'action la plus utile maintenant, ou RIEN.

    page CONSEIL      le conseil en clair, l'heure du jugement et les faits
                      qui l'ont motive (« RDV 2H10  BAT 66% »)
    appui LONG 1      demande un nouveau jugement : le seul geste de la page
                      (COURT reste la navigation, comme partout)
    /conseil          lit le dernier conseil en JSON (texte, faits, heure, etat)
    /conseil?o=1      force une passe

Quand elle juge : une passe au demarrage, puis toutes les 30 minutes, et plus
tot si un rendez-vous tombe dans moins de deux heures. Elle ne prend jamais la
parole pendant une session (voix, radio, assistant) et ne cree jamais de
rappel : le modele n'a aucune prise sur le materiel.

Si le cerveau repond RIEN, la page l'ecrit noir sur blanc — c'est une
information, pas un manque. Cout : un appel toutes les 30 minutes, sans TLS ni
reveil de la dalle entre deux.

## L'assistant peut créer un rappel

Le modèle n'a pas la main sur le matériel : le seul ordre qu'il peut donner est
un rappel, et la carte le valide. Sa réponse JSON accepte
`"rappel":{"d":<secondes>,"n":"<3 mots>"}` ; le firmware vérifie la durée
(10 s à 7 jours), en fait un minuteur nommé et, à l'échéance, joue le carillon
puis la voix (« Rappel : sortir le linge. »). Un rappel par question.

Sans wifi, l'heure n'est pas synchronisée : le rappel est refusé proprement
plutôt que créé avec une échéance fausse. Preuves à distance : `/rappel?d=600&n=test`
(crée), `/rappel?a=1` (coupe la sonnerie), `/state` -> `rappel_nb`,
`rappel_dernier`, `annonce`.

En plus : la **page REPONSE**, éphémère. Dès qu'une réponse arrive, elle prend
tout l'écran, en plusieurs pages si besoin (24 signes × 10 lignes en corps 2,
donc jusqu'à ~5 pages). Bouton 1 = page suivante, bouton 2 = page précédente ;
au bout d'un côté, un appui de plus ferme la réponse (l'appui long ferme
toujours). Le texte affiché est translittéré en ASCII (la dalle ne connaît pas
les accents : « OXYGÈNE » devenait « OXYGNE »).

## Le tamagotchi (moteur local, déterministe)

Le LLM ne touche jamais une jauge : la vie du personnage est calculée sur la
carte, seconde par seconde (`pet_tick`), et elle survit aux coupures.

    œuf 20 s -> bébé 20 min -> enfant 3 h -> ado 3 h -> adulte 4-10 j
    -> vieillard ~2 j -> départ, puis génération suivante

* Quatre jauges en points (100 = 1 cœur, 400 = 4 cœurs) : faim, bonheur,
  énergie, propreté. Plus l'obéissance (0-100).
* `care_mistakes` monte quand un cœur reste vide plus de 15 min. C'est lui qui
  décide de la forme adulte : héros, normal, grognon ou maladif.
* Le caca arrive après les repas et salit ; la maladie vient de la faim, du
  caca ou de la saleté prolongés ; non soignée, elle emporte le personnage.
* La nuit (22 h - 7 h), il dort ; il se balade tout seul (un pas toutes les
  2-4 s), et la page MONDE ne se redessine **que s'il a vraiment bougé** :
  l'e-paper met plusieurs secondes à se rafraîchir.

Vérifié hors carte : `tools/pettest.cpp` rejoue des jours en secondes —
avec des soins parfaits le personnage survit et devient héros, sans soin il
part en 12 h avec 8 fautes de soin, et la sauvegarde fait un aller-retour.

## La carte SD (128 Go), partitionnee

La carte porte maintenant deux partitions (ecrites par le firmware, `/part`) :

    p1  FAT32   93,1 Go   LBA 2048        montee sur /sdcard
    p2  brute   23,9 Go   LBA 195313664   zone d'echange, hors FAT

Fichiers sur la partition FAT :

    /sdcard/mino/pet.txt        sauvegarde de l'état (reprise après coupure)
    /sdcard/mino/journal-*.md   journal du jour, une ligne par événement
    /sdcard/mino/soul.md        la personnalité, modifiable depuis le PC
    /sdcard/mino/memory.json    notes durables apprises
    /sdcard/voix.wav            dernière prise du micro
    /sdcard/backup/*.tar.gz     sauvegardes completes (source + git bundle)

Debits mesures sur cette carte (bus 1 bit, 40 MHz — seule cette largeur est
cablee sur la Waveshare) :

    FATFS (fichiers)                  0,37 Mo/s ecriture | 1,6 Mo/s lecture
    zone brute, tampon relais 16 Ko   3,4 Mo/s ecriture  | 3,5 Mo/s lecture
    zone brute, tampon 64-256 Ko PSRAM 0,4-0,6 Mo/s (le tampon PSRAM casse le DMA)

La taille de bloc FATFS ne change rien (0,37 Mo/s de 4 a 256 Ko) : le plafond
vient de FATFS, pas de la carte ni du bus (4,65 Mo/s en secteurs bruts).

Cette zone n'est **pas un swap systeme** : l'ESP32-S3 n'a pas de MMU, donc
aucune pagination. C'est un stockage explicite, utilise par `swap_ecrire()` /
`swap_lire()` (blocs de 16 Ko, memcpy PSRAM <-> carte).

    /part?op=info        etat des partitions
    /part?op=repart&fat_go=100   repartitionne (DESTRUCTIF) puis formate la FAT32
    /part?op=restore     retour a une carte entiere en FAT32
    /part?op=st&n=4      ecrit puis relit n Mo dans la zone d'echange (verifie)
    /part?op=rawbench&bloc=K     debit brut de la zone

La flash interne (SPIFFS, 1,5 Mo) ne sert plus que de secours : tout ce qui
compte vit sur la carte.

## La mémoire : tout sur la carte, en clair

Chaque échange (question + réponse) est écrit **sur la partition brute** de la
carte micro SD (p2, celle qui fait 3,5 Mo/s de lecture/écriture, pas la FAT32 à
0,37 Mo/s). Un message = un secteur de 512 octets : rôle, longueur, texte. Le
secteur 0 porte l'en-tête (index d'écriture, nombre de messages, compteur
absolu). 4096 messages de réserve, la zone en fait 24 Go.

Rien n'est perdu au redémarrage : au boot, la carte relit les 32 derniers tours
dans la fenêtre PSRAM (`hist_p`) et le journal complet reste lisible par
`/hist?n=40`.

    /hist?n=40     les derniers échanges tels qu'ils sont sur la carte
    /state         hist_sd, hist_sd_n, hist_sd_tours, hist_sd_ko, hist_sd_etat

## Le cerveau : moins de tokens, et il se souvient

Économie : les **6 derniers tours** partent tels quels au modèle, les plus
anciens sous forme d'un **rappel compact** d'une ligne chacun (« MINO: …22
signes »). C'est `contexte_octets` dans `/state` qui montre ce qui a vraiment
été envoyé (quelques centaines d'octets au lieu de plusieurs Ko).

Cache d'échanges : si la question renvoie à quelque chose de déjà dit et que le
modèle n'est pas sûr, il rend `{"souvien":"2 a 4 mots","dit":""}` ; la carte va
alors **chercher ces mots dans l'historique de la SD** (200 derniers messages),
envoie les extraits trouvés (le message + la réponse qui suit) et redemande la
réponse une seule fois. C'est ce qui permet de reprendre le fil sans renvoyer
tout l'historique.

## L'assistant vocal

Tout se passe sur la carte, sans PC : micro -> WAV en PSRAM -> Groq
(whisper-large-v3-turbo) -> texte -> DeepSeek/Groq (chat) -> reponse -> TTS
(Google Traduction) -> haut-parleur. Le modele et les cles se changent par
`/cle` et `/modele` (stockes en NVS, pas dans le binaire).

    appui court bouton 1 sur l'accueil -> "PARLE MAINTENANT" (le micro s'ouvre,
    l'enregistrement s'arrete seul apres 1 s de silence, max 6 s)
      -> "JE REFLECHIS" -> la reponse s'affiche en pages plein ecran
    appui court bouton 2 -> l'ecoute est coupee proprement, page suivante

**Le haut-parleur est muet par defaut** : le robot répond à l'écran, il ne parle
pas (`voix_sortie:false` dans `/state`, et `voix_dit_nb` compte les phrases
produites). `/voix?o=1` rallume la synthèse vocale, `/voix?o=0` la coupe.
La veille automatique de nuit (22h-7h) est également désactivée par défaut :
`/veille?o=0|1`.

**Les informations défilent toutes seules** : ACCUEIL -> METEO -> RAPPELS ->
SYSTEM -> HISTORIQUE -> YOUTUBE, une page toutes les 30 s, sans toucher aux
boutons (`/state` -> `rotation:true`, `/rotation?o=0|1&s=SEC` pour régler,
valeur gardée en NVS). Un appui ou un geste tactile met la rotation en pause
90 s pour laisser lire la page choisie ; elle ne tourne jamais pendant une
écoute, une réflexion ou une réponse en pages.

Si le Wi-Fi manque, la carte le dit a l'ecran au lieu d'attendre.

### Le cerveau (maj 2026-10-19)

* Modele : **deepseek-flash** = DeepSeek V4.1 Flash (l'ancien `deepseek-chat`
  donnait des reponses pauvres). Reglable par `/modele?m=deepseek-flash`.
* Style : **assistante pure, pas de personnage**. Le prompt impose : aucune formule
  d'ouverture ni de conclusion, pas de liste markdown, pas d'emoji, aucun commentaire
  sur soi-meme ni sur l'humeur de Taz. La reponse va aussi loin que la question
  l'exige (un mot si un mot suffit, sinon jusqu'a 900 signes, ce que l'ecran affiche
  en quatre pages). Le champ `emotion` n'est plus une reaction : c'est une etiquette
  technique qui choisit un visage, dans une liste sobre (neutre, calme, reflexion,
  concentration, doute, fatigue...). `max_tokens` 500.
* Contexte injecte a chaque appel : date + heure en francais, meteo
  maintenant, meteo de demain (min/max, pluie), et les 8 titres du moment
  (franceinfo, rafraichis toutes les 30 min). Le modele ne repond donc plus
  avec sa memoire d'entrainement quand on lui demande le jour ou l'actu.
* Web : si le modele ne sait pas, il rend `{"cherche":"2 a 6 mots"}` ; la carte
  interroge alors **Wikipedia** (`generator=search` + `extracts`, 2 articles)
  et **Google News RSS** (4 titres, seulement si la question est une actu), puis
  redemande une reponse courte avec ces resultats. Pas de cle d'API.
  DuckDuckGo/Bing/Mojeek sont inutilisables depuis l'ESP (captcha/202).
* Cout : ~2-4 s pour une question simple, ~10-20 s quand il faut chercher.
* Echec de recherche : le modele repond quand meme en une phrase, il ne dit
  plus "je ne trouve rien".

### Wi-Fi : deux bugs corriges

* Un mot de passe de 1 caractere etait enregistre en NVS (saisie ratee dans
  `/reglages`) : le handshake echouait en boucle avec la raison 15. Un mot de
  passe enregistre de moins de 8 caracteres est maintenant ignore.
* `caca` (LAN isole, 192.168.1.x) passait en secours AVANT `MON_RESEAU_SECOURS_2` : le
  robot y devenait injoignable depuis le PC. Il est desormais en dernier, et
  on attend 2,5 s entre deux tentatives (la borne rejette les reconnexions
  trop rapides).

L'ancien cerveau PC (`mino-cerveau`) n'est plus necessaire ; s'il tourne encore
il faut l'arreter, il tient le port serie :

    systemctl --user stop mino-cerveau

## Anti-ghosting

Le pilote ne rechargeait jamais la seconde RAM (0x26) ni la LUT pleine, donc
l'ancienne image restait collée. Ajouté : `EPD_ShowFull`, `EPD_ShowBase`,
`EPD_ShowFullClean` (LUT pleine + 0x24/0x26), image blanche au démarrage,
et dans `show_ui` un rafraîchissement complet à chaque changement de page et
tous les 10 rafraîchissements partiels.

## Compiler, flasher, regarder

    cd /home/taz/mino-fw
    source tools/env.sh          # voir la note ci-dessous
    idf build
    idf -p /dev/ttyACM0 flash
    # ou, sans cable, par wifi :
    python3 tools/maj_wifi.py 192.168.8.209     # POST /maj, redemarrage, revient en ligne

> **`export.sh` casse depuis la montée de Python 3.14** : le venv
> `/opt/espressif-tools/python_env/idf5.5_py3.14_env` n'a plus de
> `pydantic_core` utilisable et `check-python-dependencies` refuse `click 8.5`
> / `cryptography 50`. Inutile de réinstaller ESP-IDF : `tools/env.sh` pose
> `IDF_PATH`, le PATH du toolchain xtensa et un `PYTHONPATH=~/.idf-extra` où
> `pydantic-core==2.50.1` est installé, puis expose la fonction `idf`.
> Créer ce dossier une fois :
> `python3 -m pip install --target ~/.idf-extra pydantic-core==2.50.1`
> (avec le python du venv ESP-IDF).

    cd tools
    g++ -O2 -o preview preview.cpp ../main/art.cpp ../main/petart.cpp ../main/pet.cpp -lm -lz
    ./preview monde            # montage 4x3 : œuf, bébé, enfant, ado, adulte...
    ./preview pet 4 0          # une scène en grand (4 = adulte, 0 = héros)
    ./preview sel colere,peur,surprise,degout,joie,tristesse,neutre,sourire_en_coin,reflexion,curiosite,fierte,douleur sel3.png

    systemctl --user stop mino-cerveau
    /opt/mino-venv/bin/python talk.py --fb ecran_reel.png     # l'écran réel
    /opt/mino-venv/bin/python talk.py state                   # état complet
    systemctl --user start mino-cerveau

Ou en un seul geste (recommandé) :

    systemctl --user stop mino-cerveau
    /opt/mino-venv/bin/python ecran.py ecran.png "ui 0" "menu 4"
    /opt/mino-venv/bin/python ascii_fb.py                     # le framebuffer en ASCII
    systemctl --user start mino-cerveau

Attention : **ouvrir le port série redémarre la carte** (DTR/RTS du
USB-Serial/JTAG). Les outils ci-dessus baissent DTR/RTS puis attendent que la
carte réponde avant d'envoyer une commande — c'est indispensable, sinon la
commande part pendant le démarrage et personne ne répond.

## API HTTP de la carte

    /state                 état complet (stade, jauges, cpu, forme, voix, historique)
    /hist?n=40             les derniers échanges, lus sur la carte SD
    /voix?o=0|1            coupe / rallume le haut-parleur (muet par défaut)
    /veille?o=0|1          mise en veille automatique la nuit (désactivée)
    /rotation?o=0|1&s=30   pages d'info qui défilent seules (actif, 30 s/page)
    /dit?t=...             pose une question au cerveau et affiche la réponse
    /tts?q=...             synthèse vocale directe (ne fait rien si muet)
    /face?e=<famille|expression>   visage (familles : colere, peur, joie...)
    /pet?a=manger|jouer|calin|nettoyer|soigner|dormir|reveiller
    /say?emotion=<famille>&text=... texte + visage
    /soul, /note, /rec, /play, /sd

## Ce qui reste

* WiFi `MON_RESEAU_SECOURS` : corrige (mot de passe NVS pourri + ordre des
  secours), le robot vit sur 192.168.8.x sans PC.
* Le capteur de température lit la chaleur de la carte : le décalage
  (`offset`) est à refaire pour ce boîtier.
* Pistes non faites : reconnaissance de mots pour réveiller sans bouton,
  plusieurs personnages sur la carte SD, jeux à deux boutons.
