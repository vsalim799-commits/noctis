# Environment Factory — flore, bois mort et sols de Hell Creek (Maastrichtien) pour UE5

Générateurs procéduraux (Python + Blender 4.2 `bpy`, numpy, scipy) qui produisent les assets d'environnement de la
tranche verticale de Noctis : vallée fluviale de la Formation de Hell Creek, graine de simulation 7.
Tout est reproductible (graines fixes) et sort dans `Build/Environment/` (ignoré par git) ; les rendus de contrôle
sortent dans `Output/environment/` (ignoré aussi).

Documents liés : `Docs/ENVIRONMENT.md` (matériau de paysage, graphes PCG, ordre des scripts Unreal),
`Docs/LEVEL_DESIGN.md` et `Content/Python/build_level.py` (import du paysage, volumes PCG),
`Tools/CreatureFactory/scenes/README.md` (utiliser la végétation dans un plan de créature).

## Lancer

```bash
PY=/home/user/.venvs/noctis-tools/bin/python
cmake --build build --target noctis_sim            # si build/noctis_sim est absent
nice -n 10 $PY Tools/EnvironmentFactory/factory.py                       # tout (voir « Temps » plus bas)
nice -n 10 $PY Tools/EnvironmentFactory/factory.py --stages flora --assets fern_tufted,fern_lace
nice -n 10 $PY Tools/EnvironmentFactory/factory.py --stages aerial --preview      # 25 % de la résolution
NOCTIS_PREVIEW_PCT=50 nice -n 10 $PY Tools/EnvironmentFactory/factory.py --stages ground_shot --preview
```

| Étape | Rôle | Sortie |
|---|---|---|
| `sim` | `noctis_sim export-terrain` + `export-ground` (graine 7) | `Build/Environment/sim/` |
| `flora` | atlas de feuilles, écorces, maillages LOD0–2 (+ LOD3 en cartes croisées pour les arbres et le grand palmier), collisions UCX, FBX, décalques maillés, bibliothèque `.blend`, métadonnées | `textures/`, `fbx/`, `meta/`, `library/` |
| `ground` | 9 jeux de textures de sol 2K raccordables (tuiles de 4 à 6 m), atlas de décalques de pistes, bruit macro | `ground/` |
| `landscape` | 9 couches de peinture du paysage UE (2017²) + 2 masques RGBA (humidité/corridors/crue/eau ; couvert bas/bande de rive), puis textures de probabilité d'apparition PCG par asset (`envlib/pcgbake.py`) | `landscape/`, `pcg/` |
| `manifest` | contrat de données lu par les scripts Unreal et le helper Blender | `manifest.json` |
| `sheet` | planche contact de tous les assets | `Output/environment/assets_sheet.png` |
| `aerial`, `ground_shot` | rendus Cycles de la vallée | `Output/environment/valley_*.png` (+ `.json` : réglages, instances, temps) |

Options : `--assets id1,id2` (flore), `--force-textures` (recalcule les textures en cache), `--samples N`
(rendus et planche ; `--stages sheet --samples 0` recompose la planche depuis les vignettes en cache), `--preview`
(`NOCTIS_PREVIEW_PCT`, 25 % par défaut), `--seed N`. Les temps de chaque étape sont ajoutés à
`Output/environment/timings.json` ; chaque rendu écrit aussi son temps CPU et la charge machine dans son `.json`
(la machine est partagée : le temps mural dépend des autres tâches).

### Données de simulation utilisées

