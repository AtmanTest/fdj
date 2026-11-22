# US-006 — Deux pages : l'accueil-conversation et le système

**Demande (Taz)** : « on va mettre le parler directement dans le 0, inutile de
faire une page différente, on peut juste changer l'apparence de la petite boule
en mode écoute ou plus simple mettre le texte listening puis reflexion. On
supprime radio, on supprime timer. Page 2 je veux un mini btop like, uniquement
pour le cpu, ram, gpu (si existe), watt, quantité batterie, vitesse network,
état disque dur et ssd. »

## Ce qui existe maintenant

    PAGE_MAITRE (0)  ACCUEIL : heure, date, météo, watts, le compagnon.
                     Pendant une conversation, l'accueil RESTE affiché : le
                     compagnon passe en grand et le texte d'état s'affiche
                     (ECOUTE / REFLEXION / JE PARLE).
    PAGE_SYS    (1)  SYSTEM : le mini btop.

Plus de pages PARLER, RADIO ni TIMER. Le moteur radio et le moteur de timers
restent dans le code (ils ne sont simplement plus accessibles par l'écran) :
rien n'a été détruit, /radio et /timer répondent toujours.

## La page SYSTEM

Tout est dessine en corps 2 (10 px de haut) et le chiffre du CPU en corps 4
(20 px) : sur 200x200, le corps 1 (5 px) etait illisible.

| y | contenu |
|---|---------|
| 2 | SYSTEM (corps 3) + GPU AUCUN (corps 2) |
| 19 | CPU (corps 2), le pourcentage en corps 4, la frequence a droite |
| 44 | graphe CPU : 38 colonnes de 4 px sur 28 px de haut |
| 79 | RAM : libres / total, barre de 192 px |
| 104 | PSRAM : libres / total, barre |
| 130 | watts (corps 3) et BATTERIE en % (corps 3), barre de batterie |
| 161 | NET : octets/s montants et descendants |
| 174 | SD et FLASH cote a cote, puis deux barres de 92 px |

Sources : CPU = temps d'execution par tache (FreeRTOS) ; RAM/PSRAM =
heap_caps ; watts = conso_mw() ; batterie = ADC1 canal 3 (GPIO4, VBAT =
2 x VADC) ; reseau = compteurs applicatifs ; disques = esp_vfs_fat_info et
esp_spiffs_info.

## Memoire : chaque page rend ce qu'elle a pris

Le changement de page ne laisse rien derriere lui :

| ressource | quand | liberation |
|-----------|-------|-----------|
| `TaskStatus_t[48]` (~1,8 Ko) | a l'entree sur SYSTEM | a la sortie (`free`) |
| unite ADC (batterie) | au premier dessin de SYSTEM | a la sortie (`adc_oneshot_del_unit`) |
| echantillonnage CPU/reseau | seulement page SYSTEM | arret immediat en sortant |

Verifie sur la carte, trois allers-retours : RAM interne constante a 17539 o,
PSRAM qui oscille de 1828 o exactement entre les deux pages (l'allocation du
tableau de taches) et qui revient a sa valeur de depart. Aucune fuite.

`/state` lit la batterie dans un cache : sinon la simple requete HTTP
rallumerait l'ADC et garderait la ressource allouee.

## Cadence

Une image de cette page toutes les 5 s (573 ms de clignotement à chaque image,
c'est le plancher du matériel). Les mesures, elles, sont prises chaque seconde.

## Pièges payés

- **Compter le CPU avec un hook idle ne marche pas** à 100 Hz de tick : le hook
  n'est pas appelé assez souvent et le pourcentage sort faux (93 % sur un CPU
  au repos). Il faut `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` +
  `CONFIG_FREERTOS_USE_TRACE_FACILITY` et `uxTaskGetSystemState()`.
- **La somme des temps de tâche inclut les tâches IDLE** : sans les exclure, le
  total vaut toujours 100 % des deux cœurs. Exclure les noms qui commencent par
  "IDLE".
- Le pourcentage réel au repos est ~0,7 % : arrondi à l'entier il affiche 0,
  donc on garde un dixième (`cpu_pct10`).
- **ESP-IDF n'expose pas les compteurs de l'interface Wi-Fi** (pas de MIB2 dans
  son lwIP, `mib2_counters` n'existe pas). On compte donc les octets applicatifs
  (http_get, envoi STT) — c'est ce qui circule vraiment pour la voix.

## PAGE_REG (2) : les reglages

Trois vues, naviguees avec les deux boutons :

1. **menu** : WIFI (nom du reseau actuel), ECRAN (cadence de la page systeme,
   5/10/30 s), INFOS (log serie). En bas, l'adresse a ouvrir depuis le
   telephone.
2. **liste des reseaux** : scan dedie (tache separee, cache de 14 reseaux trie
   par RSSI, doublons fusionnes), 5 lignes a l'ecran, selection en inverse.
3. **clavier** : 6x5 touches, alphabetique, ESP / EFF / OK / 123 (chiffres et
   symboles), mot de passe affiche en clair avec un curseur.

Commandes : bouton 1 = suivant, bouton 2 = choisir/taper, appui long 1 =
effacer, appui long 2 = valider ou remonter d'un cran, double appui 1 = sauter
une rangee de touches.

Le reseau change a chaud : le SSID et le mot de passe sont ecrits en NVS
(namespace `mino`, cles `ssid` / `pass`), l'ancien est oublie, et au demarrage
suivant c'est celui-la qui est utilise. Si la connexion echoue, `wifi_try`
passe aux reseaux compiles dans `secrets.h` : on ne se coupe jamais les pieds.

### Depuis le telephone

`http://<ip>/reglages` sert une page de 2 Ko : liste des reseaux avec le
signal, choix, mot de passe dans une invite, et champ manuel pour un reseau
cache. C'est la voie normale pour taper un mot de passe : 30 caracteres au
clavier du robot, c'est 300 appuis a 600 ms.

API : `/wscan[?go=1]`, `/wifi?ssid=..&pass=..`, `/reglages`.

### Voir l'ecran en vrai

`/ecran?raw=1` renvoie le framebuffer en hexadecimal (10000 caracteres).
`python3 tools/shot.py sortie.png` le convertit en PNG 400x400, ce qui permet
de juger un dessin sans photographier la dalle.
