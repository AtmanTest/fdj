# Journal des versions

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
