# US-004 — Laura : nom, rôle, et radio désactivée

**Besoin (Taz)** : « je veux qu'elle s'appelle Laura » ; « supprime la radio, ça
ne m'intéresse pas pour l'instant, désactive le module, on pourra activer plus
tard » ; et la réponse doit cesser d'être fausse quand on demande la radio.

## Critères d'acceptation
1. Elle se présente comme **Laura**, assistante personnelle de Taz.
2. Le SOUL sur la carte (`/sdcard/mino/soul.md`) décrit Laura : adulte, experte
   IT/IA/développeuse/coach, réponses complètes — plus « 2 phrases d'enfant ».
3. Le module radio est **désactivé** : aucune lecture, aucun envoi de liste de
   stations au modèle, aucun champ `radio` dans le JSON demandé.
4. Si on demande la radio, elle répond que c'est désactivé — elle n'invente pas
   d'action et le module reste inactif.
5. Réactivation possible plus tard sans reflasher : `/radio?actif=1` (état gardé
   en NVS, donc il survit au redémarrage).
6. Le reste (conversation, clone, historique, voix) est inchangé.

## Cas de test

### Intégration (carte) — `bash tests/api.sh`
| # | Cas | Attendu |
|---|-----|---------|
| I1 | `/radio` sans paramètre | `{"actif":false,"etat":"desactivee"}` |
| I2 | `/dit?t=ok lance la radio` | la réponse dit que c'est désactivé |
| I3 | `/radio` après ce tour | toujours `actif:false` (aucune action) |
| I4 | `/dit?t=comment tu t'appelles` | la réponse contient « Laura » |

### Composant
| # | Cas | Attendu |
|---|-----|---------|
| C1 | démarrage | journal `module radio : desactive` |
| C2 | `/radio?actif=1` puis reboot | journal `module radio : actif`, puis retour à `desactive` après `/radio?actif=0` |

## Implémentation
`radio_actif` (NVS clé `radio`), garde dans `radio_joue()`, interrupteur dans
`h_radio`, prompt système conditionnel (`%s` radio-note + champ JSON retiré),
`DEFAULT_SOUL` et `soul.md` réécrits pour Laura.

## Etat
- [x] code + flash + soul.md déposé sur la carte (1637 octets)
- [x] « comment tu t'appelles » → Laura, rôle complet
- [x] « ok lance la radio » → « La radio est désactivée… je n'invente aucune action »
- [x] `/radio` reste `actif:false` après la demande
- [x] 12/12 unitaires, 13/13 intégration
