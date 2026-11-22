# Journal des versions

## 4.2.0 — 2026-11-22 — application Unibet, sans son, copie sur carte SD

- **Marque Unibet** : l'accueil affiche UNIBET (plus BATMINO), la ligne de
  version devient « UNIBET 4.2.0 », l'écran de code et la page règles
  renvoient vers UNIBET.FR.
- **Effets sonores supprimés** : plus de génération de PCM, plus de file de
  notes, plus de tâche audio, plus de champ `son_clic` dans l'état (blob NVS
  v5). La page Accessibilité affiche « SON : AUCUN ».
- **Copie sur la carte SD par le réseau** : `POST /fichier?p=/sdcard/...`
  écrit un fichier de livraison sur la micro SD de la carte (chemin imposé
  sous /sdcard, 6 Mo max, réseau local). `tools/sauvegarde_sd.sh` s'en sert
  quand aucune carte n'est montée sur le PC.
- Contraste : noir franc sur blanc, sélections en bandeau noir, aucune trame.
- 598 vérifications, firmware recompilé et flashé, capture relue.

## 4.1.5 — 2026-11-22 — zéro propre, accueil resserré

- Police 5×10 : le zéro était barré (0x19/0x15/0x13), il lisait « Ø » dans les
  montants. Remplacé par un zéro rond, sur tous les chiffres de l'interface.
- Accueil : « 2 MATCHS EN DIRECT » et le score remontés de 4 px, pour que la
  ligne FDT ne touche plus le bandeau de message.
- Vérifié sur la dalle réelle après flash (capture /ecran).

## 4.1.4 — 2026-11-22 — lisibilité 5×10, son coupé

- **Toute l'interface passe en police 5×10** (FONT5) : 33 caractères par ligne,
  10 px de haut, au lieu du 3×5 de 5 px illisible sur la dalle. Conséquence
  assumée : moins de contenu par écran (6 sélections, 4 paris, 3 titres), mais
  lisible à 50 cm. Les 37 écrans ont été relus un par un.
- Accueil : bloc BATMINO / maquette / mention légale en 5×10, solde en corps
  double, date sous le bloc.
- Le réglage « gros texte » disparaît : il n'a plus d'objet quand tout est
  déjà en 5×10. La page Accessibilité annonce « TEXTE : 5x10 PARTOUT ».
- **Son coupé par défaut** (`son_clic = false`) : plus de clic à chaque geste.
  Le réglage reste disponible page Accessibilité, désactivé.
- Corrections de mise en page : pourcentage qui chevauchait la coche, barre de
  sélection qui coupait la 3e ligne de l'historique, colonne « n PARIS » qui
  touchait le montant, jauge mémoire collée à sa valeur, aide de pied tronquée.
- 598 vérifications (7 suites), firmware 1 271 664 o, 37 captures.

## 4.1.3 — 2026-11-22 — le M se lit enfin comme un M

- Police 3×5 : le glyphe M avait deux barres au milieu, ce qui le rendait
  identique à un H sur la vraie dalle (« BATHINO », « HAQUETTE »). Sa barre
  passe en haut du glyphe, et le bloc logo de l'accueil (BATMINO, maquette,
  mention légale) est désormais composé en 5×10, où le M est sans ambiguïté.
- Accueil : date replacée sous le bloc logo, encadrés décalés de 2 px.
- Vérifié sur la dalle réelle après flash : « BATMINO » lisible, 2 matchs en
  direct, Wi-Fi connecté, version 4.1.3 annoncée par /etat.
- Aucun autre changement ; 599 vérifications, firmware recompilé et flashé.

## 4.1.2 — 2026-11-22 — outil de flash

- `tools/flash.sh` : détecte le port (ttyACM ou ttyUSB), compile, flashe, puis
  relit le log série et **confirme la version** annoncée par la carte. Sans
  carte branchée, il le dit et sort en 2 au lieu d'échouer salement.

