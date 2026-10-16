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
| `flora` | atlas de feuilles, écorces, maillages LOD0–2, collisions UCX, FBX, bibliothèque `.blend`, métadonnées | `textures/`, `fbx/`, `meta/`, `library/` |
| `ground` | 8 jeux de textures de sol 2K raccordables | `ground/` |
| `landscape` | couches de peinture du paysage UE (2017²) + masque RGBA humidité/corridors/crue/eau | `landscape/` |
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

## Assets de flore et de bois mort (26)

Échelle métrique réelle, pivot à la base (0,0,0 au sol), Z vers le haut. Triangles LOD0 / LOD1 / LOD2 (valeurs de
`manifest.json`, régénérées à chaque passage) :

| Asset (`id`) | Nom UE | Type simulé | Hauteur | Triangles | Confiance (présence) |
|---|---|---|---|---|---|
| `conifer_metasequoia` | SM_Conifer_Metasequoia_A | conifer | 28,9 m | 53 495 / 28 617 / 5 529 | FORTEMENT_ETAYE |
| `conifer_taxodium` | SM_Conifer_Taxodioid_B | conifer | 35,2 m | 35 804 / 18 716 / 3 663 | FORTEMENT_ETAYE (port SPECULATIF) |
| `conifer_glyptostrobus` | SM_Conifer_Glyptostrobus_C | conifer | 20,4 m | 11 131 / 5 829 / 2 478 | FORTEMENT_ETAYE |
| `conifer_young` | SM_Conifer_Metasequoia_Young | conifer | 6,5 m | 3 311 / 1 182 / 196 | FORTEMENT_ETAYE |
| `broadleaf_canopy_a` | SM_Broadleaf_Canopy_A | angio_tree | 24,0 m | 19 898 / 9 796 / 2 170 | ETABLI |
| `broadleaf_canopy_b` | SM_Broadleaf_Lobed_B | angio_tree | 17,3 m | 9 665 / 5 121 / 1 334 | ETABLI |
| `broadleaf_riparian_c` | SM_Broadleaf_Riparian_C | angio_tree | 12,4 m | 14 959 / 3 945 / 1 739 | ETABLI |
| `ginkgo_tree` | SM_Ginkgo_A | ginkgo | 16,9 m | 8 216 / 2 289 / 1 073 | FORTEMENT_ETAYE |
| `shrub_ovate` | SM_Shrub_Ovate_A | angio_shrub | 2,9 m | 1 890 / 766 / 380 | ETABLI (groupe) |
| `shrub_small_leaf` | SM_Shrub_SmallLeaf_B | angio_shrub | 1,8 m | 2 222 / 706 / 476 | ETABLI (groupe) |
| `palm_sabalites_tall` | SM_Palm_Sabalites_Tall | palm | 7,6 m | 6 316 / 2 072 / 527 | INFERE |
| `palm_sabalites_short` | SM_Palm_Sabalites_Acaulescent | palm | 1,8 m | 3 588 / 1 208 / 287 | INFERE |
| `fern_tufted` | SM_Fern_Tufted_A | fern | 0,85 m | 760 / 180 / 72 | FORTEMENT_ETAYE (groupe) |
| `fern_lace` | SM_Fern_Bipinnate_B | fern | 0,38 m | 696 / 160 / 60 | FORTEMENT_ETAYE (groupe) |
| `fern_marsh` | SM_Fern_Marsh_C | fern | 1,16 m | 888 / 240 / 96 | FORTEMENT_ETAYE (groupe) |
| `horsetail_unbranched` | SM_Horsetail_Stand_A | horsetail | 1,22 m | 3 350 / 952 / 234 | INFERE |
| `horsetail_branched` | SM_Horsetail_Stand_B | horsetail | 1,19 m | 2 286 / 504 / 108 | INFERE |
| `emergent_strap` | SM_Aquatic_Emergent_Strap | aquatic_macrophyte | 1,06 m | 576 / 256 / 48 | INFERE |
| `emergent_broadleaf` | SM_Aquatic_Emergent_Broad | aquatic_macrophyte | 0,81 m | 572 / 112 / 10 | INFERE |
| `floating_rosettes` | SM_Aquatic_Floating_Rosettes | aquatic_macrophyte | surface | 28 / 22 / 6 | INFERE |
| `cycadophyte` | SM_Cycadophyte_A | cycadophyte | 1,6 m | 588 / 248 / 81 | INFERE (rare) |
| `herb_dicot` | SM_Herb_Dicot_A | angio_herb | 0,22 m | 64 / 18 / 10 | INFERE |
| `deadwood_log` | SM_Deadwood_FallenLog | — | 9,6 m de long | 9 418 / 372 / 84 | INFERE |
| `deadwood_stump` | SM_Deadwood_Stump | — | 1,6 m | 3 360 / 260 / 113 | INFERE |
| `deadwood_rootwad` | SM_Deadwood_RootWad | — | 3,1 m | 6 156 / 684 / 178 | INFERE |
| `deadwood_branches` | SM_Deadwood_BranchLitter | — | 0,3 m | 1 044 / 558 / 60 | INFERE |

