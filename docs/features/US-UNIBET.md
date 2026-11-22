# User stories — FDT 4.0.0 (BATMino x FDJ)

Six histoires couvrent toute la maquette. Chacune a ses **critères
d'acceptation** (AC), ses **tests** (suite + nombre de vérifications réelles)
et sa **technologie**. La somme des vérifications est celle qu'affiche la
écran 12 du firmware, avec la date de la dernière recette.

Technologies : **C++17** (métier + rendu), **C11** (machine à gestes),
**ESP-IDF 5.5.1** (carte), **cJSON** (flux sportifs), **g++/gcc** (recette
hôte), **python3** (génération des chiffres, PNG, capture USB), **e-paper
200×200 1 bit**, **NVS** (persistance), **TLS/mbedTLS** (flux).

---

## US-01 — Parier en direct sur les 10 meilleures opportunités

*En tant que joueur, je veux voir les 10 paris les plus probables des matchs
en cours, pour choisir vite.*

**AC**
1. Les matchs viennent d'un flux réel, jamais d'un tableau codé en dur.
2. Chaque sélection affiche match, marché, minute et probabilité du modèle.
3. Au plus 3 sélections par match, 10 au total, classées par score.
4. Un pari déjà gagné (p > 97 %) n'est pas proposé ; un match sans données
   affiche `N.D.` et non un score inventé.
5. Chaque sélection porte un code de coupon à 3 caractères, lisible (sans
   I/O/0/1), reproductible à graine égale.

**Tests** — `test_modele` (Poisson, bornes, tri, codes, déterminisme),
`test_parse` (vraies réponses TheSportsDB, accents pliés, flux vide),
`test_pages` (page 2 avec et sans données), `test_fonctionnel` (bout en bout
sur fixtures). ≈ **120 vérifications**.

## US-02 — Jouer une grille de loto qui ne soit pas celle de tout le monde

*En tant que joueur, je veux une grille équilibrée, pour ne pas partager un
gain avec des milliers de personnes.*

**AC**
1. 5 numéros distincts dans 1..49, triés, + 1 numéro chance dans 1..10.
2. Somme entre 100 et 180, 2 ou 3 pairs, 2 ou 3 numéros bas.
3. Tous les numéros sortent statistiquement (pas de biais).
4. 3 grilles jouables, cochables une par une, mise 2,20 € chacune.
5. La page rappelle la vraie probabilité (1 sur 19 068 840).

**Tests** — `test_modele` : 2 000 grilles contrôlées (bornes, unicité, équilibre,
couverture des 49 numéros), générateur sans biais modulo. ≈ **35 vérifications**.

## US-03 — Suivre ce que j'ai joué et ce que ça m'a coûté

*En tant que joueur, je veux voir mon historique et mon bilan par période.*

**AC**
1. Chaque pari validé écrit : libellé, code, mise, gain potentiel, date, jeu.
2. Historique en anneau de 32 entrées, la plus récente en tête.
3. Gains/pertes calculés sur 7, 30 et 365 jours **depuis l'historique**.
4. Un pari en cours n'est **jamais** compté comme gagné.
5. L'état survit à un redémarrage (blob NVS + CRC).

**Tests** — `test_modele` (anneau, périodes, refus d'un blob corrompu),
`test_integration` (cycle pari → historique → solde), `test_fonctionnel`
(relecture après « redémarrage »). ≈ **45 vérifications**.

## US-04 — Ne jamais dépasser le budget que je me suis fixé

*En tant que joueur, je veux des plafonds par profil, vérifiés à chaque mise.*

**AC**
1. 4 profils maximum, chacun avec un plafond **7 jours** et **30 jours**.
2. La dépense réelle est recalculée depuis l'historique, pas stockée à part.
3. Un coupon qui dépasse le plafond semaine, mois, le solde ou 20 € est refusé,
   avec un motif affiché et un son descendant.
4. Le plafond du profil **actif** est celui qui s'applique.
5. La pause « jeu responsable » bloque toute mise.

**Tests** — `test_modele` (mises, plafonds, solde, pause, profils),
`test_integration` (saturation puis refus, changement de profil). ≈ **55
vérifications**.

## US-05 — Protéger la maquette par un code à 4 chiffres

*En tant que propriétaire, je veux un code de démarrage, et pouvoir le changer.*

**AC**
1. Code d'usine `0000`, 4 chiffres, saisie aux deux boutons.
2. Code faux : message, son descendant, on reste sur l'écran de saisie.
3. Code bon : accueil + jingle de 5 s (si la musique est activée).
4. Changement depuis la page 15, persisté en NVS.
5. Le code n'est jamais envoyé sur le réseau.

**Tests** — `test_modele` (validation, changement, bornes), `test_nav`
(saisie chiffre par chiffre, retour à 0000, annulation), `test_integration`
(code changé → relecture → saisie). ≈ **30 vérifications**.

## US-06 — Lire l'info et rester honnête quand le réseau manque

*En tant qu'utilisateur, je veux des titres frais, et un écran clair quand
il n'y a pas de réseau.*

**AC**
1. 4 titres maximum, heure et source, marqués lus au choix.
2. Rafraîchissement automatique toutes les 3 h, ou forcé par B2 double.
3. Entités HTML décodées, accents pliés pour la police 3×5, titres tronqués
   proprement (jamais au milieu d'un octet).
4. Flux vide ou illisible : « FLUX INDISPONIBLE », aucun titre inventé.
5. Hors ligne : les pages concernées affichent `N.D.`, jamais `0`.

**Tests** — `test_parse` (RSS réel, entités, robustesse aux réponses vides),
`test_pages` (rendu sans données), `test_fonctionnel`. ≈ **30 vérifications**.

---

## Pyramide de tests

| Étage | Suite | Ce qui est vérifié | Techno |
|-------|-------|--------------------|--------|
| Unitaire | `test_modele`, `test_nav`, `test_parse` | règles d'argent, hasard, Poisson, gestes, parsing | C++17 / cJSON |
| Composant | `test_pages`, `test_defauts` | chaque page rendue, chaque curseur, déterminisme, écrans plein cadre | C++17 + `art.cpp` |
| Intégration | `test_integration` | nav + argent + persistance ensemble | C++17 |
| Fonctionnel | `test_fonctionnel` | la démo complète sur fixtures serveur, 20 écrans, perf, mémoire | C++17 |
| Matériel | `tools/ecran_usb.py`, API `/etat` | la vraie dalle, la vraie mémoire, le vrai réseau | Python + USB |

`tools/recette.sh` enchaîne les sept suites des quatre étages, écrit `docs/RECETTE.md` et
`main/fdj_tests_stats.h`, reconstruit le firmware et rend les 20 écrans en PNG.
Un étage en échec arrête tout : aucune page n'est livrée sans ses tests.
