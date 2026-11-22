# 02 — UNE seule page

## Ce que ça doit faire

Depuis le 11/11, le robot n'a plus de cycle d'applis : **un seul écran, le
tableau**, porte tout ce qu'il sait. Rien à faire défiler pour retrouver une
information : elle est là, ou elle n'existe pas.

    y 0    heure en corps 7 (18:54 = 133 px de large) et l'icône de signal
    y 40   la date en corps 3 : MER 11 NOV
    y 58   filet
    y 62   la température du moment, dehors, en corps 4
    y 86   l'état du ciel en clair, en corps 3
    y 106  INT 24C : le capteur interne, en corps 3
    y 126  DEM 5-11C : demain, en corps 3
    y 144  filet
    y 148  71% en corps 4 et la jauge de batterie (18 px de haut)
    y 172  filet
    y 176  l'agenda du jour en corps 4 (ou ALARME 1=STOP)

**Rien en dessous du corps 3.** La plus petite taille de texte de la page est
le corps 3 (glyphes de 9 x 15 px) : en dessous, c'est illisible sur une dalle de
200 px de côté. Tout ce qui ne tenait pas dans cette taille a été retiré de
l'écran : état système (cpu, ram, uptime), nom de la borne, consommation en
watts, le compagnon et ses quatre jauges (faim, bonheur, énergie, santé), son
visage, le conseil, le dernier mot, YouTube et le livre. Une ligne à demain ne
porte que la plage de températures : à ce corps, « PLUIE 20% » ne tient plus à
côté.

Règle de lecture, héritée du reste du firmware : **une valeur indisponible
affiche `N.D.` ou `--`, jamais zéro ni un nom supposé.** Une jauge inconnue
affiche un tiret, pas une barre vide (qui se lirait « à zéro »).

## Ce qui a disparu, et où c'est allé

| Avant | Maintenant |
|---|---|
| page METEO | bloc « ciel » du tableau |
| page SYSTEM | **retirée** : cpu, ram, watts et uptime ne tiennent pas en corps 2 |
| page ACCUEIL (heure, date, rdv) | en-tête et ligne agenda |
| page HISTORIQUE | « dernier mot » en bas : les deux dernières lignes de MINO |
| page CONSEIL | **retiré de l'écran** (le conseil reste dans /state et pour l'assistant) |
| page YOUTUBE | **retirée** le 11/11 (plus affichée, plus relue : son TLS toutes les 30 min n'a plus de consommateur) |
| page LIVRE | **retirée de l'écran** le 11/11 ; le lecteur reste dans le firmware et se pilote par le web (`/livre?op=liste`) |
| launcher (grille de 8 tuiles) | supprimé : plus rien à lancer |
| personnalité, voix, radio, rappels, réglages | inchangés (écrans plein écran et web) |

Les fonctions `draw_app_*` des anciennes pages sont toujours dans `main.cpp`
(le routeur et la table `PAGE_ACT` les connaissent), mais le cycle ne contient
plus qu'une page : `PAGE_COUNT` vaut 1. La page RÉGLAGES (`PAGE_REG`, hors
cycle) reste joignable par le web pour le clavier Wi-Fi.

## Gestes de la page

| Geste | Effet |
|---|---|
| bouton 1 court | relit les données (météo, capteurs) et redessine |
| bouton 2 court | rien de plus : il n'y a pas d'autre page |
| double bouton 1 | l'assistant vocal (inchangé, depuis n'importe quel écran) |
| double bouton 2 | sans effet (plus de livre à ouvrir) |
| appui long (1 ou 2) | retour au tableau, arrêt de ce qui tourne (voix, radio) |

Depuis le 11/11, aucun geste n'ouvre plus le livre : la page unique ne le
montre plus et son lecteur se pilote par le web.


## Page 2 : l'astuce de streaming

Le bouton 2 court bascule entre le tableau et cette page ; le bouton 1 court, sur
elle, redemande une astuce tout de suite.

    y 0    ASTUCE (corps 4) et, a droite, la source : R/TWITCH ou R/KICK
    y 26   STREAMING a gauche, la date de lecture a droite
    y 50   filet
    y 56   le texte de l'astuce, 16 signes par ligne, 7 lignes maximum
    y 184  2=TABLEAU (le geste pour revenir)

