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

> Les chiffres de ce document viennent de l'export du 16/10/2026. La simulation des créatures évolue (corrections en
> cours dans `Source/NoctisCore/Private/Creatures`) : le préchauffage, donc l'activité, les corridors, les abreuvoirs et
> les capteurs, peuvent bouger d'un export à l'autre. Le JSON fait foi ; les règles ci-dessous ne changent pas.

**Convention de jour** : les jours affichés (horloge du jeu, cartes, documents) sont numérotés à partir de 1. Le départ
est le **jour 141** ; `SimConfig::startDayOfYear` et `time.start_day_of_year` valent 140 (indice en base 0).

---

## 1. Fantasme du joueur

Le joueur est une **chercheuse ou un chercheur de terrain en paléo-écologie**, physiquement présent au Maastrichtien
terminal (≈ 66,5 Ma), **seul sur le terrain** et relié par radio au camp de base. Le cadre narratif du voyage est hors du
périmètre de ce document.

- Jamais chasseur : aucune arme, aucune capture, aucun appât, aucune manipulation d'œufs. La seule « prise » est une
  donnée : une observation, une photo, un enregistrement, une mesure d'empreintes, un spécimen fossile.
- Ses outils sont ceux d'un écologue de terrain d'aujourd'hui (jumelles télémétriques, téléobjectifs, micros, enregistreurs
  autonomes, pièges photographiques, drone, véhicule-laboratoire 6×6, packraft) et ceux d'un paléontologue (fouille,
  identification, analyse de pistes, examen de carcasses).
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

Le départ est **biaisé vers les heures et la saison actives** : jour 141 (dans la saison humide supposée, indices
100–200, et dans la fenêtre de reproduction simulée de plusieurs espèces), **05:00**, à l'aube. Aucune scène n'est
scriptée : ce qui suit est le parcours que la géographie rend naturel, avec les indices diégétiques du **carnet de
terrain** (seul canal d'aide). Les temps de marche viennent de `walk_time_from_basecamp_min` (réseau à pied, 1,25 m/s sur
sol plat). Chaque étape est un **lieu distinct** : le point de vue est à 506 m de l'affleurement le plus proche et à
436 m du point d'eau ou de franchissement le plus proche, l'affût à 130 m de l'abreuvoir qu'il surveille.

