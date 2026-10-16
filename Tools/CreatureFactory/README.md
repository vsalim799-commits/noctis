# Creature Factory (Noctis)

Chaîne de production **procédurale et reproductible** des créatures du jeu (moteur final : Unreal Engine 5).
À partir d'une fiche anatomique (`Data/Anatomy/<espèce>.json`) elle produit : une sculpture haute
définition avec un vrai relief de peau, un maillage basse définition prêt pour l'animation, ses UV, ses
textures 4K, un squelette avec poids de peau, des animations procédurales issues de la biomécanique, les
exports FBX (UE5) / GLB (three.js) et des rendus Cycles « studio ».

Tout est piloté par :

```bash
/home/user/.venvs/noctis-tools/bin/python Tools/CreatureFactory/factory.py <species_id> \
    [--stages anatomy,sdf,detail,retopo,uv,bake,rig,anim,export,render,video] \
    [--quality hero|standard] [--shots side,head,...] [--samples N] [--no-preview]
```

Sans `--stages`, toutes les étapes sauf `video` sont enchaînées. Chaque étape relit les sorties
mises en cache de la précédente dans `Build/Creatures/<espèce>/` : on peut relancer n'importe quelle
étape seule (ex. `--stages render --shots head`). Les machines étant partagées, lancer avec `nice -n 10`.

