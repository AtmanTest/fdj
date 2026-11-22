PROMPT DE REPRISE — « FDT 3.0.0 » / BATMino × FDJ
À donner tel quel à l'agent qui doit améliorer le sujet.
Rédigé le 22/11/2026 après lecture du dépôt, du dernier push et exécution des tests.

====================================================================
0. TA MISSION
====================================================================
Tu reprends une maquette fonctionnelle : une borne FDJ de démonstration qui
tourne sur une carte ESP32-S3 à écran e-paper 200×200 et deux boutons. Elle a
19 pages, des données réelles (scores en direct, titres RSS), un modèle
d'argent, et 410 vérifications automatiques qui passent.

Ta mission : améliorer le sujet (contenu, justesse, lisibilité, cohérence,
profondeur de démo) SANS casser ce qui est déjà prouvé. Tu n'as pas le droit
d'inventer une valeur affichée : pas de réseau = « N.D. », jamais 0 ni un nom
supposé.

Langue du produit : français, tout en majuscules, police 3×5, écran 1 bit.

====================================================================
1. ÉTAT EXACT DU DÉPÔT (vérifié le 22/11/2026)
====================================================================
Dépôt      https://github.com/AtmanTest/batmino (privé, compte AtmanTest)
Branches   main et fdj-demo, toutes deux sur le même commit
Dernier push 2c1532b — « recette : journal de la version 3.0.0 » (20/11/2026 14:27)
Tag        v3.0.0 = 2c1532b (l'ancien compagnon vocal reste sous v2.0-mino)
Arbre      propre (0 fichier modifié) au moment de la rédaction
VERSION    3.0.0 ; projet CMake « mino » ; binaire build/mino.bin (1 264 672 o)
Tests      ./tests/run_unit.sh → 410 vérifications, 0 échec (relancé le 22/11/2026)

Historique utile des commits :
  c6ad1cf  la refonte complète FDJ (19 pages, nav 2 boutons, tests)
  2c1532b  la recette : docs/RECETTE.md corrigé avec les chiffres du jour
  v2.0-mino  dernier « Mino » (assistant vocal) avant la refonte, déplacé dans legacy/mino/

Fichiers de référence, déjà écrits, à lire avant de toucher au code :
  README-FDJ.md            ce que fait la carte, table des 19 pages, gestes, limites
  docs/features/US-FDJ.md  6 user stories + critères d'acceptation + pyramide de tests
  docs/RECETTE.md          les chiffres réels de la dernière recette
  CHANGELOG.md             ce qui a été ajouté/corrigé en 3.0.0
  main/fdj_model.h         constantes et structures (source de vérité)
  main/fdj_nav.h           la table des gestes, en commentaire d'en-tête

====================================================================
2. CE QUI A ÉTÉ FAIT JUSQU'ICI (pour que tu ne refasses pas)
====================================================================
- Le code a été refait de zéro : main.cpp ne garde que le matériel, tout le
  reste est découpé en modules testables sur PC (fdj_model, fdj_parse,
  fdj_nav, fdj_ui), plus art.cpp (dessin partagé hôte/carte) et geste.h.
- 19 pages dessinées en 1 bit, rendues à l'identique sur le PC en PNG
  (tools/apercu_fdj.cpp + tools/fb2png.py) pour vérification à l'œil.
- Navigation à deux boutons, table validée sur la carte (voir §5).
- Argent : 4 profils max, plafonds 7 j / 30 j par profil, dépense recalculée
  depuis l'historique, refus motivés, pause « jeu responsable ».
- Persistance : un seul blob NVS versionné avec CRC32 ; blob corrompu refusé,
  retour au profil d'usine.
- Réseau : scores TheSportsDB toutes les 5 min, titres franceinfo toutes les
  3 h, TLS vérifié, repli honnête.
- Démo à distance : API HTTP /etat /ecran /bouton /page + console USB.
- Tests : 410 vérifications sur PC (unitaire 168, composant 143, intégration
  41, fonctionnel 58), 0 échec, chiffres injectés dans main/fdj_tests_stats.h
  et affichés page 11.
- Trois bugs corrigés pendant la refonte : pile TLS en PSRAM (sinon reboot à la
  première requête), lettre N de la police dessinée comme un M (« BATMIMO »),
  heure non synchronisée affichée 1970, et un pari en cours qui comptait son
  gain potentiel comme un gain.

====================================================================
3. MATÉRIEL ET CONTRAINTES (non négociables)
====================================================================
Carte      BATMino : ESP32-S3-PICO-1, 8 Mo flash, 8 Mo PSRAM, Wi-Fi/BLE
Écran      e-paper 200×200, 1 bit, pas de gris, rafraîchissement partiel
Entrées    2 boutons seulement (B1, B2), pas de tactile, pas de clavier
Audio      petit haut-parleur (sons montants/descendants, jingle 5 s)
Stockage   micro SD (optionnelle), NVS interne pour l'état
Contraintes de dessin, respectées partout :
  - police 3×5, avance 4·s px : 48 caractères en corps 1, 24 en corps 2 ;
  - titre de page toujours en haut, souligné, avec « NN/19 » à droite ;
  - pied toujours à la même place (gestes), ligne de 1 px à y=178 ;
  - une coche = carré + trait, jamais une couleur ;
  - « E » à la place du signe € (absent de la police) ;
  - accents pliés (la police n'a pas les diacritiques).

Chaîne de compilation vérifiée :
  export IDF cassé tel quel → il faut IDF_TOOLS_PATH et sortir du venv Hermes :
  env -i HOME=/home/taz PATH=/usr/local/bin:/usr/bin:/bin IDF_TOOLS_PATH=/home/taz/esp/tools \
      bash -c '. /home/taz/esp/esp-idf/export.sh >/dev/null 2>&1; idf.py build'

====================================================================
4. MODÈLE DE DONNÉES ET CONSTANTES (main/fdj_model.h)
====================================================================
FDJ_PAGES 19 · PROFILS_MAX 4 · HIST_MAX 32 (anneau) · MATCH_MAX 16
SEL_MAX 10 (top) · GRILLE_NB 5 · GRILLE_MAX 3 · NEWS_MAX 6 · NEWS_LEN 118
8 jeux déclarés : SPORT, LOTO, EUROMILLIONS, KENO, ILLIKO, POKER, COURSES, CRILL
État d'usine : titulaire « T. ZOU », compte « FDJ-0001 », code 0000,
  profil « DEFAUT » (niveau régulier, plafonds 25 € / 7 j et 60 € / 30 j,
  solde de démo 100 €), musique ON, news auto ON, VPN OFF, pause 24 h OFF,
  graine 0x1234567 (tout est déterministe : à graine égale, même sortie).
Modèle sportif : Poisson, 2,6 buts/match, λ recalculés selon score et minute ;
  marchés 1/N/2, +/-2,5 buts, BTTS ; probas hors [0,05 ; 0,97] écartées
  (> 0,97 = déjà joué) ; au plus 3 sélections par match, 10 au total.
Loto : grille équilibrée (somme 100-180, 2-3 pairs, 2-3 bas, max 3 consécutifs),
  générateur sans biais modulo ; 3 grilles jouables, mise 2,20 € ; la page
  rappelle 1 sur 19 068 840.

====================================================================
5. NAVIGATION — LA TABLE DES GESTES (validée sur la carte, à garder)
====================================================================
B2 court        item suivant ; après le dernier item → page suivante
B2 long tenu    défilement rapide (répétition)
B2 tenu 3 s     retour page 1, d'où qu'on soit
B2 double       action principale de la page (valider, régénérer, rafraîchir)
B1 court        cocher / valider l'option sous le curseur (+)
B1 double       annuler / décocher (−)
B1 long         page précédente
Principe : B2 ne déplace que, B1 ne décide que. On ne peut pas miser en
croyant changer de page. Modes : NAV, CODE (saisie 4 chiffres), CONFIRME (RGPD).
Au démarrage, le code est TOUJOURS demandé (0000 d'usine) ; bon code → accueil
+ jingle 5 s si la musique est activée.

====================================================================
6. LES 19 PAGES, PAGE PAR PAGE
====================================================================
Chaque page : titre exact affiché, nombre d'items navigables (entre
parenthèses, source fdj_page_items), ce qui est dessiné, les gestes réels
tels qu'implémentés, la source des données, et les leviers d'amélioration.

--- 01/19  ACCUEIL FDJ  (1 item) -------------------------------------------
Dessiné : bloc plein « FDJ / PARIS EN LIGNE », « BATMINO / MAQUETTE FDJ 2026 »,
date « LUN 22/11 09:47 » ou « HEURE NON SYNCHRONISEE », solde du profil actif en
corps 3, encadré PROFIL (nom + plafonds 7 j/mois), barre d'état du direct
(« n MATCHS EN DIRECT », premier score, « MAJ hh:mm ») ou « HORS LIGNE » +
« PAS DE DONNEES : N.D. (JAMAIS 0 INVENTE) », item « DEMARRER LA VISITE »,
« FDT 3.0.0 - n DEMARRAGES », « CODE DEMARRAGE : 0000 PAR DEFAUT ».
Gestes : B1 court ou B2 double = aller page 2.
Données : état local + compteur de démarrages + dernier relevé live + NTP.
Leviers : l'item unique est redondant avec B2 ; pas d'accès direct aux pages
clés (coupon, solde) ; la ligne heure et la ligne direct se marchent dessus
quand il n'y a pas de réseau (deux messages « hors ligne ») ; le solde est
affiché sans rappel de la dépense de la semaine (le plafond, lui, est écrit).

--- 02/19  PARIS SPORTIFS  (1 à 10 items) ----------------------------------
Dessiné : jusqu'à 10 lignes de 15 px. Par ligne : pastille du code coupon
(3 lettres sans I/O/0/1), match élidé (120 px), 2e ligne = ligue + info du
match (minute/score) + marché, probabilité du modèle en % à droite, coche.
Pied : « PROBA = MODELE POISSON, PAS UNE COTE FDJ ».
Vide : « AUCUN MATCH EN DIRECT » / « LE MODELE NE DEVINE PAS UN SCORE » /
« B2X2 : RAFRAICHIR » ou « HORS LIGNE : N.D. ».
Gestes : B1 court = cocher/décocher la sélection (elle entre au coupon) ;
B1 double = retirer ; B2 double = « TOP 10 REGENERE » (avance la graine et
recalcule, SANS refaire de requête réseau) ; B2 court = item/page suivants.
Données : thesportsdb.com livescore.php + eventsday.php (clé publique 123),
16 matchs max, rafraîchi toutes les 5 min.
Leviers : B2 double annonce un rafraîchissement mais ne touche pas au réseau
(trompeur, à corriger) ; aucune vue détail d'un match (compo, forme) ; pas de
filtre par compétition ni par marché ; la proba est un entier sans marge
d'incertitude ; rien n'indique l'âge de la donnée par ligne.

--- 03/19  LOTO  (4 items) -------------------------------------------------
Dessiné : 3 grilles, chacune 5 boules encadrées (corps 2) + « C7 » pour le
numéro chance, coche à droite ; encadré « ASTUCE DU JOUR » (6 astuces qui
tournent au jour) + rappel « 5/49 + CHANCE : 1 SUR 19 068 840 » ; 4e item
« NOUVELLE GRILLE (B2X2) ».
Gestes : B1 court = cocher la grille (ou régénérer sur le 4e item) ;
B1 double = retirer la grille ; B2 double = nouvelles grilles.
Données : générateur local déterministe, aucun réseau.
Leviers : les astuces affirment des choses non sourcées (« 2 pairs / 3 impairs :
1 grille sur 3 », « somme 100-180 : 7 sur 10 ») alors que la règle du projet
interdit les chiffres non vérifiés — soit les calculer et les tester, soit les
retirer ; pas de tirage réel (l'API FDJ n'est pas branchée) ; pas de « jouer
mes numéros » (saisie impossible sans clavier).

--- 04/19  HISTORIQUE  (1 à 6 items) ---------------------------------------
Dessiné : 6 lignes de 25 px. Par pari : libellé élidé, statut à droite
(GAGNE / PERDU / EN COURS), 2e ligne « jj/mm M mise G gain » (ou « POT » pour
un pari en cours, « PERDU » sinon), 3e ligne « CODE xxx  nom du jeu ».
Pied : « B1 : OUVRIR / FERMER LE DETAIL ».
Gestes : B1 court bascule detail_ouvert ; B1 double → « RIEN A ANNULER » ;
B2 double → « PAGE D'INFORMATION ».
Données : anneau NVS de 32 entrées, la plus récente en tête.
Leviers : LE DÉTAIL N'EXISTE PAS — la variable est calculée puis ignorée
(seule la hauteur de la barre change) : le pied promet une fonction absente.
Pas de pagination au-delà de 6 paris, pas de filtre par jeu, pas de tri, pas
d'annulation ni de « marquer comme réglé » alors que les statuts ne changent
jamais (tout reste EN COURS sauf ce que le modèle a réglé).

--- 05/19  GAINS / PERTES  (3 items) ---------------------------------------
Dessiné : 3 blocs de 33 px (7 JOURS, 30 JOURS, 1 AN) : net signé en corps 2 à
droite (+/−/=), 2e ligne « n PARIS  MISE x  GAIN y » ; puis barres du net des
7 derniers jours (hauteur 30 px, négatif plafonné à −15) et « NET PAR JOUR ».
Gestes : B1 court → « PERIODE SELECTIONNEE » (aucun effet) ;
B2 double → « PAGE D'INFORMATION ».
Données : recalcul depuis l'historique ; seuls les paris GAGNÉS comptent ;
« N.D. » si aucun pari sur la période.
Leviers : la sélection ne fait rien (soit la brancher — détail par jour, par
jeu —, soit retirer l'item) ; barres sans axe, sans étiquette de jour ni
échelle ; « 1 AN » est structurellement faux au-delà de 32 paris puisque
l'anneau ne garde que 32 entrées (à dire à l'écran ou à agrandir l'anneau) ;
pas de ventilation par jeu alors que 8 jeux sont déclarés.

--- 06/19  NEWS LIVE  (1 à 4 items) ----------------------------------------
Dessiné : 4 blocs de 38 px. Par titre : heure + source, puis le titre sur deux
lignes, coupé au dernier espace (jamais au milieu d'un mot), coche « lu ».
Pied : « MAJ TOUTES LES 3 H - B2X2 POUR FORCER ».
Vide : « FLUX INDISPONIBLE » / « PAS DE TITRE INVENTE : N.D. » /
« NOUVELLE TENTATIVE DANS 3 H ».
Gestes : B1 court = marquer lu / non lu ; B2 double = forcer la mise à jour.
Données : flux RSS franceinfo, entités HTML décodées, accents pliés,
118 caractères par titre.
Leviers : une seule source ; 4 titres ; pas de vue « lire le titre en entier » ;
pas d'horodatage relatif (« il y a 12 min ») ; impossible de distinguer
« jamais chargé » de « chargé et vide ».

--- 07/19  MON COUPON  (0 à 10 items + 2) ----------------------------------
Dessiné : une ligne par sélection cochée (code, match, marché, « 2,00 »), puis
une ligne par grille loto (« LOTO1 03 11 19 28 41 C7 », « 2,20 »), puis
« VALIDER LE COUPON (B2X2) » et « VIDER LE COUPON », puis « MISE TOTALE »,
« GAIN POSSIBLE (MODELE) » ou « GAIN POSSIBLE : N.D. (AUCUN PARI) », et
« LOTO : GAIN SELON LE RANG DU TIRAGE ».
Gestes : B1 court sur une ligne = retirer ; sur VALIDER = valider ; sur VIDER =
vider ; B2 double = valider. Validation : contrôle plafond 7 j, plafond 30 j,
solde et plafond absolu de 20 € — refus motivé + son descendant ; succès =
écriture de l'historique, sauvegarde NVS, saut à la page 04.
Données : top en cours + grilles du jour + état (solde, plafonds, pause).
Leviers : DÉFAUT REPRODUIT — avec 10 sélections (8 paris + 2 grilles), les
quatre dernières lignes se chevauchent et deviennent illisibles (vérifié en
rendant la page avec un coupon plein). Il faut compacter (2 lignes max par
paragraphe), paginer, ou n'afficher que le récapitulatif au-delà de 6 items.
Le gain possible est calculé en 200/proba sans marge ; la mise est figée
(2,00 € par pari, 2,20 € par grille) : aucune possibilité de la modifier.

--- 08/19  JEU RESPONSABLE  (6 items) --------------------------------------
Dessiné : 5 affirmations (« JOUER EST UN PLAISIR, PAS UN BESOIN », « JE NE JOUE
JAMAIS POUR ME REFAIRE », « MON BUDGET EST FIXE ET TENU », « JE SAIS DIRE STOP
APRES UNE PERTE », « LE JEU NE REMPLACE PAS MES PROCHES ») avec OUI/NON et
coche ; jauge « PROTECTION » n/5 ; item « PAUSE 24 H (AUTO-EXCLUSION) » avec
coche ; pied « L'ARGENT ENGAGE RESTE ENGAGE : RIEN N'EST CACHE ».
Gestes : B1 court = répondre OUI ; sur la pause = basculer ; B1 double =
effacer la réponse ; B2 double = « PAGE D'INFORMATION ».
Données : état local (bits 8..12 réservés aux réponses).
Leviers : le score n'a aucune conséquence (0/5 ne bloque rien, ni message) ;
la pause 24 h est un simple booléen sans échéance affichée (elle ne se lève pas
seule après 24 h) ; pas de lien avec les plafonds de la page 16 ; pas
d'historique des pauses ; le texte ne dit jamais combien de temps reste.

--- 09/19  STATS / PROBAS  (4 items) ---------------------------------------
Dessiné : 4 blocs de 35 px — « LOTO 5/49 + CHANCE 1 / 19 068 840 » (5 paris =
5 chances de plus), « EUROMILLIONS 1 / 139 838 160 » (2 étoiles, 7,3× pire),
« COTE 2,00 → 50 % » (espérance 1,90, la maison garde 5 %), « MES PARIS REGLES
gagnés / total » ou « N.D. » (win rate réel sur l'historique).
Gestes : aucun effet (B1 et B2 double → information).
Données : constantes vérifiables + historique réel pour le win rate.
Leviers : page statique à 75 % ; pourrait montrer le TRJ par jeu, l'espérance
d'une mise, la comparaison proba du modèle vs cote implicite, et l'évolution du
win rate ; les 7,3× et 5 % méritent d'être testés (aujourd'hui ils ne le sont
pas).

--- 10/19  USER STORIES  (6 items) -----------------------------------------
Dessiné : les 6 US (id, titre, détail, « n AC » à droite) — US-01 parier en
direct (5 AC), US-02 loto équilibré (4), US-03 historique et gains (4),
US-04 profils et plafonds (6), US-05 code de démarrage (4), US-06 news et hors
ligne (4) ; pied « 410 VERIFICATIONS AU TOTAL - PAGE 11 ».
Gestes : aucun (information).
Données : constantes du firmware (ce n'est pas lu dans les .md).
Leviers : les critères d'acceptation ne sont pas consultables à l'écran (alors
qu'ils existent dans docs/features/US-FDJ.md) ; duplique la doc, donc risque de
divergence ; le total de vérifications est codé en dur à deux endroits.

--- 11/19  TESTS  (4 items) ------------------------------------------------
Dessiné : 4 étages avec leurs compteurs RÉELS issus de fdj_tests_stats.h —
1. UNITAIRES (168), 2. COMPOSANTS (143), 3. INTEGRATION (41),
4. FONCTIONNEL (58) ; puis « 410 VERIFS, 0 ECHEC - RECETTE DU 2026-11-20 »,
« UNIT -> COMPOSANT -> INTEG -> FONCTION », « CORRECTION APRES CHAQUE PAGE,
PUIS RETEST », « AUCUNE PAGE N'EST LIVREE SANS SES TESTS ».
Gestes : aucun (information).
Données : main/fdj_tests_stats.h, régénéré par tools/gen_stats.py à chaque
tools/recette.sh — la page affiche donc la dernière exécution, pas une promesse.
Leviers : n'affiche ni la durée, ni le taux de réussite, ni ce que couvre
chaque suite ; après une nouvelle recette il faut reflasher pour voir les
nouveaux chiffres (le préciser).

--- 12/19  ARCHITECTURE  (4 items) ----------------------------------------
Dessiné : 4 boîtes (MODELE / ARGENT-POISSON, PARSEURS / JSON-RSS, NAV+UI /
19 PAGES 1 BIT, MATERIEL / EPD-AUDIO-NVS) reliées par des flèches ;
« MEME CODE SUR LA CARTE ET SUR LE PC : L'APERCU PNG EST LE VRAI DESSIN. »,
« 4 000 OCTETS DE RAM » en corps 2, « PAS DE JSON EN MEMOIRE LONGUE : 16 MATCHS
MAX », « UNE IMAGE = 5 000 OCTETS, ENVOYEE EN PARTIEL », « ETAT PERSISTANT =
1 BLOB NVS + CRC32 », « AUCUNE ALLOCATION DANS UNE PAGE ».
Gestes : aucun (information).
Leviers : les deux chiffres (4 000 octets de RAM, 5 000 octets par image) ne
sont mesurés par aucun test — les remplacer par une mesure réelle ou les
retirer ; les boîtes ne sont pas cliquables (pas de zoom sur un module).

--- 13/19  PERF / SECU / VPN  (4 items) -----------------------------------
Dessiné : 3 compteurs mesurés à l'instant — « DESSIN D'UNE PAGE n MS »,
« RAFRAICHISSEMENT EPD n MS », « HEAP INTERNE LIBRE n KO » ; bloc SECURITE
(« TLS VERIFIE (BUNDLE IDF), PAS DE MODE INSECURE », « CODE DE DEMARRAGE LOCAL,
JAMAIS ENVOYE », « DONNEES EN NVS, EFFACABLES PAGE 18 ») ; item
« VPN OPTIONNEL (RELAIS LOCAL) » avec ON/OFF et coche.
Gestes : AUCUN — la page tombe dans le groupe « information » ; le VPN affiché
ne peut donc pas être basculé par l'utilisateur, alors que l'écran montre une
coche et un état.
Données : mesures live (esp_timer, heap_caps) pour les 3 premiers.
Leviers : brancher le VPN (et dire honnêtement qu'aucun tunnel n'est monté, cf.
README) ou le retirer ; ajouter la taille du firmware, le temps de boot, le
nombre de requêtes réseau, la dernière erreur TLS ; afficher la mémoire PSRAM
libre ici aussi.

--- 14/19  RAM & SWAP  (3 items) ------------------------------------------
Dessiné : 3 jauges (RAM INTERNE LIBRE / 512 Ko, PLUS GROS BLOC / 128 Ko,
PSRAM LIBRE / 8 Mo) avec valeur en Ko ; bloc « SWAP SUR MICRO SD (OPTIONNEL) »
— carte présente : « AUCUN PAGING EN COURS : TOUT TIENT EN RAM » ; sinon
« PAS DE CARTE SD : SWAP INDISPONIBLE » ; « MESURES PRISES A L'INSTANT, PAS DE
SIMULATION », « L'IMAGE E-PAPER N'OCCUPE QUE 5 000 OCTETS », « LES TAMPONS
RESEAU SONT EN PSRAM », et « B2X2 : REFAIRE LES MESURES ».
Gestes : AUCUN — l'invite « B2X2 : REFAIRE LES MESURES » n'est pas câblée.
Données : mesures live.
Leviers : câbler le geste annoncé (une ligne de code) ; les jauges utilisent
des totaux fixes (512 Ko / 128 Ko) donc les pourcentages sont approximatifs —
utiliser heap_caps_get_total_size ; le mot « SWAP » est trompeur : il n'y a
aucun swap implémenté, seulement une carte détectée.

--- 15/19  COMPTE & REGLAGES  (6 items) -----------------------------------
Dessiné : TITULAIRE (« T. ZOU »), COMPTE FDJ (« FDJ-0001 »), RESEAU (SSID +
dBm, ou HORS LIGNE), SECRET (« ******** (MASQUE) » ou « REVELE 3 SECONDES »),
CODE 4 CHIFFRES (« 0000 - CODE D'USINE » ou « CODE PERSO », avec un chevron),
AMBIANCE (« JINGLE 5 S AU DEMARRAGE (ON) » + coche). Pied : « B1 SUR SECRET :
AFFICHER 3 SECONDES », « B1 SUR CODE : CHANGER (4 CHIFFRES, DEFAUT 0000) ».
Gestes : B1 sur SECRET = bascule l'état « révélé » ; sur CODE = ouvre la
saisie du nouveau code ; sur AMBIANCE = bascule la musique ; les trois autres
sont en lecture seule ; B2 double sur CODE = même chose, ailleurs « RIEN A
VALIDER ICI ».
Données : NVS.
Leviers : le « mot de passe » n'est jamais stocké, donc la révélation se
contente d'afficher les mots « REVELE 3 SECONDES » (fonction cosmétique) et les
3 secondes ne sont pas chronométrées ; le code de démarrage est affiché en clair
(« 0000 ») sur la page ; le nom du réseau ne dit pas s'il est le réseau prévu ;
rien pour changer le nom du titulaire.

--- 16/19  PROFILS  (nb_profils + 1 items) --------------------------------
Dessiné : par profil : « > » sur l'actif, nom en corps 2, niveau (débutant /
régulier / expert), « plafonds 7/30 E PAR 7/30 J », solde (ou « ---- » si non
actif) ; dernier item « NOUVEAU PROFIL (B2X2) / DEBUTANT-REGULIER-EXPERT » ou,
à 4 profils, « 4 PROFILS MAXIMUM : SUPPRIMER D'ABORD » et « B1 : SUPPRIMER LE
PROFIL (SAUF ACTIF) » ; en bas « ACTIF x : DEPENSE 7J n / plafond E ».
Gestes : B1 / B2 double = select_ou_cree_profil() — sur un profil existant :
il devient actif ; sur le dernier item : création (DEBUTANT 10/30 €, puis
REGULIER 25/60 €, puis EXPERT 50/200 €, solde initial 20 €) ; à 4 profils : dit
« 4 PROFILS MAXIMUM » et refuse.
Données : NVS + dépense recalculée depuis l'historique.
Leviers : INCOHÉRENCE — l'écran annonce « B1 : SUPPRIMER LE PROFIL » mais le
code ne supprime jamais ; le changement de profil actif se fait sans
confirmation (un appui B1 mal placé change le plafond applicable) ; le niveau
n'est pas modifiable ; les plafonds ne sont pas modifiables ; « p->niveau » et
« emoji » sont à moitié utilisés.

--- 17/19  ACCESSIBILITE  (4 items) ---------------------------------------
Dessiné : GROS TEXTE (état réel), CONTRASTE FORT (coché en dur), SON DES
ACTIONS (coché en dur, « POSITIF MONTE, NEGATIF DESCEND »), LANGUE
(« FRANCAIS (SEULE LANGUE EMBARQUEE) », coché en dur) ; « AUCUNE INFORMATION
PAR LA COULEUR SEULE », « COCHE = CARRE + TIRET, LISIBLE DE LOIN », puis une
ligne d'exemple rendue en corps 1 ou 2 selon l'option.
Gestes : B1 sur GROS TEXTE = bascule ; ailleurs « REGLAGE FIXE ».
Données : NVS pour gros_texte, constantes pour le reste.
Leviers : LE GROS TEXTE EST COSMÉTIQUE — il ne change que la ligne d'exemple,
pas les autres pages (vérifié dans le code) : soit l'appliquer réellement
(facteur d'échelle dans fdj_ui), soit le présenter comme aperçu ; les trois
autres lignes affichent « coché » sans état réel ; la langue n'est pas un choix.

--- 18/19  DONNEES & RGPD  (3 items) --------------------------------------
Dessiné : « EFFACER MES DONNEES / PROFILS, HISTORIQUE, CODE : TOUT »,
« EXPORTER MON HISTORIQUE / FICHIER CSV SUR LA CARTE SD », « A PROPOS /
FDT 3.0.0 - BATMino x FDJ » ; bloc « OU VIVENT LES DONNEES ? » (NVS interne,
aucun envoi vers un serveur, réseau limité aux scores et titres, suppression =
effacement NVS + redémarrage) ; signature « @PARISTZOU - JUSTE POUR LES FILLES ».
Gestes : B1 sur EFFACER = passe en mode confirmation plein écran (OUI/NON, B1
inverse, B2 double confirme) puis efface tout et repart à l'écran de code ;
B1 sur EXPORTER = écrit /sdcard/fdj_historique.csv (en-tête
date;jeu;libelle;code;mise_eur;gain_eur;statut) ; B1 sur A PROPOS = page 19.
Données : NVS + SD.
Leviers : l'export ne dit pas combien de lignes ont été écrites ni où
exactement (juste « EXPORTE SUR /sdcard ») ; pas d'aperçu avant effacement ;
pas de mention de la durée de conservation ; le CSV n'est pas relisible depuis
la carte.

--- 19/19  REGLES FDJ  (3 items déclarés, aucun curseur dessiné) ----------
Dessiné : les 8 jeux en deux colonnes avec une puce (PARIS SPORTIFS, LOTO,
EUROMILLIONS, KENO, ILLIKO, POKER, COURSES, CRILL) ; puis « CONDITIONS : JEU
INTERDIT AUX -18 ANS », « JOUEZ AVEC MODERATION (PLAFONDS PAR PROFIL) », « LES
GAINS SONT SOUMIS A LA FISCALITE », « MAQUETTE DE DEMONSTRATION : PAS DE MISE
REELLE », « REGLES COMPLETES SUR FDJ.FR » ; « 3 FAMILLES : PARIS = PRONOSTIC,
LOTERIE = TIRAGE, CASINO = HASARD », « TOUTES ONT UN TRJ INFERIEUR A 100 % »,
« PROTOTYPE TECHNIQUE - DEMONSTRATION ».
Gestes : B1 et B2 double → « CONDITIONS SUR FDJ.FR ».
Données : constantes.
Leviers : 3 items sont déclarés mais rien ne dessine le curseur (incohérence) ;
les 8 jeux ne sont que des noms (ni principe, ni prix de la prise, ni
probabilité) alors que c'est la page « règles » ; les TRJ ne sont pas chiffrés
et ne sont pas sourcés.

====================================================================
7. SOURCES RÉSEAU ET RÈGLE D'HONNÊTETÉ
====================================================================
Scores  GET https://www.thesportsdb.com/api/v1/json/123/livescore.php?s=Soccer
        et eventsday.php?d=AAAA-MM-JJ&s=Soccer (clé de test publique « 123 »),
        16 matchs max, rafraîchi toutes les 5 min, tâche réseau en PSRAM.
Titres  RSS https://www.franceinfo.fr/titres.rss (l'ancien domaine redirige en
        301 et http_get ne suit pas les redirections), 4 titres, toutes les 3 h.
TLS     vérifié avec le bundle IDF, aucun mode « insecure ».
Règle   Une valeur non disponible s'écrit « N.D. », un match absent donne
        « AUCUN MATCH EN DIRECT », une heure non synchronisée s'écrit
        « HEURE NON SYNCHRONISEE ». Un pari en cours affiche son gain
        POTENTIEL, jamais un gain. Les gains d'une période ne comptent que les
        paris gagnés. Aucune cote FDJ n'est inventée : la page 02 écrit que la
        probabilité vient d'un modèle Poisson.

====================================================================
8. VÉRIFIER SANS CÂBLE (à utiliser avant de toucher l'écran)
====================================================================
Sur PC       ./tests/run_unit.sh            (6 suites, 410 vérifications)
             tools/recette.sh               tests + chiffres + build + 19 PNG
             build/apercu/p01.png ... p19.png, puis tools/fb2png.py pour tout .fb
Sur la carte API HTTP (une fois sur le wifi, secrets.h non versionné) :
             /etat              page, curseur, mode, profil, solde, hist, top, heap
             /ecran             dernière image en hexa (→ tools/fb2png.py)
             /bouton?n=1&m=court|double|long|repeat3   même geste que le doigt
             /page?n=7          forcer une page
Par USB      console : « p 7 », « b 1 double », « e » (état JSON), « s » (écran),
             puis tools/ecran_usb.py build/ecran.png 7 (aucun wifi nécessaire)
Flasher      env -i … IDF_TOOLS_PATH=/home/taz/esp/tools bash -c \
             '. /home/taz/esp/esp-idf/export.sh >/dev/null 2>&1; idf.py -p /dev/ttyACM0 flash'
             (la carte est un ESP32-S3 en USB natif : /dev/ttyACM0, pas ttyUSB)

ATTENTION : au 22/11/2026 la carte branchée ne porte PAS ce firmware FDJ, elle
porte « oldmac-esp32 1.0.0 » (e-paper 200×200, API /etat /ecran /vue /bouton).
« Réinstaller » = reflasher ce dépôt-ci ; vérifie ensuite le log série :
« FDT 3.0.0 pret : 19 pages, code d'usine 0000 ».

====================================================================
9. TESTS : CE QUI EXISTE DÉJÀ (ne pas casser, compléter)
====================================================================
tests/run_unit.sh lance 6 suites :
  modele       106 vérifs  argent, plafonds, profils, hasard, Poisson, loto,
                           anneau d'historique, refus d'un blob corrompu
  nav           33 vérifs  la table des gestes, appuis rapprochés, saisie du code
  parse         29 vérifs  TheSportsDB (fixtures réelles), RSS, entités, flux vide
  pages        143 vérifs  chaque page rendue, curseurs, déterminisme, plein cadre
  integration   41 vérifs  nav + argent + persistance ensemble
  fonctionnel   58 vérifs  démo complète sur fixtures, 19 pages, perf, mémoire
  TOTAL        410 vérifs, 0 échec
tools/recette.sh enchaîne les 6 suites, écrit main/fdj_tests_stats.h et
docs/RECETTE.md avec les chiffres obtenus, recompile, rend les 19 PNG.
Un étage en échec arrête tout : aucune page n'est livrée sans ses tests.

====================================================================
10. DÉFAUTS CONSTATÉS, PAR ORDRE DE GRAVITÉ (à traiter, preuves fournies)
====================================================================
1) Page 07 — coupon plein : les lignes « MISE TOTALE », « GAIN POSSIBLE »,
   « VALIDER LE COUPON » et « VIDER LE COUPON » se chevauchent dès 10 sélections.
   Reproduit en rendant la page avec 8 paris + 2 grilles : illisible.
2) Page 04 — le « détail » promis par le pied n'affiche rien (variable calculée
   puis ignorée).
3) Page 14 — « B2X2 : REFAIRE LES MESURES » n'est pas câblé.
4) Page 13 — le VPN est affiché avec un état et une coche mais aucun geste ne
   le bascule.
5) Page 16 — l'écran annonce « B1 : SUPPRIMER LE PROFIL », le code refuse et
   ne supprime jamais.
6) Page 17 — « GROS TEXTE » ne change qu'une ligne d'exemple, pas l'interface.
7) Page 02 — « B2X2 : RAFRAICHIR » recalcule sans refaire de requête réseau.
8) Page 05 — « 1 AN » ne peut pas couvrir plus de 32 paris (taille de l'anneau).
9) Page 15 — « SECRET » ne révèle jamais de secret (rien n'est stocké) et la
   révélation n'est pas chronométrée ; le code est affiché en clair.
10) Page 03 — les « astuces » du loto sont des affirmations non sourcées.
11) Page 12 — « 4 000 OCTETS DE RAM » et « 5 000 OCTETS par image » ne sont
   mesurés par aucun test.
