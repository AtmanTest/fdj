# FDT 3.0.0 — BATMino x FDJ (maquette d'entretien)

Firmware complet pour la carte **BATMino** (ESP32-S3, e-paper 200×200, deux
boutons, micro SD, wifi) : **19 pages** autour de l'univers FDJ, navigation
à deux boutons, argent réel du modèle, scores **en direct**, tests sur le PC.

Le code est refait de zéro. L'ancien Mino (compagnon, assistant vocal) reste
disponible : `git checkout v2.0-mino` ou dossier `legacy/mino/`.

---

## 1. Ce que la carte sait faire

| # | Page | Ce qu'elle montre | Source des données |
|---|------|-------------------|--------------------|
| 1 | ACCUEIL FDJ | logo, solde, profil, état du direct | état local |
| 2 | PARIS SPORTIFS | top 10 des sélections, proba du modèle | scores en direct |
| 3 | LOTO | 3 grilles équilibrées + astuce du jour | générateur local |
| 4 | HISTORIQUE | paris passés, détail (code, jeu, mise/potentiel) | NVS |
| 5 | GAINS / PERTES | 7 j / 30 j / 1 an + barres par jour | historique |
| 6 | NEWS LIVE | 4 manchettes, marquées lues | RSS franceinfo (3 h) |
| 7 | MON COUPON | sélections cochées, mise, gain possible | état local |
| 8 | JEU RESPONSABLE | 5 questions, jauge, pause 24 h | état local |
| 9 | STATS / PROBAS | 1/19 068 840, cotes, win rate réel | calculs locaux |
| 10 | USER STORIES | 6 US, critères d'acceptation, techno | ce dépôt |
| 11 | TESTS | chiffres **réels** de la dernière recette | `fdj_tests_stats.h` |
| 12 | ARCHITECTURE | modules, flux, RAM | ce dépôt |
| 13 | PERF / SÉCU / VPN | ms de rendu et d'écran, TLS, PIN, VPN | mesures live |
| 14 | RAM & SWAP | heap interne, PSRAM, carte SD | mesures live |
| 15 | COMPTE & RÉGLAGES | nom, compte, SSID, mot de passe masqué, code | NVS |
| 16 | PROFILS | 4 max, plafonds 7 j / 30 j par profil | NVS |
| 17 | ACCESSIBILITÉ | gros texte, contraste, sons, langue | NVS |
| 18 | DONNÉES & RGPD | où vivent les données, effacement, export CSV | NVS + SD |
| 19 | RÈGLES FDJ | 8 jeux, 3 familles, conditions, crédits | ce dépôt |

Règle tenue partout : **aucune valeur inventée**. Pas de réseau → `N.D.`,
pas de match → « AUCUN MATCH EN DIRECT », pas d'heure → « HEURE NON
SYNCHRONISÉE ». Un pari en cours affiche son gain **potentiel**, et les gains
d'une période ne comptent que les paris **gagnés**.

## 2. Les deux boutons (table validée sur la carte)

| Geste | Effet |
|-------|-------|
| **B2** court | item suivant ; après le dernier item → page suivante |
| **B2** maintenu | défilement rapide (répétition) |
| **B2** maintenu **3 s** | retour page 1, d'où qu'on soit |
| **B2** double | valider la page (action principale : régénérer, jouer, changer…) |
| **B1** court | cocher / valider l'option sous le curseur — **son montant** |
| **B1** double | annuler / décocher — **son descendant** |
| **B1** long | page précédente |

Principe : **B2 ne fait que déplacer, B1 ne fait que décider.** On ne peut pas
miser en croyant changer de page. L'état d'une option se lit à la coche
(carré + trait), jamais à la couleur.

## 3. Architecture

```
main/
  main.cpp        matériel : alimentation, e-paper, audio, SD, boutons, wifi
  fdj_app.cpp     application : état NVS, réseau, sons, actions, API, console
  fdj_model.cpp   métier pur : argent, profils, hasard, Poisson, loto   (testé PC)
  fdj_parse.cpp   flux : TheSportsDB (JSON), franceinfo (RSS)           (testé PC)
  fdj_nav.cpp     gestes → intentions                                   (testé PC)
  fdj_ui.cpp      19 pages en 1 bit                                     (testé PC)
  art.cpp         primitives de dessin (partagées hôte/carte)
  geste.h         machine à gestes (inchangée, déjà éprouvée)
```

`fdj_model`, `fdj_parse`, `fdj_nav` et `fdj_ui` ne dépendent **pas** d'ESP-IDF :
le même code tourne sur la carte et sur le PC, ce qui permet de tester les 19
pages et de les rendre en PNG avant de flasher.

État persistant : **un seul blob NVS** (`magic`, `version`, `taille`, `CRC32`).
Un blob corrompu est refusé, l'application repart sur le profil d'usine.

## 4. Construire, tester, flasher

```bash
# tests + chiffres réels + firmware + PNG des 19 pages
tools/recette.sh

# seulement les tests
tests/run_unit.sh

# flasher (la carte est en /dev/ttyACM0)
. ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash

# voir l'écran réel par USB (aucun wifi nécessaire)
python3 tools/ecran_usb.py build/ecran.png 7
```

La recette écrit `docs/RECETTE.md` et `main/fdj_tests_stats.h` : la page 11
affiche donc les chiffres de la **dernière** exécution, pas une promesse.

## 5. Vérification à distance (démo)

Une fois sur le wifi (voir `main/secrets.h`), la carte expose :

| URL | Effet |
|-----|-------|
| `/etat` | page, curseur, mode, profil, solde, historique, top, heap |
| `/ecran` | la dernière image en hexadécimal (→ `tools/fb2png.py`) |
| `/bouton?n=1&m=court\|double\|long\|repeat3` | le même geste que le doigt |
| `/page?n=7` | forcer une page |

Sans wifi, la **console USB** fait la même chose : `p 7`, `b 1 double`,
`e` (état JSON), `s` (écran en hexa).

## 6. Sources des données

* **Scores** : `thesportsdb.com` (clé de test publique `123`), 16 matchs max,
  rafraîchis toutes les 5 min. Aucun direct → matchs du jour, marqués « à venir ».
* **Titres** : flux RSS franceinfo, 4 affichés, rafraîchi toutes les 3 h.
* **Modèle** : Poisson (2,6 buts/match), λ recalculés selon le score et la
  minute ; marché 1/N/2, +/-2,5 buts, BTTS. C'est un **modèle**, pas une cote
  FDJ — la page 2 l'écrit en clair.
* **Loto** : tirage local équilibré (somme 100–180, 2–3 pairs, 2–3 bas).
  Cela ne change **pas** la probabilité (1 sur 19 068 840), seulement le
  risque de partager un gain.

## 7. Limites assumées

* Maquette de démonstration : **aucune mise réelle**, aucun compte FDJ.
* Le mot de passe wifi n'est ni stocké ni affiché en clair (masqué, révélation
  3 s demandée explicitement).
* Le code de démarrage est stocké en clair en NVS (4 chiffres, comme demandé) :
  c'est un verrou de démonstration, pas un secret cryptographique.
* Le « VPN » est une option d'affichage : le firmware ne monte pas de tunnel.

---

Signature : **@paristzou** — *juste pour les filles*.
