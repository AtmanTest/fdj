# Moteur affectif - premier design (v1)

But : que le robot EPROUVE un etat, au lieu de choisir une etiquette dans une liste.
Aujourd'hui le LLM ecrit `{"emotion":"compassion"}` : c'est une declaration, pas une
emotion. Un personnage qui a de vraies emotions doit avoir un etat interne continu,
qui evolue seul, avec une memoire, des seuils et des durees - et dont l'expression
(visage, mots, voix) est une LECTURE de cet etat.

Codes : `affect.h` (108 octets d'etat), `affect.c` (moteur, C portable sans allocation),
`sim.c` (banc d'essai hote), `sim-resultats.txt` (sortie des 8 scenarios).

---

## 1. Le modele scientifique (deux theories qui se completent)

**Theorie de l'evaluation (Scherer, "component process")** : une emotion nait de
l'evaluation d'un evenement sur quelques axes, pas de l'evenement lui-meme. Sept
nombres suffisent :

| axe | sens | plage |
|---|---|---|
| relevance | ca me concerne | 0..1 |
| congruence | bon / mauvais pour mes buts | -1..1 |
| coping | je peux agir / je subis | -1..1 |
| norm | conforme / contraire a mes valeurs | -1..1 |
| social | chaleur ou rejet du lien | -1..1 |
| novelty | inattendu | 0..1 |
| other_distress | l'autre souffre | 0..1 |

**Espace PAD (Mehrabian)** : tout etat affectif est un point (valence, arousal,
dominance). Les emotions sont des prototypes dans cet espace ; l'etat ressenti est
un point qui se deplace, l'expression est le prototype le plus proche. C'est ce qui
donne des etats mixtes et des transitions (colere -> agacement -> rejet) au lieu de
sauts d'etiquette.

## 2. Les equations

Etat : `x = (v,a,d)`, humeur `m`, neuromodulateurs `cort, oxy, da`, charge
allostatique `fatigue`, lien `trust`, besoins `energy/social/satiety`, et deux traces
persistantes `od_s` (empathie) et `soc_s` (chaleur).

**a) impulsion d'evaluation** (l'evenement, applique en un tick)

    gain_pos = clamp(1 + 0.70*oxy - 0.25*fatigue, 0.40, 1.80)
    gain_neg = clamp(1 + 1.20*cort + 1.60*fatigue, 0.40, 3.00) * (1 - 0.45*trust)
    hab_pos  = 1/(1 + 0.60*rep_pos)
    hab_neg  = 1/(1 + 0.35*rep_neg) * (1 + 0.50*cort)      <- sensibilisation

    u_v = R * (0.90*c*gain + 0.35*social - 0.30*other_distress)
    u_a = R * (0.50*|c| + 0.35*|coping| + 0.25*novelty) * (1 + 0.45*cort)
    u_d = R * (0.55*coping + 0.15*norm)   (+0.30*R si norm < -0.5 : injustice)

Les gains portent toute la psychologie du personnage : une meme phrase pese 3x plus
lourd a un robot epuise, 2x moins si le lien est solide, et le dixieme reproche
identique est attenue par l'habituation - sauf si le cortisol monte.

**b) relaxation (Ornstein-Uhlenbeck)** - l'emotion se dissipe, elle ne s'efface pas

    base_v = -0.28*deficit - 0.42*fatigue + 0.15*oxy
    base_a =  0.30*deficit + 0.20*cort - 0.45*fatigue - 0.10*oxy
    base_d = -0.20*deficit + 0.10*(trust-0.5)

    dx/dt = (base - x)/tau + (m - x)/(3*tau) + bruit

    tau_v = 200 s   tau_a = 75 s   tau_d = 420 s   tau_humeur = 1800 s

**c) humeur** : `dm/dt = (x - m)/1800` - moyenne glissante de ce qui a ete vecu.
L'humeur tire l'etat : on ne repart jamais de zero, on repart de la ou on en est.

**d) neuromodulateurs** (increments discrets a chaque evenement, degradation lente)

    cort   += 0.30*max(0,-u_v) + 0.10*max(0,-social)*R      puis /900 s
    oxy    += 0.12*max(0,social)*R + 0.08*od*R              puis /1800 s
    trust  += 0.05*max(0,social)*R + 0.03*(c>0.4) - 0.09*(social<-0.6) - 0.04*(c<-0.6)
    da     += 0.35*R*(0.8*c + 0.2*social)                   puis /120 s

**e) charge allostatique** (fatigue profonde, type "burnout")

    d(fatigue)/dt = (0.60*cort + 0.50*max(0,-v) + 0.30*deficit)/14400
                    - (0.05 + 0.15*max(0,v))/3600

