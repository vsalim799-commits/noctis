# Conception du niveau — Vallée de Hell Creek (tranche verticale)

> Niveau : `hell_creek_valley` · environnement `hell_creek` · graine canonique **7** · profil par défaut **standard** (modifiable)
> Données : [`Data/Levels/hell_creek_valley.json`](../Data/Levels/hell_creek_valley.json) (schéma `noctis.level/1`, fichier **généré**)
> Générateur : `noctis_sim export-level` ([`Tools/Cli/LevelExport.cpp`](../Tools/Cli/LevelExport.cpp))
> Unreal : [`Content/Python/build_level.py`](../Content/Python/build_level.py) (UE 5.4+, World Partition, PCG)
> Cartes : `Output/level/overview.png` (carte annotée), `Output/level/constraints.png`, `Output/level/mask_*.png`

Ce document décrit le niveau jouable de la tranche verticale : ce que le joueur y fait, comment la vallée est
construite, où se trouvent les points d'intérêt (POI) et **pourquoi**, quels événements la simulation y produit, quelles
règles de sécurité s'appliquent, et comment tout cela reste scientifiquement honnête. Les positions et les chiffres cités
viennent de l'export de la graine 7 ; une autre graine produit une autre vallée avec les mêmes règles.

---

## 1. Fantasme du joueur

Le joueur est une **chercheuse ou un chercheur de terrain en paléo-écologie**, physiquement présent au Maastrichtien
terminal (≈ 66,5 Ma), **seul sur le terrain** et relié par radio au camp de base. Le cadre narratif du voyage est hors du
périmètre de ce document.

- Jamais chasseur : aucune arme, aucune capture, aucun appât, aucune manipulation d'œufs. La seule « prise » est une
  donnée : une observation, une photo, un enregistrement, une mesure d'empreintes, un spécimen fossile.
- Ses outils sont ceux d'un écologue de terrain d'aujourd'hui (jumelles télémétriques, téléobjectifs, micros, enregistreurs
  autonomes, pièges photographiques, drone, véhicule-laboratoire 6×6) et ceux d'un paléontologue (fouille, identification,
  analyse de pistes, examen de carcasses).
