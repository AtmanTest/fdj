# 01 — Les boutons

## Ce que ça doit faire

Deux boutons, et il n'y en aura jamais d'autres. Chaque geste garde un sens
constant d'une page à l'autre, et **aucun appui ne doit être perdu**, même quand
la dalle met une seconde à se rafraîchir.

| Geste | Bouton 1 (BOOT) | Bouton 2 (PWR) |
|---|---|---|
| appui court | relit les données du tableau (1 = suite dans REPONSE) | rien (2 = avant dans REPONSE) |
| double appui | lance l'assistant vocal (et **lui seul**) | sans effet |
| appui long | retour au tableau, tâche en cours arrêtée | idem |
| maintien prolongé | rien (aucune page à faire défiler) | rien |

Depuis la page unique (11/11), il n'y a plus qu'un écran : « page suivante » n'a
plus de sens, et le bouton 1 court sert à redemander une lecture fraîche (météo,
capteurs). Le livre a quitté l'écran le même jour : plus aucun geste ne l'ouvre,
il se pilote par le web (`/livre?op=liste`).

**Une tâche en cours passe avant la relecture** : pendant une écoute, une radio
ou une voix, l'appui simple l'arrête au lieu de relire la météo.

Règles qui ne changent pas :

- Pendant une réponse en plein écran : 1 = suite, 2 = avant, un appui de plus à la
  fin referme.
- Pendant une écoute, une radio ou une voix : n'importe quel appui arrête et
  ramène à l'accueil.
- Sous RAPPELS et RÉGLAGES, la page reprend la main sur ses propres gestes.
- Un appui long ne produit jamais d'appui court : on ne fait pas les deux.

## Comment c'est fait

### Deux tâches, et c'est tout le sujet

Avant, une seule tâche (`button_task`) lisait les broches **et** exécutait
l'action. Or une action peut bloquer près d'une seconde : rafraîchissement
e-paper, attente anti-rafale de 1,2 s, tourne-page du livre. Pendant ce temps,
plus personne ne lisait les broches — l'appui tombait dans le vide. D'où les
rustines du genre « on ATTEND plutôt que de perdre l'appui ».

Aujourd'hui le travail est coupé en deux :

    button_echantillonne   priorité 5, réveil toutes les 10 ms
                           lit les deux broches, appelle geste_maj(), pousse le
                           geste dans une file. Ne bloque jamais, ne dessine rien.

    button_action          priorité 3
                           vide la file et exécute. A le droit de bloquer : pendant
                           ce temps l'échantillonneur continue de remplir la file.

Conséquence directe : un appui peut attendre 1,2 s dans la file sans être perdu,
et la protection de la dalle ne coûte plus aucun geste.

### Le classement des gestes : `main/geste.h`

Header seul, sans dépendance (ni FreeRTOS ni ESP-IDF), donc testable sur le PC.

- **anti-rebond** 40 ms ; l'heure du geste est celle du *premier* passage du
  front, pas celle de sa confirmation, sinon chaque appui perdrait la durée du
  rebond au passage.
- **COURT retenu** 320 ms sur un bouton qui accepte le double appui : c'est le
  prix à payer pour que le double appui lance l'assistant sans changer de page
  au passage. Sur un bouton sans double (`double_autorise[1] = false`), le COURT
  part immédiatement.
- **LONG** part à 600 ms *pendant* l'appui, sans attendre le relâchement.
- **DOUBLE** = deuxième appui commencé dans les 320 ms du relâchement. Il annule
  le COURT retenu : un double ne laisse jamais un court derrière lui.
- **REPEAT** = le bouton reste enfoncé après le LONG : un événement toutes les
  400 ms. C'est ce qui permet de faire défiler avec deux boutons seulement. Une
  page qui n'en veut pas ne fait rien.
- **File** de 16 gestes avec compteur `perdus` : un débordement est compté, pas
  ignoré en silence.

Les appuis simulés par `/bouton?n=0&m=court` passent par la même file que le vrai
doigt, donc par le même chemin de décision.

## Tests

### Sur le PC (sans carte)

    bash tests/run_unit.sh

`tests/unit/test_geste.c` simule l'horloge au pas de 10 ms et vérifie :

| Cas | Attendu |
|---|---|
| appui de 100 ms | un COURT, une seule fois, durée mesurée |
| appui court puis silence | pas de COURT avant la fin des 320 ms |
| deux appuis rapprochés | un DOUBLE, **et aucun COURT** |
| appui de 700 ms | un LONG à 600 ms, aucun COURT au relâchement |
| maintien de 2 s | un LONG puis trois REPEAT |
| parasite de 20 ms | rien |
| bouton sans double | le COURT part tout de suite |
| dix appuis, consommateur absent | dix gestes en file, zéro perdu |
| file pleine | les gestes en trop sont comptés |
| deux boutons tenus ensemble | deux LONG, pas un DOUBLE |

### Sur la carte

Rejoués par `tools/test_global.py` (section 2), et à la main par la console USB :

    btn 1            -> page suivante
    btn 2            -> page précédente
    btn 1 long       -> ACCUEIL
    btn 1 dbl        -> lance l'assistant (voix_etat=1)

| Test | Attendu |
|---|---|
| `btn 1` depuis le tableau | on reste sur le tableau, `meteo_force`/`yt_force` armes, un bip |
| `btn 2` depuis le tableau | on reste sur le tableau (aucune page à tourner) |
| `btn 2 long` depuis le tableau | on reste sur le tableau (plus de livre) |
| `btn 1 long` depuis n'importe où | ACCUEIL |
| deux `btn 1` à moins de 1,2 s | deux relectures, aucun appui perdu |
| appui simple du bouton 1 | ne lance **jamais** l'assistant |

Sur le PC, `tests/unit/test_routeur.cpp` rejoue ces gestes (42 vérifications) :
le vrai bloc du routeur, extrait de `main.cpp` et compilé contre des stubs.

### Le routage : quatre fonctions, pas de table

Il n'y a plus qu'une page, donc plus de table « une ligne par page ». Le
routeur tient dans `bouton_court`, `bouton_double`, `bouton_long` et
`bouton_repeat`, avec un seul geste propre (`act_tableau_court`, le bouton 1).

L'ordre de priorité est celui d'avant, et il est explicite dans le code :

1. la réponse plein écran (REPONSE) capture tout ;
2. la sonnerie s'arrête au bouton 1 ;
3. la page, si sa ligne prend le geste ;
4. une tâche en cours s'arrête ;
5. sinon : navigation dans le cycle.

Le bloc du routeur est délimité dans `main.cpp` par `@@ROUTEUR_DEBUT@@` et
`@@ROUTEUR_FIN@@` : `tests/unit/extrait_routeur.py` le tire du firmware et
`tests/unit/test_routeur.cpp` le compile contre des stubs. Ce qui est testé est
donc le vrai code, pas une réécriture — 30 vérifications sur l'ordre des
priorités et le contenu de la table.

## Ce qui reste ouvert

- `GESTE_REPEAT` n'est branché que sur la page LIVRE. HISTORIQUE et RAPPELS y
  gagneraient, mais chacune demande son test sur la carte.
- Le routeur n'est pas encore compilé pour la carte : il faut ESP-IDF
  (`idf.py build`), absent de la machine de développement.

