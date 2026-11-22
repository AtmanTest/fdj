# UNIBET 5.0.0 — maquette d'entretien sur BATMino

Huit écrans, une dalle e-paper de 200×200 en noir et blanc, deux boutons.
L'objectif n'est pas d'en mettre partout : c'est qu'à 50 cm, sur cette dalle,
tout se lise du premier coup. D'où la règle unique de mise en page — **police
5×10 partout** (10 px de haut, 33 caractères par ligne, rien en dessous).

## Les 8 écrans

| # | Écran | Ce qu'on y montre |
|---|-------|-------------------|
| 1 | **ACCUEIL** | état de session, nom, solde en corps double, plafond, score en direct |
| 2 | **CONNEXION** | compte de démonstration, code 4 chiffres, ce que fait chaque bouton |
| 3 | **PARIS** | 4 sélections live : match, probabilité du modèle, code, minute, marché |
| 4 | **COUPON** | mise, gain potentiel, sélections retenues, valider / vider |
| 5 | **COMPTE** | solde, plafond, net sur 7 jours, deux derniers paris, session |
| 6 | **RESPONSABLE** | 5 questions de protection, jauge, pause 24 h datée |
| 7 | **PREUVES** | nombre de vérifications, 0 échec, latences, mémoire, API |
| 8 | **RÉGLAGES** | contraste, texte, son, langue, code, effacement, version |

Les 37 écrans de la 4.x ont été supprimés : à cette taille, une page qui
demande de lire dix lignes n'est pas lue. Chaque écran porte une seule idée.

## Connexion (compte)

- Écran 1 → **SE CONNECTER** : saisie d'un code à 4 chiffres (code d'usine
  `0000`), puis la session s'ouvre et le solde s'affiche.
- La session est enregistrée en NVS (`connecte`) et survit au redémarrage.
- **SE DÉCONNECTER** depuis l'écran 5 ou l'écran 1 : le solde repasse en
  « N.D. », aucune donnée personnelle n'est affichée hors session.
- Aucun mot de passe n'est demandé, aucun secret n'est transmis : c'est une
  maquette, pas un back-office.

## Gestes (deux boutons)

| Geste | Effet |
|-------|-------|
| B1 court | cocher une sélection / répondre OUI / choisir |
| B1 double | décocher / vider |
| B1 long | écran précédent |
| B2 court | item suivant, puis écran suivant en fin de liste |
| B2 double | action principale de l'écran |
| B2 tenu 3 s | retour à l'accueil |

## Rien d'inventé

Une valeur indisponible s'affiche **N.D.** — jamais 0, jamais un nom supposé.
Pas de réseau : « HORS LIGNE : N.D. ». Pas encore de scores : « SCORES :
N.D. / RÉCEPTION EN COURS ». Pas de pari réglé : « 7 JOURS : N.D. ».

## Recette

```bash
./tests/run_unit.sh      # 387 vérifications, 7 suites, 0 échec
tools/recette.sh         # tests + firmware + 8 PNG relus
tools/flash.sh           # build, flash, relecture de version
```

L'API locale de recette, sur la carte :

| Route | Effet |
|-------|-------|
| `GET /etat` | page, curseur, solde, historique, top, wifi, mémoire, version |
| `GET /ecran` | framebuffer en hexadécimal (200×200, 1 bit) |
| `GET /page?n=7` | forcer un écran |
| `GET /bouton?n=1&m=court\|double\|long\|repeat3` | injecter un geste |
| `POST /fichier?p=/sdcard/x.zip` | écrire un fichier sur la micro SD |
| `GET /liste` | contenu de la micro SD (nom + taille) |

## Matériel

ESP32-S3-PICO-1 (8 Mo flash, 8 Mo PSRAM), dalle e-paper 200×200 1 bit,
deux boutons, micro SD. Firmware ESP-IDF 5.5.1, ~1,25 Mo.

```bash
cp main/secrets.h.example main/secrets.h   # Wi-Fi + clés, jamais versionné
```

## Auteur

Thasin Jahangir — @paristzou — « juste pour les filles ».
Maquette de démonstration, sans lien avec Unibet : aucune mise réelle.