- Sa satisfaction vient de **comprendre** : passer d'une impression (« ils ont l'air nerveux près de l'eau ») à un résultat
  chiffré, avec ses biais (« la vigilance augmente quand je suis à moins de 150 m — mais j'ai peu de sessions de nuit »).
- La voix radio du camp (une coordinatrice qui ne quitte pas le camp) gère la logistique et la sécurité ; elle ne donne
  **jamais** d'objectif, de position d'animal ou de réponse scientifique.

## 2. Boucle centrale

**Observer → enregistrer → analyser → formuler une hypothèse → rapporter**, avec les systèmes du cœur :

| Étape | Ce que fait le joueur | Systèmes (`Source/NoctisCore/Public/Noctis/Research/`) |
|---|---|---|
| Observer | sessions focales ou par balayage depuis un point de vue, un affût, le véhicule, le drone | `ObservationSystem` (`startSession`, `observeNow`, `takePhoto`), `FieldMap::reveal` |
| Enregistrer | micro canon / parabolique, enregistreurs autonomes, réseau de micros, pièges photo | `startRecording`, `deployMicArray`, `placeSensor` |
| Analyser | spectrogrammes, détection et classement des appels, localisation, pistes, carcasses, fossiles | `analyzeRecording`, `clusterCalls`, `localizeEvent`, `measureTrackway`, `examineCarcass`, `identifySpecimen` |
| Hypothèse | prédiction testable sur **ses propres données** | `HypothesisSystem` (comparaison, réponse à un événement, corrélation, association) |
| Rapporter | fin d'expédition : rapport généré à partir des données, avec niveaux de confiance | `ExpeditionSystem`, `generateReport` |

La carte du joueur (`FieldMap`) ne contient **que** ce que ses données ont révélé : terrain réellement vu, effort
d'observation, eau, nids, carcasses, affleurements, pistes, observations, dangers. Les POI de ce document sont des données
de conception : le jeu ne les affiche pas (seul le camp de base, `visible_at_start: true`, est connu au départ).

## 3. Les deux premières heures (accueil sans marqueur de quête)

Le départ est **biaisé vers les heures et la saison actives** : jour 140 (dans la saison humide supposée, jours 100–200, et
dans la fenêtre de reproduction simulée de plusieurs espèces), **05:00**, à l'aube. Aucune scène n'est scriptée : ce qui
suit est le parcours que la géographie rend naturel, avec les indices diégétiques du **carnet de terrain** (seul canal
d'aide). Les temps de marche viennent de `walk_time_from_basecamp_min` (réseau à pied, 1,25 m/s sur sol plat).

| Temps réel | Lieu (graine 7) | Ce qui se passe | Indice du carnet |
|---|---|---|---|
| 0–10 min | **BC01** camp de base (3624, 4224) | contrôle radio, choix du matériel (le carnet suggère jumelles + micro omni), la carte est vide | « L'aube est le meilleur moment pour écouter. Monter au-dessus de la berge ? » |
| 10–35 min | **OV01** point de vue sur le bluff de l'affleurement E (9,5 min à pied) | premier balayage : le gué FD04 et les affleurements EX04 et EX06 de la rive opposée sont en vue ; premier enregistrement du chœur d'aube (vocalisations = **hypothèses**, spectrogramme et sous-titres) ; l'affût **BL01** (tente portable à 70 m du gué FD04) est juste à côté | « Noter l'heure, la distance, le nombre d'individus. » |
| 35–60 min | **EX05** affleurement E (au pied du point de vue) | la berge concave expose des couches plus anciennes ; un os affleure ; premier spécimen | « Ces couches sont plus anciennes que le sol où je marche. De combien ? Je ne peux pas le dire ici. » |
| 60–90 min | **FD01** gué de la rivière (17,7 min du camp) et l'affût **BL04** (13,5 min, à 130 m du gué) | empreintes fraîches dans la boue des berges : mesurer une piste ; poser un piège photo sur l'approche ; constater que la rivière (2,3 m au gué) est infranchissable à pied | « Longueur de pas, longueur du pied : quelle vitesse ? » |
| 90–120 min | retour au véhicule ; **WA03** abreuvoir (15 min de route) ou **BS01** marais arrière | première hypothèse proposée **à partir des données du joueur** (ex. « ils boivent surtout tôt le matin »), planification de l'expédition 1 | « Combien de sessions faudrait-il pour le savoir ? » |

Principes : la carte se remplit **au fil des observations** (viewsheds réels de `FieldMap::reveal`) ; le carnet ne nomme
jamais une destination, il pose des questions ; un joueur qui part dans une autre direction trouve d'autres choses, parce
que les animaux et les événements sont émergents. Prédateurs : l'export a relevé de l'activité de chasse **sur la rive du
camp**, à 500–750 m du camp pendant les premières heures (`designer.predator_activity_warmup`) ; le véhicule-refuge garé au
camp est donc une sécurité réelle dès le début.

## 4. Structure de la vallée (générateur)

Lu dans `Source/NoctisCore/Private/World/TerrainGenerator.cpp` (générateur `meandering_floodplain`) et
`Data/Environments/hell_creek.json` :

- **Carte** 8 192 × 8 192 m, champ de hauteurs 2 017 × 2 017 (pas 4,06 m), relief 16,1–46,7 m pour la graine 7.
  Plaine alluviale et côtière de très faible pente (0,0004 m/m vers l'est) : cadre FORTEMENT ÉTAYÉ
  (`fastovsky2016_hellcreek_primer`, `johnson2002_hellcreek_megaflora`, `wilf2003_kpg_climate`).
- **Rivière à méandres** : tracé « sine-generated » (`langbein1966_meanders`), chenal de 55 m de large et 4 m de
  profondeur de plein bord, longueur d'onde des méandres ≈ 12 largeurs, sinuosité ≈ 2. Elle traverse **toute** la carte
  d'ouest en est. Profil en travers asymétrique : mouille contre la berge concave, **barres de méandre** sableuses côté
  convexe.
- **Levées** naturelles (≈ 1,3 m, décroissance sur ~130 m) bordant le chenal ; **marais arrière** (backswamps) en plaine
  distale basse (jusqu'à −0,9 m) ; **crevasse splays** (lobes sableux) ; **terrasses** montant vers les plateaux boisés
  au nord et au sud (l'habitat « upland_conifer_forest » est SPÉCULATIF pour la Hell Creek).
- **Bluffs / berges concaves** sur les boucles les plus serrées : la berge est surélevée de ~3,5 m et l'érosion latérale
  y expose une coupe ; ce sont les **sites d'affleurement** (7 pour la graine 7, coupes de ~6,3 m au-dessus de l'eau).
- **Ruisseau affluent** (9 m × 1,4 m, non pérenne) descendant de la marge nord jusqu'à la rivière (x ≈ 5 800 m).
- **Bras morts** : le générateur en cherche sur un méandre abandonné ; **aucun n'a été retenu pour la graine 7** (le
  critère de dégagement rejette les arcs trop proches du chenal actif). Le niveau utilise donc deux **marais arrière**
  (BS01, BS02) comme milieux d'eau calme. Voir § 13.

Conséquence majeure pour le jeu : la rivière principale coupe la vallée en deux. **Rive du camp** (sud, `camp_bank`) et
**rive opposée** (nord, `far_bank`, avec le ruisseau) forment deux composantes du réseau à pied (`summary.foot_components
= 2`).

## 5. Points d'intérêt

Tous les POI sont choisis par algorithme sur le monde de `run` (même graine, même `SimConfig`) après un **préchauffage de
6 h** de la simulation vivante (présence des animaux, événements « boire », vocalisations, chasses, pontes). Chaque POI du
JSON porte : `type`, `position_m` et `ue_location_cm`, `radius_m`, `rationale_fr` (justification chiffrée),
`confidence` + `confidence_subject_fr` (ce que le niveau de confiance qualifie), `placement` (d'où vient la position :
`generated_terrain`, `simulation_state`, `model_prediction`, `game_design`), `links` (réseau à pied), `metrics`
(rive, classe de crue, accès véhicule, couverture radio…).

| Type | Algorithme | Graine 7 | Confiance (sujet) |
|---|---|---|---|
| `basecamp` | choix de `WorldSimulation` : sol sec peu pentu à ~250 m de l'eau, près du centre | BC01 (3624, 4224) | sans objet (installation de jeu) |
| `flood_refuge` | terrain praticable en véhicule le plus proche du camp que le niveau maximal simulé (1,6 × plein bord) n'atteint jamais, à > 250 m du chenal | FR01 (3888, 1936), 17 min de route | sans objet (règle de sécurité) |
| `exposure_site` | sites de `PaleontologySystem` sur les berges concaves (bluffs) | EX01–EX07 | FORTEMENT ÉTAYÉ (berge exposant des alluvions plus anciennes) ; contenu fossile SPÉCULATIF |
| `backswamp` | plus grandes composantes connexes d'habitat « marais arrière », point le plus humide, une par rive | BS01 (2 km²), BS02 (1,2 km²) | FORTEMENT ÉTAYÉ (dépôts d'étangs et de marais) |
| `oxbow_lake` | méandres abandonnés du générateur | aucun pour la graine 7 | FORTEMENT ÉTAYÉ |
| `ford` | sections transversales notées : profondeur max, berges, fermeté du fond, usure de piste, corridors prédits, courbure (inflexions) ; 4 sur la rivière, 2 sur le ruisseau | FD01–FD06 | INFÉRÉ (lieu de traversée prédit par le modèle) |
| `water_access` | sites d'abreuvement de la simulation classés par événements « boire », présence d'herbivores, habitat ; 2 max par rive | WA01–WA03 | INFÉRÉ |
| `overlook` | **couverture maximale gloutonne** d'un champ de vision pondéré par l'activité animale (horizon du relief, canopée dense opaque, feuillage atténuant), 120 hauteurs candidates, 5 retenues espacées de ≥ 900 m | OV01–OV05 (≈ 0,26–0,46 km² visibles) | sans objet (choix méthodologique) |
| `blind` | affût à 70–240 m des cibles les plus actives (gués, abreuvoirs, marais), ligne de visée dégagée à 1,2 m, sous le vent dominant, végétation ou tente-affût portable ; au moins 2 sur la rive du camp | BL01–BL04 | sans objet |
| `nesting_area` | modèle d'habitat par espèce : habitat préféré, sol hors crue de plein bord, substrat selon le type de nid, pente, distance à l'eau, faible activité de prédateurs, pontes simulées | NA01–NA08 | **SPÉCULATIF**, affichage soumis au réglage de profil `nesting_grounds` |
| `carcass_site` | carcasses présentes à l'export (état dynamique) | CA01 | ÉTABLI (décomposition et charognage, taphonomie actualiste) |
| `camera_trap` | corridors prédits et usure observée, près des gués et abreuvoirs, 2 max par rive | CT01–CT04 | sans objet |
| `audio_recorder` | activité et vocalisations dans 700 m, pénalité de bruit d'eau courante (masquage), espacement 1,6 km | AR01–AR03 | sans objet ; les cris enregistrés restent des HYPOTHÈSES |
| `mic_array` | sol plat (< 6°) et ouvert, calme (< 30 dB de bruit d'eau), hors zone inondable, au centre de l'activité | MA01 | sans objet |

Lecture rapide de la graine 7 :

- **Rive du camp** : BC01, FR01, EX01/EX03/EX05/EX07, BS01, FD01/FD02 (accès par la berge sud), WA03, OV01/OV03/OV05,
  BL01/BL02/BL04, NA05, CT03/CT04, AR02/AR03, MA01.
- **Rive opposée** : EX02/EX04/EX06, BS02, FD03/FD04 (accès par la berge nord), gués du ruisseau FD05/FD06, WA01/WA02,
  OV02/OV04, BL03, sept des huit aires de nidification, la carcasse CA01, CT01/CT02, AR01.
- Les points de vue sont tous sur les bluffs : dans une plaine boisée, ce sont les seules hauteurs qui dominent le chenal ;
  la canopée limite les vues à ~0,3–0,5 km² chacune. C'est un résultat, pas un choix esthétique : l'observation de longue
  portée passe par le drone.

## 6. Corridors, itinéraires, véhicule, radio

- **Corridors prédits** (`trails`, 19 pour la graine 7) : chemins de moindre coût pour un grand herbivore (pente, eau,
  couvert, enfoncement Bekker, usure existante) depuis 10 « points chauds » d'alimentation vers l'eau la plus proche
  (`drinking_route`), et entre points chauds (arbre couvrant minimal, `forage_link`). `evidence` vaut `observed_wear` quand
  l'usure de végétation simulée confirme le tracé, sinon `predicted_only`. Trois corridors traversent la rivière : les
  animaux, eux, peuvent la passer. Confiance INFÉRÉE (réutilisation de pistes par les grands herbivores actuels), tracé =
  prédiction du modèle.
- **Réseau à pied** (`connections`) : liaisons entre POI voisins + arbre couvrant minimal par composante ; coût de marche
  (pente, eau ≤ 0,7 m, couvert, enfoncement) ; `time_min`, pente et profondeur d'eau maximales.
- **Véhicule** : praticable si eau ≤ 1,0 m (profondeur de gué de `equipment.json`), pente ≤ min(25°, arctan(μ − résistance
  au roulement)), enfoncement < 70 % du seuil d'enlisement de `VehicleSystem`, canopée ≤ 0,55, hors berges abruptes.
  47 % de la carte est praticable **et** relié au camp. Chaque POI a `rover_access` : point de dépose, temps de route,
  marche restante **sur la même rive** (22 POI sur 46 accessibles).
- **Radio** : antenne de 10 m au camp, ligne de vue sur le relief seul (la végétation n'est pas modélisée pour la radio —
  simplification) ; 96 % de la carte couverte, ombres derrière les bluffs et sur les plateaux (`mask_radio_coverage.png`).
- **Rivière** (`river_crossing`) : en saison humide (niveau 2,36 m pour 4 m de plein bord) aucun gué de la rivière n'est
  praticable, ni à pied ni en véhicule. Chaque gué donne le niveau et la **fraction du débit moyen** sous lesquels il le
  devient (relation de géométrie hydraulique du `WaterSystem`, `leopold1953_hydraulic_geometry`) : ≈ 5,4–6,2 % du débit moyen
  à pied, ≈ 12,6–13,8 % en véhicule. Les gués du ruisseau (FD05, FD06), sur la rive opposée, sont praticables dès maintenant. La géographie du jeu change donc avec la saison. **Décision ouverte** : un packraft dans
  l'équipement pour traverser aux gués (absent de `Data/Equipment/equipment.json`) ; le véhicule reste alors sur la rive du
  camp et la rive opposée devient la zone « sans refuge ».

## 7. Événements émergents

Rien n'est scripté (`scripted: false`) : la simulation produit les événements, le niveau sait seulement **où** ils sont
probables (`events[].likely_poi_ids`). Chaque indice sonore a un équivalent visuel.

| Événement | Moteur (simulation) | Lieux probables | Réponse attendue | Confiance |
|---|---|---|---|---|
| Traversée de troupeau | groupes, `Drink`, profondeur aux gués | gués, abreuvoirs | observer, filmer, mesurer la piste laissée | INFÉRÉ |
| Approche d'un prédateur | `HuntSearch/Stalk/Chase`, `ResearcherNoticed`, `alarm_calls` (profil) | abreuvoirs, gués, carcasses | se mettre à couvert, rejoindre le véhicule | SPÉCULATIF (cris d'alarme, réactions) |
| Orage | `WeatherSystem` (probabilité d'orage par jour), foudre et tonnerre | points de vue (exposés) | quitter les hauteurs, véhicule | INFÉRÉ |
| Crue | débit > plein bord, inondation par connexité | gués, marais, repli FR01 | rejoindre les terrasses ; après : carcasses enfouies, pistes effacées | FORTEMENT ÉTAYÉ (crues répétées dans les dépôts) ; fréquence non quantifiée |
| Incendie (rare) | `VegetationSystem::updateFire`, allumage par la foudre si sécheresse | — | s'éloigner contre le vent, eau ou véhicule | INFÉRÉ (`bamforth2014_frenchman_fire`) ; fréquence inconnue |
| Brouillard matinal | probabilité de brouillard de l'environnement | marais, stations d'écoute | privilégier l'écoute | SPÉCULATIF |
| Nouvelle carcasse | `CarcassSystem` | carcasses | examen à distance de sécurité | ÉTABLI |
| Activité de nidification | `NestSystem` (selon profil) | aires de nidification | observer de loin, ne jamais manipuler | SPÉCULATIF |

## 8. Risques et sécurité

- **Vulnérabilité** : le joueur est petit, lent et seul. Les animaux le perçoivent (vue, ouïe, odorat) et réagissent
  (`ResearcherNoticed`, `ResearcherWarned`, `ResearcherAttacked`).
- **Blessure et évacuation, pas de mort définitive** : santé graduée (`ResearcherSystem::applyAttack`) ; sous 0,35 :
  déplacements ralentis, instruments inutilisables, appel d'évacuation ; à 0 : évacuation vers le camp, expédition en cours
  « abandonnée », données déjà enregistrées conservées, convalescence. Hors couverture radio, il faut d'abord rejoindre une
  zone couverte ou activer la balise du véhicule.
- **Le véhicule est un refuge**, limité au terrain praticable (§ 6). Il perturbe la faune : moteur électrique ~52 dB à 1 m,
  générateur ~80 dB à 1 m (la différence de 28 dB se mesure en jeu par la réaction des animaux, `VehicleNoticed`).
- **Orage** : les bluffs-points de vue deviennent dangereux (règle de sécurité moderne).
- **Crue** : la plaine entière est inondable (classes `frequent`, `occasional`, `extreme_only`, `never` dans `metrics.
  flood_class`) ; **le camp de base lui-même est en zone de crue fréquente** : FR01 est le point de repli.
- **Distance de tolérance** : les affûts sont placés à 70–240 m des cibles ; la vraie distance de fuite des espèces
  simulées est une **question de recherche** du jeu (mission M02), pas une donnée.

## 9. Expéditions et objectifs de recherche

Les expéditions sont ouvertes (`ExpeditionSystem`) : question, zone, durée, équipement, hypothèses, cibles de données.
Le JSON propose des **gabarits** (`missions`) liés au système d'hypothèses (`Hypothesis.h` : types `comparison`,
`event_response`, `correlation`, `association` ; mesures `measureKey`) :

| Id | Question | POI recommandés | Hypothèse type |
|---|---|---|---|
| M01 | Premier relevé : qui fréquente les abords du camp, où et quand boit-on ? | BC01, OV01, WA03, FD01 | aucune imposée (accueil) |
| M02 | La vigilance des Edmontosaurus augmente-t-elle sous 150 m ? | affûts | `comparison` vigilant_fraction / `observer_within` 150 m |
| M03 | Le drone dérange-t-il ? | point de vue, abreuvoir | `comparison` vigilant_fraction / `drone_airborne` |
| M04 | Après un type d'appel, le groupe se resserre-t-il ? | stations d'écoute | `event_response` group_spacing après `call_cluster` (cris = hypothèses) |
| M05 | Le taux d'appels change-t-il la nuit ? | réseau de micros, marais | `comparison` call_rate / `night` |
| M06 | La formule d'Alexander estime-t-elle bien des vitesses filmées ? | gués | calibration de pistes (`alexander1976_dinosaur_speeds`) |
| M07 | Que contiennent les couches plus anciennes des berges ? | affleurements | fouille, identification, météorisation |
| M08 | Depuis quand cet animal est-il mort, qui l'a mangé ? | carcasses | examen (stade, degrés-jours, morsures) |
| M09 | Les adultes reviennent-ils au même endroit en saison de reproduction ? | aires de nidification | observations répétées (seulement si `nesting_grounds`) |

Le système d'hypothèses ne dit jamais « vrai » : il dit si **les données du joueur** soutiennent la prédiction, avec quelle
force, et quels biais pèsent (distance, drone, heure, pseudo-réplication, taille d'échantillon).

## 10. Rythme, durée du jour et durée d'expédition

- **Temps de jeu 1:1** pendant l'observation. Vitesses, taux d'appels par minute, distances de fuite et budgets d'activité
  sont mesurés par le joueur et testés : comprimer le temps fausserait ses données. Une journée simulée dure donc 24 h de
  simulation ; en pratique le joueur en vit 2–3 h réelles grâce aux attentes.
- **Accéléré « time-lapse »** plafonné à **×8** (`FullDetailMaxTimeScale` : les animaux restent simulés en détail),
  signalé à l'écran et **exclu** des mesures de comportement.
- **Saut ×360** (simulation grossière) pour les nuits et les pluies, uniquement au camp ou dans le véhicule.
- **Durée du jour simulé** : 24 h et 365 jours (hypothèse de jeu de `Data/Science/sim_constants.json`). Le jour était un
  peu plus court au Crétacé supérieur ; la valeur pour la Hell Creek n'est pas établie dans le projet — **Information
  insuffisante — recherche nécessaire.** Piste ajoutée à la bibliographie : `dewinter2020_rudist_daily` (non vérifiée).
- **Expédition par défaut : 3 jours** (min 1, max 7). Trois jours donnent au moins deux aubes et deux crépuscules pour
  répliquer les sessions, une nuit d'écoute complète, et une logistique crédible (batterie du véhicule 200 kWh + générateur,
  batteries de drone et de capteurs rechargées au camp). Au-delà de 7 jours, météo et crues dominent et la session de jeu
  devient trop longue. La tranche verticale tient en **1 jour**.
- Rythme d'une journée type : aube (écoute, comptages) → matinée (pistes, gués, affûts) → milieu de journée (fossiles,
  laboratoire mobile, analyse) → crépuscule (affûts, drone) → nuit (enregistreurs, réseau de micros, attente au camp).

## 11. Accessibilité

- Tout indice acoustique a un **équivalent visuel** : spectrogramme en direct, indicateur de direction, sous-titres
  descriptifs ; les vocalisations portent toujours l'étiquette **« hypothèse »** (« hypothesis » en anglais).
- Icônes de carte distinguées par la **forme** autant que par la couleur (triangle, losange, carré, anneau, croix).
- Accueil par le carnet uniquement, sans marqueur ; la carte est construite par le joueur.
- Langues de l'interface : **FR / EN** (noms des POI et des gabarits fournis dans les deux langues).

## 12. Honnêteté scientifique

- **Deux étiquettes distinctes** par élément : `confidence` dit ce qu'on sait de l'existence ou de la nature de l'élément
  au Maastrichtien (avec `confidence_subject_fr`) ; `placement` dit d'où vient **sa position** (terrain généré, état de la
  simulation, prédiction du modèle, choix de jeu). Un affleurement est FORTEMENT ÉTAYÉ comme objet géologique, alors que son
  contenu fossile est SPÉCULATIF.