`export-terrain` : `heightmap.png` (16 bits, 2017², 4,06 m/px, ligne 0 = bord nord), `terrain.json`,
`weight_<habitat>.png` (habitats), `foliage_<type>.png` (densité simulée de chaque type fonctionnel de plante).
`export-ground` (fonction **nouvelle** `cmdExportGround` de `Tools/Cli/noctis_sim.cpp`, ajoutée pour ce pipeline
sans toucher au code partagé) : `substrate_<substrat>.png`, `wetness.png`, `moisture_initial.png`,
`water_surface.png` (niveau d'eau absolu, 16 bits) et `water_mask.png`. Les masques de corridors et de risque de
crue viennent de `noctis_sim export-level` (`Output/level/`), quand ils existent.

## Assets de flore et de bois mort (27)

Échelle métrique réelle, pivot à la base (0,0,0 au sol), Z vers le haut. Triangles LOD0 / LOD1 / LOD2 (/ LOD3 en
cartes croisées pour les arbres et le grand palmier) — valeurs de `manifest.json`, régénérées à chaque passage :

| Asset (`id`) | Nom UE | Type simulé | Hauteur | Triangles | Confiance (présence) |
|---|---|---|---|---|---|
| `conifer_metasequoia` | SM_Conifer_Metasequoia_A | conifer | 28,9 m | 53 495 / 28 617 / 5 529 / 6 | FORTEMENT_ETAYE |
| `conifer_taxodium` | SM_Conifer_Taxodioid_B | conifer | 26,3 m | 46 108 / 23 950 / 4 721 / 6 | FORTEMENT_ETAYE (port SPECULATIF) |
| `conifer_glyptostrobus` | SM_Conifer_Glyptostrobus_C | conifer | 20,4 m | 11 131 / 5 829 / 2 478 / 6 | FORTEMENT_ETAYE |
| `conifer_young` | SM_Conifer_Metasequoia_Young | conifer | 6,5 m | 3 311 / 1 182 / 196 / 6 | FORTEMENT_ETAYE |
| `broadleaf_canopy_a` | SM_Broadleaf_Canopy_A | angio_tree | 24,0 m | 19 898 / 9 796 / 2 170 / 6 | ETABLI |
| `broadleaf_canopy_b` | SM_Broadleaf_Lobed_B | angio_tree | 17,3 m | 9 665 / 5 121 / 1 334 / 6 | ETABLI |
| `broadleaf_riparian_c` | SM_Broadleaf_Riparian_C | angio_tree | 12,4 m | 14 959 / 3 945 / 1 739 / 6 | ETABLI |
| `broadleaf_spreading_d` | SM_Broadleaf_Spreading_D | angio_tree | 15,0 m | 5 921 / 3 055 / 730 / 6 | ETABLI |
| `ginkgo_tree` | SM_Ginkgo_A | ginkgo | 18,0 m | 18 067 / 6 129 / 2 250 / 6 | FORTEMENT_ETAYE |
| `shrub_ovate` | SM_Shrub_Ovate_A | angio_shrub | 2,8 m | 1 778 / 700 / 360 | INFERE |
| `shrub_small_leaf` | SM_Shrub_SmallLeaf_B | angio_shrub | 1,77 m | 2 080 / 634 / 464 | INFERE |
| `palm_sabalites_tall` | SM_Palm_Sabalites_Tall | palm | 6,4 m | 24 244 / 3 616 / 1 011 / 6 | INFERE (rare) |
| `palm_sabalites_short` | SM_Palm_Sabalites_Acaulescent | palm | 1,33 m | 7 698 / 1 272 / 395 | INFERE (rare) |
| `fern_tufted` | SM_Fern_Tufted_A | fern | 0,82 m | 760 / 180 / 72 | FORTEMENT_ETAYE |
| `fern_lace` | SM_Fern_Bipinnate_B | fern | 0,42 m | 728 / 180 / 60 | FORTEMENT_ETAYE |
| `fern_marsh` | SM_Fern_Marsh_C | fern | 1,08 m | 888 / 240 / 96 | FORTEMENT_ETAYE |
| `horsetail_unbranched` | SM_Horsetail_Stand_A | horsetail | 1,10 m | 3 558 / 852 / 219 | INFERE |
| `horsetail_branched` | SM_Horsetail_Stand_B | horsetail | 1,01 m | 2 122 / 416 / 96 | INFERE |
| `emergent_broadleaf` | SM_Aquatic_Emergent_Broad | aquatic_macrophyte | 0,69 m | 572 / 112 / 10 | INFERE (port SPECULATIF) |
| `emergent_strap` | SM_Aquatic_Emergent_Strap | aquatic_macrophyte | 0,25 m | 384 / 160 / 56 | INFERE (port SPECULATIF) |
| `floating_rosettes` | SM_Aquatic_Floating_Rosettes | aquatic_macrophyte | surface | 28 / 22 / 6 | INFERE |
| `cycadophyte` | SM_Cycadophyte_A | cycadophyte | 1,56 m | 588 / 248 / 81 | INFERE (rare) |
| `herb_dicot` | SM_Herb_Dicot_A | angio_herb | 0,43 m | 118 / 66 / 10 | INFERE |
| `deadwood_log` | SM_Deadwood_FallenLog | — | 9,6 m de long | 9 418 / 372 / 84 | INFERE |
| `deadwood_stump` | SM_Deadwood_Stump | — | 1,64 m | 3 360 / 260 / 113 | INFERE |
| `deadwood_rootwad` | SM_Deadwood_RootWad | — | 3,1 m | 6 156 / 684 / 178 | INFERE |
| `deadwood_branches` | SM_Deadwood_BranchLitter | — | 0,32 m | 1 044 / 558 / 60 | INFERE |

Ginkgo et cycadophyte ne sont produits que parce que `Data/Environments/hell_creek.json` les déclare présents
(Ginkgo FORTEMENT_ETAYE ; cycadophytes rares, INFERE). Les palmiers (présence INFÉRÉE, rare) restent rares dans la
dispersion (30 inst./ha à masque = 1 : ≈ 2 % des tiges ligneuses). **Aucune graminée, aucune prairie, aucune
herbacée à fleurs voyantes** : les milieux ouverts sont couverts de fougères, prêles, herbacées dicotylédones basses
et arbustes, comme l'indiquent les données du projet. Parmi les aquatiques, les rosettes flottantes (type
Quereuxia, seul taxon appuyé par les données) dominent ; la rosette semi-émergente à feuilles rubanées dentées et
l'émergente à limbes larges sont une minorité étiquetée SPÉCULATIF (ni iris, ni massette, ni graminée).

