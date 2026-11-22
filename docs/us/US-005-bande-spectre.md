# US-005 — La bande : le spectre de la voix à la place du tamagotchi

**Besoin (Taz)** : « quand l'IA parle je veux rester sur la page principale mais à
la place du tamagotchi on met une bande qui dynamiquement change de taille […]
je veux vraiment montrer qu'une intelligence existe, comme dans les films ».
Puis : « commençons par 1 [le spectre], travail énormément sur l'esthétisme, tu
peux même supprimer l'état du tamagotchi et aussi la ligne appui long je
t'entends ».

## Ce que fait la bande
1. Pendant l'écoute (état 1), la réflexion (2) et la parole (3), la page
   principale **reste affichée** : plus de saut sur la page PARLER.
2. À la place du tamagotchi et de la météo : 24 colonnes dont la hauteur suit
   l'énergie réelle du son, mesurée par un **Goertzel** (24 bandes log de 90 Hz
   à 7 kHz, fenêtre de Hann de 256 échantillons à 16 kHz).
3. La source est le vrai son : le PCM décodé du MP3 de Laura quand elle parle,
   le PCM du micro quand elle écoute. Rien n'est simulé.
4. Attaque vive (0,75) et retombée lente (0,18) ; les crêtes (tirets) glissent
   vers le bas à 0,012/image.
5. Au repos : une vague lente traverse la bande (respiration), plus un point de
   présence qui bat.
6. L'état du tamagotchi et la ligne « APPUI LONG : JE T'ÉCOUTE » sont supprimés
   de la page pendant la conversation.

## Contrainte matérielle mesurée (le point clé)
Un rafraîchissement partiel coûte **865 ms**, quelle que soit la zone
(24, 56 ou 99 lignes) et quelle que soit la séquence 0x22 (0xC7 ou 0xCF) :

| essai | lignes | séquence | ms/image |
|-------|--------|----------|----------|
| A | 99 | 0xCF | 844 |
| B | 56 | 0xCF | 863 |
| C | 24 | 0xCF | 863 |
| D | 99 | 0xC7 | 866 |

Soit **1,15 image/s** : c'est la forme d'onde de l'e-paper, pas le transfert
(2 475 octets ≈ 3 ms). D'où les choix d'esthétique :
- une texture qui **défile d'un pixel par image** à l'intérieur des barres
  (l'œil perçoit un flux continu là où les hauteurs sautent) ;
- des hauteurs qui ne sautent pas : attaque/retombée lissées ;
- une silhouette très lisible (barres pleines de 4 px, 4 px d'écart) plutôt
  qu'un dégradé fin, qui deviendrait du bruit à cette cadence.

## Cas de test

### Intégration (carte) — `bash tests/api.sh`
| # | Cas | Attendu |
|---|-----|---------|
| I1 | `/bande` | y0=88, y1=186, période 60 ms |
| I2 | `/bande?op=bench&n=10` | ms/image ≈ 850-900, pas d'erreur |
| I3 | `/ecran?q=bande` | la bande se dessine (ASCII non vide) |
| I4 | `/parle?ms=9000` pendant la conversation | `/bande` répond `vive:true` |

### Composant
| # | Cas | Attendu |
|---|-----|---------|
| C1 | TTS de 4 morceaux avec la bande active | les 4 `mp3 en … ms` apparaissent, aucun redémarrage |
| C2 | fin de conversation | la bande s'arrête, le tamagotchi revient |

## Outils ajoutés
- `/ecran` : l'écran en ASCII (2×2 pixels par caractère), `?q=bande` pour la
  bande seule — permet de juger le dessin sans être devant la carte.
- `/bande?op=bench&n=..&h=..&seq=c7|cf&lut=part` : mesure du rafraîchissement.
- `/bande?op=demo&v=0.7` : remplit des niveaux plausibles pour inspecter le dessin.
- Driver : `EPD_ShowPartWindow()` / `EPD_ShowPartWindowSeq()` — rafraîchit une
  fenêtre (y0..y1) au lieu des 5 000 octets de l'écran entier.

## Etat
- [x] code + flash
- [x] mesure du rafraichissement documentée
- [x] tests d'intégration
- [ ] validation visuelle par Taz (le rendu réel sur l'e-paper)
- [ ] option future : LUT partielle plus rapide (risque de déséquilibre DC)

## Ce qui a été mesuré pour de vrai (18/10/2026, après la coupure d'alim)

| opération | durée réelle |
|-----------|--------------|
| rafraîchissement complet (LUT pleine, 0xC7) | 1758 ms |
| rafraîchissement partiel (0xCF) | 573 ms |

Une image toutes les 573 ms = la dalle clignote en permanence : **cette dalle ne
peut pas animer**. Le Goertzel 24 bandes a été abandonné (il affamait le CPU) au
profit d'une énergie RMS par bloc de 40 ms qui ouvre la bouche du compagnon.

Ce qui est en place :
- le compagnon en grand avec la bouche ouverte pendant que Laura parle (une
  image au début de la parole, une à la fin) ;
- `show_ui()` ne parle à la dalle que si le framebuffer a changé (`memcmp`) et
  au plus une fois toutes les 3 s (`UI_CADENCE_MS`) ;
- le compagnon ne se promène plus tout seul : il avance d'un pas par minute.

Piste pour une vraie bande animée : il faut une dalle qui accepte le partiel
rapide (SSD1681/6" ou LCD). Sur celle-ci, toute animation est un stroboscope.