- **Nidification** : les zones sont des prédictions d'un modèle d'habitat, marquées SPÉCULATIF, affichées seulement si le
  profil active `nesting_grounds` (activé dans `standard` et `speculative`, désactivé dans `conservative`). Le type de nid
  utilisé et la confiance de la fiche espèce sont rappelés (`nest_type_evidence`). Aucun site de nidification n'est connu
  pour ces espèces dans la Hell Creek ; des restes de jeunes Edmontosaurus de taille de nidification y sont signalés
  (`wosik2017_edmontosaurus_nestling`) et des nids d'hadrosaures sont connus ailleurs (`horner1979_maiasaura_nest`).
- **Vocalisations** : le jeu ne prétend jamais savoir ce qu'un cri « signifiait » ni comment il sonnait exactement ; les
  sons sont des hypothèses de reconstitution (lecture « bouche fermée » par défaut, `riede2016_closed_mouth_vocalization`,
  `clarke2016_vegavis_syrinx`).
- **Âge des affleurements** : âge relatif sûr (plus ancien que la surface), âge absolu non contraint — Information
  insuffisante — recherche nécessaire.
- **Corridors et gués** : prédictions d'un modèle de coûts sur un monde reconstitué, présentées comme telles.
- Les règles de sécurité (foudre, véhicule) sont des règles modernes de jeu, sans prétention paléontologique.
- Aucun POI n'est placé « pour le spectacle » : chaque position a une justification algorithmique écrite dans le JSON.