Environnement : Python du venv `noctis-tools` (bpy 4.2 en module, numpy, scipy, numba, scikit-image,
Pillow). Blender est utilisé « headless » ; seul Cycles CPU est employé (pas d'EEVEE).

## Repère et unités

* **Espace anatomique** (fiches, calculs) : mètres, +x vers l'avant (museau), +y vers le haut, +z = côté
  droit de l'animal, sol à y = 0.
* **Blender** : Z en haut, la créature regarde vers −Y (convention « front » de Blender), son côté droit
  est en −X : `blender = (−z, −x, y)` (`lib/bl.py`).
* **FBX pour UE5** : 1 unité = 1 cm (voir « Export »). **GLB** : mètres, +Y en haut.

## Étapes

| étape | module | entrée → sortie |
|---|---|---|
| `anatomy` | `lib/anatomy.py` + `species/<espèce>.py` | (T. rex) `trex_anatomy.js` → dump JSON (`export_anatomy.mjs`, node) → `Data/Anatomy/<espèce>.json` |
| `sdf` | `lib/sdf.py`, `lib/mesher.py`, `lib/sdf_stage.py`, `lib/accessories.py` | fiche → `base.npz` (sculpture lisse, marching cubes), `accessories.pkl` (griffes, dents, yeux) |
| `detail` | `lib/skin.py`, `lib/detail.py` | → `highpoly.npz` / `highpoly.blend` (≈3,3 M triangles, relief **réellement déplacé**) |
| `retopo` | `lib/retopo.py` | `base.npz` → `lowpoly.blend` (QuadriFlow symétrique, quads, projeté exactement sur le SDF) |
| `uv` | `lib/uv.py` | → `uv.blend` (coutures anatomiques, dépliage, empaquetage) |
| `bake` | `lib/bake.py` | → `textures/T_<espèce>_*.png` (4K hero / 2K standard) |
| `rig` | `lib/rig.py` | → `rig.blend` (squelette, poids, matériaux, accessoires fusionnés) |
| `anim` | `lib/anim.py` | → `anim.blend` (Actions procédurales) + `anim_clips.json` |
| `export` | `lib/export.py` | → `export/` : FBX (mesh + LOD + une FBX par animation), GLB, textures, métadonnées |
| `render` | `lib/render.py` | → `Output/factory/<espèce>/<espèce>_<plan>.png` + planche contact |
| `video` | `lib/video.py` | → `Output/factory/<espèce>/<espèce>_walk.mp4` (travelling 1280×720, 24 i/s, 4 s) |

### anatomy
La fiche contient : les os (noms stables, tête/queue, parents), les primitives SDF (ellipsoïdes, cônes
arrondis, « loft » à sections superellipses pour le crâne et la mandibule, « sweep » le long de la colonne
pour le tronc/cou/queue) avec leur os, leur région (tête, mâchoire, cou, tronc, ventre, queue, cuisse,
jambe, pied, bras), leurs rayons de fusion et des **masques** (plis du cou, gorge, articulations, écailles
scutellées du métatarse/des orteils, coussinets, peau rugueuse cornée, paupières, lèvres, narines…), les
yeux, griffes et dents, les paramètres de peau (`skin`) et la palette (`palette`, SPÉCULATIF).
Pour le T. rex elle est dérivée du modèle three.js (`Tools/Preview/src/creature/trex_anatomy.js`), dont
`species/tyrannosaurus_rex.py` corrige les défauts connus (voir son en-tête) :

* **tête refaite** en deux « lofts » (crâne + lèvre supérieure, mandibule) à sections superellipses
  **effilées** (paramètre `tw` : la demi-largeur diminue vers le haut → museau en ogive, nasaux étroits
  au-dessus de maxillaires inclinés) ; silhouette dorsale « en trou de serrure » (museau étroit, région
  temporale large de ~0,9 m) pour des orbites tournées vers l'avant ; profil dorsal le plus haut au-dessus
  du lacrymal/postorbitaire ; mandibule profonde au surangulaire ; muscles adducteurs, joues
  (jugal/quadratojugal), bajoues (ptérygoïdien), gorge ;
* **lèvres** (Cullen et al. 2023) : les deux lofts se rejoignent par des faces plates de même largeur ; leurs
  coins arrondis forment la commissure (sillon labial) qui remonte vers l'arrière et disparaît sous la joue
  derrière le coin de la bouche ; dents cachées à l'intérieur ;
* **œil** : globe séparé (UV polaires, iris), affleurant ~8 mm au-dessus de la surface, paupières en arcs
  épais en amande, pli palpébral, **arcade** continue (lacrymal → postorbitaire) qui surplombe et ombre
  l'œil, corne lacrymale et bosse postorbitaire rugueuses, renflement jugal, narine ovale à l'avant ;
* **pattes** ré-posées à la hauteur de hanche de l'espèce (acétabulum 3,1 m) avec les longueurs osseuses
  inchangées (fémur 1,30 / tibia 1,155 / métatarse 0,61 m) et une **musculature massive** : cuisse en
  « pilon » (iliotibial, fémorotibial), caudofémoral reliant la base de la queue au 4e trochanter
  (Persons & Currie 2011), gastrocnémien effilé vers le tendon, orteils plus épais ;
* tronc plus large au thorax, **queue plus épaisse plus longtemps** (moins conique).

Les tables de sections (tête, tronc) et les formes sont des valeurs de sculpture documentées dans le
module ; ce qui n'est pas mesuré sur des fossiles est listé comme spéculatif dans les métadonnées.

### sdf
Évaluation numba du champ (union lisse polynomiale, soustractions lisses, culling par grille + AABB),
marching cubes par blocs (scikit-image, blocs actifs seulement), soudure, orientation. Les griffes sont
des maillages séparés (chaînes de cônes arrondis aplatis latéralement), les dents des cônes recourbés, les
yeux des sphères à UV polaires.

### detail
Relief de peau (`lib/skin.py`, même fonction pour le maillage et pour la cuisson) :
écailles « basement » non imbriquées (3 couches de Worley, poids par région, plus grosses sur le dos, plus
fines sous le ventre), micro-écailles des paupières et des lèvres, écailles de grande taille éparses
(hypothèse de design), plaques cornées rugueuses du museau et des bosses orbitaires, plis du cou
(dorso-ventraux), de la gorge et des articulations, rides (sillons étroits le long des lignes d'un bruit
fBm « ridged », étirés le long de l'axe de l'os dominant : plis longitudinaux du tronc et de la queue, peau
bombée entre les sillons), scutelles transversales irrégulières à l'avant du métatarse et sur les orteils,
coussinets plantaires, rangées d'écailles labiales plus grandes. Un filtre (taille de voxel ou de texel) supprime
l'aliasing. Le maillage dense (voxel 6 mm en hero) est déplacé le long de la normale SDF.

### retopo
Remaillage voxel OpenVDB de Blender (quads uniformes, taille de voxel déduite de la cible de faces de la
qualité) — QuadriFlow a été essayé d'abord mais perdait des morceaux de queue sur ce modèle —, puis
projection de Newton sur la surface exacte du SDF et relaxation tangentielle ; densification locale
(subdivision) autour des yeux et des narines, projetée le long de la normale. Écart sculpture → low-poly
mesuré (`retopo_stats.json`). Griffes décimées (~360 triangles chacune). Les LOD sont produits à l'export.

### uv
Cartes anatomiques (os dominant de chaque face) : tête+mâchoire, corps (bassin, dos, cou, queue), cuisse
G/D, jambe+pied G/D, bras G/D. Leurs frontières sont des coutures (anneaux aux articulations) ; dans chaque
carte un arbre de coupe (Dijkstra pondéré par la visibilité) relie les bords et les extrémités (bout de la
queue, menton, bouts des orteils et des doigts) par la ligne médiane ventrale (corps, tête) ou la face
interne/postérieure (membres). Dépliage « angle based », normalisation des aires, tête agrandie ×1,6 (plus
de texels sur la face), empaquetage avec rotation. Densité de texels documentée dans `uv_stats.json`.

### bake
Cuisson **analytique depuis la surface haute définition implicite** (équivalente à un high-poly de
résolution infinie) : chaque texel couvert (rastériseur numba en espace UV) récupère position, normale et
tangente MikkTSpace interpolées du low-poly, lance un rayon le long de la normale jusqu'au SDF, évalue le
relief et son gradient tangent (différences finies à ½ texel), et encode la normale en espace tangent.
Cartes : `BaseColor` (sRGB), `Normal` (OpenGL +Y), `Normal_DX` (DirectX −Y), `ORM` (R occlusion SDF ×
micro-cavités, G rugosité, B métal = 0), `Height` (16 bits), `SSS` (poids de diffusion sous-surfacique),
`Cavity`, `Masks` (R scutelles/coussinets, G rugueux/grandes écailles, B peau fine). Remplissage des texels
vides par le plus proche (pas de fuite dans les mipmaps). Albédo : contre-ombrage (ventral → flancs →
dos), dessus de tête plus sombre, lèvres plus claires, marbrures fBm, bandes transversales légères sur la
queue, variation par écaille, sillons et plis assombris, pieds plus sombres et boue séchée — **SPÉCULATIF**.

### rig
Os déformants de la fiche (noms stables, voir ci-dessous) ; objet armature nommé `Armature` (UE5 l'ignore
et prend `root` comme os racine). Roll : tronc/cou/tête/mâchoire/queue/yeux avec Z local vers le haut,
membres avec Z local vers l'avant. Contrôles non déformants (non exportés) : `ik_foot_l/r`,
`pole_knee_l/r`, `look_target`, avec contraintes IK (cuisse→métatarse) et visée de tête à influence 0.
Poids : volumes SDF de chaque os évalués aux sommets (même pondération que la fusion des formes), lissés
sur le maillage, 4 influences max. Griffes, dents et yeux : rigides (100 % sur leur os). Matériaux :
`M_Body`, `M_Eye`, `M_Claw` (couleur par sommet base sombre → pointe claire), `M_Teeth`.

Os : `root, pelvis, spine_01..03, neck_01..03, head, jaw, eye_l, eye_r, tail_01..10,
thigh_l/r, shin_l/r, metatarsus_l/r, toes_l/r, upperarm_l/r, forearm_l/r, hand_l/r`.

### anim
FK précalculée (une clé par image à 30 i/s, interpolation linéaire), mouvement racine sur `root`.
**Marche** : vitesse préférée `sim.locomotion.walk_speed_ms`, hauteur de hanche `h = sim.hip_height_m`,
nombre de Froude `Fr = v²/(g·h)`, longueur de foulée `λ = 2,3·Fr^0,3·h` (Alexander 1976), facteur de
service `sim.locomotion.duty_factor_walk` ; période `T = λ/v` arrondie à l'image (vitesse ajustée,
consignée). Pendant l'appui, l'articulation métatarso-phalangienne est fixe dans le monde (glissement
mesuré et consigné), déroulé digitigrade sur le bout des orteils en fin d'appui, trajectoire de
balancement en arc ; IK analytique par jambe (angle du métatarse selon la phase, puis IK à deux os,
genou vers l'avant). Tronc : bascule verticale (2 par foulée), balancement latéral vers la patte d'appui,
roulis et lacet du bassin, onde latérale déphasée le long de la queue, stabilisation de la tête.
Clips : `idle` (respiration, transferts de poids), `walk`, `walk_fast` (si `max_speed_ms` le permet ;
marche rapide, **pas de course** pour l'adulte), `turn_left_90` (pas sur place), `look_around`, `sniff`,
`bite` (petite ouverture : intérieur de la bouche non modélisé), `alert`, `vocal_closed_mouth`
(vocalisation bouche fermée, hypothèse Riede et al. 2016), `lie_down`, `rest_loop`, `get_up`.
Durées, vitesses, Froude, foulée, glissement : `anim_clips.json` et métadonnées.

### export
* `SK_<espèce>.fbx` (+ `_LOD1..3.fbx`), `A_<espèce>_<clip>.fbx` (une animation par Action), `<espèce>.glb`
  (toutes les animations, textures 2K), `textures/`, `<espèce>_metadata.json` (copié dans `Output/factory/<espèce>/`).
* **Réglages FBX (UE5, 1 unité = 1 cm)** : la scène est convertie en centimètres avant l'export (échelle
  d'unité de scène 0,01, armature/maillages/clés de translation ×100), puis
  `apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', global_scale=1.0, axis_forward='-Z',
  axis_up='Y', add_leaf_bones=False, primary_bone_axis='Y', secondary_bone_axis='X',
  use_armature_deform_only=True, mesh_smooth_type='FACE', use_tspace=True, use_triangles=True`,
  animations : `bake_anim=True`, pas = 1, sans simplification. Le fichier a donc `UnitScaleFactor = 1`
  (cm), des positions en cm et aucune échelle de nœud : vérifié par un lecteur FBX binaire intégré
  (`fbx_verification_*` dans les métadonnées).
* **Import UE5** : Import Uniform Scale 1,0 ; Convert Scene activé ; Convert Scene Unit désactivé ;
  importer les LOD comme LOD1..3 du même Skeletal Mesh ; normal map `_Normal_DX` (ou `_Normal` + « Flip
  Green Channel ») ; ORM/Masks/Height/SSS/Cavity en sRGB désactivé ; la créature regarde +Y (comme le
  Mannequin) : tourner le composant mesh de −90° en lacet pour l'aligner sur +X de l'acteur ; activer
  « Enable Root Motion » sur les séquences.

### render / video
Studio Cycles comparable à la référence : cyclorama anthracite sans raccord avec sol gris, grande
lumière douce zénithale légèrement en avant, deux contre-jours, débouchage faible ; Principled BSDF avec
diffusion sous-surfacique (carte SSS), cartes de rugosité/normales, micro-occlusion cuite multipliée dans
l'albédo (rendu seulement). Plans : `side`, `front34`, `head`, `flank`, `midstride` (pose de marche,
contrôle des déformations), `foot` ; 1600×900, 64 échantillons + OpenImageDenoise ; planche contact.
`video` : travelling de marche 1280×720, 24 i/s, 4 s, encodé avec `/usr/bin/ffmpeg`.

## Politique science / spéculation

* Les proportions suivent `sim` de la fiche espèce (longueur, hauteur de hanche, masse, longueur du pied)
  et les longueurs osseuses publiées (fémur, tibia, métatarse d'un adulte de la taille de Sue) ; les
  volumes de tissus mous sont une reconstruction plausible.
* Le tégument suit la revendication `science.integument` de l'espèce (T. rex : petites écailles non
  imbriquées, Bell et al. 2017 ; pas de plumes) ; les lèvres couvrent les dents au repos
  (`soft_tissue`, Cullen et al. 2023, confiance « inferred »).
* Toutes les couleurs sont **SPÉCULATIVES** (couleur inconnue) : palettes ternes, contre-ombrées,
  étiquetées dans la fiche anatomique et les métadonnées.
* Aucune référence n'est inventée : les identifiants cités existent dans `Data/Sources/*.json` (les
  métadonnées listent leur drapeau `verified` et signalent ceux qui manqueraient). Tout choix de design
  (tailles d'écailles, grandes écailles, plaques rugueuses, scutelles et coussinets, plis, boue, couleur
  de l'iris, pupille ronde, posture de repos, comportements autres que la marche) est listé dans
  `speculative_elements` des métadonnées.
* La vidéo de référence (art tiers) ne sert que de **repère de qualité** (détail, lumière, matières) :
  ni son design ni son motif de couleur ne sont repris.

## Sorties

* `Build/Creatures/<espèce>/` (non versionné) : `base.npz`, `highpoly.npz/.blend`, `lowpoly.blend`,
  `uv.blend`, `textures/`, `rig.blend`, `anim.blend`, `export/`, `*_stats.json`, `timings.json`,
  `factory.log`.
* `Output/factory/<espèce>/` : rendus, planche contact, vidéo, métadonnées, `stages/` (contrôles
  intermédiaires : argile, UV, cartes).

## Limites connues

Voir la section en fin de fichier (mise à jour après chaque passe).