### Construction

* **Arbres** (`envlib/treegen.py`) : squelette récursif (tronc, ordres 1 à 3, gravitropisme, enveloppe de
  houppier, contreforts et empattement cannelé des Cupressaceae), tubes d'écorce à repère de transport parallèle,
  cartes alpha de rameaux / bouquets de feuilles posées sur les derniers ordres. LOD1 et LOD2 : moins d'ordres en
  géométrie, moins de côtés, cartes plus grandes et moins nombreuses (même silhouette).
* **Plantes non ramifiées** (`envlib/plants.py`) : palmiers de type Sabal/Sabalites dont chaque limbe est une
  vraie géométrie costapalmée — costa prolongeant le pétiole dans le limbe, ~40 segments induplicés (pliés en V)
  soudés sur la partie interne et libres sur le tiers externe, extrémités bifides retombantes, couronne sphérique et
  jupe de feuilles mortes pour le grand palmier ; LOD1 par paires de segments, LOD2 par groupes de quatre (atlas
  `palm_segments`) ; touffes de frondes (atlas 2 × 4 : pennées pinnatifides, bipennées à pinnules, pennées étroites,
  sèche, jaunissante ; longueur, angle et courbure de chaque penne tirés au hasard) ; prêles (tiges dressées
  presque parallèles issues de rhizomes traçants, extrémités fermées, strobiles allongés et apiculés brun sombre
  sur ~13 % des tiges, verticilles à partir de ~0,3 m pour la variante B) ; rosette semi-émergente, émergente à
  limbes larges, rosettes flottantes ; cycadophyte ; herbacée de 0,45 m (rosette + tiges feuillées).