## 13. Format `Data/Levels` (`noctis.level/1`)

| Clé | Contenu |
|---|---|
| `id`, `name`, `environment`, `reconstruction_version` | identité ; noms FR/EN |
| `generation` | graine, profil, échelle de population, heures de préchauffage, générateur, heure d'export, comptes d'événements, commande |
| `frame` | repère du cœur (x est, y nord, m) et correspondance Unreal (`X = x·100`, `Y = −y·100`, `Z = z·100`, lacet = −cap), transformation du paysage |
| `playable_area` | toute la carte de `run` ; limites naturelles signalées par le carnet |
| `time` | jour simulé, échelles de temps et justification, départ, durée d'expédition |
| `river_crossing` | gués de la rivière, praticabilité actuelle, proposition d'équipement |
| `rules` | rôle du joueur, blessure/évacuation, véhicule (refuge, gué, bruit), marche, orage, drone |
| `masks` | dossier et fichiers PNG 256 × 256 (cellules de 32 m, ligne 0 = nord) |
| `pois[]` | voir § 5 |
| `trails[]` | corridors prédits (points `[x, y, z]`), usure observée, preuve, confiance |
| `connections[]` | réseau à pied entre POI (longueur, temps, pente et eau max, tracé) |
| `designer` | points chauds d'alimentation et activité de prédateurs du préchauffage — jamais montrés au joueur |
| `events[]`, `missions[]` | événements émergents et gabarits d'expédition |
| `world_partition`, `accessibility`, `summary` | réglages Unreal proposés, accessibilité, compteurs |

