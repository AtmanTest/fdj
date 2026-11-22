# 06 — L'écran e-paper

## Ce que ça doit faire

200×200, noir et blanc, reflète l'appli au premier plan et rien d'autre. Un
changement de page doit se voir **tout de suite**, et la dalle ne doit jamais
rester en panne.

## Comment c'est fait

- Framebuffer global de 5 000 octets (200×25), primitives dans `art.cpp` avec
  bornes strictes (`raw_px` refuse hors écran : pas de corruption mémoire).
- Rafraîchissement **complet** à chaque changement de page (sinon les fantômes
  de la page précédente restent : c'est ça, les « flèches » qui traînent), et
  partiel toutes les 10 images autrement.
- Mutex `ui_mux` (dessin) et `epd_mux` (dalle) : une seule image à la fois.
- Si la dalle répond BUSY trop longtemps, le pilote la met en panne ;
  `ecran_reprise()` la remet hors tension puis la réinitialise.
- Une image demandée mais non envoyée met `redraw_demande` : elle repart dès que
  possible (voir 05-stabilite.md).

## Tests

| Test | Attendu |
|---|---|
| `fb` sur chaque page | 200×25 octets, 5 images différentes |
| changement de page | l'image arrive en moins de ~2 s, sans fantôme |
| BUSY | `epd: BUSY pendant …` < 2 000 ms, jamais de `panne` |
| rafale | la dalle reste vivante, l'écran affiche la dernière page demandée |

Section 3 de `tools/test_global.py`. Les PNG produits par `fb` sont écrits dans
`/tmp/test_<page>.png` ; `tools/shot_usb.py` fait la même chose en une commande
(`tools/shot_usb.py sortie.png /dev/ttyACM1 "ui 2"`).
