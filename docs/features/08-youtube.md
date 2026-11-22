# 08 — La page YouTube

## Ce que ça doit faire

La dernière page du cycle montre la chaîne YouTube de l'utilisateur : le nom de
la chaîne, la dernière vidéo, ses vues et son âge. Sans clé d'API, sans
application à installer.

## Comment c'est fait

1. La chaîne se règle une seule fois, par la console USB ou par le web :

       yt @monpseudo          # la forme courte du lien de la chaîne
       yt UCxxxxxxxxxxxxxxxxxxxxxx    # ou l'identifiant, si on l'a

   C'est gardé en NVS (`yt_h`, `yt_id`), donc ça survit aux reboots et aux
   mises à jour.
2. À l'ouverture de la page, le firmware lit **la page publique de la chaîne**
   (`https://www.youtube.com/@pseudo`) avec un User-Agent de navigateur et le
   cookie `CONSENT=YES+cb; SOCS=CAI`. Sans ce cookie, YouTube répond un 302 vers
   `consent.youtube.com` et on n'obtient rien — c'était le premier échec.
3. Il en tire :
   - l'identifiant `UC…` (lien canonique), gardé pour la fois suivante ;
   - le nom de la chaîne (`og:title`) ;
   - la vidéo mise en avant : `channelVideoPlayerRenderer` → `title.text` ;
   - `viewCountText` → les vues ; `publishedTimeText` → « il y a 11 h ».
4. Le flux `feeds/videos.xml` **ne sert plus à rien** : YouTube le renvoie en 404
   (vérifié sur trois chaînes). C'est pour ça que la lecture passe par la page.
5. La page pèse ~2 Mo : le tampon de 1,5 Mo est alloué en **PSRAM** à l'entrée de
   l'appli et **rendu** en la quittant (voir 07-applis-memoire.md). Lecture :
   ~7 s, faite dans la tâche réseau, jamais dans la tâche écran.
6. Le texte est remis à plat (accents → lettres simples, majuscules, `@` retiré)
   avant d'être dessiné : la police de l'écran est en majuscules sans accents,
   et les octets UTF-8 des accents cassaient la coupe des lignes.

## Tests

| Test | Attendu |
|---|---|
| `yt @youtube` puis log | `youtube : @youtube -> UCBR8-60-…` |
| log après lecture | `youtube YouTube : '<titre>' (398717 vues, il y a 11 h)` |
| `diag` | `yt_chaine='@…' yt_video='…' yt_vues=…` |
| page YOUTUBE | titre sur 3 lignes max, coupé aux espaces, vues ligne du bas, âge en dessous |
| sans chaîne réglée | `CHAINE ?` + `console : yt @moncompte` |
| pendant la lecture | `LECTURE EN COURS / REVIENS DANS 10 S` |
| `app_mem` sur la page | ≈ 1 572 864 o, et 0 en sortant |

Sections 2 et 3 de `tools/test_global.py` (la page fait partie du tour) ; la
lecture se vérifie à la main : `yt @ta-chaine` dans la console, puis `ui 5`.
