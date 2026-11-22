# 05 — Stabilité (le bug « ça reboote / c'est figé »)

## Le symptôme

« Le système bug, l'écran s'éteint, ça reboote, impossible de changer d'app, ça
reste figé avec les flèches. »

## Ce qui a été trouvé et corrigé

1. **Changement de page perdu.** Si un appui tombait pendant qu'une image était
   en cours d'envoi, `show_ui()` abandonnait sans rien dire : la page changeait
   dans la mémoire mais **pas à l'écran**, jusqu'au tick suivant (une minute).
   C'est exactement « ça reste figé ». → drapeau `redraw_demande` : l'image
   sautée est redessinée dès que la dalle se libère.

2. **Rafale d'appuis = dalle en panne.** Chaque changement de page déclenche un
   rafraîchissement complet (~1,8 s). Deux appuis serrés, et la dalle répond
   BUSY jusqu'au timeout : le pilote la met en panne, coupe son alimentation
   (`ecran_reprise`) et **l'écran reste éteint**. → anti-rafale de 1,2 s entre
   deux changements de page.

3. **Pile trop petite pour dessiner.** `button_task` dessine une page avec
   3 072 octets de pile ; les nouvelles applis (météo, minuteurs) appellent
   `snprintf` et les primitives de dessin en profondeur. → 5 120 octets.

4. **Les téléchargements gelaient l'écran.** Météo, titres du jour et YouTube
   étaient lus **dans la tâche écran** : un TLS de 3 à 20 s (YouTube : 7 s pour
   1,5 Mo) bloquait l'affichage *et* les boutons pendant tout le transfert.
   → une tâche `reseau` séparée reçoit les demandes par file (`net_demande`) et
   prévient l'écran quand la donnée est arrivée. La tâche écran ne fait plus
   jamais d'entrée-sortie réseau.

5. **Use-after-free en quittant une appli (la vraie cause des reboots).** En
   quittant METEO ou YOUTUBE, la page rendait son tampon… pendant que la tâche
   réseau écrivait encore dedans. L'allocateur lâchait :
   `assert failed: block_locate_free tlsf_control_functions.h:618`
   → reboot en pleine navigation, écran figé sur la dernière image.
   → le tampon météo (2 Ko) vit aussi longtemps que la carte ; celui de YouTube
   n'est libéré qu'à la fin de la lecture (`yt_en_cours` / `yt_a_liberer`).
   C'est ce qui faisait « ça reboote et les flèches ne répondent plus ».

6. **Pas de traces après un reboot.** Impossible de savoir si la carte a
   paniqué, eu une chute d'alimentation ou été bloquée. → au démarrage :
   `raison du reset = panic | brownout | task_wdt | …` et un **compteur de
   démarrages en NVS** (`demarrage n42`), repris dans `diag` (`boot_nb`).
   S'il monte sans qu'on ait flashé, la carte a rebooté toute seule.

## Tests

| Test | Attendu |
|---|---|
| `diag` en début et fin de test | `boot_nb` identique (aucun reboot) |
| `ui_ticks` en fin de test | strictement supérieur au début (tâche écran vivante) |
| log complet du test | pas de `Guru Meditation`, pas de `abort()` |
| `heap_int_max` | > 8 000 octets libres d'un bloc en RAM interne |
| 10 changements de page en rafale | l'écran suit, la dalle ne se met pas en panne |

Section 5 de `tools/test_global.py`. Si le bug revient : lire `raison du reset`
et `demarrage nN` au boot — la cause (panic / brownout / wdt) sera écrite.
