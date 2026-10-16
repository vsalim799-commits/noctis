# Scènes « beauty shot » — végétation de Hell Creek

Ce dossier accueille les scripts de scène de la Creature Factory (plans de présentation d'une créature dans son
milieu). `vegetation_library.py` fournit le décor : terrain de la simulation, sol, rivière et plantes du
Maastrichtien de Hell Creek produits par `Tools/EnvironmentFactory`.

## Prérequis

```bash
nice -n 10 /home/user/.venvs/noctis-tools/bin/python Tools/EnvironmentFactory/factory.py \
    --stages sim,flora,ground,manifest
```

Cela produit `Build/Environment/library/noctis_flora.blend` (collections `<asset>_LOD0/1/2`, mètres, pivot à la
base), `Build/Environment/manifest.json` (métadonnées et règles de dispersion) et `Build/Environment/sim/`
(masques de la simulation, graine 7).

## Utilisation dans un script de scène

```python
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))     # Tools/CreatureFactory/scenes
import vegetation_library as veg

# 1. Terrain (coordonnées de simulation : x = est, y = nord, mètres). L'objet est centré sur 'center'.
terrain = veg.build_terrain(center=(3700, 4700), size_m=600, res_m=1.0, shading=True, water=True,
                            detail=dict(center=(3720, 4690), size=60, res=0.1))   # patch fin près de la caméra
# 2. Caméra et créature placées en coordonnées Blender : veg.sim_to_blender(terrain, x, y)
# 3. Végétation : sources (non rendues) puis dispersion selon les masques, LOD selon la distance à la caméra
veg.append_assets()
stats = veg.scatter(terrain, camera=cam, seed=7, density=1.0,
                    keep_clear=[(x_animal, y_animal, 5.0)],                # dégager l'animal (mètres)
                    extra=[dict(asset="fern_tufted", x=..., y=...)])       # plantes de premier plan à la main
```

* `append_assets(asset_ids=None, lods=(0, 1, 2))` ajoute les collections dans `NoctisFloraSources` (masquée).
* `scatter(...)` crée un objet « nuage de points + Geometry Nodes » par (asset, LOD) dans `NoctisScatter` :
  instances Cycles légères, rotation, inclinaison et échelle aléatoires (plage d'échelle du manifeste, plafonnée à la
  hauteur du type fonctionnel), collision entre arbres, exclusion de l'eau et des pentes, bruit d'agrégation,
  densités par hectare du manifeste. La probabilité de maintien de chaque candidat vient des **mêmes fonctions** que
  les textures PCG d'Unreal (`Tools/EnvironmentFactory/envlib/pcgbake.py`, `envlib/fields.py`) : le plan rendu
  montre la répartition du jeu. La densité de chaque catégorie s'éteint progressivement sur le dernier quart de sa
  distance maximale (pas d'anneau visible) ; les distances de LOD sont légèrement aléatoires par instance.
  Paramètres utiles : `density` (multiplicateur global), `range_scale` (portée de dispersion par catégorie),
  `lod_distances={"tree": (70, 260), "fern": (18, 55)}`, `max_distances={"fern": "cull", "herb": 40}` (portée par
  catégorie ; `"cull"` = distance de disparition du jeu, lue dans le manifeste : au-delà, la teinte de couvert bas du
  sol prend le relais), `frustum_margin`, `budget`, `decals=True` (décalques de pistes d'empreintes conformés au
  terrain : vers l'eau sur les berges, le long des corridors prédits), `extra_decals=[dict(x=..., y=..., cell=0)]`
  (pistes placées à la main : 0 théropode, 1 hadrosaure, 2 petits tridactyles, 3 sol piétiné).
* Les règles écologiques (quels types fonctionnels, où, à quelle densité) sont des **hypothèses de jeu** documentées
  dans `Docs/ENVIRONMENT.md` ; la répartition elle-même vient de la simulation (`foliage_*.png`, `weight_*.png`).

## Conseils pour un plan de créature

* Terrain de 400–800 m autour de l'animal, `res_m` 1 m, patch `detail` de 40–80 m à 0,1 m sous l'animal et devant
  la caméra (micro-relief cuit depuis les hauteurs des textures de sol : rides de sable, litière ; il se fond dans le
  terrain grossier sur ses 8 derniers mètres). Le micro-relief procédural n'est ajouté qu'aux maillages de pas
  ≤ 1 m (au-delà il crénelle en bandes parallèles) et jamais sur les barres de sable. Les empreintes sont des
  décalques (`scatter(..., decals=True)`), plus des motifs répétés de la texture.
* Garder `keep_clear` autour des pattes (3–6 m) pour éviter qu'une fougère traverse la créature, puis replacer à la
  main 3 à 10 plantes de premier plan via `extra` pour composer le cadre.
* Lumière : voir `Tools/EnvironmentFactory/envlib/scenes.py` (ciel Nishita + soleil, brume par passe Mist,
  brouillard bas « analytique » au compositeur : `mist_compositor(..., height_fog=dict(z0=niveau_eau, cam_z=...,
  density=0.0065, height=2.5, max=0.5, color=..., sun_color=..., sun_side=±1))`, sans bruit et sans coût de rendu ;
  `max` plafonne l'opacité au sol (au-delà de ~0,5 il lit comme de la neige), `sun_color` réchauffe la brume du
  côté du soleil ; un volume Cycles reste possible mais il est
  lent et granuleux à peu d'échantillons) ; les deux rendus de référence sont `Output/environment/valley_aerial.png`
  et `valley_ground.png`. Réutiliser `mist_compositor` plutôt qu'un mélange Mist maison : il applique une ouverture
  morphologique de 1 px à la passe Mist (non débruitée), sinon la brume perce en points clairs à travers les
  cartes de feuillage ; et son brouillard bas tire la distance de la passe Mist (anti-crénelée), pas de Depth.
* Plans larges sous soleil rasant : les reflets spéculaires de milliers de cartes de feuilles sous-pixel
  scintillent ; `envlib.scenes.tune_leaf_materials(roughness=0.7, specular=0.2)` (Tools/EnvironmentFactory) les
  adoucit après `scatter()`.
* Densité des arbres : `density_gamma` des règles (manifeste) rend la mosaïque d'habitats lisible (plaine ouverte
  clairsemée, forêt riveraine fermée) ; `density=` reste le multiplicateur global.
* Budget CPU : les cartes alpha du feuillage coûtent des rebonds de transparence ; garder
  `cycles.transparent_max_bounces` à 64 (sinon les chemins coupés dans les touffes denses donnent des points
  noirs/clairs), 16–32 échantillons + débruitage OIDN suffisent en 1920 × 1080 (64–128 pour un gros plan de
  peau). Lancer avec `nice -n 10`.
