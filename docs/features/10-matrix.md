# 10 — MATRIX : l'analyse du système en pluie de code

## Ce que ça doit faire

Une page qui se lance comme les autres : au bouton 1 dans le cycle, depuis le
launcher (tuile MATRIX), ou par `/bouton?p=6`
et qui **analyse vraiment la carte** : pluie de code en ouverture, puis un rapport
fixe avec les mesures réelles.

C'est la réponse à « un truc comme Matrix, moderne, qui analyse le système » —
mais dessinée autour de ce que la dalle sait faire, pas contre elle.

## Les trois contraintes de la dalle

| Contrainte | Conséquence sur le dessin |
|---|---|
| 1 bit par pixel, 200×200 | pas de vert : la pluie est en **vidéo inversée** (glyphes blancs sur fond noir), ce qui est le rendu d'un terminal et reste lisible |
| un rafraîchissement partiel coûte **865 ms** et fait clignoter tout l'écran | pas de pluie continue : une **séquence de 4 images**, une toutes les 2,4 s |
| un enchaînement de partiels laisse des fantômes | chaque image de la séquence passe par un **refresh complet** (`plein = true` quand `mtx_anim`) |

## Le déroulé

    étape 0  CONNEXION          la pluie entre par le haut
    étape 1  LECTURE CPU        elle descend, ligne de balayage
    étape 2  MEMOIRE + CARTE    plus bas
    étape 3  RESEAU + IA        en bas
    étape 4  RAPPORT            écran fixe, mesures réelles

Le rapport affiche CPU (avec la fréquence), RAM interne libre/totale, PSRAM,
nombre de tours lus sur la carte, RSSI wifi, batterie (pourcentage + mV) et le
modèle du cerveau. Le verdict est `SYSTEME STABLE` avec l'uptime, ou `ATTENTION`
avec la première alerte trouvée, dans cet ordre : batterie < 15 %, réseau perdu,
carte absente, RAM < 40 Ko, signal < −78 dBm.

## Comment c'est fait

- `PAGE_MATRIX` 6, `PAGE_LIVRE` 7, `PAGE_REG` 8, `PAGE_COUNT` 8. MATRIX passe
  **avant** LIVRE : sur LIVRE les deux boutons tournent les pages du livre, une
  page placee apres serait injoignable au bouton 1.
- `mtx_lance()` remet l'étape à 0 ; `mtx_tick()` (appelée par la tâche de fond de
  700 ms) avance d'une image toutes les 2,4 s puis s'arrête sur le rapport.
- `mtx_colonne()` : un fil vertical de 1 px, des glyphes tous les 6 px, une tête
  en bloc 3×5. Tout est borné à la bande `MTX_Y0..MTX_Y1` (22..166) — sans ça, la
  pluie passait sur le titre et sur la barre.
- L'en-tête est dessiné **après** la pluie, sinon une colonne le traverse.
- `emotion`/`/state` : le champ `matrix` donne l'étape courante (0..3 pluie, 4 rapport),
  ce qui permet de vérifier la séquence à distance sans regarder l'écran.

## Boutons

    1 court       relancer la séquence (rejoue l'analyse)
    1 long        accueil (comportement commun)

## Pièges rencontrés

- **`a_px(x, y, 0)` est blanc**, `a_px(x, y, 1)` est noir (1 = pixel piloté). Un
  fond peint avec `a_px(..., 0)` laisse la page blanche : la première version
  affichait une pluie invisible sur fond blanc.
- `a_hline()` est toujours noire : sur fond noir, les têtes étaient invisibles
  jusqu'à ce qu'elles soient dessinées pixel par pixel en blanc.
- Un glyphe en police 3×5 à l'échelle 1 mesure 3×5 px : à l'écran, on lit des
  caractères de près, pas de loin. C'est la limite de la dalle, pas du dessin.

## Tests

| Test | Attendu |
|---|---|
| `/bouton?p=7` | page MATRIX, `matrix` passe 0→4 en ~10 s |
| `/state` -> `matrix` | 0, 1, 2, 3, puis 4 et reste à 4 |
| `/ecran?raw=1` à l'étape 0 | majoritairement noir, titre MATRIX lisible en haut, barre en bas |
| étape 4 | 7 lignes de mesure + verdict, aucun chevauchement |
| bouton 1 court sur la page | la séquence repart de 0 |
