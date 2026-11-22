# US-002 — Clone numerique de Taz + role d'expert

**Besoin (Taz)** : « je veux que tu lui transmettes ma personne a travers tes
donnees, un condense de ma personne dans sa memoire, et changer son role :
expert ultime IT, specialite IA, technicien superieur, developpeur, coach de vie
personnel, toujours reflechir avant d'agir. »

## Critères d'acceptation
1. `/sdcard/mino/clone.md` contient le portrait de Taz ; il est relu à chaque
   démarrage et injecté dans le prompt système.
2. Le prompt système impose le rôle : expert IT de très haut niveau, spécialiste
   IA, technicien supérieur, développeur, coach de vie personnel.
3. « Toujours réfléchir avant d'agir » figure explicitement dans le prompt.
4. Si le fichier est absent, un portrait minimal par défaut est utilisé (pas de
   plantage, pas de prompt vide).
5. Le portrait est modifiable depuis le PC, sans reflasher : `POST /fichier?p=/mino/clone.md`.
6. La mémoire durable (`memory.json`, notes) reste transmise en plus du clone.

## Cas de test

### Unitaire (PC)
| # | Cas | Attendu |
|---|-----|---------|
| U1 | historique partagé (cf. US-001) inchangé | 12/12 |

### Intégration (carte) — `bash tests/api.sh`
| # | Cas | Attendu |
|---|-----|---------|
| I1 | `/dit?t=quel est mon nom…` | la réponse cite « Taz » |
| I2 | `/dit?t=…` sur un sujet du clone | réponse cohérente avec le portrait |
| I3 | `/fichier?p=/mino/clone.md` (GET) | le fichier se relit à l'identique |

### Composant
| # | Cas | Attendu |
|---|-----|---------|
| C1 | clone.md absent au boot | démarrage normal, portrait par défaut |
| C2 | clone.md de 1593 octets | journal « CLONE lu sur la carte SD (1593 octets) » |

## Implémentation
`clone_txt` chargé à côté du SOUL ; prompt système construit avec
`%.900s` (soul) + `%.1600s` (clone) + `%.480s` (mémoire) + rôle.

## Etat
- [x] fichier `clone.md` déposé sur la carte (1593 octets) et relu au boot
- [x] rôle injecté, vérifié (« Oui, tu es Taz, mon createur… »)
- [x] tests d'intégration
