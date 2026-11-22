# 07 — Une appli par page : la mémoire aussi

## Ce que ça doit faire

Une page = une appli. Quand on la quitte, l'appli est **tuée** : sa mémoire de
travail est rendue. Ne tournent en permanence que les services vitaux.
C'est la règle demandée : « une app par page, les autres apps sont killées en
mémoire, excepté la connexion wifi et les services vitaux ».

## Qui est vital (jamais arrêté)

- la connexion Wi-Fi et la synchro de l'heure (NTP) ;
- les boutons, l'écran, la console USB, le serveur web ;
- les rappels/minuteurs et la sonnerie (une alarme doit sonner même si on est
  sur une autre page) ;
- l'assistant, à la demande ;
- le compagnon (il vieillit même quand on ne le regarde pas).

## Qui est une appli (né et meurt avec sa page)

| Page | Allocation à l'entrée | Rendue à la sortie |
|---|---|---|
| METEO | 0 o (le tampon de 2 Ko appartient au service réseau, alloué une fois) | oui |
| SYSTEM | tableau des tâches 48×`TaskStatus_t` (PSRAM) + unité ADC | oui |
| HISTORIQUE | position de lecture remise à zéro | — |
| RAPPELS | rien de privé (la liste est vitale) | — |
| YOUTUBE | 1,5 Mo (page de la chaîne, PSRAM) | oui |
| ACCUEIL | rien de privé | — |

## Comment c'est fait

`appli_suivre_page()` est appelé par `show_ui()` : si la page a changé, il
appelle `appli_sort(ancienne)` puis `appli_entre(nouvelle)`. Chaque appli alloue
et libère dans ces deux fonctions, jamais ailleurs. Chaque transition est
journalisée avec les octets rendus et la RAM interne libre :

    appli METEO : demarree (0 o prives, interne 210000 o libres)
    appli METEO : arretee, 0 o rendus (interne 210000 o libres)

Le tampon de la météo (2 Ko) n'est **pas** libéré en quittant la page : une
lecture TLS pouvait encore écrire dedans, et le libérer plantait l'allocateur
(`assert failed: block_locate_free`) → reboot. 2 Ko pour toute la vie de la
carte, c'est le prix de la stabilité. Le gros tampon de YouTube (1,5 Mo), lui,
n'est libéré qu'une fois la lecture finie (`yt_en_cours` / `yt_a_liberer`).

La météo n'est plus lue toutes les 15 min que si la page METEO est au premier
plan : pas de TLS pour une page qu'on ne regarde pas.

## Tests

| Test | Attendu |
|---|---|
| `diag` sur ACCUEIL | `app=aucune app_mem=0` |
| `diag` sur METEO | `app=METEO app_mem=0` (le tampon est un service) |
| `diag` sur SYSTEM | `app=SYSTEM app_mem=…` |
| retour sur ACCUEIL après METEO | `app_mem=0`, RAM interne libre ≥ celle d'avant |
| 50 changements de page | `heap_int_max` ne baisse pas (pas de fuite) |

    for i in $(seq 50); do echo "btn 1" > /dev/ttyACM1; done
    # puis : diag -> heap_int_max doit etre stable

Fait partie de la section 5 (`RAM interne suffisante`) et se vérifie à la main
avec `diag` entre deux pages.
