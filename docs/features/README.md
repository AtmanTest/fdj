# Tests — comment on vérifie que le build est bon

Chaque fonctionnalité a son fichier dans `docs/features/` avec ses tests. Le
script `tools/test_global.py` les rejoue **tous** sur la carte branchée en USB,
dans l'ordre, et rend un verdict. Aucun réseau n'est nécessaire : tout passe par
la console USB (commandes `diag`, `btn`, `ui`, `fb`).

## Lancer

    source tools/env.sh
    $IDF_PYTHON_ENV_PATH/bin/python tools/test_global.py /dev/ttyACM1

Sortie type :

    1. demarrage et reseau (docs/features/03-demarrage.md)
      [OK]    la carte repond sur l'USB
      [OK]    wifi associe — MON_RESEAU_SECOURS
      [OK]    meteo lue — 11.5C code 3
    2. une appli par page (docs/features/02-pages.md)
      [OK]    bouton 1 fait le tour des 5 pages — METEO -> RAPPELS -> SYSTEM -> HISTORIQUE -> ACCUEIL
    ...
    16 tests bons, 0 en echec
    verdict : le build est bon

Code de sortie **0** = tout bon, **1** = au moins un échec (utilisable dans un
script de CI ou avant un `git push`).

## Ce qui est testé

| Section | Fichier | Points |
|---|---|---|
| 1 | 03-demarrage.md | carte joignable, pas de panic, wifi, météo, tâche écran vivante |
| 2 | 01-boutons.md + 02-pages.md | tour des 5 pages, retour arrière, appui long, état réel lu par `diag` |
| 3 | 06-ecran-eink.md | 5 images différentes, une par page, écrites en PNG |
| 4 | 04-rappel.md | l'assistant crée un rappel, compté par le firmware, page RAPPELS |
| 5 | 05-stabilite.md + 07-applis-memoire.md | aucun reboot (`boot_nb`), `ui_ticks` qui monte, RAM interne, absence de panic |

## Notes de terrain

- Ouvrir le port USB-JTAG peut **redémarrer** la carte : le script attend donc
  qu'elle réponde au lieu de supposer un délai.
- Une commande console peut mettre ~3 s (le firmware dessine l'écran avant de
  répondre) : le script attend l'écho d'une commande au lieu d'un temps fixe.
- Deux changements de page à moins de 1,2 s sont refusés par le firmware
  (protection de la dalle) : les tests les espacent.
- Le modèle de langage peut répondre à côté : le test du rappel a un second
  essai avec une phrase plus directive.