D'où vient le texte : du **flux RSS du top du jour** de r/Twitch
(`https://www.reddit.com/r/Twitch/top/.rss?t=day`), à défaut r/Kick. Reddit
répond **403 en JSON** à un client qui n'est pas un navigateur ; son flux Atom,
lui, passe (22999 octets au premier essai sur la carte). Le parseur accepte
`<item>` comme `<entry>`.

Fréquence : **une lecture par jour**, jamais plus.
- `astuce_jour` (numéro de jour = epoch / 86400) est gardé en NVS : tant qu'il
  est le jour courant, aucun accès réseau, la page 2 s'affiche hors ligne.
- Les titres récupérés tournent : l'entrée choisie est `jour % nombre_d_entrées`,
  donc une astuce différente chaque jour sans nouvelle lecture.
- Après un échec, on réessaie au plus toutes les 30 minutes (`astuce_essai_ms`),
  pour ne pas marteler le réseau (Reddit a répondu 429 en boucle lors de la
  mise au point).
- `/state` expose `astuce`, `astuce_src`, `astuce_quand`, `astuce_jour`.

## Comment c'est fait

- `main/tableau.h` définit `tableau_t` (des données, aucun pointeur vers le
  firmware) et `tableau_dessine()`.
- `main/tableau.cpp` porte **tout le dessin** et n'inclut que `art.h` : aucune
  dépendance ESP-IDF. Le même code est donc rendu sur le PC par
  `tools/apercu_tableau.cpp`, ce qui permet de juger une mise en page sans
  flasher la carte :

      g++ -O2 -o build/apercu_tableau tools/apercu_tableau.cpp main/tableau.cpp \
          main/art.cpp main/petart.cpp main/pet.cpp main/font5.cpp -Imain -lm
      build/apercu_tableau plein /tmp/plein     # jeu de données complet
      build/apercu_tableau vide  /tmp/vide      # tout ce qui peut manquer manque
      python3 tools/fb2png.py /tmp/plein.fb /tmp/plein.png 3

- `tableau_maitre()` dans `main.cpp` remplit la structure avec les valeurs
  vivantes (horloge, `wifi_actif`, SHTC3, météo, compagnon sous `pet_mux`,
  `sys_cpu10`, agenda `ms_*`, dernier tour de MINO lu dans `hist`,
  `conseil_txt`) puis appelle `tableau_dessine()`.
- Le signe `%` est dessiné (deux points + barre) et non pris dans la police
  3x5 : son glyphe ressemble à un `Z` dès le corps 2.
- L'icône Wi-Fi de `art.h` fait 24 px de haut et ne rentre pas dans la bande
  d'état : la page dessine trois barres de signal (une par niveau de RSSI).
- Le banc système (`sys_page_entre` / `sys_echantillon`) tourne aussi sur le
  tableau, sinon le CPU resterait à zéro ; seule la page SYSTEM redessine à sa
  propre cadence.

## Tests

| Test | Attendu |
|---|---|
| `bash tests/run_unit.sh` | `test_tableau` : 18 vérifications vertes |
| encre | la page porte du contenu (14 % de pixels noirs avec un jeu complet) et reste sous 45 % |
| zones | les six bandes (en-tête, ciel, batterie, jauges, agenda, dernier mot) contiennent de l'encre |
| déterminisme | deux rendus de la même donnée sont identiques au pixel |
| sensibilité | changer l'heure, la batterie, la météo, l'agenda, les jauges ou les deux textes change l'image |
| données absentes | la page reste dessinée, `N.D.` / `--`, et le cadre de la jauge FAIM reste vide |
| texte très long | 95 signes dans chaque champ : la dernière colonne de pixels reste blanche |

Sur la carte (après flash) : `/state` doit annoncer le nouvel `app`, `page` =
`ACCUEIL` en permanence, et `/ecran?raw=1` doit rendre exactement l'aperçu du
PC (passé à `tools/fb2png.py`, qui accepte l'hexadécimal). `btn 1` ne doit plus
changer de page.