Le fichier est **généré** : on le régénère, on ne l'édite pas (`noctis_sim export-level`). Options : `--seed`, `--profile`,
`--env`, `--scale`, `--warmup-hours` (6 par défaut), `--json`, `--out` (défaut `Output/level`). Durée : ~2–4 min, dont le
préchauffage.

## 14. Unreal Engine 5.4+

Ordre : `setup_project.py` (carte World Partition, ciel, brouillard, lumières, gestionnaire de créatures) puis
`build_level.py` (contenu du niveau). Entrées : `noctis_sim export-terrain --out Build/Terrain` et `noctis_sim export-level`.
Le script fonctionne aussi hors Unreal (`--dry-run`) pour vérifier les entrées et le plan.

**Import du paysage** (`build_level.py`, d'après `terrain.json` d'`export-terrain`) :

- 2 017 échantillons → 2 016 quads = 16 × 16 composants de 2 × 2 sections de 63 quads (126 quads, ~512 m par composant).
- Échelle XY = 8 192 / 2 016 × 100 = **406,349 cm**.
- Unreal : `Z = Location.Z + Scale.Z × (v − 32768) / 128` (cm). L'export normalise `v = (h − min)/(max − min) × 65535`, donc
  **Scale.Z = (max − min) × 100 / 512** = 5,98552 et **Location.Z = (min + (max − min)/2) × 100** = 3 137,3 cm pour la
  graine 7 ; précision verticale (max − min)/65535 ≈ 0,47 mm.
