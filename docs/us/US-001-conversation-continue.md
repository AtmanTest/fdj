# US-001 — Conversation continue sans rappuyer sur le bouton

**Besoin (Taz)** : « je veux pouvoir repondre sans rappuyer le bouton, laisser
l'ecoute active pendant 10 secondes, donc une vraie conversation. »

## Critères d'acceptation
1. Un appui court sur le bouton 1 lance le premier tour comme avant.
2. Après la réponse vocale, le micro se réouvre tout seul pendant **10 s**.
3. Si Taz parle pendant ces 10 s, un nouveau tour démarre (STT → LLM → TTS),
   sans aucune action sur les boutons.
4. Si personne ne parle pendant la fenêtre, la conversation s'arrête et
   l'écran revient à l'accueil.
5. L'historique des échanges est conservé d'un tour à l'autre (voir US-002).
6. Le bouton 2 interrompt immédiatement la conversation en cours.
7. Garde-fou : 10 tours maximum, pour ne pas boucler indéfiniment.

## Cas de test

### Unitaire (PC) — `bash tests/run_unit.sh`
| # | Cas | Attendu |
|---|-----|---------|
| U1 | historique vide | 0 tour |
| U2 | deux ajouts | 2 tours, rôles corrects |
| U3 | texte vide ou NULL | ignoré |
| U4 | plus de `HIST_MAX` tours | l'ancien sort, l'ordre reste |
| U5 | texte de 900 octets | tronqué à `HIST_TXT_SZ-1` |
| U6 | indices hors bornes | pas de plantage |

### Intégration / composant (carte) — `bash tests/api.sh`
| # | Cas | Attendu |
|---|-----|---------|
| I1 | `/state` | répond, contient `historique_tours` |
| I2 | deux `/dit` successifs | les deux répondent et l'historique grandit |
| I3 | `/tts` | accepté (voix disponible) |

### Manuel (sur la carte)
| # | Cas | Attendu |
|---|-----|---------|
| M1 | bouton 1 puis parler | premier tour normal |
| M2 | parler pendant les 10 s | deuxième tour sans toucher au bouton |
| M3 | se taire 10 s | retour à l'accueil |
| M4 | bouton 2 en pleine écoute | arrêt immédiat |

## Implémentation
`assistant_wake_task()` boucle sur `ecoute_pcm()` ; fenêtre de reprise
`CONV_FENETRE_MS` (10 s), `CONV_MAX_TOURS` (10). Chaque tour écrit
`/sdcard/voix.wav` et appelle `voix_tour()`.

## Etat
- [x] code + flash
- [x] tests unitaires 12/12
- [x] tests d'intégration
- [ ] validation manuelle par Taz (M1–M4)