Ginkgo et cycadophyte ne sont produits que parce que `Data/Environments/hell_creek.json` les déclare présents
(Ginkgo FORTEMENT_ETAYE ; cycadophytes rares, INFERE). **Aucune graminée, aucune prairie, aucune herbacée à
fleurs voyantes** : les milieux ouverts sont couverts de fougères, prêles, herbacées dicotylédones basses et
arbustes, comme l'indiquent les données du projet. Les « joncs » aquatiques sont des émergentes à feuilles en
ruban et à feuilles larges (monocotylédones / dicotylédones aquatiques génériques), pas des graminées.

### Construction

* **Arbres** (`envlib/treegen.py`) : squelette récursif (tronc, ordres 1 à 3, gravitropisme, enveloppe de
  houppier, contreforts et empattement cannelé des Cupressaceae), tubes d'écorce à repère de transport parallèle,
  cartes alpha de rameaux / bouquets de feuilles posées sur les derniers ordres. LOD1 et LOD2 : moins d'ordres en
  géométrie, moins de côtés, cartes plus grandes et moins nombreuses (même silhouette).
* **Plantes non ramifiées** (`envlib/plants.py`) : palmiers costapalmés (stipe à bases foliaires, éventails à
  segments plissés), touffes de frondes (3 formes : dressée pennée, bipennée basse en dentelle, fougère de marais
  arquée), prêles (tiges articulées à gaines, verticilles de rameaux pour la variante B, strobiles terminaux),
  émergentes, rosettes flottantes, cycadophyte, herbacée basse.
* **Bois mort** (`envlib/deadwood.py`) : tronc couché à moitié écorcé, chicot à cassure en esquilles, galette
  racinaire avec motte de limon, branches au sol. LOD0 « Nanite-ready » : relief d'écorce déplacé depuis les
  cartes de hauteur.