- Ligne 0 de l'image = bord **nord** = Y Unreal le plus négatif : **Location.XY = (0, −819 200 cm)** ; le paysage couvre
  X ∈ [0 ; 819 200], Y ∈ [−819 200 ; 0], comme `FNoctisSpace::ToUE`.
- UE 5.4 n'expose pas de création de paysage depuis un fichier en Python stable : le script utilise un utilitaire de projet
  (`NoctisEditorLibrary.import_landscape_from_heightmap`, à ajouter au module `NoctisEditor`) s'il existe, sinon il
  imprime les réglages exacts de l'import manuel, puis **vérifie et corrige** la transformation au passage suivant. Il
  recoupe ses valeurs avec `NoctisEditorLibrary.compute_terrain_info()` (même calcul en C++ via `FNoctisSpace`).

**World Partition** : grille d'exécution `MainGrid` de 256 m (25 600 cm), chargement à ~1 km (102 400 cm) — couvre le rayon
des acteurs créatures (450 m) et les affûts ; au-delà, HLOD du paysage et de la végétation pour les vues des points de vue.
Proxys de streaming du paysage : 1 composant. Couches de données : `DL_POI_Markers` (éditeur), `DL_Nesting_Speculative`
(runtime, suit `nesting_grounds`), `DL_Basecamp`.

