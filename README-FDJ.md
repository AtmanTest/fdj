# FDT 4.1.0 — BATMino x FDJ (maquette d'entretien)

Firmware complet pour la carte **BATMino** (ESP32-S3, e-paper 200×200, deux
boutons, micro SD, wifi) : **37 écrans** autour de l'univers FDJ : un intertitre descriptif avant chaque module, navigation
à deux boutons, argent réel du modèle, scores **en direct**, tests sur le PC.

C'est une **maquette indépendante de démonstration** : aucun compte FDJ,
aucune mise, aucun paiement. La mention « AUCUNE MISE RÉELLE » est à l'écran.

L'ancien Mino (compagnon, assistant vocal) reste disponible :
`git checkout v2.0-mino` ou dossier `legacy/mino/`.

---

## 1. Les 20 écrans

| # | Écran | Ce qu'il montre | Source des données |
|---|------|-----------------|--------------------|
| 1 | ACCUEIL FDJ | identité, solde de démo, profil, état du direct | état local + réseau |
| — | *intertitre* | grand titre + phrase avant **chaque** module | — |
| 2 | **ANALYSE & DECISION** | intertitre éditorial du module 01 | — |
| 3 | PARIS SPORTIFS | top 10, proba du modèle, âge de la donnée | scores en direct |
| 4 | LOTO | 3 grilles + 3 faits **exacts** sur les chances | générateur local |
| 5 | HISTORIQUE | 6 paris par page + **détail plein écran** | NVS |
| 6 | GAINS / PERTES | 7 j / 30 j / tout l'anneau, net par jour | historique |
| 7 | NEWS LIVE | 4 manchettes horodatées, marquées lues | RSS franceinfo |
| 8 | MON COUPON | 5 lignes + compteur, totaux fixes, validation | état local |
| 9 | JEU RESPONSABLE | 5 questions qui **changent le plafond**, pause 24 h datée | état local |
| 10 | STATS / PROBAS | 1/19 068 840, cote implicite, win rate réel | calculs locaux |
| 11 | USER STORIES | 6 US et leurs critères | ce dépôt |
| 12 | TESTS | chiffres **réels** de la dernière recette | `fdj_tests_stats.h` |
| 13 | ARCHITECTURE | modules, flux, tailles mesurées par `sizeof` | ce dépôt |
| 14 | PERF / SÉCURITÉ | ms de rendu et d'écran, TLS, API locale | mesures live |
| 15 | MÉMOIRE RAM | heap interne, plus gros bloc, PSRAM, capacités réelles | mesures live |
| 16 | COMPTE & RÉGLAGES | titulaire, compte, réseau, son, code (jamais affiché) | NVS |
| 17 | PROFILS | 4 max, activation, **suppression réelle** avec confirmation | NVS |
| 18 | ACCESSIBILITÉ | gros texte, contraste, son, langue | NVS |
| 19 | DONNÉES & RGPD | où vivent les données, flux sortants, effacement, export | NVS + SD |
| 20 | RÈGLES FDJ | 3 familles, conditions, 8 jeux, limites de la maquette | ce dépôt |

Règle tenue partout : **aucune valeur inventée**. Pas de réseau → `N.D.`,
pas de match → « AUCUN MATCH EN DIRECT », pas d'heure → « HEURE NON
SYNCHRONISÉE ». Un pari en cours affiche son gain **potentiel**, les gains
d'une période ne comptent que les paris **gagnés**. Le code de démarrage
n'est jamais écrit à l'écran, ni le mot de passe Wi-Fi.

## 2. Les deux boutons (table validée sur la carte)

| Geste | Effet |
|-------|-------|
| **B2** court | item suivant ; après le dernier item → page suivante |
| **B2** maintenu | défilement rapide (répétition) |
| **B2** maintenu **3 s** | retour accueil, d'où qu'on soit |
| **B2** double | action principale de la page (actualiser, valider, créer…) |
| **B1** court | cocher / valider / régler l'option sous le curseur |
| **B1** double | annuler, décocher, ou confirmer une suppression |
| **B1** long | page précédente |

Principe : **B2 ne fait que déplacer, B1 ne fait que décider.** L'état d'une
option se lit à la coche (carré + trait), jamais à la couleur. Le pied de
chaque page rappelle les gestes **réellement câblés** sur cette page.

## 3. Architecture

```
main/
  main.cpp        matériel : alimentation, e-paper, audio, SD, boutons, wifi
  fdj_app.cpp     application : état NVS, réseau, clic, actions, API, console
  fdj_model.cpp   métier pur : argent, profils, hasard, Poisson, loto   (testé PC)
  fdj_parse.cpp   flux : TheSportsDB (JSON), franceinfo (RSS)           (testé PC)
  fdj_nav.cpp     gestes → intentions                                   (testé PC)
  fdj_ui.cpp      37 écrans en 1 bit                                    (testé PC)
  art.cpp         primitives de dessin (partagées hôte/carte)
  geste.h         machine à gestes (inchangée, déjà éprouvée)
```

`fdj_model`, `fdj_parse`, `fdj_nav` et `fdj_ui` ne dépendent **pas** d'ESP-IDF :
le même code tourne sur la carte et sur le PC, ce qui permet de tester les
20 écrans et de les rendre en PNG avant de flasher.

