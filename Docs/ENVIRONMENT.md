# Environnement de Hell Creek dans Unreal Engine 5 — flore, sols, paysage, PCG

Ce document décrit comment les assets de `Tools/EnvironmentFactory` (voir son `README.md`) arrivent dans UE 5.4 et
comment le paysage et la végétation de la vallée sont pilotés par la **simulation** (`noctis_sim`, graine 7).
Il complète `Docs/LEVEL_DESIGN.md` (niveau, POI, routes) et `Content/Python/build_level.py` (import du paysage,
volumes PCG) sans les dupliquer : `build_level.py` crée le paysage et pose un volume par graphe PCG ; les scripts
décrits ici fournissent le **matériau** de ce paysage, les **maillages**, les **Foliage Types** et le graphe
**PCG_Noctis_Foliage** que ces volumes utilisent.

Principe : la simulation décide *où* (habitats, densités simulées par type fonctionnel de plante, humidité, eau,
corridors d'animaux) ; l'Environment Factory décide *à quoi cela ressemble* (assets, textures) et fixe les
*densités de peuplement* (hypothèses de jeu, documentées).

---

## 1. Chaîne de production

```
noctis_sim export-terrain --seed 7      heightmap.png (16 bits, 2017², 4,06 m), weight_<habitat>.png, foliage_<type>.png
noctis_sim export-ground  --seed 7      substrate_*.png, wetness.png, moisture_initial.png, water_surface.png, water_mask.png
noctis_sim export-level                 mask_corridor_use.png, mask_flood_risk.png, POI (Output/level, cf. LEVEL_DESIGN.md)
        │
Tools/EnvironmentFactory/factory.py     flora → ground → landscape → manifest  (Build/Environment/)
        │
Unreal (Python Editor Script Plugin), dans cet ordre :
  1. setup_project.py                   MPC_NoctisWeather (vent, humidité, crue), réglages projet
  2. import_environment.py              /Game/Environment : textures, matériaux maîtres + instances, maillages LOD0-2
                                        + UCX, Foliage Types, graphe /Game/Noctis/PCG/PCG_Noctis_Foliage
  3. setup_landscape_material.py        /Game/Noctis/Landscape/M_NoctisLandscape, LayerInfo, T_LandscapeMasks,
                                        import des 8 couches peintes si un paysage est ouvert
  4. build_level.py                     paysage (heightmap), matériau M_NoctisLandscape, volumes PCG, POI, départ
```

Chaque script accepte `--dry-run` hors Unreal (vérifie le manifeste et tous les fichiers, affiche ce qui serait
créé). Les appels d'API Unreal qui n'ont pas pu être vérifiés sur un éditeur 5.4 sont marqués `# VERIFY` et
protégés : un échec est journalisé avec la procédure manuelle, la suite continue.

Le **contrat de données** est `Build/Environment/manifest.json` : assets (fichiers FBX par LOD, matériaux,
triangles, bornes, tailles d'écran, distances de disparition, collision, Nanite, confiance scientifique),
matériaux (textures par carte), sols (tuile en mètres), couches du paysage, règles de dispersion.

---

## 2. Assets dans `/Game/Environment`

| Dossier | Contenu |
|---|---|
| `Textures/Foliage`, `Textures/Bark`, `Textures/Ground` | atlas de feuilles 2K (BaseColor sRGB, Normal DirectX, Opacity, Translucency, Roughness), écorces 1K, sols 2K (BaseColor, Normal, Roughness, Height 16 bits) |
| `Materials/M_Noctis_Foliage` | Masked, Two Sided, *Two Sided Foliage* (clip 0,45) ; Subsurface Color = BaseColor × Translucency × teinte ; vent en WPO |
| `Materials/M_Noctis_Bark` | opaque, compatible Nanite et ISM ; vent en WPO faible |
| `Materials/Instances/MI_<slot>` | une instance par slot de matériau des FBX (`M_Leaf_*`, `M_Bark_*`, `M_Deadwood`, …) |
| `Flora/<Catégorie>/SM_<Nom>` | LOD0 (+ collisions `UCX_`), LOD1 et LOD2 importés comme LOD du même Static Mesh, tailles d'écran du manifeste |
| `FoliageTypes/FT_<Nom>` | Foliage Types (outil Foliage et PCG) |

**Vent** : couleur de sommet R = souplesse (0 tronc → 1 bout de feuille), G = phase aléatoire par élément,
B = hauteur normalisée. WPO = sin(Temps × WindFrequency + 2π·G) × R × WindAmplitudeCm × (0,15 × WindSpeed)
le long de `MPC_NoctisWeather.WindDirection`. Simple et peu coûteux ; Pivot Painter pourra le remplacer.

**Foliage Types** (réglages par catégorie, `FOLIAGE_SETTINGS` de `import_environment.py`) :

| Catégorie | Aligné sur la normale | Pente max | Échelle | Collision | Disparition (cm, manifeste) |
|---|---|---|---|---|---|
| arbres, palmiers | non | 30° | 0,85–1,15 | BlockAll (UCX du tronc) | grands arbres : jamais (0) → HLOD ; jeune conifère 25 000 ; palmiers 15 000–30 000 |
| arbustes, cycadophyte | oui (15°) | 35° | 0,8–1,2 | aucune | 8 000–12 000 |
| fougères, prêles, herbacées | oui (15–30°) | 35–40° | 0,75–1,2 | aucune | 3 500–6 000 ; *density scaling* actif |
| aquatiques | non | 20° | 0,8–1,2 | aucune | 5 000–6 000 |
| bois mort | oui (35°) | 40° | 0,8–1,2 | BlockAll (coques UCX) | 4 000 (branches) – 15 000 |

**LOD / Nanite** : feuillage en cartes alpha → LOD classiques (Nanite + masqué = surdessin coûteux en 5.4) ; au-delà
du LOD2 (taille d'écran < 0,06), HLOD *Mesh Approximation* de World Partition ou imposteurs cuits dans l'éditeur.
Bois mort opaque : Nanite possible sur le LOD0 (relief d'écorce réel). Budgets : voir `Docs/PERFORMANCE.md`.

---

## 3. Matériau du paysage `M_NoctisLandscape`

Créé par `Content/Python/setup_landscape_material.py` au chemin que `build_level.py` assigne au paysage
(`LANDSCAPE_MATERIAL = /Game/Noctis/Landscape/M_NoctisLandscape`).

### 3.1 Couches peintes (depuis les habitats simulés)

`factory.py --stages landscape` convertit les poids d'habitat `weight_<habitat>.png` en **8 couches pondérées**
à la résolution du heightmap (2017 × 2017, ligne 0 = bord nord) : bords adoucis (flou gaussien 1,6 px) et
légèrement ondulés (déformation fbm de ±2,5 px, pour casser la grille de 4 m), somme exactement 255 par pixel,
pixels non couverts → limon.

| Couche UE | Jeu de textures | Habitats simulés (poids) | Tuile |
|---|---|---|---|
| `Gravel` | RiverGravel | channel 1,0 | 1,5 m |
| `MudTracks` | RiverbankMudTracks | riverbank 1,0 | 2,5 m |
| `Sand` | PointBarSand | sandbar 1,0 | 2,0 m |
| `Backswamp` | BackswampOrganicMud | backswamp 1,0, oxbow_lake 1,0 | 2,0 m |
| `LitterBroadleaf` | LeafLitterBroadleaf | riparian_forest 1,0, levee_woodland 0,6 | 2,0 m |
| `Silt` | FloodplainSilt | floodplain_open 1,0, levee_woodland 0,4 | 2,0 m |
| `LitterConifer` | LeafLitterConifer | upland_conifer_forest 1,0 | 2,0 m |
| `Strata` | CutbankStrata | cutbank_cliff 1,0 | 4,0 m |

Fichiers : `Build/Environment/landscape/layer_<Couche>.png` → *Landscape Mode > Paint > Target Layers >
Import* (le script tente l'import automatique ; sinon il affiche la procédure). `LI_<Couche>` = LayerInfo
*weight-blended*.

### 3.2 Graphe du matériau

* **UV** : coordonnées monde XY / (tuile × 100) — pas de dépendance aux composants du paysage. Échantillonneurs
  partagés (*Shared: Wrap*) : 8 couches × 4 cartes sans atteindre la limite de 16 samplers.
* **Mélange** : trois `Landscape Layer Blend` (BaseColor, Normal, Roughness) en **LB_HeightBlend**, la carte
  Height de chaque couche servant de hauteur : le sable remplit les creux des graviers, la boue s'insinue entre les
  feuilles de litière.
* **Humidité** (`T_LandscapeMasks`, R = indice d'humidité statique de `export-ground`, B = risque de crue de
  `export-level`) : `wetness = saturate(R·StaticWetness + MPC.GroundWetness·(0,3 + 0,7·R) + B·MPC.Flooding)`
  assombrit l'albédo (×(1 − 0,45·wetness)) et rapproche la rugosité de 0,12 (alpha = 0,8·wetness) — la pluie et
  les crues de la simulation passent par `MPC_NoctisWeather`.
* **Sentiers** (G = corridors d'animaux prédits par `export-level`, plus `RT_NoctisTrails` = usure en temps réel
  dans le rectangle `MPC.TrailMaskRect`) : `max(G·StaticTrails, RT.R)` fait glisser le sol vers la couche
  **MudTracks** (boue piétinée à empreintes), sans repeindre le paysage.
* **Eau** (A) : masque d'eau fourni pour les effets de rive (non branché par défaut dans le matériau ; la PCG
  utilise `T_WeightChannel` / `T_WeightOxbowLake` pour ses exclusions).
* **Anti-répétition** : un échantillon très basse fréquence de la hauteur du limon module l'albédo.
* **Orientation des masques** : paramètre `LandscapeRectCm` = (0, −taille, taille, taille) ;
  U = X / taille, V = (Y + taille) / taille, donc V = 0 au bord nord (Y = −taille) = ligne 0 de l'image, comme
  le place `build_level.py` (paysage sur X ∈ [0, taille], Y ∈ [−taille, 0]).

Le rendu Blender de contrôle (`Tools/EnvironmentFactory/envlib/terrain_shading.py`) applique la même logique
(mêmes couches, mêmes poids, même assombrissement d'humidité) pour que les rendus `valley_*.png` soient fidèles à ce
qu'on verra en jeu.

---

## 4. Graphes PCG

`build_level.py` pose un `PCGVolume` couvrant tout le paysage (8 192 m) par graphe de `/Game/Noctis/PCG`
(`PCG_GRAPHS`), partitionné (grille 256 m) et généré **à la demande dans l'éditeur** (résultat cuit, pas de
génération à l'exécution), conformément à `world_partition.pcg` du JSON de niveau. Les textures de masque sont
importées par `build_level.py` sous `/Game/Noctis/Level/Textures/Terrain` (`foliage_angio_tree.png` →
`T_FoliageAngioTree`), transformées sur les bornes du paysage (U → +X, V → +Y, linéaire, sans sRGB).

### 4.1 `PCG_Noctis_Foliage` (créé par `import_environment.py`, une branche par type fonctionnel)

Comportement visé (celui du helper Blender, donc des rendus) : chaque point candidat est **gardé avec la
probabilité** `p = masque^γ × ((1 − a) + a × bruit_agrégation)` — densité proportionnelle à la densité simulée,
modulée par des bosquets — puis filtré (pente, eau, espacement).

```
Get Landscape Data
 └─ Surface Sampler            points/m² = per_ha / 10 000 (règle du type), Looseness 1
     └─ ∩ Get Texture Data     T_Foliage<Type> : Density = R (densité simulée du type fonctionnel)
         └─ Attribute Maths    Density = Density ^ density_gamma   (arbres 1,6 ; ginkgo 1,3 ; autres 1)
             └─ Density Noise  multiplie par (1 − a) + a·bruit, cellule = cluster_m, a = cluster_amp
                 └─ Tirage     Attribute Noise : U ∈ [0, 1] (graine du volume) → Attribute Filter : U < Density
                     └─ Filtre de pente     normale du paysage, pente < slope_max_deg
                         └─ Difference      eau : T_WeightChannel / T_WeightOxbowLake > 0,3, dilaté de water_buffer_m
                             │              (négatif = la plante peut entrer dans l'eau : prêles, aquatiques)
                             │              + clairières des POI (acteurs tagués noctis.clearing)
                             └─ Self Pruning   rayon = min_spacing_m (concurrence, pas de troncs imbriqués)
                                 └─ Transform Points   lacet 0–360°, échelle 0,82–1,15
                                     └─ Static Mesh Spawner   sélecteur pondéré (assets et parts de la règle),
                                                               distance de disparition du manifeste
```

Le graphe créé automatiquement par `import_environment.py` est une **ébauche simplifiée** (Surface Sampler ∩
texture → *Density Filter* 0,5–1 → Self Pruning → Transform → Spawner) : ce seuil ne garde que les zones où le
masque dépasse 0,5, donc des lisières plus franches que le tirage proportionnel ci-dessus. Remplacer le Density
Filter par la paire *Attribute Noise / Attribute Filter* et ajouter pente, eau et γ dans l'éditeur (nœuds
dont l'API Python n'a pas pu être vérifiée).

Règles (`envlib/catalog.py`, `PFT_RULES` ; densités = **hypothèses de jeu** à masque = 1, choisies pour une
structure de peuplement plausible : forêt riveraine fermée ≈ 250–400 tiges/ha). **γ (`density_gamma`)**, hypothèse
de jeu aussi : les masques d'arbres de la simulation sont diffus (≈ 0,15 en plaine ouverte, ≈ 0,5 en forêt
riveraine) ; avec des couronnes de 10–18 m, une réponse linéaire fermerait la canopée partout. γ = 1,6 garde la
mosaïque lisible (≈ 12 arbres de canopée/ha en plaine ouverte, ≈ 100/ha en forêt riveraine).

| Type fonctionnel (masque) | inst./ha | γ | Espacement | Pente max | Tampon eau | Agrégation | Assets (part) |
|---|---|---|---|---|---|---|---|
| conifer (`foliage_conifer`) | 220 | 1,6 | 5,5 m | 30° | 4 m | 120 m / 0,6 | Metasequoia 0,40, taxodioïde 0,15, Glyptostrobus 0,25, jeune 0,20 ; marais arrière : Glyptostrobus ×2, taxodioïde ×1,6 |
| angio_tree | 260 | 1,6 | 6,0 m | 30° | 3 m | 90 m / 0,5 | canopée A 0,42, lobé B 0,38, riverain C 0,20 ; berge : C ×4 |
| ginkgo | 40 | 1,3 | 8,0 m | 30° | 4 m | 150 m / 0,8 | Ginkgo |
| palm | 160 | 1 | 3,0 m | 30° | 3 m | 60 m / 0,9 | Sabalites haut 0,45, acaule 0,55 |
| angio_shrub | 900 | 1 | 1,6 m | 35° | 2 m | 30 m / 0,7 | ovale 0,55, petites feuilles 0,45 |
| cycadophyte | 120 | 1 | 2,0 m | 30° | 4 m | 40 m / 0,9 | cycadophyte |
| fern | 4 000 | 1 | 0,8 m | 40° | 1 m | 12 m / 0,8 | dressée 0,45, dentelle 0,35, marais 0,20 ; marais arrière : marais ×4 |
| horsetail | 2 600 | 1 | 0,9 m | 35° | −1,5 m | 10 m / 0,9 | sans rameaux 0,6, à verticilles 0,4 |
| angio_herb | 3 000 | 1 | 0,5 m | 40° | 1 m | 8 m / 0,8 | herbacée dicotylédone |
| aquatic_macrophyte | 3 000 (+ 900 en rive) | 1 | 0,8 m | 20° | −6 m | 10 m / 0,9 | émergente ruban 0,45, large 0,35, rosettes flottantes 0,20 (posées sur `water_surface`) |

Les biais d'habitat (`habitat_bias`) se font avec une branche supplémentaire par habitat :
`Get Texture Data T_Weight<Habitat>` → multiplication du poids de l'asset concerné dans le sélecteur
(ou deux spawners filtrés par le masque d'habitat).

### 4.2 Bois mort près des rivières

Le bois mort n'est pas un type de plante simulé : il est placé par **habitat** (`DEADWOOD_RULES`, hypothèses de
jeu), plus dense près du chenal — laisses de crue, chablis, arbres déracinés par l'érosion des berges concaves :

| Asset | inst./ha par habitat | Espacement |
|---|---|---|
| tronc tombé | forêt riveraine 12, levée 10, berge 10, marais arrière 8, conifères 8, barre 6 | 8 m |
| chicot | marais arrière 6, forêt riveraine 5, levée 5, berge 4 | 8 m |
| galette racinaire | berge concave (cutbank) 8, berge 6, barre 5 | 12 m |
| branches au sol | forêt riveraine 60, levée 50, conifères 40, marais 25, barre 15, berge 15, plaine 8 | 3 m |

Dans UE : `import_environment.py` ajoute à `PCG_Noctis_Foliage` une chaîne par couple (asset, habitat) :
Surface Sampler (inst./ha de l'habitat) ∩ `T_Weight<Habitat>` (poids de l'habitat simulé) → Density Filter →
Self Pruning (espacement de la règle) → Transform Points → Static Mesh Spawner `SM_Deadwood_*`. Le graphe
**`PCG_Noctis_RiverDebris`** de `build_level.py` (`T_WeightSandbar` + `T_MaskFloodRisk`, bois flotté sur les
barres et les lignes de crue) pose aujourd'hui des maillages provisoires : y choisir `SM_Deadwood_FallenLog`,
`SM_Deadwood_RootWad` et `SM_Deadwood_BranchLitter` (lacet aligné sur le courant) une fois ceux-ci importés.
Les autres graphes de `build_level.py` restent inchangés : `PCG_Noctis_Trails` (corridors, *Difference* contre les points de végétation pour garder les sentiers
dégagés) et `PCG_Noctis_Exposures` (falaises de berge ; texture `CutbankStrata`).

### 4.3 Équivalence avec le helper Blender

`Tools/CreatureFactory/scenes/vegetation_library.py` implémente la chaîne visée (échantillonnage par hectare ×
masque de simulation^γ, bruit d'agrégation, tirage proportionnel, pente, exclusion de l'eau, espacement minimal,
biais d'habitat, bois mort par habitat) en numpy, plus un LOD par distance à la caméra et un découpage au
frustum : les rendus de contrôle montrent la répartition que le graphe PCG complet doit produire.

---

## 5. Hypothèses scientifiques (rappel)

* Flore : arbres angiospermes dominants (feuilles de dicotylédones entières, lobées, lancéolées), conifères
  Cupressaceae s.l. (ports de type Metasequoia / Glyptostrobus ; un port « taxodioïde » à cime aplatie, analogue
  actuel *spéculatif*), palmiers costapalmés de type Sabalites (présence dans la Hell Creek même : INFERE),
  fougères, prêles, plantes aquatiques ; Ginkgo et cycadophytes rares, parce que `Data/Environments/hell_creek.json`
  les déclare. **Pas de graminées ni de prairies, pas de prairies fleuries modernes.**
* Ports, couleurs, densités par hectare, bois mort : hypothèses de design, notées dans chaque
  `Build/Environment/meta/<id>.json` (`botany.design_assumptions`, `botany.speculative`) avec le niveau de confiance
  du projet (ETABLI / FORTEMENT_ETAYE / INFERE / SPECULATIF).
* Empreintes de la texture `RiverbankMudTracks` : silhouettes génériques (grand théropode, hadrosaure, petit
  tridactyle) pour la lecture de jeu, pas des moulages d'ichnotaxons.

## 6. Limites et suites

* Les appels `# VERIFY` doivent être validés une fois sur un éditeur 5.4 ; le graphe PCG créé par script est un
  point de départ (les nœuds d'exclusion par pente / eau et les biais d'habitat sont à brancher à la main si
  l'API Python ne les expose pas).
* Pas d'imposteurs ni de HLOD générés ; pas de Pivot Painter.
* Le terrain de simulation (4,06 m) rend les berges verticales en pentes de 30–51° : les falaises sont des maillages
  (graphe Exposures), la texture `CutbankStrata` les habille.