* **Bois mort** (`envlib/deadwood.py`) : tronc couché à moitié écorcé, chicot à cassure en esquilles, galette
  racinaire avec motte de limon, branches au sol. LOD0 « Nanite-ready » : relief d'écorce déplacé depuis les
  cartes de hauteur.
* **Textures de feuilles** (`envlib/foliage.py`, `envlib/paint.py`) : atlas 2048 px peints procéduralement
  (nervation, marges, dégâts d'insectes, variation de teinte) → BaseColor (sRGB), Normal (DirectX),
  Opacity, Translucency, Roughness. **Écorces** (`envlib/bark.py`) : jeux raccordables 1024 px BaseColor /
  Normal / Roughness / Height (Cupressaceae fibreuse, angiosperme lisse et rugueuse, stipe de palmier, bois mort,
  tige de prêle, tige verte, motte de terre).
* **Couleur de sommet = vent** : R = souplesse (0 tronc → 1 bout de feuille), G = phase aléatoire par élément,
  B = hauteur normalisée. Le matériau maître UE (`M_Noctis_Foliage`) en tire un World Position Offset déphasé par
  instance (PerInstanceRandom) et modulé par des rafales en position monde. Le bois mort exporte ses propres
  slots `*_Static` (mêmes textures) : leurs instances UE coupent le WPO (*static switch* `UseWind`).
* **Collision** : `UCX_<Nom>_NN` convexes dans le FBX du LOD0 (troncs jusqu'à 4 m, bois mort en plusieurs
  coques) ; fougères, prêles, herbacées, émergentes et branches au sol sans collision (traversables).
* **LOD / Nanite / imposteurs** : trois LOD classiques avec tailles d'écran recommandées
  (`lod_screen_size`, ex. 1,0 / 0,35 / 0,12 pour les arbres) ; pour les arbres et le grand palmier, un **LOD3 en
  3 cartes croisées** (6 triangles, `envlib/impostor.py` : rendu orthographique du LOD1 → albédo, opacité, normale
  en espace tangent DirectX ; taille d'écran 0,035). Au-delà : HLOD *Mesh Approximation* de World Partition. Le
  feuillage en cartes alpha reste en LOD classiques en UE 5.4 (Nanite + matériau masqué = surdessin coûteux). Le
  bois mort (opaque) peut activer Nanite sur le LOD0.

### Métadonnées (`Build/Environment/meta/<id>.json`)

Schéma `noctis.environment_asset/1` : générateur (version, graine), unités, bornes, LOD (triangles, sommets,
matériaux), tailles d'écran, avis Nanite / imposteur, collision, textures par matériau, sémantique de la couleur
de sommet, distances de disparition, règle de dispersion (masque de simulation, part dans le type fonctionnel),
vérification des unités du FBX relu, et bloc **`botany`** :
`basis` (ce que l'asset représente et ce que disent les données du projet), `confidence` +
`confidence_subject` (vocabulaire ETABLI / FORTEMENT_ETAYE / INFERE / SPECULATIF de `Data/Environments/NOTES.md`),
`habit_confidence` (le port de la plante, presque toujours INFERE ou SPECULATIF), `design_assumptions`
(choix de jeu : analogue moderne, couleurs, proportions), `speculative`, `sources` (identifiants de
`Data/Sources/bib_environments.json` uniquement, pour les seules affirmations que les données du projet leur
attribuent). La couleur des feuilles, des écorces et du bois n'est jamais fossilisée : c'est toujours une
hypothèse de design, notée comme telle.

## Matériaux de sol (9 jeux 2K raccordables, `envlib/ground.py`)

| Jeu | Tuile | Habitats / substrats | Contenu |
|---|---|---|---|
| `FloodplainSilt` | 6 m | floodplain_open, levee_woodland | limon humide, débris de fougères et d'herbacées, croûtes de mousses / hépatiques, fentes rares et discrètes |
| `FloodplainSiltDry` | 5 m | creux secs de la plaine ouverte | limon desséché à fentes polygonales (deux générations) |
| `RiverbankMud` | 5 m | riverbank, sentiers | boue humide, rigoles, flaques, débris (empreintes : décalques séparés) |
| `PointBarSand` | 6 m | sandbar | rides de courant (~0,12 m) d'orientation et de longueur d'onde variables, crêtes interrompues, débris dans les creux, galets |
| `LeafLitterBroadleaf` | 4 m | riparian_forest, levee_woodland | feuilles de dicotylédones, brindilles |
| `LeafLitterConifer` | 4 m | upland_conifer_forest | rameaux caducs et aiguilles de Cupressaceae, petits cônes |
| `BackswampOrganicMud` | 5 m | backswamp | vase organique, eau stagnante, débris |
| `CutbankStrata` | 4 m | cutbank_cliff (pentes > 20–28°) | alternance grès / silts / argiles / niveaux carbonés (projection biplanaire, V = altitude) |
| `RiverGravel` | 4 m | channel | galets roulés, matrice sableuse, film d'algues |

Cartes : `T_Ground_<Jeu>_BaseColor.png` (sRGB), `_Normal.png` (DirectX), `_Roughness.png`, `_Height.png` (16 bits).
Les jeux ne contiennent **aucun élément mémorable** qui trahirait la répétition : les empreintes sont un atlas de
**décalques** (`T_Decal_Trackways_*`, 4 cellules de 0,75 × 3 m : piste de théropode, piste d'hadrosaure, traces de
petits tridactyles, sol piétiné ; maillages `SM_Decal_Trackway_*` placés par PCG le long des berges et des
corridors), les fentes marquées sont la couche `FloodplainSiltDry`. `T_Ground_MacroNoise.png` (bruit basse
fréquence indépendant, 240 m) sert à la variation macro et au mélange anti-répétition du matériau de paysage
(second échantillon tourné / ×1,618, échantillon lointain ×4,3, cf. `Docs/ENVIRONMENT.md` §3.2).
**Écart assumé par rapport à un « bake » de shaders Blender** : les champs procéduraux (bruit FFT périodique,
Worley périodique, rides à directions entières, éléments estampillés avec repli) sont évalués en numpy pour que
les tuiles soient *exactement* périodiques et reproductibles ; Blender/Cycles sert ensuite à les contrôler
(planche contact, rendus de la vallée, micro-déplacement du terrain proche). La hauteur est le champ maître ; la
normale en est dérivée.

## Export FBX pour UE5

* Scène d'export en centimètres (`unit_settings.scale_length = 0.01`, géométrie ×100) → `UnitScaleFactor = 1`,
  aucune mise à l'échelle de nœud ; `axis_forward = -Z`, `axis_up = Y` (conversion standard Blender → FBX ;
  Unreal la ramène en Z haut avec *Convert Scene* activé) ; `apply_scale_options = FBX_SCALE_NONE`, triangles,
  tangentes, lissage par face, couleurs de sommet linéaires. Réglages complets : `envlib/common.py`
  (`FBX_SETTINGS`), relus après export (`fbx_verification` dans les métadonnées).
* Import UE : échelle uniforme 1,0, *Convert Scene* oui, *Convert Scene Unit* non, couleurs de sommet
  « Replace », pas de collision automatique (les `UCX_` suffisent), pas d'UV de lightmap (Lumen).
  `Content/Python/import_environment.py` applique ces réglages, monte les LOD1/LOD2 sur le LOD0, crée les
  instances de matériaux et les Foliage Types (distances de disparition du manifeste).
* Fichiers : `fbx/<id>/SM_<Nom>.fbx` (LOD0 + UCX), `SM_<Nom>_LOD1.fbx`, `SM_<Nom>_LOD2.fbx`.

## Rendus de contrôle

* `assets_sheet.png` : vignette Cycles du LOD0 de chaque asset (triangles des LOD, hauteur) + les 9 sols et
  l'atlas de décalques de pistes.
* `valley_aerial.png` : vol bas (135 m) au-dessus des méandres en fin d'après-midi (soleil à 12°, lumière de
  trois-quarts latérale venant de la gauche : la caméra regarde à ~105° du soleil), brume de distance peu
  saturée, couche de nuages ; arbres en LOD0/LOD1 jusqu'à 250/700 m (les conifères émergents gardent leur
  flèche) ; recadrage de 3,2 km de la simulation (centre x 3600, y 4800) + la vallée entière à basse résolution
  au-delà (canopée lisse, sans relief aléatoire, couleur de forêt variée, entièrement mate).