| Temps réel | Lieu (graine 7) | Ce qui se passe | Indice du carnet |
|---|---|---|---|
| 0–10 min | **BC01** camp de base (3624, 4224) | contrôle radio, choix du matériel (le carnet suggère jumelles + micro omni), la carte est vide | « L'aube est le meilleur moment pour écouter. D'où voit-on l'eau ? » |
| 10–40 min | **OV01** point de vue sur une crête de levée (15,9 min à pied ; 1,5 m de proéminence, 0,40 km² visibles) | premier balayage : l'abreuvoir WA01 au pied du bluff E, l'abreuvoir WA02 sur la rive opposée et les affleurements EX04–EX06 sont en vue ; premier enregistrement du chœur d'aube (vocalisations = **hypothèses**, spectrogramme et sous-titres) | « Noter l'heure, la distance, le nombre d'individus. » |
| 40–75 min | **BL02** affût surélevé (12,9 min du camp, à 130 m de **WA01**) | les animaux viennent boire : 3 des 5 événements « boire » du préchauffage sont dans le carré de 160 m centré sur WA01 ; à 1,2 m d'œil le sous-bois masque l'eau, d'où la plate-forme (œil à 3,5 m) | « Combien de temps restent-ils à l'eau ? Qui surveille ? » |
| 75–100 min | **EX05** affleurement E, par sa **banquette** (point d'accès à 160 m de l'affût, 3,5 m au-dessus de l'eau) | une fois les animaux partis : coupe de 6,3 m (talus de 2,8 m au-dessus de la banquette, berge du chenal de 3,5 m dessous) ; un os affleure ; premier spécimen | « Ces couches sont plus anciennes que le sol où je marche. De combien ? Je ne peux pas le dire ici. » |
| 100–120 min | **FD02** point de franchissement (17,7 min du camp) puis retour | empreintes fraîches dans la boue des berges : mesurer une piste ; la rivière fait 2,3 m de fond : seul le **packraft** permet de passer (jamais à pied, même en saison sèche) ; planifier l'expédition 1 | « Longueur de pas, longueur du pied : quelle vitesse ? » |

Principes : la carte se remplit **au fil des observations** (viewsheds réels de `FieldMap::reveal`) ; le carnet ne nomme
jamais une destination, il pose des questions ; un joueur qui part dans une autre direction trouve d'autres choses, parce
que les animaux et les événements sont émergents. Prédateurs : l'export a relevé de l'activité de chasse **sur la rive du
camp**, la plus forte à ~2,2 km au sud du camp pendant les premières heures (`designer.predator_activity_warmup`) ;
le véhicule-refuge garé au camp est donc une sécurité réelle dès le début.

## 4. Structure de la vallée (générateur)

Lu dans `Source/NoctisCore/Private/World/TerrainGenerator.cpp` (générateur `meandering_floodplain`) et
`Data/Environments/hell_creek.json` :

- **Carte** 8 192 × 8 192 m, champ de hauteurs 2 017 × 2 017 (pas 4,06 m), relief 16,1–46,7 m pour la graine 7.
  Plaine alluviale et côtière de très faible pente (0,0004 m/m vers l'est) : cadre FORTEMENT ÉTAYÉ
  (`fastovsky2016_hellcreek_primer`, `johnson2002_hellcreek_megaflora`, `wilf2003_kpg_climate`).
- **Rivière à méandres** : tracé « sine-generated » (`langbein1966_meanders`), chenal de 55 m de large et 4 m de
  profondeur de plein bord, longueur d'onde des méandres ≈ 12 largeurs, sinuosité ≈ 2. Elle traverse **toute** la carte
  d'ouest en est. Profil en travers asymétrique : mouille contre la berge concave, **barres de méandre** sableuses côté
  convexe. Le profil ne varie pas le long du chenal : **pas de seuils (radiers) aux inflexions**, la profondeur est
  presque uniforme (≈ 2,3 m au niveau actuel). D'où des « points de franchissement candidats » et non des gués (§ 5, § 6).
- **Levées** naturelles (≈ 1,3 m, décroissance sur ~130 m) bordant le chenal ; **marais arrière** (backswamps) en plaine
  distale basse (jusqu'à −0,9 m) ; **crevasse splays** (lobes sableux) ; **terrasses** montant vers les plateaux boisés
  au nord et au sud (l'habitat « upland_conifer_forest » est SPÉCULATIF pour la Hell Creek).
- **Bluffs / berges concaves** sur les boucles les plus serrées (|courbure| > 0,004 /m) : le générateur y **surélève la
  berge d'une valeur fixe de 3,5 m** et l'érosion latérale y expose une coupe ; ce sont les **sites d'affleurement**
  (7 pour la graine 7, coupes de ~6,3 m au-dessus de l'eau). Le principe (une berge concave érodée expose des dépôts plus
  anciens) est FORTEMENT ÉTAYÉ ; la **hauteur** du bluff est une **hypothèse de jeu** : dans une plaine côtière en
  aggradation de pente 0,0004, une coupe plus haute que plein bord + levée (~5,3 m au-dessus du lit) supposerait une
  terrasse plus ancienne (`metrics.bluff_height_basis`).
- **Ruisseau affluent** (9 m × 1,4 m, non pérenne) descendant de la marge nord jusqu'à la rivière (x ≈ 5 800 m).
- **Bras morts** : le générateur en cherche sur un méandre abandonné ; **aucun n'a été retenu pour la graine 7** (le
  critère de dégagement rejette les arcs trop proches du chenal actif). Le niveau utilise donc deux **marais arrière**
  (BS01, BS02) comme milieux d'eau calme. Voir § 15.

Conséquence majeure pour le jeu : la rivière principale coupe la vallée en deux. **Rive du camp** (sud, `camp_bank`) et
**rive opposée** (nord, `far_bank`, avec le ruisseau) forment deux composantes du réseau à pied (`summary.foot_components
= 2`), réunies par les traversées en packraft (`summary.components_with_packraft = 1` : les 46 POI sont atteignables depuis
le camp).

## 5. Points d'intérêt

Tous les POI sont choisis par algorithme sur le monde de `run` (même graine, même `SimConfig`) après un **préchauffage de
6 h** de la simulation vivante (présence des animaux, événements « boire », vocalisations, chasses, pontes). Chaque POI du
JSON porte : `type`, `position_m` et `ue_location_cm`, `radius_m`, `rationale_fr` (justification chiffrée),
`confidence` + `confidence_subject_fr` (ce que le niveau de confiance qualifie), `placement` (d'où vient la position :
`generated_terrain`, `simulation_state`, `model_prediction`, `game_design`), `links` (réseau d'itinéraires), `metrics`
(rive, classe de crue **calculée à la hauteur propre du POI**, accès véhicule, couverture radio, distance au bord…).
Aucun POI choisi par le niveau n'est à moins de **300 m d'un bord** de la carte (§ 6, limites).

| Type | Algorithme | Graine 7 | Confiance (sujet) |
|---|---|---|---|
| `basecamp` | choix de `WorldSimulation` : sol hors d'eau et peu pentu à ~250 m de l'eau, près du centre. **Le cœur n'a pas de critère de crue** : le camp est à 0,2 m au-dessus du plein bord (classe `frequent`) → règle d'alerte de crue (§ 8) | BC01 (3624, 4224) | sans objet (installation de jeu) |
| `flood_refuge` | terrain praticable en véhicule le plus proche du camp que le niveau maximal simulé (1,6 × plein bord) n'atteint jamais, à > 250 m du chenal | FR01 (3888, 1936), 17 min de route, 55 min à pied | sans objet (règle de sécurité) |
| `exposure_site` | sites de `PaleontologySystem` sur les berges concaves (bluffs) ; **point d'accès** séparé (`metrics.access_point`) : la banquette sèche au pied du talus, rejointe par la levée depuis les flancs du bluff | EX01–EX07 | FORTEMENT ÉTAYÉ pour le principe ; hauteur du bluff = hypothèse de jeu ; contenu fossile SPÉCULATIF |
| `backswamp` | plus grandes composantes connexes d'habitat « marais arrière », point le plus humide à ≥ 300 m du bord, une par rive | BS01, BS02 | FORTEMENT ÉTAYÉ (dépôts d'étangs et de marais) |
| `oxbow_lake` | méandres abandonnés du générateur | aucun pour la graine 7 | FORTEMENT ÉTAYÉ |
| `ford` | **points de franchissement candidats** aux inflexions (\|courbure\| ≤ 0,0015 /m ; 1 400 sections de la rivière rejetées comme sommets de boucle), section mouillée cherchée autour de l'axe (les sections sans eau sont des « lits à sec », jamais classées), notées sur les berges, le fond, l'usure, les corridors ; position au milieu de la section mouillée. 4 sur la rivière (packraft), 2 gués sur le ruisseau | FD01–FD04 (rivière), FD05–FD06 (ruisseau) | INFÉRÉ (lieu de traversée prédit ; pas un haut-fond) |
| `water_access` | sites d'abreuvement de la simulation **dont la station est au bord de l'eau** : sol ≤ 0,5 m au-dessus de l'eau, berge ≤ 20° sur les 10 derniers mètres (883 sites rejetés pour la graine 7) ; classés par activité animale, événements « boire » observés (comptés dans le carré de 160 m centré sur la station, la fenêtre même du terme de score), présence d'herbivores, habitat, bonus de barre de méandre ; 2 max par rive, ≥ 150 m d'un point de franchissement | WA01–WA04 | INFÉRÉ |
| `overlook` | **couverture maximale gloutonne** d'un champ de vision pondéré par l'activité animale (horizon du relief, canopée dense opaque, feuillage atténuant), à **≥ 150 m de tout affleurement**, à **≥ 150 m des abreuvoirs et des franchissements** (un observateur à découvert plus près que la distance d'un affût serait lui-même la perturbation), au moins 0,5 m au-dessus du plein bord (hors crues fréquentes, pas « hors zone inondable ») et en **retrait de 18 m** de toute arête de berge, 5 retenues espacées de ≥ 900 m | OV01–OV05 (≈ 0,33–0,43 km² visibles) | sans objet (choix méthodologique) |
| `blind` | affût à 70–240 m des cibles les plus actives (franchissements, abreuvoirs, marais), ligne de visée dégagée, sous le vent dominant, en **retrait de 15 m** des arêtes de berge et des bluffs, à **≥ 150 m de tout point de vue** (pas de postes d'observation empilés), **sous la ligne d'horizon** vue depuis la cible ; affût au sol (œil 1,2 m) si le sous-bois laisse voir, sinon **affût surélevé** (plate-forme portable, œil 3,5 m) ; au moins 2 sur la rive du camp | BL01–BL04 (4 surélevés pour la graine 7) | sans objet |
| `nesting_area` | modèle d'habitat par espèce : habitat préféré, sol hors crue de plein bord, substrat selon le type de nid, pente, distance à l'eau, faible activité de prédateurs, pontes simulées ; **aptitude ≥ 0,4** (aucune zone forcée) | NA01–NA07 ; Champsosaurus : aucune zone (aptitude 0,18) | **SPÉCULATIF**, affichage soumis au réglage de profil `nesting_grounds` |
| `carcass_site` | carcasses présentes à l'export (état dynamique) | CA01 | ÉTABLI (décomposition et charognage, taphonomie actualiste) |
| `camera_trap` | sur ≥ 1 corridor prédit ; près d'un franchissement ou d'un abreuvoir (≤ 200 m) **du côté atteint sans traverser** (rive du camp pour un franchissement de la rivière) ; magnitude = présence d'herbivores échantillonnée toutes les 10 min ; l'usure de végétation ne compte que comme « passage observé » ; 2 max par rive | CT01–CT04 | sans objet |
| `audio_recorder` | activité et vocalisations dans 700 m, pénalité de bruit d'eau courante (masquage), espacement 1,6 km | AR01–AR03 | sans objet ; les cris enregistrés restent des HYPOTHÈSES |
| `mic_array` | sol plat (< 6°) et ouvert, calme (< 30 dB de bruit d'eau), hors zone inondable, au centre de l'activité | MA01 | sans objet |

Lecture rapide de la graine 7 :

- **Rive du camp** : BC01, FR01, EX01/EX03/EX05/EX07, BS01, FD02, WA01/WA04, OV01/OV03/OV04/OV05, BL01/BL02/BL04,
  NA04, CT02/CT04, AR02/AR03, MA01.
- **Rive opposée** (packraft) : EX02/EX04/EX06, BS02, FD01/FD03/FD04 (franchissements dont le POI est côté nord), gués du
  ruisseau FD05/FD06, WA02/WA03, OV02, BL03, six des sept aires de nidification, la carcasse CA01, CT01/CT03, AR01.
- **Points de vue** : ce sont des crêtes de levée de 1,4–1,5 m de proéminence ; la canopée limite les vues à ~0,3–0,4 km²
  chacune. Les sommets des bluffs ne sont plus retenus : ils sont à moins de 150 m de leur propre affleurement (même
  lieu) et leur hauteur vient d'une hypothèse du générateur. Aucun point de vue n'est à moins de 150 m d'un abreuvoir ou
  d'un franchissement (le plus proche : 436 m pour OV01), ni à moins de 150 m d'un affût. L'observation de longue portée
  passe par le drone.
- **Affûts** : à 1,2 m d'œil, le sous-bois simulé des levées (fougères, arbustes, plantes herbacées) masque les cibles (transmission du feuillage sous le seuil de 0,3) ; les
  quatre affûts retenus sont donc des **plates-formes** (œil 3,5 m, au-dessus du sous-bois et sous la canopée). BL01
  surveille l'abreuvoir WA02 de l'autre côté de la rivière, à 70 m.
- **Nidification** : la preuve du **type** de nid vient de `sim_basis["reproduction.nest_type"]` de la fiche espèce
  (hypothèse de jeu → SPÉCULATIF ; valeur dérivée → au mieux INFÉRÉ, jamais plus fort que la fiche « reproduction ») ;
  `sources` ne contient que les références citées pour ce type de nid. Sans référence (Edmontosaurus, Borealosuchus,
  Struthiomimus) : « Information insuffisante — recherche nécessaire. ». Les sources générales de la fiche
  reproduction sont rangées à part (`metrics.species_reproduction_claim`, qui ne documentent pas le nid).

## 6. Corridors, itinéraires, véhicule, radio, rivière, limites

- **Corridors prédits** (`trails`, 19 pour la graine 7) : chemins de moindre coût pour **un grand herbivore générique de
  3 000 kg** (pente, eau, couvert, enfoncement Bekker, usure existante) depuis 10 « points chauds » d'alimentation vers
  l'eau la plus proche (`drinking_route`), et entre points chauds (arbre couvrant minimal, `forage_link`). `evidence`
  vaut `observed_wear` quand l'usure de végétation simulée confirme le tracé, sinon `predicted_only`. Chaque corridor
  porte `usable_by` / `not_usable_by` : les herbivores agents dont les limites permettent tout le tracé (un non-nageur
  s'arrête au-delà de ~0,95 × sa hauteur de hanche, comme dans `Locomotion.cpp` ; pente de cellule ≤ pente max de
  l'espèce + 5°). La profondeur d'eau est lue **le long du tracé** (tous les 8 m), sans compter l'arrivée à l'eau d'une
  route d'abreuvement ; `crosses_main_river` signifie que le tracé passe d'une rive à l'autre (côté de la rivière évalué
  au point le plus proche de l'axe), pas qu'il touche le chenal. Ankylosaurus (ne nage pas, hanche 1,4 m) ne peut pas
  suivre les corridors qui traversent la rivière : les nageurs, eux, le peuvent. Confiance INFÉRÉE (réutilisation de pistes par les grands herbivores actuels), tracé =
  prédiction du modèle.
- **Réseau d'itinéraires** (`connections`) : liaisons entre POI voisins + arbre couvrant minimal par composante ; coût
  de marche (pente, eau ≤ 0,7 m, couvert, enfoncement) ; `mode` = `foot` ou `foot_packraft` (traversée de la rivière à un
  point de franchissement : vitesse de pagaie 0,8 m/s et ~10 min de mise en œuvre) ; `time_min`, pente et profondeur
  d'eau maximales des parties à pied.
- **Véhicule** : praticable si eau ≤ 1,0 m (profondeur de gué de `equipment.json`), pente ≤ min(25°, arctan(μ − résistance
  au roulement)), enfoncement < 70 % du seuil d'enlisement de `VehicleSystem`, canopée ≤ 0,55, hors berges abruptes.
  47 % de la carte est praticable **et** relié au camp. Chaque POI a `rover_access` : point de dépose, temps de route,
  marche restante (`via_packraft` quand elle passe la rivière) ; 26 POI sur 46. Le véhicule ne franchit **jamais** la
  rivière (aux franchissements, `fordable_rover_*` exige aussi des berges ≤ 25°, la limite du masque de praticabilité).
- **Radio** : antenne de 10 m au camp, ligne de vue sur le relief seul (la végétation n'est pas modélisée pour la radio —
  simplification) ; 96 % de la carte couverte, ombres derrière les bluffs et sur les plateaux (`mask_radio_coverage.png`).
- **Rivière** (`river_crossing.main_river`) : elle n'est **jamais guéable**. `WaterSystem::update` donne à une rivière
  pérenne un apport de `débit moyen × (0,45 + 0,55 × pluie relative)` : sans pluie, le débit tend vers **45 % du débit
  moyen** et ne descend pas plus bas ; le niveau minimal est 0,6 × 4 m × 0,45^0,4 ≈ **1,74 m**, soit encore ~**1,6 m**
  d'eau aux points de franchissement, au-delà des limites à pied (0,7 m) et en véhicule (1,0 m). Chaque franchissement
  porte `never_fordable: true` et « impraticable (débit minimal simulé 45 %) ». **Décision** : le joueur traverse en
  **packraft** (`Data/Equipment/equipment.json`, id `packraft`, 4,5 kg avec pagaie et gilet) aux franchissements dont les
  berges permettent la mise à l'eau (≤ 30°), sous **1,2 m/s** de courant moyen et hors alerte de crue (niveau ≤ 3,2 m) ;
  au niveau actuel (2,36 m, courant 0,67 m/s) les quatre franchissements FD01–FD04 sont utilisables. Le véhicule reste
  sur la rive du camp : la rive opposée est la zone « sans refuge proche ». Le **ruisseau** (non pérenne, débit minimal
  5 % du débit moyen, niveau ≥ 0,25 m) se passe à gué (FD05, FD06 praticables maintenant ; FD06 n'est qu'un filet d'eau de 5 cm, le lit creusé y étant 0,46 m plus haut que le fond de référence du modèle d'eau, `metrics.bed_above_reference_m`) ou à sec (13 sections « lit à
  sec » sur 452 échantillonnées) : c'est **lui**, pas la rivière, dont la praticabilité change avec la saison.
- **Limites** (`playable_area.edges`) : nord et sud = terrasses et plateaux de la marge de vallée. Est et ouest : le bord
  coupe la plaine et la rivière sans obstacle naturel ; c'est la **limite de la zone d'étude**, traitée de façon
  diégétique : la radio prévient à 300 m du bord, le carnet trace la limite sur la carte du joueur, à 100 m la
  coordinatrice demande de faire demi-tour, et un volume bloquant invisible borde la carte (le seul mur invisible du
  niveau). Aucun POI choisi par le niveau n'est à moins de 300 m d'un bord (`metrics.distance_to_map_edge_m`).

## 7. Événements émergents

Rien n'est scripté (`scripted: false`) : la simulation produit les événements, le niveau sait seulement **où** ils sont
probables (`events[].likely_poi_ids`). Chaque indice sonore a un équivalent visuel.

| Événement | Moteur (simulation) | Lieux probables | Réponse attendue | Confiance |
|---|---|---|---|---|
| Traversée de troupeau | groupes, `Drink`, profondeur aux franchissements | franchissements, abreuvoirs | observer, filmer, mesurer la piste laissée | INFÉRÉ |
| Approche d'un prédateur | `HuntSearch/Stalk/Chase`, `ResearcherNoticed`, `alarm_calls` (profil) | abreuvoirs, franchissements, carcasses | se mettre à couvert, rejoindre le véhicule | SPÉCULATIF (cris d'alarme, réactions) |
| Orage | `WeatherSystem` (probabilité d'orage par jour), foudre et tonnerre | points de vue (exposés) | quitter les hauteurs, véhicule | INFÉRÉ |
| Crue | débit > plein bord, inondation par connexité | franchissements, marais, repli FR01 | alerte (§ 8), rejoindre FR01 ; après : carcasses enfouies, pistes effacées | FORTEMENT ÉTAYÉ (crues répétées dans les dépôts) ; fréquence non quantifiée |
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
  générateur ~80 dB à 1 m (la différence de 28 dB se mesure en jeu par la réaction des animaux, `VehicleNoticed`) ; en
  roulant, le bruit pneus / terrain (85 + 35 log10(v / 14 m/s) dB à 1 m, hypothèse de conception) domine dès ~5 m/s.
- **Orage** : les points de vue dégagés deviennent dangereux (règle de sécurité moderne).
- **Crue et camp de base** (`rules.flood_alert`) : le choix du camp par le cœur n'a pas de critère de crue ; BC01 est en
  plaine d'inondation basse (0,2 m au-dessus du plein bord, classe `frequent`) et il est **inondé dès que le niveau
  dépasse 4,18 m** (≈ 4 × le débit moyen). Le niveau garde donc le camp mais ajoute une règle :
  - **alerte de crue** au-dessus de **3,2 m** (0,8 × plein bord, ≈ 2 × le débit moyen) : jauge sur la carte, sous-titre,
    radio ; l'alerte **interrompt tout saut de temps** ;
  - pendant l'alerte, le **saut ×360 est interdit au camp** et dans le véhicule garé en plaine ; il reste permis au repli
    **FR01** (jamais inondé, 17 min de route, 55 min à pied) ; les traversées en packraft sont interdites ;
  - marge : avec le réservoir linéaire de 3 jours du modèle d'eau, le débit passe de 2 à 4 × la moyenne en ≈ 3 h sous un
    apport soutenu de 45 × la moyenne (orage fort) et en ≈ 10 h sous 17 × la moyenne (calcul sur le modèle, pas une
    donnée de terrain) ; l'alerte laisse donc toujours le temps de rouler jusqu'à FR01. En ×360, ces heures ne durent que
    quelques dizaines de secondes : c'est pourquoi l'alerte coupe le saut.
- **Classes de crue** (`metrics.flood_class` : `frequent`, `occasional`, `extreme_only`, `never`) : calculées à la
  **hauteur propre du POI** au-dessus du niveau de plein bord local (plus la cellule d'analyse de 32 m, qui mêlait chenal
  et haut de bluff).
- **Distance de tolérance** : les affûts sont placés à 70–240 m des cibles ; la vraie distance de fuite des espèces
  simulées est une **question de recherche** du jeu (mission M02), pas une donnée.

## 9. Expéditions et objectifs de recherche

Les expéditions sont ouvertes (`ExpeditionSystem`) : question, zone, durée, équipement, hypothèses, cibles de données.
Le JSON propose des **gabarits** (`missions`) liés au système d'hypothèses (`Hypothesis.h` : types `comparison`,
`event_response`, `correlation`, `association` ; mesures `measureKey`) :

| Id | Question | POI recommandés | Hypothèse type |
|---|---|---|---|
| M01 | Premier relevé : qui fréquente les abords du camp, où et quand boit-on ? | BC01, OV01, WA01, FD02 | aucune imposée (accueil) |
| M02 | La vigilance des Edmontosaurus augmente-t-elle sous 150 m ? | affûts | `comparison` vigilant_fraction / `observer_within` 150 m |
| M03 | Le drone dérange-t-il ? | point de vue, abreuvoir | `comparison` vigilant_fraction / `drone_airborne` |
| M04 | Après un type d'appel, le groupe se resserre-t-il ? | stations d'écoute | `event_response` group_spacing après `call_cluster` (cris = hypothèses) |
| M05 | Le taux d'appels change-t-il la nuit ? | réseau de micros, marais | `comparison` call_rate / `night` |
| M06 | La formule d'Alexander estime-t-elle bien des vitesses filmées ? | franchissements et gués | calibration de pistes (`alexander1976_dinosaur_speeds`) |
| M07 | Que contiennent les couches plus anciennes des berges ? | affleurements (par leur banquette) | fouille, identification, météorisation |
| M08 | Depuis quand cet animal est-il mort, qui l'a mangé ? | carcasses (CA01 : rive opposée, packraft) | examen (stade, degrés-jours, morsures) |
| M09 | Les adultes reviennent-ils au même endroit en saison de reproduction ? | aires de nidification (surtout rive opposée, packraft) | observations répétées (seulement si `nesting_grounds`) |

Toutes les cibles des gabarits sont atteignables depuis le camp (`metrics.reachable_from_basecamp`), celles de la rive
opposée avec le packraft (`needs_packraft`, `travel_time_from_basecamp_min`).

Le système d'hypothèses ne dit jamais « vrai » : il dit si **les données du joueur** soutiennent la prédiction, avec quelle
force, et quels biais pèsent (distance, drone, heure, pseudo-réplication, taille d'échantillon).

## 10. Rythme, durée du jour et durée d'expédition

- **Temps de jeu 1:1** pendant l'observation. Vitesses, taux d'appels par minute, distances de fuite et budgets d'activité
  sont mesurés par le joueur et testés : comprimer le temps fausserait ses données. Une journée simulée dure donc 24 h de
  simulation ; en pratique le joueur en vit 2–3 h réelles grâce aux attentes.
- **Accéléré « time-lapse »** plafonné à **×8** (`FullDetailMaxTimeScale` : les animaux restent simulés en détail),
  signalé à l'écran et **exclu** des mesures de comportement.
- **Saut ×360** (simulation grossière) pour les nuits et les pluies, au camp ou dans le véhicule — **sauf pendant une
  alerte de crue** (§ 8) : l'alerte interrompt le saut et le ×360 n'est alors permis qu'au repli FR01.
- **Durée du jour simulé** : 24 h et 365 jours (hypothèse de jeu de `Data/Science/sim_constants.json`). Le jour était un
  peu plus court au Crétacé supérieur ; la valeur pour la Hell Creek n'est pas établie dans le projet — **Information
  insuffisante — recherche nécessaire.** Piste ajoutée à la bibliographie : `dewinter2020_rudist_daily` (non vérifiée).
- **Convention de jour** : jours affichés numérotés à partir de 1 (« jour 141 ») ; `time.start_day_of_year = 140` est
  l'indice en base 0 de `SimConfig` (`time.start_day_display = 141`). La carte `overview.png` affiche le départ (jour 141,
  05:00) et l'heure d'export (jour 141, 11:00, après 6 h de préchauffage).
- **Expédition par défaut : 3 jours** (min 1, max 7). Trois jours donnent au moins deux aubes et deux crépuscules pour
  répliquer les sessions, une nuit d'écoute complète, et une logistique crédible (batterie du véhicule 200 kWh + générateur,
  batteries de drone et de capteurs rechargées au camp). Au-delà de 7 jours, météo et crues dominent et la session de jeu
  devient trop longue. La tranche verticale tient en **1 jour**.
- Rythme d'une journée type : aube (écoute, comptages) → matinée (pistes, franchissements, affûts) → milieu de journée
  (fossiles, laboratoire mobile, analyse) → crépuscule (affûts, drone) → nuit (enregistreurs, réseau de micros, attente au
  camp).

## 11. Accessibilité

- Tout indice acoustique a un **équivalent visuel** : spectrogramme en direct, indicateur de direction, sous-titres
  descriptifs ; les vocalisations portent toujours l'étiquette **« hypothèse »** (« hypothesis » en anglais).
- L'alerte de crue a une jauge visuelle et un sous-titre, en plus de la radio.
- Icônes de carte distinguées par la **forme** autant que par la couleur (triangle, losange, carré, anneau, croix) ;
  sur `overview.png`, les étiquettes sont placées sans se chevaucher (trait de rappel quand une étiquette est déplacée).
- Accueil par le carnet uniquement, sans marqueur ; la carte est construite par le joueur.
- Langues de l'interface : **FR / EN** (noms des POI et des gabarits fournis dans les deux langues).

## 12. Honnêteté scientifique

- **Deux étiquettes distinctes** par élément : `confidence` dit ce qu'on sait de l'existence ou de la nature de l'élément
  au Maastrichtien (avec `confidence_subject_fr`) ; `placement` dit d'où vient **sa position** (terrain généré, état de la
  simulation, prédiction du modèle, choix de jeu). Un affleurement est FORTEMENT ÉTAYÉ comme objet géologique, alors que
  son contenu fossile est SPÉCULATIF et la hauteur de son bluff une hypothèse de jeu.
- **Nidification** : les zones sont des prédictions d'un modèle d'habitat, marquées SPÉCULATIF, affichées seulement si le
  profil active `nesting_grounds` (activé dans `standard` et `speculative`, désactivé dans `conservative`). La preuve du
  type de nid (`nest_type_evidence`, `nest_type_basis`) vient de la fiche espèce, sans emprunter les sources générales de
  la reproduction. Aucun site de nidification n'est connu pour ces espèces dans la Hell Creek ; des restes de jeunes
  Edmontosaurus de taille de nidification y sont signalés (`wosik2017_edmontosaurus_nestling`) et des nids d'hadrosaures
  sont connus ailleurs (`horner1979_maiasaura_nest`). Pour Champsosaurus, le mode de reproduction reste débattu
  (`katsura2004_champsosaurus` ; une viviparité est rapportée chez des choristodères asiatiques, piste non vérifiée dans
  le projet) : aucune zone n'est proposée.
- **Vocalisations** : le jeu ne prétend jamais savoir ce qu'un cri « signifiait » ni comment il sonnait exactement ; les
  sons sont des hypothèses de reconstitution (lecture « bouche fermée » par défaut, `riede2016_closed_mouth_vocalization`,
  `clarke2016_vegavis_syrinx`).
- **Âge des affleurements** : âge relatif sûr (plus ancien que la surface), âge absolu non contraint — Information
  insuffisante — recherche nécessaire.
- **Corridors et franchissements** : prédictions d'un modèle de coûts sur un monde reconstitué, présentées comme telles ;
  le niveau ne prétend pas qu'il existe des hauts-fonds là où le chenal généré n'en a pas.
- Les règles de sécurité (foudre, véhicule, packraft, alerte de crue) sont des règles modernes de jeu, sans prétention
  paléontologique.
- Aucun POI n'est placé « pour le spectacle » : chaque position a une justification algorithmique écrite dans le JSON.

## 13. Format `Data/Levels` (`noctis.level/1`)

| Clé | Contenu |
|---|---|
| `id`, `name`, `environment`, `reconstruction_version` | identité ; noms FR/EN |
| `generation` | graine, profil, échelle de population, heures de préchauffage, générateur, heure d'export, comptes d'événements, **commande complète** (`--scale`, `--warmup-hours` compris) |
| `frame` | repère du cœur (x est, y nord, m) et correspondance Unreal (`X = x·100`, `Y = −y·100`, `Z = z·100`, lacet = −cap), transformation du paysage |
| `playable_area` | toute la carte de `run` ; `edges` (terrasses au nord et au sud, limite d'étude à l'est et à l'ouest), `poi_edge_margin_m` |
| `time` | jour simulé, convention de jour, échelles de temps et justification, départ, durée d'expédition |
| `river_crossing` | `main_river` (débit minimal simulé, niveau minimal, `never_fordable`, franchissements, traversées packraft possibles), `creek` (gués, lits à sec), composantes à pied / avec packraft, décision |
| `rules` | rôle du joueur, blessure/évacuation, véhicule (refuge, gué, bruit), marche, **packraft**, **alerte de crue**, **accès au terrain** (angle marchable, berges), orage, drone |
| `masks` | dossier et fichiers PNG 256 × 256 (cellules de 32 m, ligne 0 = nord) |
| `pois[]` | voir § 5 |
| `trails[]` | corridors prédits (points `[x, y, z]`), usure observée, preuve, confiance, `usable_by` / `not_usable_by` |
| `connections[]` | réseau d'itinéraires entre POI (`mode` `foot` / `foot_packraft`, longueur, temps, pente et eau max, tracé) |
| `designer` | points chauds d'alimentation et activité de prédateurs du préchauffage — jamais montrés au joueur |
| `events[]`, `missions[]` | événements émergents et gabarits d'expédition |
| `world_partition`, `accessibility`, `summary` | réglages Unreal proposés, accessibilité, compteurs (composantes, POI atteignables, sites d'abreuvement rejetés, espèces sans zone de nidification) |

Le fichier est **généré** : on le régénère, on ne l'édite pas (`noctis_sim export-level`). Options : `--seed`, `--profile`,
`--env`, `--scale`, `--warmup-hours` (6 par défaut), `--json`, `--out` (défaut `Output/level`). Durée mesurée : **~115 s
de calcul** (72 à 225 s de temps réel selon la charge de la machine), dont la majeure partie pour le préchauffage.

## 14. Unreal Engine 5.4+

Ordre : `setup_project.py` (carte World Partition **vide**, ciel, brouillard, lumières, gestionnaire de créatures) puis
`build_level.py` (contenu du niveau). Entrées : `noctis_sim export-terrain --out Build/Terrain` et `noctis_sim
export-level`. Le script fonctionne aussi hors Unreal (`--dry-run`) pour vérifier les entrées et le plan ; il avertit
alors des étapes qui seront manuelles, des masques absents (par exemple `weight_oxbow_lake.png`, absent pour la graine 7)
et du repli silencieux de `Build/Terrain` vers `Output/terrain`.

**Carte** : `LevelEditorSubsystem.new_level(chemin, True)` crée une carte partitionnée vide (équivalent d'*Empty Open
World*). Le gabarit *Open World* n'est **pas** utilisé : il contient un paysage d'exemple que l'étape « paysage » aurait
pris pour le terrain Noctis.

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
  imprime les réglages exacts de l'import manuel. Il recoupe ses valeurs avec `NoctisEditorLibrary.compute_terrain_info()`.
- **Reconnaissance du paysage** : le paysage Noctis porte le tag `noctis.landscape` (posé à l'import par l'utilitaire, ou
  au passage suivant quand un import manuel a exactement la transformation attendue). Un paysage **sans ce tag** dont la
  transformation ou l'emprise diffère (paysage de gabarit) arrête l'étape **en erreur** et annule les étapes POI,
  corridors, PCG et départ ; `--replace-foreign-landscape` le supprime avant l'import. Un paysage Noctis dont la
  transformation diffère n'est **jamais** déplacé ni remis à l'échelle (les proxys de streaming de World Partition ne
  suivraient pas, et un import mal réglé ne se répare pas par une échelle) : le script s'arrête avec les réglages de
  réimport.

**Angle marchable et berges** : au pas de 4,06 m, les berges verticales deviennent des rampes de 30–51° ; le talus d'un
bluff (~3 m sur 6–8 m) serait gravissable avec l'angle marchable par défaut du personnage (**44,77°**) et la berge du
chenal (~51°) ne le serait pas, sans que cela corresponde au terrain voulu. Règle : les talus de bluff et les berges
concaves sont des **parois bloquantes** posées par `PCG_Noctis_Exposures` ; l'accès à pied à l'eau se fait aux gués du
ruisseau, aux barres de méandre et aux abreuvoirs (berge ≤ 20°) ; la mise à l'eau du packraft aux points de
franchissement (berges ≤ 30°) ; la banquette au pied de chaque affleurement (`metrics.access_point`) est un maillage
marchable étroit (`rules.terrain_access`).

**World Partition** : grille d'exécution `MainGrid` de 256 m (25 600 cm), chargement à ~1 km (102 400 cm) — couvre le rayon
des acteurs créatures (450 m) et les affûts ; au-delà, HLOD du paysage et de la végétation pour les vues des points de vue.
Proxys de streaming du paysage : 1 composant. `AWorldSettings::WorldPartition` n'étant pas exposé à Python, le réglage de
la grille est une **étape manuelle** (World Settings > World Partition Setup > Runtime Settings > Grids[0]) que le script
journalise en avertissement, y compris en `--dry-run`. **Couches de données** : deux couches **éditeur**,
`DL_POI_Markers` et `DL_Nesting_Speculative` ; le script assigne chaque marqueur à sa couche
(`DataLayerEditorSubsystem.add_actors_to_data_layer`, `# VERIFY`). Pas de couche runtime : les marqueurs sont réservés à
l'éditeur, et l'affichage en jeu des aires de nidification suit `profile_toggle` lu dans le JSON.

**PCG** (un volume par graphe couvrant tout le paysage, génération **partitionnée** sur la grille PCG de 256 m, à la
demande dans l'éditeur ; `world_partition.pcg.volume_size_m = 8192`) :

- `PCG_Noctis_Foliage` — échantillonnage de surface du paysage, une branche par type fonctionnel végétal, densité lue dans
  `T_Foliage<Pft>` (masques `foliage_*.png` du cœur), élagage par rayon, exclusions (chenal, eau, clairières des POI ;
  branche bras morts ignorée quand `weight_oxbow_lake.png` est absent), transformations aléatoires, maillages provisoires.
- `PCG_Noctis_Trails` — splines des corridors (`noctis.trail`), largeur selon l'usure.
- `PCG_Noctis_Exposures` — le long de `weight_cutbank_cliff` : parois et strates provisoires à collision **BlockAll**,
  alignées sur l'arête de berge, face vers le chenal ; banquette marchable au pied de chaque affleurement.
- `PCG_Noctis_RiverDebris` — barres sableuses et laisses de crue (`T_WeightSandbar`, `T_MaskFloodRisk`).

Les marqueurs de POI sont des maillages de base **cachés en jeu** et réservés à l'éditeur ; à l'exécution, le jeu lit le JSON.
Les appels d'API non vérifiables sans moteur sont marqués `# VERIFY` dans le script ; chacun a un repli journalisé.

## 15. Limites et décisions ouvertes

1. **Bras morts absents pour la graine 7** : le critère de dégagement du générateur est trop strict pour cette vallée ; à
   corriger dans `TerrainGenerator.cpp` (tâche séparée — changer le générateur change la vallée canonique pour tout le
   projet). En attendant, les marais arrière jouent ce rôle.
2. **Traversée de la rivière — décidée** : packraft (`equipment.json`). La rivière n'est jamais guéable avec le plancher
   de débit de 45 % de `Water.cpp` ; abaisser ce plancher changerait `run` pour tout le projet et n'est pas fait ici.
3. **Pas de seuils ni de mouilles** dans le chenal généré : les « gués » de la rivière sont des points de franchissement
   candidats. Un contraste radier/mouille aux inflexions relèverait de `TerrainGenerator.cpp` (tâche séparée).
4. **Choix du camp sans critère de crue** dans `WorldSimulation.cpp` : signalé aux responsables du cœur. Dans la vallée
   de la graine 7, aucun sol à ≥ 1,5 m au-dessus du plein bord n'existe à moins de 1 km de la rivière en dehors des
   bluffs (talus en érosion) ; un camp de terrasse serait à ~2,3 km de l'eau. Le niveau garde BC01 avec l'alerte de crue.
5. **Test de pente des sites d'abreuvement** (`Water.cpp`, `drinkSites_`) : seule la pente de la cellule sèche est
   testée, pas la hauteur au-dessus de l'eau ni la berge ; signalé aux responsables du cœur. Le niveau filtre lui-même
   (sol ≤ 0,5 m au-dessus de l'eau, berge ≤ 20° sur 10 m).
6. **Hauteur des bluffs** : surélévation fixe de 3,5 m du générateur (hypothèse de jeu, § 4).
7. **Sous-bois dense** : à 1,2 m d'œil, la végétation simulée des levées masque les cibles à 70 m et plus ; les affûts
   de la graine 7 sont tous des plates-formes. À revoir si le modèle de végétation change.
8. **Durée du jour** : 24 h faute de source vérifiée (piste `dewinter2020_rudist_daily`, non vérifiée).
9. **Radio** : propagation en ligne de vue sur le relief seul (pas d'atténuation par la forêt).
10. Les poids et seuils des algorithmes (coûts de marche, seuils de praticabilité, distances d'affût, retraits, seuil
    d'aptitude de nidification, limites du packraft, seuil d'alerte) sont des **hypothèses de jeu** documentées dans
    `LevelExport.cpp` et `equipment.json`, pas des données.
11. Le préchauffage de 6 h donne encore peu d'usure de pistes (`predicted_only` majoritaire) et peu d'événements
    « boire » (5 pour la graine 7) ; un préchauffage plus long (`--warmup-hours 48`) renforcerait l'évidence des corridors
    et des abreuvoirs au prix de ~20 min de calcul.
