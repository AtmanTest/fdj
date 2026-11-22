# 09 — Le livre (e-reader)

## Ce que ça doit faire

Le robot lit. Un fichier texte par livre dans `/sdcard/livres` ; la page LIVRE
affiche la page courante en pleine résolution, et on tourne les pages avec les
deux boutons. Rien d'autre sur l'écran : pas de jauges, pas de compagnon. C'est
un livre.

    APPUI COURT bouton 1 : page suivante
    APPUI COURT bouton 2 : page précédente
    DOUBLE APPUI bouton 2 : le livre suivant du dossier
    APPUI LONG (1 ou 2)   : retour à l'accueil (le geste commun)

La position est retenue (NVS) : on rouvre le livre exactement où on l'a laissé,
même après un redémarrage. Quitter la page rend la mémoire (PSRAM) ; y revenir
relit le fichier.

## L'écran

    200x200, police 5x10 (main/font5.cpp), 32 colonnes, 16 lignes
    en-tête : le titre du fichier              page courante / total
    filet
    16 lignes de texte (interligne 11 px)
    pied : « 1 SUIVANT  2 PRECEDENT »          pourcentage lu

Un paragraphe est une ligne vide (comme dans un livre) ; un vers va à la ligne
sans blanc. Une ligne trop longue est coupée au mot ; un mot plus long que
l'écran est coupé net à la colonne.

## Comment c'est fait

- `main/font5.h/.cpp` — police 5 colonnes x 10 lignes, 123 glyphes :
  ASCII, minuscules, majuscules, **lettres accentuées françaises** (é è ê ë à â
  ä ç ù û ü î ï ô ö, É È Ê À Â Ç Ù Î Ô Ï Ü) et les guillemets « ». Générée par
  `tools/gen_font5.py` à partir de la police 5x7 d'Adafruit (glcdfont.c),
  accents ajoutés au-dessus de la lettre. **Ne pas éditer à la main.**
- `main/livre.h/.cpp` — encodage, pagination, rendu. Le texte est ramené à
  **un octet par caractère affiché** (indice de glyphe) au chargement :
  la pagination compte alors des colonnes, jamais des octets UTF-8, et la
  tourne de page ne fait aucune conversion.
  `0xFF` = fin de ligne, `0xFE` = fin de paragraphe (ligne vide).
- Le texte entier vit en PSRAM (6 Mo max par livre) : tourner une page ne
  touche pas la carte SD, ça ne coûte qu'un rafraîchissement **partiel**
  (~0,9 s), complet toutes les 10 pages comme partout ailleurs.
- `livre_dessine()` est dans `livre.cpp`, donc `tools/preview.cpp` dessine
  exactement la même page que la carte : `./preview livre fichier.txt 12`.
- API : `/livre?op=liste|ouvre&f=nom.txt&n=|page&n=|suiv&d=|change&d=|ferme`,
  l'état est dans `/state` (`livre`, `livre_fichier`, `livre_page`,
  `livre_pages`, `livre_pct`, `livres_sd`). Console : `livre`, `livre liste`,
  `livre ouvre <nom> [page]`, `livre page <n>`, `livre suiv`, `livre prec`.
- Mise en place d'un livre : `tools/epub2txt.py` convertit un EPUB en texte
  (ordre du spine, entités, césures recollées, vers serrés, paragraphes
  détachés), puis `curl --data-binary @livre.txt
  'http://<robot>/fichier?p=/livres/livre.txt'`.

## Tests

Sur le PC, sans la carte : `bash tests/run_unit.sh` (32 vérifications, dont
accents distincts, espaces compressés, paragraphes, mot plus long que la
colonne, bornes de pagination, fichier absent ou vide).

Sur la carte :

| Test | Attendu |
|---|---|
| `curl '/livre?op=liste'` | les .txt du dossier, dossier créé au premier appel |
| `curl '/livre?op=ouvre&f=fleurs.txt&n=11'` | `page: 11`, `pages: 456` pour *Les fleurs du mal* |
| `./preview livre fleurs.txt 10` | même compte de pages que `/state` |
| `/bouton?n=1&m=court` | page précédente, `/state` suit |
| `/bouton?n=1&m=double` | le livre suivant s'ouvre, page 1 |
| `/bouton?n=0&m=long` | retour ACCUEIL, PSRAM rendue, position gardée |
| redémarrage puis page LIVRE | le livre reprend à la même page |
| `/ecran?raw=1` → PNG | la page du livre, accents et guillemets lisibles |

Vérifié sur la carte le 25/10 : *Les fleurs du mal* (456 pages, 154 Ko) et un
texte d'essai ; reprise après `reset`, double appui = livre suivant, appui long =
accueil.
