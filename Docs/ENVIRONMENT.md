# Environnement de Hell Creek dans Unreal Engine 5 — flore, sols, paysage, PCG

Ce document décrit comment les assets de `Tools/EnvironmentFactory` (voir son `README.md`) arrivent dans UE 5.4 et
comment le paysage et la végétation de la vallée sont pilotés par la **simulation** (`noctis_sim`, graine 7).
Il complète `Docs/LEVEL_DESIGN.md` (niveau, POI, routes) et `Content/Python/build_level.py` (import du paysage,
volumes PCG) sans les dupliquer : `build_level.py` crée le paysage et pose un volume par graphe PCG ; les scripts
décrits ici fournissent le **matériau** de ce paysage, les **maillages**, les **Foliage Types**, les **décalques**
et le graphe **PCG_Noctis_Foliage** que ces volumes utilisent.

Principe : la simulation décide *où* (habitats, densités simulées par type fonctionnel de plante, humidité, eau,
corridors d'animaux) ; l'Environment Factory décide *à quoi cela ressemble* (assets, textures) et fixe les
*densités de peuplement* (hypothèses de jeu, documentées). La **probabilité d'apparition** de chaque asset est
calculée une seule fois en Python (`envlib/pcgbake.py`) et utilisée à l'identique par les rendus Blender et par le
graphe PCG : les rendus de contrôle montrent la répartition qu'aura le jeu.

---

## 1. Chaîne de production

```
noctis_sim export-terrain --seed 7      heightmap.png (16 bits, 2017², 4,06 m), weight_<habitat>.png, foliage_<type>.png
noctis_sim export-ground  --seed 7      substrate_*.png, wetness.png, moisture_initial.png, water_surface.png, water_mask.png
noctis_sim export-level                 mask_corridor_use.png, mask_flood_risk.png, POI (Output/level, cf. LEVEL_DESIGN.md)
        │
Tools/EnvironmentFactory/factory.py     flora → ground → landscape (+ textures PCG) → manifest  (Build/Environment/)
        │
Unreal (Python Editor Script Plugin), dans cet ordre :
  1. setup_project.py                   MPC_NoctisWeather (vent, humidité, crue), réglages projet
  2. import_environment.py              /Game/Environment : textures (mips à couverture alpha préservée), matériaux
                                        maîtres + instances, maillages LOD0-3 + UCX, décalques de pistes, Foliage Types,
                                        textures de probabilité PCG, graphe /Game/Noctis/PCG/PCG_Noctis_Foliage
  3. setup_landscape_material.py        /Game/Noctis/Landscape/M_NoctisLandscape, LayerInfo, T_LandscapeMasks(2),
                                        import des 9 couches peintes si un paysage est ouvert
  4. build_level.py                     paysage (heightmap), matériau M_NoctisLandscape, volumes PCG, POI, départ
```

Chaque script accepte `--dry-run` hors Unreal (vérifie le manifeste et tous les fichiers, affiche ce qui serait
créé). Le dry-run de `import_environment.py` évalue en plus **le graphe PCG tel qu'il sera construit** (§4.4) et
sort en erreur si un asset ne peut apparaître nulle part. Les appels d'API Unreal qui n'ont pas pu être vérifiés
sur un éditeur 5.4 sont marqués `# VERIFY` et protégés : un échec est journalisé avec la procédure manuelle, la
suite continue.

Le **contrat de données** est `Build/Environment/manifest.json` : assets (fichiers FBX par LOD, matériaux,
triangles, bornes, tailles d'écran, distances de disparition, plage d'échelle, collision, Nanite, confiance
scientifique), matériaux (textures par carte, vent ou non), sols (tuile en mètres), décalques, couches du paysage,
règles de dispersion, et le plan PCG `Build/Environment/pcg/pcg_spawn.json`.

---

## 2. Assets dans `/Game/Environment`

| Dossier | Contenu |
|---|---|
| `Textures/Foliage`, `Textures/Bark`, `Textures/Ground`, `Textures/Decals` | atlas de feuilles 2K (BaseColor sRGB, Normal DirectX, Opacity, Translucency, Roughness), écorces 1K, sols 2K (BaseColor, Normal, Roughness, Height 16 bits), atlas de pistes, bruit macro |
| `PCG/Textures` | `T_PcgSpawn_<Asset>` (probabilité de maintien, RGBA identiques), `T_PcgWaterDepth` : données linéaires, sans mips, jamais streamées |
| `Materials/M_Noctis_Foliage` | Masked, Two Sided, *Two Sided Foliage* (clip 0,45) ; Subsurface Color = BaseColor × Translucency × teinte ; vent en WPO |
| `Materials/M_Noctis_Bark` | opaque, compatible Nanite et ISM ; vent en WPO faible |
| `Materials/M_Noctis_Decal` | décalque différé (DBuffer couleur + normale + rugosité), opacité douce |
| `Materials/Instances/MI_<slot>` | une instance par slot de matériau des FBX (`M_Leaf_*`, `M_Bark_*`, `*_Static` du bois mort, `M_Decal_Trackways`) |
| `Flora/<Catégorie>/SM_<Nom>` | LOD0 (+ collisions `UCX_`), LOD1, LOD2 et, pour les arbres et le grand palmier, LOD3 en cartes croisées |
| `Decals/SM_Decal_Trackway_*` | 4 décalques maillés de pistes (0,75 × 3 m) |
| `FoliageTypes/FT_<Nom>` | Foliage Types (outil Foliage) |

**Vent** : couleur de sommet R = souplesse (0 tronc → 1 bout de feuille), G = phase aléatoire par élément,
B = hauteur normalisée. WPO = sin(Temps × WindFrequency + 2π·(G + PerInstanceRandom)) × R × WindAmplitudeCm ×
rafale × (0,15 × WindSpeed) le long de `MPC_NoctisWeather.WindDirection`, avec
rafale = 0,55 + 0,45 sin(position monde · direction du vent / GustWavelengthCm − Temps × GustSpeed) : chaque
instance a sa propre phase (plus d'arbres qui se balancent en parfaite synchronie) et des fronts de rafale
traversent la vallée. Le WPO est derrière un *static switch* `UseWind` : les instances `*_Static` du bois mort le
coupent (aucun WPO compilé, pas d'invalidation des pages de Virtual Shadow Maps), le Foliage Type et le
descripteur du spawner PCG désactivent aussi *Evaluate World Position Offset*. Pivot Painter pourra remplacer ce
vent plus tard.

**Mips des cartes alpha** : les textures `Opacity` sont importées avec *Scale Mips for Alpha Coverage* (seuil
0,45 = clip du matériau) : la couverture de chaque mip reste celle de la pleine résolution, les houppiers ne
« fondent » plus avec la distance.

**Foliage Types** (réglages par catégorie, `FOLIAGE_SETTINGS` de `import_environment.py` ; plage d'échelle par
asset depuis le manifeste) :

| Catégorie | Aligné sur la normale | Pente max | Échelle | Collision | Disparition (cm, manifeste) |
|---|---|---|---|---|---|
| arbres, palmiers | non | 30° | 0,78–1,1, plafonnée à la hauteur du type fonctionnel (30 m conifères, 25 m angiospermes, 20 m ginkgo, 6 m palmier) | BlockAll (UCX du tronc) | grands arbres : jamais (0) → LOD3 puis HLOD ; jeune conifère 25 000 ; palmiers 15 000–30 000 |
| arbustes, cycadophyte | oui (15°) | 35° | 0,8–1,2 | aucune | 8 000–12 000 |
| fougères, prêles, herbacées | oui (15–30°) | 35–40° | 0,75–1,2 | aucune | 3 500–6 000 ; *density scaling* actif |
| aquatiques | non | 20° | 0,8–1,2 | aucune | 4 000–6 000 |
| bois mort | oui (35°) | 40° | 0,8–1,2 | BlockAll (coques UCX) | 4 000 (branches) – 15 000 ; WPO désactivé |

**LOD / Nanite / imposteurs** : feuillage en cartes alpha → LOD classiques (Nanite + masqué = surdessin coûteux en
5.4). Arbres et grand palmier : **LOD3 = 3 cartes croisées à 60°** (6 triangles, texture rendue depuis le LOD1 :
albédo, opacité, normale), taille d'écran 0,035 ; au-delà, HLOD *Mesh Approximation* de World Partition
(couche `HLOD_Foliage_Instanced` du JSON de niveau) avant toute mesure de performance. Bois mort opaque : Nanite
possible sur le LOD0 (relief d'écorce réel). Budgets : voir `Docs/PERFORMANCE.md`.

**Décalques de pistes** (`SM_Decal_Trackway_Theropod`, `_Hadrosaur`, `_SmallTridactyl`, `_Trampled`) : plans fins
subdivisés (0,75 × 3 m, +Y = sens de marche) portant `MI_Decal_Trackways` (domaine *Deferred Decal*). Ce sont des
maillages, donc instanciables par PCG ; ils remplacent les empreintes qui étaient cuites dans la texture de berge
et qui se répétaient tous les 2,5 m. Silhouettes génériques (grand théropode, hadrosaure, petits tridactyles),
pas des moulages d'ichnotaxons.

---

## 3. Matériau du paysage `M_NoctisLandscape`

Créé par `Content/Python/setup_landscape_material.py` au chemin que `build_level.py` assigne au paysage
(`LANDSCAPE_MATERIAL = /Game/Noctis/Landscape/M_NoctisLandscape`).

### 3.1 Couches peintes (depuis les habitats simulés)

`factory.py --stages landscape` convertit les poids d'habitat `weight_<habitat>.png` en **9 couches pondérées**
à la résolution du heightmap (2017 × 2017, ligne 0 = bord nord) : bords adoucis (flou gaussien 1,6 px) et
légèrement ondulés (déformation fbm de ±2,5 px, pour casser la grille de 4 m), somme exactement 255 par pixel,
pixels non couverts → limon.

| Couche UE | Jeu de textures | Habitats simulés (poids) | Tuile |
|---|---|---|---|
| `Gravel` | RiverGravel | channel 1,0 | 4,0 m |
| `Mud` | RiverbankMud | riverbank 1,0 (+ sentiers, §3.2) | 5,0 m |
| `Sand` | PointBarSand | sandbar 1,0 | 6,0 m |
| `Backswamp` | BackswampOrganicMud | backswamp 1,0, oxbow_lake 1,0 | 5,0 m |
| `LitterBroadleaf` | LeafLitterBroadleaf | riparian_forest 1,0, levee_woodland 0,6 | 4,0 m |
| `Silt` | FloodplainSilt | floodplain_open 1,0, levee_woodland 0,4, moins les creux secs | 6,0 m |
| `SiltDry` | FloodplainSiltDry | floodplain_open × creux secs (humidité initiale < 0,22, sans canopée) | 5,0 m |
| `LitterConifer` | LeafLitterConifer | upland_conifer_forest 1,0 | 4,0 m |
| `Strata` | CutbankStrata | cutbank_cliff 1,0, seulement sur les pentes > 20–28° (le reste → Silt) | 4,0 m |

Le limon de plaine (`Silt`) est un sol **humide** couvert de débris de fougères et d'herbacées, de croûtes de
mousses et d'hépatiques, avec seulement quelques fentes discrètes : c'est la plaine humide et végétalisée du
Maastrichtien, pas les badlands actuels. Les fentes de dessiccation marquées n'existent que dans `SiltDry`
(≈ 7 % du limon de plaine pour la graine 7, creux en cours d’assèchement).

Fichiers : `Build/Environment/landscape/layer_<Couche>.png` → *Landscape Mode > Paint > Target Layers >
Import* (le script tente l'import automatique ; sinon il affiche la procédure). `LI_<Couche>` = LayerInfo
*weight-blended*.

### 3.2 Graphe du matériau

* **Anti-répétition** (toutes les couches sauf Strata) : deux échantillons par carte — UV = XY monde / (tuile × 100)
  et un second échantillon tourné d'un angle propre à la couche (0,6 + 0,9 i rad), agrandi de 1,618 et décalé —
  mélangés par un **bruit macro indépendant** (`T_Ground_MacroNoise`.G, tuile de 240 m) ; l'albédo glisse en plus
  vers un troisième échantillon 4,3 fois plus grand avec la distance à la caméra (15–60 m). Les jeux de sol ne
  contiennent plus d'élément mémorable (empreintes, grand réseau de fentes) ; tuiles de 4 à 6 m (texel 2–3 mm).
  Coût : 9 lectures de texture par couche (8 pour Strata) ; le paysage ne compile que les couches présentes dans
  chaque composant. Échantillonneurs partagés (*Shared: Wrap*) : pas de limite de 16 samplers.
* **Strata en projection biplanaire** : U = X ou Y monde (l'axe horizontal le plus parallèle à la face, choisi par
  la normale du paysage, transition douce), V = −Z / tuile : les bancs suivent l'altitude (les courbes de niveau),
  au lieu d'une projection verticale qui les ferait courir en diagonale sur la pente.
* **Mélange** : trois `Landscape Layer Blend` (BaseColor, Normal, Roughness) en **LB_HeightBlend**, la carte
  Height de chaque couche servant de hauteur.
* **Humidité** (`T_LandscapeMasks`, R = indice d'humidité statique de `export-ground`, B = risque de crue de
  `export-level` ; `T_LandscapeMasks2`.G = bande de rive 0–12 m) :
  `wetness = saturate(R·StaticWetness + MPC.GroundWetness·(0,3 + 0,7·R) + B·MPC.Flooding + 0,6·rive)`
  assombrit l'albédo (×(1 − 0,45·wetness)) et rapproche la rugosité de 0,3 (alpha = 0,7·wetness : boue mouillée
  satinée ; une cible de 0,12 faisait des berges un miroir gris-bleu du ciel).
* **Sentiers** (G = corridors prédits, plus `RT_NoctisTrails` dans `MPC.TrailMaskRect`) :
  `max(G·StaticTrails, RT.R)` fait glisser le sol vers la couche **Mud** ; les empreintes viennent des décalques.
* **Couvert bas lointain** (`T_LandscapeMasks2`.R = fougères + prêles + herbacées + arbustes simulés) : teinte
  vert-brun `GroundCoverTint` d'intensité couvert × (0,25 → 0,85 entre 25 et 65 m de la caméra), pour que le sol de
  la plaine reste coloré là où ces petites plantes ne sont plus dessinées (disparition à 35–60 m).
* **Variation macro** : `T_Ground_MacroNoise`.R (bruit indépendant, pas une carte de sol réutilisée) module
  l'albédo (0,82 + 0,36 R).
* **Eau** (`T_LandscapeMasks`.A) : fournie pour les effets de rive (non branchée par défaut).
* **Orientation des masques** : paramètre `LandscapeRectCm` = (0, −taille, taille, taille) ;
  U = X / taille, V = (Y + taille) / taille, donc V = 0 au bord nord (Y = −taille) = ligne 0 de l'image, comme
  le place `build_level.py` (paysage sur X ∈ [0, taille], Y ∈ [−taille, 0]).

### 3.3 Rendu Blender de contrôle : ce qui est identique, ce qui ne l'est pas

`Tools/EnvironmentFactory/envlib/terrain_shading.py` reproduit le matériau : mêmes 9 couches et mêmes poids (lus
dans `envlib/landscape.LAYERS`, `oxbow_lake` compris dans Backswamp, creux secs `SiltDry`, Strata limitée aux
pentes), même anti-répétition (angles, facteur 1,618, bruit macro, échantillon lointain ×4,3), même projection
biplanaire de Strata, même humidité (MPC à leurs valeurs par défaut : GroundWetness = Flooding = 0), même
superposition des sentiers vers Mud (corridors × 0,6), même teinte de couvert lointain et même variation macro.
Différences, propres au rendu :

* `canopy_shade` (vue aérienne seulement) assombrit le sol des habitats boisés, parce que les houppiers en cartes
  sont plus poreux qu'une canopée réelle ;
* le terrain du rendu porte un micro-relief procédural (bruit isotrope, seulement sur les maillages assez fins,
  pas sur les barres de sable) et un patch fin près de la caméra ; le paysage UE n'a que le heightmap (4,06 m) ;
* les masques du rendu sont rééchantillonnés à la résolution du recadrage (2 048 px), avec la même déformation ;
* l'eau est un shader Cycles simple, pas `ANoctisWaterActor` ;
* les décalques sont des maillages conformés au terrain (lancer de rayons), pas des décalques différés.

---

## 4. Graphes PCG

`build_level.py` pose un `PCGVolume` couvrant tout le paysage (8 192 m) par graphe de `/Game/Noctis/PCG`
(`PCG_GRAPHS`), partitionné (grille 256 m) et généré **à la demande dans l'éditeur** (résultat cuit), conformément à
`world_partition.pcg` du JSON de niveau.

### 4.1 Probabilité d'apparition cuite (`envlib/pcgbake.py`)

La première version du graphe intersectait les points avec le masque simulé puis gardait ceux dont la densité
dépassait 0,5 (Density Filter [0,5 ; 1]). Or les masques simulés n'atteignent jamais 0,5 pour la plupart des types
(fougères 0,498, palmiers 0,18, ginkgo 0,24, cycadophytes 0,15, aquatiques 0,03) : ces groupes n'apparaissaient
jamais, et les arbres apparaissaient à pleine densité là où le seuil passait. Désormais, pour **chaque asset**, une
texture `T_PcgSpawn_<Asset>` porte la probabilité de maintien de chaque point candidat :

```
p = masque^γ × ((1 − a) + a × bruit_agrégation(cluster_m))        (type fonctionnel ; γ = density_gamma)
    [aquatiques : max(p, calm_water_per_ha / per_ha × Σ poids_habitat × aptitude × bruit)]
  × part de l'asset avec biais d'habitat doux (1 + Σ poids_h × (biais − 1), normalisé)
  × eau : distance signée à la rive > water_buffer_m, ou bande propre à l'asset (aquatiques)
  × pente < slope_max_deg × exclusion chenal / bras mort (poids > 0,2–0,45) × clairières des POI
```

Les clairières sont les POI que `build_level.py` étiquette `noctis.clearing` (camp de base, repli hors crue,
affûts ; rayon du JSON de niveau, moitié pour les petites plantes). Le bruit d'agrégation est un bruit isotrope à
bande limitée défini en coordonnées monde (`envlib/fields.py`), identique dans le rendu et dans la texture.
Chaque texture est normalisée par son maximum (texture = p / max p, toute la plage 8 bits) et la chaîne
échantillonne `per_ha × max p` candidats par hectare : même densité attendue, moins de points. Les quatre canaux
RGBA portent la même valeur : la lecture ne dépend pas du canal choisi par *Get Texture Data* (dont le défaut a été
Alpha en UE 5.x) ; le script règle quand même le canal sur Rouge et relit la valeur.

Résolution : 1 024 px (texels de 8 m) pour les arbres, palmiers, arbustes et bois mort ; 2 048 px (4 m) pour les
petites plantes. Une bande d'eau plus étroite qu'un texel est élargie d'un demi-texel de chaque côté et sa valeur
réduite d'autant (densité attendue conservée).

### 4.2 `PCG_Noctis_Foliage` (créé par `import_environment.py`, une chaîne par asset)

```
Get Landscape Data
 └─ Surface Sampler        candidats/m² = per_ha × max p / 10 000, demi-étendue E petite (sinon plafond de densité), Looseness 1
     └─ ∩ Get Texture Data T_PcgSpawn_<Asset> (canal R explicite, transformation absolue = bornes du paysage)
         └─ Density Noise  Add, [0, 1], densité source inversée  → densité' = (1 − p) + U
             └─ Density Filter [0 ; 0,99999]                     → garde U < p : tirage aléatoire, pas un seuil
                 └─ Bounds Modifier  Set, ± min_spacing_m / 2
                     └─ Self Pruning  Large to small, aléatoire   → espacement minimal de la règle
                         └─ Difference (Binary) contre les chaînes déjà posées : arbres et palmiers contre tous les
                            │  arbres / palmiers précédents (conifères → angiospermes → ginkgo → palmiers, comme le
                            │  helper Blender : plus de troncs imbriqués entre types) ; autres types contre les assets
                            │  précédents du même type
                            └─ [rosettes flottantes : densité remise à 1 ∩ T_PcgWaterDepth → $Position.Z += profondeur]
                                └─ Transform Points   lacet 0–360°, inclinaison ±2° (arbres) / ±4°, échelle du manifeste
                                    └─ Static Mesh Spawner   maillage de l'asset, disparition du manifeste, WPO coupé (bois mort)
```

La demi-étendue des points du Surface Sampler n'est **pas** l'espacement : UE découpe la surface en cellules de
2E(1 + Looseness) et garde au plus un point par cellule (VERIFY) ; une étendue égale à l'espacement plafonnerait
la densité (pour les arbres à ~70–80 tiges/ha). L'espacement est donc imposé après le tirage par *Bounds Modifier*
+ *Self Pruning*. Les rosettes flottantes sont élevées de la profondeur d'eau de l'export (le paysage est le fond
du chenal) ; si l'API des sélecteurs d'attributs manque, le script retombe sur un décalage constant égal à la
profondeur médiane de la chaîne et le signale. L'eau d'UE (`ANoctisWaterActor`) varie avec les crues : ces
plantes restent un placement « à l'étiage de l'export ».

Décalques de pistes : une chaîne `T_PcgSpawn_TrackwayBank` (berges et barres à 0,3–8 m de l'eau, plus dense sur les
corridors prédits, lacet aléatoire) et une chaîne le long des splines `noctis.trail` de `build_level.py`
(*Spline Sampler* tous les 7 m, tirage 0,35, orientation sur la tangente + 90° car le décalque marche selon +Y).

### 4.3 Règles (`envlib/catalog.py`, `PFT_RULES`)

Densités = **hypothèses de jeu** à masque = 1, choisies pour une structure de peuplement plausible (forêt riveraine
fermée ≈ 250–400 tiges/ha). **γ (`density_gamma`)**, hypothèse de jeu aussi : les masques d'arbres sont diffus
(≈ 0,15 en plaine ouverte, ≈ 0,5 en forêt riveraine) ; γ = 1,6 garde la mosaïque lisible.

| Type fonctionnel (masque) | inst./ha | γ | Espacement | Pente max | Eau | Agrégation | Assets (part) |
|---|---|---|---|---|---|---|---|
| conifer | 220 | 1,6 | 5,5 m | 30° | > 4 m | 120 m / 0,6 | Metasequoia 0,40, taxodioïde 0,15, Glyptostrobus 0,25, jeune 0,20 ; marais arrière : Glyptostrobus ×2, taxodioïde ×1,6 |
| angio_tree | 260 | 1,6 | 6,0 m | 30° | > 3 m | 90 m / 0,5 | canopée A 0,32, lobé B 0,30, riverain C 0,18, étalé D 0,20 ; berge : C ×4 ; plaine ouverte : D ×2 |
| ginkgo | 40 | 1,3 | 8,0 m | 30° | > 4 m | 150 m / 0,8 | Ginkgo |
| palm | **30** | 1 | 3,0 m | 30° | > 3 m | 60 m / 0,9 | Sabalites haut 0,45, acaule 0,55 (rares : ≈ 1 palmier/ha, 2 % des tiges ligneuses) |
| angio_shrub | 900 | 1 | 1,6 m | 35° | > 2 m | 30 m / 0,7 | ovale 0,55, petites feuilles 0,45 |
| cycadophyte | 120 | 1 | 2,0 m | 30° | > 4 m | 40 m / 0,9 | cycadophyte |
| fern | 4 000 | 1 | 0,8 m | 40° | > 1 m | 12 m / 0,8 | dressée 0,45, dentelle 0,35, marais 0,20 ; marais arrière : marais ×4 |
| horsetail | 2 600 | 1 | 1,1 m | 35° | > −1,5 m | 12 m / 0,9 | sans rameaux 0,6, à verticilles 0,4 |
| angio_herb | 3 000 | 1 | 0,5 m | 40° | > 1 m | 12 m / 0,8 | herbacée dicotylédone |
| aquatic_macrophyte | 3 000 (+ 900 en eaux calmes) | 1 | 0,8 m | 20° | bandes | 12 m / 0,9 | rosettes flottantes 0,65 (−8 à −0,4 m), rosette semi-émergente 0,20 (−1,5 à +0,3 m) et émergente large 0,15 (−1,2 à +0,5 m) |

Aquatiques : le masque simulé est quasi nul dans le chenal actif (max 0,031) ; l'ancien ajout « rive » bordait
toutes les rives calmes, chenal compris. Le complément `calm_water_per_ha` suit maintenant les aptitudes d'habitat
des données (`hell_creek.json` : bras mort 1, marais arrière 0,6, chenal 0,05) : rares le long de la rivière,
abondantes dans les bras morts et les mares des marais arrière quand la graine en produit (la graine 7 n'a ni bras
mort ni eau libre dans les marais arrière : les aquatiques y sont donc rares — c'est la réponse de la simulation).
Les rosettes flottantes (seul taxon aquatique appuyé par les données, type Quereuxia) dominent ; les émergentes sont
une minorité étiquetée SPÉCULATIF.

### 4.4 Contrôle hors Unreal (`import_environment.py --dry-run`)

Le dry-run construit le même plan que l'éditeur (`pcg_chain_plan`) et affiche pour chaque chaîne : part de la carte
où l'asset peut apparaître, probabilité moyenne, instances attendues par hectare, candidats/ha, plafond du Surface
Sampler, espacement, nombre de chaînes bloquantes ; puis la densité moyenne de tiges arborescentes et la part des
palmiers. **Erreur** (code de sortie 1) si un asset ne peut apparaître nulle part (couverture 0), si une texture
manque ou a changé depuis le calcul de ses statistiques (SHA-1), ou si le filtre n'est pas un tirage aléatoire.
Graine 7 : ≈ 56 tiges arborescentes/ha avant élagage en moyenne sur la carte, dont 2 % de palmiers.

### 4.5 Bois mort près des rivières

Le bois mort n'est pas un type de plante simulé : il est placé par **habitat** (`DEADWOOD_RULES`, hypothèses de
jeu), plus dense près du chenal.

| Asset | inst./ha par habitat | Espacement |
|---|---|---|
| tronc tombé | forêt riveraine 12, levée 10, berge 10, marais arrière 8, conifères 8, barre 6 | 8 m |
| chicot | marais arrière 6, forêt riveraine 5, levée 5, berge 4 | 8 m |
| galette racinaire | berge concave (cutbank) 8, berge 6, barre 5 | 12 m |
| branches au sol | forêt riveraine 60, levée 50, conifères 40, marais 25, barre 15, berge 15, plaine 8 | 3 m |

Partage entre graphes (plus de double placement) : `PCG_Noctis_Foliage` ne porte que le bois mort des habitats
**forestiers** (forêt riveraine, levée, marais arrière, conifères, plaine) ; le bois flotté des **barres, berges et
falaises de berge** revient à **`PCG_Noctis_RiverDebris`** de `build_level.py` (graphe construit à la main) : y
brancher `T_PcgSpawn_DriftwoodLog / Stump / Rootwad / Branches` (mêmes probabilités cuites, mêmes densités) avec
`SM_Deadwood_FallenLog`, `SM_Deadwood_RootWad`, `SM_Deadwood_Stump` et `SM_Deadwood_BranchLitter` (lacet aligné
sur le courant). Les autres graphes de `build_level.py` restent inchangés : `PCG_Noctis_Trails` (corridors,
*Difference* contre les points de végétation) et `PCG_Noctis_Exposures` (falaises de berge ; texture `CutbankStrata`).

### 4.6 Équivalence avec le helper Blender

`Tools/CreatureFactory/scenes/vegetation_library.py` importe les mêmes fonctions (`envlib/pcgbake.py`,
`envlib/fields.py`) : probabilité de maintien, parts et biais d'habitat, bandes d'eau, pente, exclusion du chenal,
clairières, puis espacement minimal et blocage des arbres par les plus grands. Il ajoute un LOD par distance à la
caméra, un découpage au frustum et une **disparition progressive** : la densité de chaque catégorie décroît
linéairement sur le dernier quart de sa distance maximale, avec un rayon tiré par instance (plus d'anneau visible).

---

## 5. Hypothèses scientifiques (rappel)

* Flore : arbres angiospermes dominants (feuilles de dicotylédones entières, lobées, lancéolées, cordées ; quatre
  silhouettes), conifères Cupressaceae s.l. (ports de type Metasequoia / Glyptostrobus ; un port « taxodioïde » à
  cime large et aplatie, rameaux alternes, analogue actuel *spéculatif*, ≤ 30 m), palmiers costapalmés de type
  Sabalites (présence INFÉRÉE et **rare** : ≈ 2 % des tiges), fougères, prêles (tiges dressées issues de rhizomes,
  strobiles allongés et apiculés sur une minorité de tiges), plantes aquatiques (rosettes flottantes dominantes) ;
  Ginkgo et cycadophytes rares, parce que `Data/Environments/hell_creek.json` les déclare. **Pas de graminées ni
  de prairies, pas de prairies fleuries modernes** ; plaine ouverte = limon humide, fougères, prêles, herbacées
  basses et arbustes.
* Ports, couleurs, densités par hectare, bois mort, décalques : hypothèses de design, notées dans chaque
  `Build/Environment/meta/<id>.json` (`botany.design_assumptions`, `botany.speculative`) avec le niveau de confiance
  du projet (ETABLI / FORTEMENT_ETAYE / INFERE / SPECULATIF).
* Empreintes (décalques) : silhouettes génériques (grand théropode, hadrosaure, petit tridactyle) pour la lecture de
  jeu, pas des moulages d'ichnotaxons.

## 6. Limites et suites

* Les appels `# VERIFY` doivent être validés une fois sur un éditeur 5.4 (classes et broches des nœuds PCG,
  sélecteurs d'attributs, propriétés d'alpha coverage, *static switch* des instances, mode de décalque).
* Imposteurs : LOD3 en cartes croisées seulement ; pas d'imposteur octaédrique ; HLOD à cuire dans l'éditeur.
* Le terrain de simulation (4,06 m) rend les berges verticales en pentes de 30–51° : les falaises sont des maillages
  (graphe Exposures), la texture `CutbankStrata` les habille en projection biplanaire.
* Les rosettes flottantes sont posées sur le niveau d'eau de l'export, pas sur l'eau dynamique.
* Dans les rendus Blender, les berges d'érosion gardent des gradins et une arête rectiligne hérités de la grille
  de 4 m de la simulation : le patch fin de 0,1 m n'ajoute que du micro-relief, il ne remodèle pas la berge.
* Avec la graine 7, les bras morts calmes sont peu nombreux : les plantes aquatiques restent rares à l'échelle de
  la vallée (comportement voulu des masques, pas un défaut du graphe).