* **Textures de feuilles** (`envlib/foliage.py`, `envlib/paint.py`) : atlas 2048 px peints procéduralement
  (nervation, marges, dégâts d'insectes, variation de teinte) → BaseColor (sRGB), Normal (DirectX),
  Opacity, Translucency, Roughness. **Écorces** (`envlib/bark.py`) : jeux raccordables 1024 px BaseColor /
  Normal / Roughness / Height (Cupressaceae fibreuse, angiosperme lisse et rugueuse, stipe de palmier, bois mort,
  tige de prêle, tige verte, motte de terre).
* **Couleur de sommet = vent** : R = souplesse (0 tronc → 1 bout de feuille), G = phase aléatoire par élément,
  B = hauteur normalisée. Le matériau maître UE (`M_Noctis_Foliage`) en tire un World Position Offset.
* **Collision** : `UCX_<Nom>_NN` convexes dans le FBX du LOD0 (troncs jusqu'à 4 m, bois mort en plusieurs
  coques) ; fougères, prêles, herbacées, émergentes et branches au sol sans collision (traversables).
* **LOD / Nanite / imposteurs** : trois LOD classiques avec tailles d'écran recommandées
  (`lod_screen_size`, ex. 1,0 / 0,35 / 0,12 pour les arbres). Le feuillage en cartes alpha reste en LOD
  classiques en UE 5.4 (Nanite + matériau masqué = surdessin coûteux) ; au-delà du LOD2 (taille d'écran < 0,06)
  utiliser un HLOD (Mesh Approximation) ou un imposteur octaédrique cuit dans l'éditeur (outil tiers) — non
  généré ici. Le bois mort (opaque) peut activer Nanite sur le LOD0.

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

## Matériaux de sol (8 jeux 2K raccordables, `envlib/ground.py`)

| Jeu | Tuile | Habitats / substrats | Contenu |
|---|---|---|---|
| `FloodplainSilt` | 2,0 m | floodplain_open, levee_woodland | limon, fentes de dessiccation, débris fins |
| `RiverbankMudTracks` | 2,5 m | riverbank | boue humide, empreintes (théropode, hadrosaure, petit tridactyle), flaques |
| `PointBarSand` | 2,0 m | sandbar | sable à rides de courant, débris végétaux, petits galets |
| `LeafLitterBroadleaf` | 2,0 m | riparian_forest, levee_woodland | feuilles de dicotylédones, brindilles |
| `LeafLitterConifer` | 2,0 m | upland_conifer_forest | rameaux caducs et aiguilles de Cupressaceae, petits cônes |
| `BackswampOrganicMud` | 2,0 m | backswamp | vase organique, eau stagnante, débris |
| `CutbankStrata` | 4,0 m | cutbank_cliff | alternance grès / silts / argiles / niveaux carbonés (affleurements) |
| `RiverGravel` | 1,5 m | channel | galets roulés, matrice sableuse, film d'algues |

Cartes : `T_Ground_<Jeu>_BaseColor.png` (sRGB), `_Normal.png` (DirectX), `_Roughness.png`, `_Height.png` (16 bits).
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

* `assets_sheet.png` : vignette Cycles du LOD0 de chaque asset (triangles des 3 LOD, hauteur) + les 8 sols.
* `valley_aerial.png` : vol bas au-dessus des méandres à l'heure dorée, brume.
* `valley_ground.png` : à hauteur d'œil sur une berge, prêles et fougères, conifères et feuillus derrière,
  brume matinale.
Les deux scènes utilisent exactement le helper public `Tools/CreatureFactory/scenes/vegetation_library.py`
(terrain recadré depuis la simulation, sol mélangé par habitats, niveau d'eau simulé, dispersion par les masques
de la simulation, LOD par distance à la caméra). Chaque rendu écrit un `.json` voisin (réglages, nombre
d'instances par asset et par LOD, temps).

## Limites connues

* Pas d'imposteurs ni de HLOD générés (à cuire dans l'éditeur).
* Les ports (architecture des arbres) sont extrapolés d'analogues actuels : aucun arbre fossile entier.
* Les densités par hectare des règles de dispersion sont des hypothèses de jeu (`envlib/catalog.py`,
  `PFT_RULES`, `DEADWOOD_RULES`, dont `density_gamma` = 1,6 pour les arbres : probabilité = masque^γ, pour que la
  mosaïque plaine ouverte / forêt riveraine reste lisible malgré des masques simulés diffus) ; la *répartition*
  vient de la simulation.
* Les textures de feuilles sont peintes (pas de photogrammétrie) : lisibles à mi-distance, simples en très gros plan.
* Le terrain de la simulation a une maille de 4,06 m : les rendus ajoutent un micro-relief procédural (±0,15 m)
  et un patch fin près de la caméra ; une berge verticale (cutbank) devient une pente de 30–51°.