## 4.1.1 — 2026-11-22 — nettoyage

- Intertitre : la taille du grand titre passe par `fit_s` (largeur) puis par
  une boucle de hauteur, au lieu de deux calculs dupliqués. Aucun changement
  visible, aucun avertissement de compilation.

## 4.1.0 — 2026-11-22 — 37 écrans et Wi-Fi de démonstration

### Ajouté
- **37 écrans** : l'accueil, puis pour chacun des 18 modules un **intertitre
  descriptif** (grand titre 4 à 6 fois la police de base, une phrase, une note
  encadrée) suivi de sa page. L'intertitre se traverse avec B1 ou B2×2.
- Wi-Fi : **trois réseaux** essayés dans l'ordre (hotspot de démonstration
  d'abord, puis les deux réseaux de la maison), bascule automatique en cas de
  perte, et affichage du **SSID réellement connecté** au lieu du nom supposé.
- Tests : 599 vérifications (les 37 écrans sont rendus et testés, chaque
  module vérifie la présence de son intertitre).

### Modifié
- Les gestes de page s'appuient sur le **module** et non plus sur un numéro
  d'écran : ajouter un module ou un intertitre ne décale plus les actions.
- Le pied de page des intertitres indique « B2 : CONTINUER ».

### Limites
- Toujours pas de carte branchée : flash, dalle et Wi-Fi réel non exécutés.

## 4.0.1 — 2026-11-22 — documentation de la démonstration

- `docs/DEMO-ENTRETIEN.md` : parcours de 3 minutes, gestes et réponses aux
  questions probables (le prompt de livraison le demandait).
- Aucun changement de comportement ; firmware recompilé et recette relancée
  pour que la version affichée (4.0.1) corresponde au dépôt.

## 4.0.0 — 2026-11-22 — FDT (BATMino x FDJ), refonte visuelle et corrections

Refonte de la maquette pour l'entretien : 20 écrans, gabarit homogène, et
correction des 13 défauts constatés en 3.0.0. Aucun module métier supprimé.

### Ajouté
- Écran 02 « ANALYSE & DECISION » : intertitre éditorial inséré entre
  l'accueil et les paris ; le parcours passe à 20 écrans sans rien perdre.
- Gabarit commun : bandeau noir titre + « NN/20 » + état réseau, curseur en
  barre, aide de pied **propre à chaque écran** (gestes réellement câblés).
- Historique : détail plein écran d'un pari (date, jeu, code, mise, gain
  confirmé ou potentiel, statut) et pagination de l'anneau par 6.
- Jeu responsable : la pause 24 h a une **échéance réelle** (début stocké,
  expiration automatique, temps restant affiché) ; sous 3 réponses de
  protection, le plafond hebdomadaire est divisé par deux.
- Profils : suppression réelle d'un profil non actif, avec confirmation.
- Gains / pertes : « TOUT (32 MAX) » au lieu du faux « 1 AN » ; le sélecteur
  pilote un détail par période et une ventilation par jeu ; barres avec
  échelle, pertes sous l'axe et jours nommés.
- Coupon : mise en page compacte (5 lignes + compteur, totaux et boutons en
  zones fixes) : plus aucun chevauchement à 10 sélections.
- Paris : `B2×2` déclenche une **vraie** requête réseau (drapeau consommé par
  la tâche réseau) et l'écran affiche l'âge de la donnée.
- News : distinction jamais chargé / chargé puis vide / indisponible, et
  garde-fou de 30 s sur les demandes forcées.
- Tests : nouvelle suite `test_defauts` (60 vérifications) — un test par
  défaut constaté, plus les 20 écrans, le gros texte et la pause.
- Suite de recette : 477 vérifications, 7 suites, 0 échec au 2026-11-22.

