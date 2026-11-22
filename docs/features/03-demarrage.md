# 03 — Démarrage sans réseau

## Ce que ça doit faire

La carte doit vivre **sans Wi-Fi** : écran, boutons, rappels et console
démarrent quand même ; et quand la borne revient, la carte se raccroche et
resynchronise l'heure toute seule.

## Le bug corrigé

Au démarrage, le firmware attendait la borne avec `portMAX_DELAY` (attente
infinie). Sans réseau, **rien** ne démarrait : ni le serveur web, ni les boutons,
ni l'écran, ni l'alarme. La carte semblait morte. C'était le « ça se fige quand
on n'a plus de wifi ».

Corrections :

- attente de la borne bornée à **20 s** (`xEventGroupWaitBits(..., 20000 ms)`),
  puis démarrage de tout le reste ;
- l'heure est retentée (`time_sync`) dès que le bit `WIFI_OK_BIT` revient ;
- météo et infos sortent immédiatement sans réseau (sinon un TLS de 20 s bloque
  la tâche écran, donc l'écran et les boutons) ;
- les rappels enregistrés ne sont plus effacés au boot avant la synchro NTP.

## Tests

| Test | Attendu |
|---|---|
| boot avec borne | IP dans les ~20 s, `diag` répond `wifi=1` |
| boot sans borne | la console répond quand même, l'écran s'affiche, les boutons tournent |
| météo sans réseau | page METEO affiche `PAS DE DONNEES` + `WIFI NECESAIRE`, pas de gel |
| retour de la borne | `wifi=1`, météo relue dans la minute |

Vérifié au démarrage de `tools/test_global.py` (section 1). Pour le cas « sans
borne », débrancher la box et refaire la même séquence : la console doit
répondre `diag` en moins de 30 s.