**PCG** (grille de partition 256 m, génération à la demande dans l'éditeur) :

- `PCG_Noctis_Foliage` — échantillonnage de surface du paysage, une branche par type fonctionnel végétal, densité lue dans
  `T_Foliage<Pft>` (masques `foliage_*.png` du cœur), élagage par rayon, exclusions (chenal, eau, clairières des POI),
  transformations aléatoires, maillages provisoires.
- `PCG_Noctis_Trails` — splines des corridors (`noctis.trail`), largeur selon l'usure.
- `PCG_Noctis_Exposures` — autour des affleurements, pentes > 30° : strates et blocs provisoires orientés vers le chenal.
- `PCG_Noctis_RiverDebris` — barres sableuses et laisses de crue (`T_WeightSandbar`, `T_MaskFloodRisk`).

Les marqueurs de POI sont des maillages de base **cachés en jeu** et réservés à l'éditeur ; à l'exécution, le jeu lit le JSON.
Les appels d'API non vérifiables sans moteur sont marqués `# VERIFY` dans le script ; chacun a un repli journalisé.

## 15. Limites et décisions ouvertes

1. **Bras morts absents pour la graine 7** : le critère de dégagement du générateur est trop strict pour cette vallée ; à
   corriger dans `TerrainGenerator.cpp` (tâche séparée — changer le générateur change la vallée canonique pour tout le
   projet). En attendant, les marais arrière jouent ce rôle.
2. **Traversée de la rivière** : packraft à décider (équipement), sinon la rive opposée n'est accessible qu'en basses eaux.
3. **Durée du jour** : 24 h faute de source vérifiée (piste `dewinter2020_rudist_daily`, non vérifiée).
4. **Radio** : propagation en ligne de vue sur le relief seul (pas d'atténuation par la forêt).
5. Les poids et seuils des algorithmes (coûts de marche, seuils de praticabilité, distances d'affût, pondérations du
   modèle de nidification) sont des **hypothèses de jeu** documentées dans `LevelExport.cpp`, pas des données.
6. Le préchauffage de 6 h donne encore peu d'usure de pistes (`predicted_only` majoritaire) ; un préchauffage plus long
   (`--warmup-hours 48`) renforcerait l'évidence des corridors au prix de ~20 min de calcul.