État persistant : **un seul blob NVS** versionné (`magic`, `version`, `taille`,
`CRC32`). Un blob corrompu est refusé, l'application repart sur le profil
d'usine. Le passage 3.0.0 → 4.0.0 change le schéma (pause datée, options) :
un blob v3 est refusé et remplacé par l'état d'usine, sans corruption.

## 4. Construire, tester, flasher

```bash
# tests + chiffres réels + firmware + PNG des 20 écrans
tools/recette.sh

# seulement les tests (7 suites)
tests/run_unit.sh

# flasher (la carte est en /dev/ttyACM0)
env -i HOME=$HOME PATH=/usr/local/bin:/usr/bin:/bin IDF_TOOLS_PATH=$HOME/esp/tools \
  bash -c '. ~/esp/esp-idf/export.sh >/dev/null 2>&1; idf.py -p /dev/ttyACM0 flash'

# voir l'écran réel par USB (aucun wifi nécessaire)
python3 tools/ecran_usb.py build/ecran.png 7
```

La recette écrit `docs/RECETTE.md` et `main/fdj_tests_stats.h` : l'écran 12
affiche donc les chiffres de la **dernière** exécution, pas une promesse.

## 5. Vérification à distance (démo)

Une fois sur le wifi (voir `main/secrets.h`), la carte expose :

| URL | Effet |
|-----|-------|
| `/etat` | page, curseur, mode, profil, solde, historique, top, heap, pages |
| `/ecran` | la dernière image en hexadécimal (→ `tools/fb2png.py`) |
| `/bouton?n=1&m=court\|double\|long\|repeat3` | le même geste que le doigt |
| `/page?n=7` | forcer un écran |

Sans wifi, la **console USB** fait la même chose : `p 7`, `b 1 double`,
`e` (état JSON), `s` (écran en hexa). L'API n'est pas exposée à Internet et
ne contient aucun secret.

## 6. Sources des données

* **Scores** : `thesportsdb.com` (clé de test publique `123`), 16 matchs max,
  rafraîchis toutes les 5 min ; `B2×2` sur la page paris refait une **vraie**
  requête (et non un simple recalcul). Aucun direct → matchs du jour.
* **Titres** : flux RSS franceinfo, 4 affichés, rafraîchi toutes les 3 h,
  `B2×2` force une requête (30 s minimum entre deux demandes).
* **Modèle** : Poisson (2,6 buts/match), λ recalculés selon le score et la
  minute ; marché 1/N/2, +/-2,5 buts, BTTS. C'est un **modèle**, pas une cote
  FDJ — la page 3 l'écrit en clair, avec l'âge de la donnée.
* **Loto** : tirage local équilibré (somme, parité, bas/haut). La page dit
  explicitement qu'une grille « équilibrée » garde **la même probabilité**
  (1 sur 19 068 840) : elle limite seulement le risque de partager un gain.

## 7. Ce que la 4.0.0 corrige (défauts de la 3.0.0)

1. Coupon plein : plus de chevauchement à 10 sélections (5 lignes + compteur).
2. Historique : le détail annoncé s'ouvre vraiment, en plein écran, et la
   pagination existe au-delà de 6 paris.
3. Mémoire : « refaire les mesures » est câblé, les jauges utilisent les
   capacités réelles (`heap_caps_get_total_size`).
4. VPN : l'option mensongère est retirée ; la page dit que le tunnel n'existe pas.
5. Profils : la suppression est réelle (profil non actif, confirmation).
6. Gros texte : il agit sur les lignes principales et allège les listes
   denses ; testé sur 12 écrans au moins.
7. Paris : `B2×2` refait la requête réseau, et l'âge de la donnée est affiché.
8. Gains : « TOUT (32 MAX) » remplace le faux « 1 AN », et le sélecteur a un
   effet réel (détail de la période + ventilation par jeu).
9. Compte : plus de faux secret révélé, plus de code affiché en clair.
10. Loto : les astuces non sourcées sont remplacées par trois faits exacts.
11. Architecture : les chiffres affichés sont mesurés (`sizeof`, 200×25).
12. Règles : trois items avec un curseur visible.
13. Pied de page : l'aide décrit le geste réel de chaque écran.

## 8. Limites assumées

* Maquette de démonstration : **aucune mise réelle**, aucun compte FDJ.
* Le mot de passe Wi-Fi n'est ni stocké en NVS applicatif ni affiché ; il vit
  dans `main/secrets.h`, hors dépôt.
* Le code de démarrage est stocké en clair en NVS (4 chiffres) : c'est un
  verrou de démonstration, pas un secret cryptographique. Il n'est plus
  affiché à l'écran.
* Aucun tunnel VPN : l'option a été retirée de l'interface.
* Un seul son : un clic de 25 ms, désactivable (plus de jingle ni de sons
  montants/descendants).
* Le Wi-Fi est forcé en **2,4 GHz** (le S3 ne voit pas le 5 GHz) ; le réseau
  de démonstration doit être un SSID 2,4 GHz. Trois réseaux sont essayés dans
  l'ordre (hotspot de démo, puis deux secours) ; sur un partage iPhone,
  activer « Maximiser la compatibilité ».
* Tests carte (flash, réseau réel, mesures sur dalle) : **non exécutés dans
  cette livraison** si aucune carte n'est branchée ; voir `docs/RECETTE.md`.

---

Signature : **@paristzou** — *juste pour les filles*.
