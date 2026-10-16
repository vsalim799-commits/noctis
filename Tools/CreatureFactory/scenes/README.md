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
  instances Cycles légères, rotation et échelle aléatoires, collision entre arbres, exclusion de l'eau et des pentes,
  bruit d'agrégation, densités par hectare du manifeste multipliées par le masque de densité de la simulation.
  Paramètres utiles : `density` (multiplicateur global), `range_scale` (portée de dispersion par catégorie),
  `lod_distances={"tree": (70, 260), "fern": (18, 55)}`, `frustum_margin`, `budget`.
* Les règles écologiques (quels types fonctionnels, où, à quelle densité) sont des **hypothèses de jeu** documentées
  dans `Docs/ENVIRONMENT.md` ; la répartition elle-même vient de la simulation (`foliage_*.png`, `weight_*.png`).

## Conseils pour un plan de créature

* Terrain de 400–800 m autour de l'animal, `res_m` 1 m, patch `detail` de 40–80 m à 0,1 m sous l'animal et devant
  la caméra (micro-relief cuit depuis les hauteurs des textures de sol : empreintes, rides de sable).
* Garder `keep_clear` autour des pattes (3–6 m) pour éviter qu'une fougère traverse la créature, puis replacer à la
  main 3 à 10 plantes de premier plan via `extra` pour composer le cadre.
* Lumière : voir `Tools/EnvironmentFactory/envlib/scenes.py` (ciel Nishita + soleil, brume par passe Mist, brouillard
  bas volumétrique) ; les deux rendus de référence sont `Output/environment/valley_aerial.png` et
  `valley_ground.png`.
* Plans larges sous soleil rasant : les reflets spéculaires de milliers de cartes de feuilles sous-pixel
  scintillent ; `envlib.scenes.tune_leaf_materials(roughness=0.7, specular=0.2)` (Tools/EnvironmentFactory) les
  adoucit après `scatter()`.
* Densité des arbres : `density_gamma` des règles (manifeste) rend la mosaïque d'habitats lisible (plaine ouverte
  clairsemée, forêt riveraine fermée) ; `density=` reste le multiplicateur global.
* Budget CPU : les cartes alpha du feuillage coûtent des rebonds de transparence ; 16–32 échantillons + débruitage
  OIDN suffisent en 1920 × 1080 (64–128 pour un gros plan de peau). Lancer avec `nice -n 10`.