* `valley_ground.png` : à hauteur d'œil (1,6 m) sur la berge ouest d'un méandre (x 3851, y 4528), prêles et
  fougères, conifères taxodiacés et feuillus derrière, brume matinale bleu-gris plafonnée à 0,5 au sol et
  réchauffée du côté du soleil, horizon au tiers supérieur ; recadrage de 900 m + patch fin de 90 m à 0,1 m
  (micro-relief cuit depuis les hauteurs des textures de sol, fondu sur 8 m dans le terrain grossier) ; une
  vingtaine de plantes de premier plan placées à la main (direction artistique), tout le reste suit les masques,
  décalques de pistes compris.
* `lod_check_small_plants.png` (aide de revue, hors étapes, `envlib/lodcheck.py`) : prêles et fougères en LOD0 à
  8 m, LOD1 à 20 m, LOD2 à 40 et 60 m, objectif 26 mm en 1920 × 1080, pixels réels agrandis : les prêles restent
  des tiges droites articulées (pas des touffes d'herbe) ; la prêle à verticilles perd ses verticilles en LOD2.
Les deux scènes utilisent exactement le helper public `Tools/CreatureFactory/scenes/vegetation_library.py`
(terrain recadré depuis la simulation, sol mélangé par habitats, niveau d'eau simulé, dispersion par les masques
de la simulation, LOD par distance à la caméra). Chaque rendu écrit un `.json` voisin (réglages, nombre
d'instances par asset et par LOD, temps mural et CPU, charge machine).

Techniques de rendu à connaître (`envlib/scenes.py`) :
* **Brume** : passe Mist mélangée au compositeur ; **brouillard bas analytique** (épaisseur optique ∝ densité ×
  distance × moyenne de exp(−hauteur au-dessus de l'eau / 2,5 m) entre la caméra et le point vu ; distance
  tirée de la passe Mist, moyennée donc anti-crénelée, hauteur de la passe Position), sans bruit et gratuit,
  à la place d'un volume Cycles (2,5 fois plus lent et granuleux à 12–16 échantillons). La passe Depth (un seul
  échantillon par pixel) ne sert qu'à exclure le ciel : l'utiliser comme distance donne des blocs sombres
  autour des tiges fines.
* **Rebonds de transparence** : un houppier vu par la tranche ou une touffe de prêles à verticilles empile des
  dizaines de cartes alpha ; au-delà de `transparent_max_bounces` le chemin est coupé (échantillon noir,
  Mist = 1) et la brume du compositeur apparaît en paillettes claires sur le feuillage (cause des points
  beiges des premiers rendus). Les scènes utilisent 64 rebonds.
* **Passes non débruitées** : Mist / Position ne passent pas par OIDN ; une ouverture morphologique de 1 px
  (Dilate/Erode −1 puis +1) sur la passe Mist retire les pixels isolés restants sans déplacer les silhouettes.
* **Ombre de sous-bois** (`canopy_shade`, vue aérienne) : assombrit et mate le sol des habitats boisés, car les
  houppiers en cartes sont plus poreux qu'une vraie canopée fermée.
* **Micro-relief** (`envlib/fields.py`) : bruit isotrope à bande limitée et domaine déformé (plus de somme d'ondes
  planes, qui dessinait des bandes parallèles sous un soleil rasant), retiré sur les barres de sable et dans l'eau,
  et seulement sur les maillages de pas ≤ 1 m (le recadrage aérien de 2,5 m n'en a pas).
* **Fin de portée progressive** : chaque catégorie de plantes s'éteint sur le dernier quart de sa distance
  maximale avec un rayon tiré par instance (les anneaux nets à 70 / 110 / 140 m dessinaient une ligne de
  points sur la rive opposée) ; distances de LOD légèrement aléatoires.

## Temps mesurés (machine partagée, 4 CPU, `nice -n 10`, charge moyenne 8–11 due aux autres tâches)

| Étape | Temps mural | Remarque |
|---|---|---|
| `flora` (textures en cache) | 409 s | 27 assets × 3 LOD, FBX, décalques, bibliothèque `.blend`, métadonnées, **+ 10 rendus d'imposteurs LOD3 (~30 s chacun)** |
| `ground` (forcé) | ≈ 310 s | 9 jeux 2K (litière de conifères 131 s), atlas de décalques 8 s, bruit macro |
| `landscape` | 65 s | 9 couches 2017², 2 masques RGBA, puis 31 textures PCG (`pcgbake`) |
| `manifest` | < 1 s | |
| `sheet` | ≈ 240 s (vignettes) ; 6 s en recomposition (`--samples 0`) | |
| `aerial` 1920 × 1080, 16 éch., OIDN | 837 s de rendu (+ 23 s de mise en place), 1 047 s CPU | 17 516 instances, arbres en LOD1 de 250 à 700 m |
| `ground_shot` 1920 × 1080, 16 éch., OIDN | 338 s de rendu (+ 25 s de mise en place), 887 s CPU (charge 6 ; 691 s sous charge 10,5) | 5 754 instances (dont 19 plantes de premier plan placées à la main) + 2 pistes placées à la main |
| `lodcheck` (hors étapes) | ≈ 20 s | 4 rendus recadrés |

Les textures (atlas, écorces, sols 2K) ne sont recalculées qu'avec `--force-textures`.

## Limites connues

* Imposteurs : LOD3 en cartes croisées seulement (pas d'imposteur octaédrique) ; HLOD à cuire dans l'éditeur.
* Les ports (architecture des arbres) sont extrapolés d'analogues actuels : aucun arbre fossile entier.
* Les densités par hectare des règles de dispersion sont des hypothèses de jeu (`envlib/catalog.py`,
  `PFT_RULES`, `DEADWOOD_RULES`, dont `density_gamma` = 1,6 pour les arbres) ; la *répartition* vient de la
  simulation, via la probabilité cuite de `envlib/pcgbake.py` partagée par le jeu et les rendus.
* Les textures de feuilles sont peintes (pas de photogrammétrie) : lisibles à mi-distance, simples en très gros plan.
* Le terrain de la simulation a une maille de 4,06 m : les rendus ajoutent un micro-relief procédural (RMS
  ≈ 6 cm) et un patch fin près de la caméra ; une berge verticale (cutbank) devient une pente de 30–51°.
* Graine 7 : pas de bras mort ni d'eau libre dans les marais arrière, donc très peu d'aquatiques (rosettes
  flottantes en eau calme près des rives, rares le long du chenal) : c'est la réponse de la simulation.
* Les prêles à verticilles et les palmiers empilent beaucoup de surfaces alpha (rebonds de transparence en
  Cycles, surdessin en UE) : à surveiller dans les budgets.
* Les matériaux de sol sont calculés en numpy puis contrôlés dans Blender (pas de « bake » de nœuds), choix fait
  pour garantir des tuiles exactement périodiques.