12) Page 19 — 3 items déclarés, aucun curseur dessiné.
13) Le pied indique « B2 3S:DEBUT » alors que le retour à l'accueil est en fait
   un appui de 3 s qui joue un son descendant, sans autre retour visuel.

====================================================================
11. CE QUE TU NE DOIS PAS FAIRE
====================================================================
- Afficher un chiffre inventé, un score supposé, un nom de suppléant, une cote
  FDJ inventée. Une valeur absente = « N.D. » ou une phrase explicite.
- Changer la table des gestes sans raison forte et sans mettre à jour le pied,
  fdj_nav.h, README-FDJ.md et les tests.
- Rendre une page non testable sur PC : le rendu doit rester du C++ pur sans
  ESP-IDF (fdj_ui ne doit pas inclure d'en-tête ESP-IDF).
- Utiliser la couleur, le gris, une police plus grande que 3×5 sans la générer.
- Livrer sans version : VERSION + CHANGELOG + tag, et un commit par étape.
- Laisser un test en échec « pour aller plus vite ».
- Toucher à legacy/mino/ (l'ancien Mino) : il reste tel quel pour la démo
  « avant / après ».
- Signer autre chose que « @paristzou — juste pour les filles » sur les écrans
  qui portent déjà la signature (pages 18 et 19).

====================================================================
12. MÉTHODE (dans cet ordre)
====================================================================
1. Lis README-FDJ.md, docs/features/US-FDJ.md, main/fdj_model.h, fdj_nav.h,
   fdj_ui.h, puis main/fdj_ui.cpp et main/fdj_app.cpp en entier.
2. Lance ./tests/run_unit.sh et tools/recette.sh --sans-build : tu dois voir
   410 vérifications et 19 PNG. Regarde les PNG (build/apercu) avant de coder.
3. Corrige d'abord les défauts 1 à 5 (ils sont visibles à la démo et cassent la
   confiance), puis 6 à 9, puis le reste.
4. Pour chaque correction : ajoute ou ajuste un test dans tests/unit/ (le test
   doit échouer avant, passer après), puis relance la suite complète.
5. Mets à jour README-FDJ.md, docs/features/US-FDJ.md, CHANGELOG.md et VERSION
   (4.0.0 pour une refonte, 3.0.x pour des correctifs), commite, tague.
6. Flashe, vérifie sur la dalle réelle (tools/ecran_usb.py) ET sur l'API /etat,
   puis raconte ce qui a été mesuré, pas ce qui a été espéré.

Définition de « fini » : les 410+ vérifications passent, les 19 pages sont
rendues en PNG et relues, la carte affiche la nouvelle version, le CHANGELOG
dit ce qui a changé, le tag est poussé, et aucune valeur affichée n'est
inventée.

====================================================================
13. PISTES D'AMÉLIORATION PAR ORDRE DE VALEUR (propositions, pas obligations)
====================================================================
a) Rendre la démo plus vivante : compte à rebours des paris en cours, minute qui
   avance sur la page 02, bandeau « dernier titre » sur l'accueil.
b) Profondeur métier : détail d'un match (3 marchés, forme, λ, espérance),
   simulateur « et si je mise 5 € ? », TRJ par jeu calculé et testé.
c) Argent crédible : mise ajustable aux boutons, plafonds modifiables, règle de
   règlement des paris (gain/perte) au lieu de statuts figés.
d) Accessibilité réelle : appliquer « gros texte » à toutes les pages, contraste
   vérifié, sons différenciés par action.
e) Robustesse démontrée : page « état du système » (dernière erreur réseau, âge
   des données, uptime, reboots), et un test qui la couvre.
f) Honnêteté affichée : marquer l'âge de chaque donnée réseau, et remplacer les
   affirmations non sourcées (astuces loto, 4 000 octets) par des mesures.

Fin du prompt.
