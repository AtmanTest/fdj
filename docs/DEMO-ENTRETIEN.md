# Démonstration d'entretien — 3 minutes

Objectif : montrer un moteur métier réel, pas un diaporama. Aucun réseau n'est
nécessaire : la carte garde son dernier état et écrit `N.D.` quand la donnée
manque ; les captures PNG de secours sont dans `build/apercu/`.

| Temps | Écran | Ce qu'on dit | Gestes |
|------:|-------|--------------|--------|
| 0:00 | 01 ACCUEIL | « Maquette indépendante, aucune mise réelle. Le solde est un solde de démonstration, le profil a son propre plafond. » | — |
| 0:20 | 02 ANALYSE & DECISION | « Un intertitre, comme dans un magazine : on entre dans le module 01. » | B2 ×2 |
| 0:30 | 03 PARIS SPORTIFS | « Scores réels TheSportsDB, modèle Poisson : la probabilité n'est pas une cote FDJ, et l'écran dit depuis combien de minutes la donnée a été lue. » | B1 sur 2 lignes |
| 1:00 | 04 LOTO | « Trois grilles, générateur local déterministe. Une grille dite équilibrée garde la même chance : 1 sur 19 068 840, la page le dit. » | B1 (cocher) |
| 1:20 | 08 MON COUPON | « Mise totale, gain potentiel du modèle. Validation : plafond 7 jours, 30 jours, solde, pause. Le refus est motivé et n'écrit rien. » | B2 jusqu'au bouton, B2 ×2 |
| 1:50 | 05 HISTORIQUE | « Chaque pari est tracé : code, jeu, mise, gain ou potentiel, statut. Et le détail s'ouvre vraiment. » | B1 (détail) |
| 2:10 | 06 GAINS / PERTES | « 7 jours, 30 jours, tout l'anneau : le sélecteur change le détail, la ventilation par jeu est calculée. » | B1 |
| 2:25 | 09 JEU RESPONSABLE | « Cinq affirmations. En dessous de trois OUI, le plafond hebdomadaire est divisé par deux ; la pause 24 h a une échéance réelle et expire seule. » | B1 (2 réponses) puis pause |
| 2:45 | 12 TESTS | « 477 vérifications, 0 échec, chiffres de la dernière recette, datés. Chaque défaut corrigé a son test, qui échouait avant. » | — |
| 2:55 | 13 ARCHITECTURE | « Le même code dessine sur la carte et sur le PC ; les tailles affichées sont mesurées par `sizeof`. » | — |

## Questions probables

* **« Et si Internet tombe ? »** — le dernier état reste affiché, les pages
  écrivent `N.D.`, rien n'est inventé. Les PNG de secours sont dans
  `build/apercu/`.
* **« Comment prouvez-vous que ça marche ? »** — `tests/run_unit.sh`
  (7 suites, 477 vérifications), `tools/recette.sh` régénère chiffres,
  firmware et les 20 captures.
* **« Et la sécurité ? »** — TLS vérifié, pas de mode insecure, aucun secret
  dans `/etat`, les logs ou l'écran ; le code de démarrage n'est jamais
  affiché ; l'API de démonstration reste sur le réseau local.
* **« Pourquoi pas de VPN ? »** — aucun tunnel n'était monté : l'option a été
  retirée plutôt que d'afficher un état mensonger.
