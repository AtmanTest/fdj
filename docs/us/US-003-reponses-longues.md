# US-003 — Réponses longues, intelligentes, et voix découpée

**Besoin (Taz)** : « je veux que ces réponses soient plus longues et
intelligentes, tu peux augmenter le contexte de réponse. »

## Critères d'acceptation
1. `max_tokens` du cerveau porté de 150 à 700 ; température 0,7.
2. Le prompt impose 3 à 6 phrases avec le raisonnement, jamais oui/non.
3. L'historique est envoyé en vrais messages `user`/`assistant` (contexte
   multi-tours, jusqu'à 12 tours de 320 octets), et plus en pavé tronqué.
4. La voix lit **toute** la réponse : découpage automatique en morceaux de
   ~180 caractères (limite de Google TTS), coupés sur la ponctuation.
5. Latence maîtrisée : le découpage ne relance pas une requête par phrase courte.

## Cas de test

### Intégration (carte) — `bash tests/api.sh`
| # | Cas | Attendu |
|---|-----|---------|
| I1 | `/dit` sur une question ouverte | réponse > 120 caractères |
| I2 | deux `/dit` liés | la 2e réponse tient compte de la 1re |

### Composant (journal série)
| # | Cas | Attendu |
|---|-----|---------|
| C1 | réponse de ~600 caractères | 3 requêtes TTS enchaînées, chacune `… octets mp3 en … ms` |
| C2 | réponse de 100 caractères | 1 seule requête TTS |
| C3 | ponctuation en fin de morceau | pas de mot coupé en deux |

## Implémentation
`ds_chat()` : `max_tokens` 700, `temperature` 0.7, messages d'historique,
prompt système de rôle. `tts_parle()` découpe et appelle `tts_parle_un()`
(l'ancienne fonction, inchangée).

## Mesures
| Cas | Résultat |
|-----|----------|
| réponse longue (3 conseils) | 3 morceaux TTS, 77 376 à 89 280 octets mp3, ~1,6 s chacun |
| question ouverte | 5 phrases, réponse développée |

## Etat
- [x] code + flash
- [x] tests d'intégration
- [x] vérification journal (3 morceaux)