### Corrigé
- Coupon plein qui se chevauchait (défaut n° 1 de la recette 3.0.0).
- Détail d'historique calculé puis jamais dessiné.
- « Refaire les mesures » de la page mémoire non câblé.
- VPN affiché avec un état et une coche alors qu'aucun tunnel n'existe :
  l'option est retirée et la page l'explique.
- « GROS TEXTE » cosmétique : il agrandit désormais les lignes principales
  (titres, valeurs, libellés) et allège les listes denses.
- Code de démarrage affiché en clair et faux « secret révélé » : supprimés.
- Astuces de loto non sourcées : remplacées par trois faits exacts
  (1 chance sur 19 068 840, etc.).
- Chiffres d'architecture non mesurés : remplacés par `sizeof(fdj_etat_t)`,
  `sizeof(fdj_vue_t)` et la taille réelle du framebuffer (200 × 25 octets).
- Page règles : trois items déclarés sans curseur.
- Pied de page incohérent avec les gestes réels.
- Musique, jingle de 5 s et sons montants/descendants : remplacés par un
  **seul clic de 25 ms**, désactivable depuis Accessibilité.
- Wi-Fi forcé en 2,4 GHz (`esp_wifi_set_band_mode`, balayage tous canaux).
- Blob NVS v4 : pause datée et option son ; un blob v3 est refusé proprement.

### Limites de la livraison
- Compilé et testé sur le PC ; **aucune carte USB branchée** pendant la
  livraison, donc flash, dalle réelle et Wi-Fi 2,4 GHz **non exécutés**.

## 3.0.0 — 2026-11-20 — FDT (BATMino x FDJ)

Refonte complète pour la démonstration FDJ. L'ancien Mino est conservé sous
`legacy/mino/` et sous l'étiquette `v2.0-mino`.

### Ajouté
- 19 pages : accueil, paris en direct, loto, historique, gains/pertes, news,
  coupon, jeu responsable, stats, user stories, tests, architecture,
  perf/sécu/VPN, RAM/swap, compte, profils, accessibilité, RGPD, règles.
- Navigation à deux boutons : B2 déplace, B1 décide ; B2 tenu 3 s = page 1 ;
  double appui B2 = action de page ; coches lisibles sans couleur.
- Top 10 des paris calculé par un modèle de Poisson sur les scores en direct
  (TheSportsDB), avec codes de coupon.
- Loto : grilles équilibrées (somme, parité, bas/haut) et 3 grilles jouables.
- Argent : plafonds 7 j / 30 j par profil (4 max), dépense recalculée depuis
  l'historique, refus motivés, pause jeu responsable.
- Compte : code de démarrage 4 chiffres (0000 d'usine), changement persisté,
  mot de passe wifi masqué, jingle de 5 s au déverrouillage.
- Données : blob NVS unique versionné avec CRC32, export CSV sur micro SD,
  effacement RGPD avec confirmation.
- Réseau : scores toutes les 5 min, titres toutes les 3 h, repli honnête
  (`N.D.`, « aucun match en direct ») quand il n'y a pas de réseau.
- Outils : `tools/recette.sh`, `tools/gen_stats.py`, `tools/apercu_fdj.cpp`,
  `tools/ecran_usb.py` (capture de l'écran réel par USB).
- API de démonstration `/etat`, `/ecran`, `/bouton`, `/page` + console USB.

### Corrigé pendant la refonte
- Pile de la tâche réseau en PSRAM (32 Ko) : le TLS débordait les 8 Ko et
  redémarrait la carte à la première requête.
- `N` de la police 3×5 dessiné comme un `M` (« BATMIMO » → « BATMINO »).
- Heure non synchronisée affichée comme 1970 : la page dit maintenant
  « HEURE NON SYNCHRONISÉE ».
- Un pari en cours comptait son gain potentiel dans les gains de la période.

### Tests
- 410 vérifications sur le PC (unitaire, composant, intégration,
  fonctionnel), 0 échec, plus la vérification sur la dalle réelle.
