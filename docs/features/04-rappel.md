# 04 — L'assistant crée des rappels

## Ce que ça doit faire

« Rappelle-moi dans 20 minutes de sortir le linge » doit marcher : l'assistant
comprend, la carte crée un minuteur nommé, et à l'échéance elle carillonne puis
le dit à voix haute.

## Comment c'est fait

Le modèle ne touche **jamais** au matériel. Il ne peut que renvoyer du JSON, et
le firmware valide avant d'agir (`emotion`, `dit`, `cherche`, `souvien`,
`radio`, `volume`, `rappel`).

    "rappel": {"d": 1200, "n": "sortir le linge"}

- durée : 10 s minimum, 7 jours maximum, refusée sinon ;
- refusée aussi si l'horloge n'est pas synchronisée (`time(NULL) < 1.7e9`),
  plutôt que de créer une échéance fausse ;
- un seul rappel par question ;
- la carte garde 8 minuteurs, nom court, sauvegarde NVS ;
- à l'échéance : carillon, puis la voix dit « Rappel : sortir le linge. » ;
- n'importe quel appui coupe la sonnerie.

Points d'entrée réseau : `/rappel?d=600&n=Cuisson`, `/rappel?a=1` (coupe),
`/state` expose `rappel_nb`, `rappel_dernier`, `annonce`.

## Tests

| Test | Attendu |
|---|---|
| phrase « rappelle-moi dans 2 minutes de … » | log `rappel cree : '…' dans 120 s` |
| `diag` après coup | `rappel_nb >= 1` |
| page RAPPELS | le rappel s'affiche avec son temps restant |
| à l'échéance | `timer/alarme N sonne` puis `alarme : Rappel : …` |
| appui bouton pendant la sonnerie | `alarme : coupee au bouton 1` |

Section 4 de `tools/test_global.py` (avec un second essai si le modèle répond à
côté). Vérifié de bout en bout sur la carte : rappel créé, sonnerie et annonce
vocale entendues.
