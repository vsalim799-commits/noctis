# Analyse de la référence visuelle

Ce document décrit la vidéo de référence fournie par le porteur du projet (enregistrement d'écran
de 10,5 s, reçu le 3 octobre 2026). Elle sert de **niveau de qualité visé**, pas de modèle à copier :
c'est le travail d'un tiers (un logo est visible), donc aucun de ses éléments (maillage, textures,
design) n'est réutilisé dans Noctis. Quand la vidéo et les données scientifiques divergent, les
données l'emportent (voir `Data/SCHEMA.md`).

## Contenu

- Sujet : un *Tyrannosaurus rex* adulte, seul, sur fond de studio gris anthracite, sol neutre.
- Séquence : cycle de marche lente vu de profil (≈ 0–5 s), puis travelling rapproché sur la tête,
  le cou, le flanc et la cuisse (≈ 5–10 s).
- Éclairage : une source principale douce en hauteur, contre-jour léger, fort contraste ; aucun
  décor, aucune atmosphère.

## Ce qui fait la qualité de l'image

| Élément | Ce qu'on voit | Ce qu'il faut côté production |
|---|---|---|
| Silhouette | Tête massive et large, cou épais en S, thorax profond, cuisse très musclée, queue épaisse à la base | Proportions issues de squelettes mesurés (déjà dans les profils de corps), sculpture anatomique |
| Peau | Petites écailles non imbriquées, plis profonds au cou et aux articulations, bosses dermiques autour de l'œil, écailles scutellées sur le métatarse et les doigts | Sculpt haute fréquence (ZBrush ou équivalent), cartes de déplacement et de normales 4K–8K, Nanite pour le détail statique |
| Matériau | Diffusion sous la peau (bords rosés), spéculaire fragmenté, mâchoire et gorge plus claires | Shader de peau Substrate avec diffusion sous-surfacique, cartes de rugosité et de cavité |
| Bouche | Lèvres fermées couvrant les dents | Conforme aux données (voir ci-dessous) |
| Mouvement | Transfert de poids visible, oscillation de la queue en contrepoids, plis qui glissent sur les muscles | Locomotion procédurale du cœur (déjà en place) + Control Rig (IK pieds, colonne) + déformation des tissus mous (ML Deformer / Chaos Flesh) |
| Mise en scène | Cadrages lents, focale moyenne, faible profondeur de champ | Caméras cinématiques du jeu (mode photo, drone, travelling depuis le véhicule) |

## Confrontation avec les données scientifiques de Noctis

| Point de la vidéo | Données Noctis (`Data/Species/hell_creek/tyrannosaurus_rex.json`) | Verdict |
|---|---|---|
| Lèvres couvrant les dents | `soft_tissue` : tissus extraoraux probables (Cullen et al. 2023), confiance INFÉRÉ-PLAUSIBLE ; hypothèse concurrente des dents exposées listée | Cohérent |
| Peau écailleuse, sans plumes visibles | `integument` : petites écailles non imbriquées (Bell et al. 2017), FORTEMENT ÉTAYÉ ; plumage partiel possible mais non démontré | Cohérent |
| Couleur brun-pourpre, ventre clair | `integument` : couleur inconnue | Choix artistique (SPÉCULATIF) : à présenter comme tel dans le jeu |
| Bosses dermiques autour de l'œil | Rugosités osseuses connues sur le crâne des tyrannosauridés ; forme exacte des tissus mous non préservée | Plausible, à traiter comme INFÉRÉ |
| Marche lente | `locomotion` : l'adulte est limité à la marche et à une marche rapide | Cohérent |

## Écart avec l'état actuel de Noctis

L'aperçu WebGL (`Tools/Preview`) montre les **données** de la simulation avec des formes
procédurales provisoires : positions, postures, appuis, relief, eau et végétation. Il ne vise pas ce
niveau de rendu. L'écart restant est un travail d'**assets artistiques**, pas de simulation :

1. Sculpture et retopologie de chaque espèce (et de ses stades de croissance), textures, rig.
2. Shader de peau UE5 (Substrate, diffusion sous-surfacique), variations individuelles des couleurs.
3. Déformation des tissus mous branchée sur les sorties `SoftTissueState` et `muscleActivation` du cœur.
4. Animation : la locomotion du cœur fournit vitesses, allures et appuis ; Control Rig et Motion
   Matching produisent la pose finale.

Pistes de production : artistes 3D (sculpt + texture + rig), ou génération assistée par IA d'un
premier maillage à partir de concepts produits pour Noctis (jamais à partir de cette vidéo), puis
nettoyage, retopologie et rig par un artiste.