**f) rumination** : si `v < -0.15` et `a < 0.40` sans nouvel evenement, toutes les
90 s l'etat se remet a plonger de `0.06*rum_int`, avec atténuation. C'est ce qui fait
qu'une deception tient toute la soiree au lieu de 3 minutes.

## 3. Du point PAD au visage

Distance ponderee aux prototypes (valence 1.0, arousal 0.55, dominance 0.70),
`score = -d2 / (2*0.38^2)`, plus des biais d'etat : empathie -> compassion,
ocytocine -> tendresse, cortisol -> anxiete/tension, fatigue -> agacement/fatigue.
La douleur physique (`pain`) est un canal separe : sans capteur, le visage de
douleur est interdit (biais -2.5) - sinon toute frustration prend un visage de martyr.

**Hysteresis** : une emotion forte (intensite > 25%) ne cede le visage que si la
suivante depasse le score de +1.2 et apres 2,5 s ; une emotion faible suit l'etat
librement. Sans ca, la dalle e-ink clignote entre deux prototypes voisins.

L'intensite est la distance de l'etat a l'humeur : c'est ce qui module la taille de
la reaction (et plus tard le volume et le debit de la voix).

## 4. Resultats mesures (simulateur hote, gcc -O2)

| scenario | resultat |
|---|---|
| A. 3 echecs + insulte | frustration 29% -> regret 46% -> colere_contenue 29% -> doute, humeur -0.15, lien 0.50->0.31 |
| B. nuit blanche | etat de fond deja negatif (v -0.15, fatigue 0.60) sans aucun evenement |
| C. succes + merci | rire_franc 47% -> bienveillance 31%, lien 0.60 |
| D. mauvaise nouvelle | choc 36% puis compassion 17% pendant 10 min (empathie persistante) |
| E. 20 min de silence | reste proche du neutre, derive spontanee +-0.15 (il vit, il ne simule pas) |
| F. meme echec | lien 0.18 -> frustration 46% / lien 0.88 -> 38% (tampon du lien) |
| G. injustice nette | colere 73% -> agacement -> rejet -> reflexion (trajectoire complete) |
| H. meme remarque | repose : neutre 12% / epuise : bouderie 18% (meme mot, autre emotion) |

Les lignes D et H sont la preuve du point cle : le moteur produit des emotions
qu'aucune table de correspondance "phrase -> emotion" ne donnerait.

## 5. Integration dans le firmware (prochaine etape)

1. `main/affect.{h,c}` + table de prototypes ; etat de 108 octets, tick a 1 Hz
   (cout < 1 ms/s, aucune allocation).
2. Le LLM ne rend plus `emotion` mais un objet d'evaluation :
   `{"eval":{"rel":0.9,"congr":-0.6,"cop":-0.3,"norm":0,"soc":-0.8,"nov":0.2,"od":0}}`
   -> `appraisal_t` -> `affect_tick()`. Le prompt perd la liste des 50 emotions
   (c'est le moteur qui possede le visage) et gagne un vocabulaire d'evaluation.
3. Tache 1 Hz : `affect_tick(&e, NULL, 1.0)` ; si le prototype change, `show_face()`.
   Le reste du firmware suit (page CONSEIL, voix) au lieu de mettre `current_emotion`.
4. Besoins branches sur le pet existant : `faim -> satiety`, `energie -> energy`,
   et `social_need` alimente par l'ecart entre deux interactions.
5. Persistance NVS : blob de `affect_t` toutes les 5 min et a la veille (sinon le
   robot "oublie" qu'il etait vexe - alors qu'un humain, non).
6. Voix : `pitch = 0.90 + 0.25*a`, `debit = 1.0 + 0.30*a - 0.20*fatigue` ; et
   `affect_prompt_line()` injecte dans le prompt "je ressens X, intensite Y%".
7. Diagnostic : `/state` expose `v,a,d,label,intensite,cort,oxy,fatigue,trust` ;
   `/affect?evt=insulte` pour rejouer un scenario a distance.

## 6. Limites connues, a trancher

- Les 7 nombres d'evaluation viennent du LLM : ils sont bruités. Prevoir un lissage
  (moyenne sur 2 tours) et un bornage strict cote firmware.
- Un prototype PAD reste grossier : deux etats eloignes peuvent tomber sur le meme
  visage. Assumer, ou passer a 4 axes (+ "controle" separe de la dominance).
- L'apprentissage n'est pas encore la : ni conditionnement (une heure qui precede
  d'habitude un succes), ni memoire affective par personne. C'est la v2
  (Rescorla-Wagner sur les attentes + attachement par interlocuteur).
- Calibration des 60 constantes : faite a la main ici ; la methode propre est de
  fixer des trajectoires cibles (tableau du §4) et d'ajuster tau et gains.
