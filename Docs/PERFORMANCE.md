# Performances, profilage et qualité du cœur de simulation

Ce document rassemble les mesures de performance du cœur `NoctisCore` (exécutable autonome, hors
Unreal), le profil d'une simulation de deux jours, le plan d'optimisation qui en découle et les
constats QA (tests, déterminisme, courses de données, portabilité). Les commandes pour tout reproduire
sont à la fin (§ 7).

**Version mesurée.** Sauf mention contraire, tout porte sur le commit `f1208ae` (16 octobre 2026,
2 h 03), copie de travail propre pour `Source/` ; `Source/NoctisCore` est identique dans le commit
suivant, `5eabfd4` (2 h 39). Benchmark lancé à 2 h 16, profils callgrind,
ThreadSanitizer et test d'endurance entre 2 h 20 et 3 h 45. Les mesures de la veille
(commit `27a58e8`, 15 octobre 22 h 53), faites avant la correction de la boucle d'alarmes, servent de
point de comparaison au § 3.2.

> **À lire d'abord — limites de ces mesures.** La machine est un conteneur Linux à 4 vCPU (Xeon
> 2,8 GHz) partagé avec d'autres agents. Chaque agent tourne dans son propre groupe de contrôle : dès
> que d'autres travaux tournent (charge moyenne 13 à 17 pendant presque toute la session), un
> processus obtient **environ un cœur, quel que soit son nombre de threads** (test de contrôle sans le
> moteur, boucle de calcul pure : rapport temps CPU / temps mural 0,99 avec 1, 2 et 4 threads). Le
> conteneur venait de redémarrer à 2 h 14 et la machine est restée inoccupée environ deux minutes : le
> même contrôle y donnait 1,00 / 1,98 / 3,06 cœurs avec 1 / 2 / 4 threads, et la moitié « 1 thread »
> du benchmark a tourné dans ce créneau (CPU / mural 0,98 à 1,00). La moitié « 4 threads » a commencé
> alors que d'autres travaux démarraient (CPU / mural 0,93 à 1,97) : les chiffres multi-thread du § 2
> sont donc des **bornes basses**, et le passage à l'échelle est aussi estimé à partir de la fraction
> parallèle mesurée au profileur (§ 4). Les temps muraux absolus sont bruités (± 30 % d'une exécution
> à l'autre sous charge) ; les comptes d'instructions de callgrind, eux, ne dépendent pas de la charge.
> MSVC et AppleClang n'ont pas pu être compilés ici : seuls GCC 13.3 et Clang 18.1 l'ont été (§ 6.7).

## 1. Configuration mesurée

| Élément | Valeur |
|---|---|
| Compilateurs | GCC 13.3.0 et Clang 18.1.3, `RelWithDebInfo` (`-O2 -g -DNDEBUG`), drapeaux stricts UE (`-fno-exceptions -fno-rtti -Werror`) |
| Monde (benchmark, profils) | Hell Creek, graine 7, carte du jeu : 2017² échantillons de hauteur, 1024² cellules de matériau, végétation en cellules de 16 m |
| Population initiale | 254 animaux en 22 groupes (échelle de population 1,0) |
| Monde des tests de simulation | Hell Creek réduit (513² hauteurs, 256² matériaux, cellules de végétation de 32 m), échelle de population 0,25 à 0,3 |
| Outil | `noctis_bench` (`Tools/Bench/bench.cpp`), rapport JSON `noctis-bench/1` |
| Profileurs | valgrind 3.22 `--tool=callgrind` (comptes d'instructions), gprof (`-pg`, échantillonnage, profil de la veille) |
| Sanitiseurs | GCC 13 : ThreadSanitizer, AddressSanitizer + UndefinedBehaviorSanitizer |

## 2. Résultats du benchmark

GCC, `noctis_bench --threads 4` (rapport complet : 59 s murales).

### 2.1 Initialisation et mémoire (1 thread)

| Étape | Durée |
|---|---|
| Chargement des données scientifiques (`EvidenceDatabase`) | 23 ms |
| Génération du terrain | 2 133 ms |
| Hydrologie | 226 ms |
| Végétation | 319 ms |
| Peuplement (254 animaux) | sous le bruit de mesure (le monde sans population a mis 3 061 ms, le monde complet 2 917 ms) |
| **Total `WorldSimulation::initialize`** | **2 917 ms** (3 174 ms pour le monde à 4 threads, initialisé ensuite sous charge) |
| Mémoire résidente du monde (après `malloc_trim`) | +95,5 Mo |
| Pic de mémoire résidente du processus | 105,9 Mo |
| Construction d'un `RenderSnapshot` (254 animaux) | 10,5 µs |

La génération du terrain représente 73 % de l'initialisation (bruit fractal de la plaine
d'inondation, P7 au § 5).

### 2.2 Coût d'un pas par animal selon le niveau de détail

Pas de 0,1 s, tous les animaux forcés dans un même niveau de détail (LOD) ; le coût du pas sans
animaux (24 µs, mesuré à 1 thread) est déduit. Dans le rapport mesuré ici, ce pas de référence n'était
mesuré qu'à 1 thread et était aussi déduit des pas à 4 threads, ce qui fausse le champ
`per_creature_step_us` à 4 threads (non cité ici ; la mise à jour de la végétation, parallèle, ne coûte
pas la même chose à 1 et à 4 threads) : depuis, `noctis_bench` mesure un pas de référence par nombre de
threads (`baseline_step` dans le rapport) et déduit de chaque pas celui de son nombre de threads. Sans
animaux, le système de créatures ne lance aucun `parallelFor` : le coût fixe du § 2.4 reste compté dans
le coût par animal à 4 threads.

| LOD | Pas moyen, 1 thread | p95 | Par animal et par pas | Par animal et par seconde simulée | Pas moyen, 4 threads (CPU / mural) | Rapport 1 → 4 threads |
|---|---|---|---|---|---|---|
| Complète | 21,0 ms | 68,8 ms | 82,6 µs | 826 µs | 11,2 ms (1,97) | ×1,88 plus rapide |
| Réduite | 0,363 ms | 2,53 ms | 1,34 µs | 13,4 µs | 0,787 ms (1,14) | ×2,2 plus lent |
| Lointaine | 0,087 ms | 0,286 ms | 0,25 µs | 2,5 µs | 0,323 ms (1,09) | ×3,7 plus lent |

Clang n'a pas été re-mesuré dans le créneau libre ; sur `27a58e8`, sur la même machine, il était 21 %
plus rapide que GCC en LOD complète (64,9 contre 82,6 µs par animal et par pas) et 30 % plus rapide
en LOD réduite (1,10 contre 1,61 µs).

Rapport de coût complète : réduite : lointaine ≈ 330 : 5 : 1. Avec un budget de 4 ms de simulation
par pas à 10 Hz sur un seul cœur, on tient environ 50 animaux en LOD complète. Le p95 vaut 3,3 fois la
moyenne : la perception (intervalle 0,25 s) tombe pour tous les animaux sur le même pas, d'où des pics
réguliers (P1-e au § 5). En LOD réduite et lointaine, le pas à 4 threads est **plus lent** qu'à
1 thread : le travail d'un pas (0,1 à 0,4 ms) est plus court que le coût fixe des trois `parallelFor`
du pas (§ 2.4).

### 2.3 Débit (secondes simulées par seconde murale)

Le monde à 1 thread et le monde à 4 threads suivent exactement la même séquence de pas. Le rapport
mesuré ici ne comparait rien : il relevait seulement, pour chaque nombre de threads, le nombre
d'événements et d'animaux en fin de parcours (1 038 événements et 254 animaux dans les deux cas), ce
qui ne prouve pas que les deux mondes finissent dans le même état — et, à cause de la course D1
(§ 6.3), ce n'est pas garanti. Depuis, `noctis_bench` enregistre dans `consistency`, pour chaque
nombre de threads, une empreinte de l'état final du monde et une empreinte ordonnée de tous les
événements diffusés, et le champ `consistent` du rapport dit si elles sont identiques à celles du
parcours à 1 thread ; en cas d'écart, le benchmark affiche un avertissement (`WARNING`) mais ne
s'arrête pas, les temps restant ceux d'une charge presque identique.

| Régime | Mélange LOD moyen (complète / réduite / lointaine) | 1 thread | 4 threads (CPU / mural) |
|---|---|---|---|
| Temps réel, pas 0,1 s | 1 / 30 / 223 | ×929 | ×184 (0,95) |
| Commande `run` du CLI, pas 1 s | 1 / 29 / 224 | ×402 | ×506 (1,44) |
| Accéléré ×100, pas 10 s | 0 / 32 / 222 | ×2 505 | ×1 937 (0,93) |

En jeu (temps réel), les 254 animaux coûtent donc environ 0,11 ms par pas de 0,1 s à 1 thread
(×929 : 1 s simulée en 1,08 ms) tant qu'un seul est en LOD complète ; chaque animal de plus en LOD
complète ajoute 83 µs par pas (§ 2.2). Le passage à 4 threads n'aide que si le pas contient assez de travail
(LOD complète, § 2.2) : à 4 threads le temps réel perd un facteur 5 (cf. § 2.4 et P6). Deux jours
simulés en régime accéléré représentent 17 280 pas : 140,0 s murales pour ces pas sur cette machine
chargée (143,9 s pour tout le processus, initialisation de 3,9 s comprise), mais 91,4 s de CPU pour
tout le processus (CPU / mural 0,64, rapporté aux 143,9 s du processus), soit au moins ×1 890 en temps
réel sur un cœur dédié (254 → 250 animaux ; 95,8 s de CPU et 254 → 215 animaux sur `27a58e8`).

### 2.4 Coût fixe d'un `parallelFor` (pool de threads autonome)

Nouveau dans le rapport (`scheduler_parallel_for`) : `parallelFor` sur 254 indices (autant que
d'animaux), lots de 8 comme dans `CreatureSystem::step`, corps trivial, 10 000 appels (2 000 en
`--quick`). Mesuré avec `--quick` (2 000 appels) sous charge (charge moyenne 16) :

| Compilateur | 1 thread | 4 threads : moyenne | médiane | p95 |
|---|---|---|---|---|
| GCC | 0,4 µs | 314 µs | 72 µs | 1 582 µs |
| Clang | 0,4 µs | 157 µs | 50 µs | 284 µs |

`CreatureSystem::step` fait trois appels par pas : sur une machine chargée, 0,15 à 1 ms de
synchronisation par pas à 4 threads, soit plus que tout le travail d'un pas en LOD réduite. Le pool
réveille tous les threads (`notify_all`) et attend la fin de chacun à chaque appel, même pour quelques
microsecondes de travail. Sur une machine libre ce coût est attendu plus bas (quelques dizaines de µs)
mais n'a pas pu être mesuré. Côté Unreal, `JobScheduler` doit être implémenté avec `ParallelFor`
(seuil de lot) : ce pool `std::thread` ne sert qu'aux outils, aux tests et à la CI.

## 3. Profil d'une simulation de deux jours

Exécution profilée : `noctis_bench --run-days 2 --regime accelerated --threads 1` (monde du jeu, pas de
10 s, accélération ×100, 17 280 pas), binaire GCC de `f1208ae`, sous valgrind `--tool=callgrind`.

### 3.1 Callgrind sur `f1208ae` (instructions exécutées)

Total : **537,7 G instructions** (84 min sous callgrind), dont 21,2 G (3,9 %) pour l'initialisation
et 516,4 G (96,0 %) pour `WorldSimulation::advance` ; 254 → 250 animaux. Le profil a été vidé en trois
tranches (`callgrind_control -d`) ; le nombre de pas de chaque tranche est compté dans les appels de
`WorldSimulation::step` depuis `advance` :

| Tranche | Pas | Temps simulé | Instructions par pas | dont perception | dont végétation sur les rayons (`transmittance`) | dont propagation sonore | dont locomotion |
|---|---|---|---|---|---|---|---|
| 1 | 1 – 6 772 | 0 – 0,78 j | 26,7 M | 9,4 M | 4,6 M | 0,06 M (0,2 %) | 13,6 M |
| 2 | 6 773 – 11 801 | 0,78 – 1,37 j | 32,8 M | 14,3 M | 7,8 M | 1,6 M (4,9 %) | 14,1 M |
| 3 | 11 802 – 17 280 | 1,37 – 2,00 j | 31,1 M | 13,5 M | 7,6 M | 1,3 M (4,2 %) | 13,8 M |

Le coût d'un pas monte de 23 % entre la première tranche (5 h → minuit du premier jour) et la
seconde (perception : rayons à travers la végétation et quelques sons de plus ; le coût des requêtes
de voisinage, lui, ne change pas), puis se stabilise : **plus d'emballement**.

Coût inclusif par poste (% de `advance`, 2 jours complets) :

| Poste | Part | Remarque |
|---|---|---|
| `CreatureSystem::step` | 94,2 % | |
| — dans les boucles `parallelFor` | 90,9 % | fraction parallélisable (§ 4) |
| `integrateLocomotion` | **46,2 %** | 10 sous-pas de 1 s par pas de 10 s en LOD lointaine |
| — `Terrain::query` / `Terrain::normalAt` | 8,6 % / 5,2 % | 4 lectures de hauteur par normale |
| `perceive` | **40,5 %** | |
| — `VegetationSystem::transmittance` (vision et audition) | 21,7 % | `opticalDensity` : 16,2 % du total en exclusif |
| — `CreatureSystem::neighbours` (`SpatialHash::query`) | 6,1 % | |
| — `Terrain::lineOfSight` | 5,5 % | |
| — `SoundField::receivedLevelDb` | 3,0 % | 28,6 % sur `27a58e8` |
| `ResearchSystem::update` → `FieldMap::reveal` | 4,5 % | à chaque pas, chercheur immobile |
| `drainFootprints` + `TrackSystem::add` | 2,1 % | séquentiel |
| `rebuildIndex` | 1,1 % | |
| `decide` + `executeBehavior` + `updatePhysiology` | 2,7 % | |
| mises à jour horaires, végétation, groupes, événements | 1,5 % | |
| Fonctions de la libm (exclusif : `sincosf`, `expf`, `logf`, `powf`, `atanf`, `atan2f`…) | 14,4 % du total | |

Coût exclusif (% du total des instructions) :

| Fonction | Part |
|---|---|
| `Terrain::heightAt` (interpolation bilinéaire ; 94 % des appels viennent de la locomotion : `normalAt`, `Terrain::query`, `integrateLocomotion`) | 20,4 % |
| `VegetationSystem::opticalDensity` | 16,2 % |
| `integrateLocomotion` (corps de la fonction) | 11,8 % |
| `SpatialHash::query` | 5,4 % |
| `VegetationSystem::cellIndexAt` | 3,2 % |
| `perceive` (corps) | 3,0 % + 1,4 % |
| `sincosf` / `expf` / `logf` / `powf` / `atanf` / `atan2f` (libm) | 3,1 / 3,0 / 2,1 / 1,9 / 1,7 / 1,0 % |
| `WaterSystem::query`, `Terrain::query` | 1,9 / 1,9 % |
| `GradientNoise2D::sample` (génération du terrain) | 1,8 % |
| `TrackSystem::add`, `VegetationSystem::transmittance` (corps), `Terrain::normalAt`, `blendSoil` | 1,6 / 1,3 / 1,1 / 1,1 % |

### 3.2 Comparaison avec la veille (`27a58e8`, avant correction de la boucle d'alarmes)

Le même profil sur `27a58e8` totalisait **707,2 G instructions** (96 min sous callgrind), dont 21,2 G
pour l'initialisation, et montrait que **le coût d'un pas doublait en deux jours** alors que la
population baissait (254 → 215 animaux) :

| Tranche (`27a58e8`) | Pas | Temps simulé | Instructions par pas | Part de la propagation sonore |
|---|---|---|---|---|
| 1 | 1 – 4 446 | 0 – 0,51 j | 27,0 M | 0,2 % |
| 2 | 4 447 – 13 612 | 0,51 – 1,58 j | 40,3 M | 25,5 % |
| 3 | 13 613 – 17 280 | 1,58 – 2,00 j | 53,6 M | 51,7 % |

La hausse venait entièrement de `SoundField::receivedLevelDb` : aucun cri d'alarme pendant les
30 premières heures, puis 19 262 entre 30 et 36 h et ≈ 40 000 par tranche de 6 h ensuite (32 cris par
animal et par heure, 58 à 65 sources sonores actives en permanence), à cause de la boucle d'alarmes
décrite au § 6.2. Avec la boucle corrigée (`f1208ae`), le même profil coûte 24 % d'instructions en
moins (537,7 G contre 707,2 G), le coût par pas plafonne à 31–33 M au lieu de grimper à 53,6 M, et la
propagation sonore passe de 28,6 % à 3,0 % de `advance`. En valeur absolue, la locomotion a un peu
augmenté (238,7 G contre ≈ 217 G : la population reste plus nombreuse, 250 animaux au lieu de 215 en
fin de période, et `Locomotion.cpp` a changé entre-temps) et devient le premier poste du régime
accéléré.

Le contrôle par échantillonnage (gprof, sur `27a58e8`, 60,4 s échantillonnées sur 117 s de CPU)
donnait le même ordre une fois corrigée une erreur de symbolisation : la première ligne du profil plat
(24,9 %, attribuée à `std::__introsort_loop<SoundSource>`) est en réalité le clone local
`Terrain::heightAt [clone .part.0]`, appelé 191 millions de fois depuis `integrateLocomotion` ; suivent
`VegetationSystem::opticalDensity` 15,1 %, `integrateLocomotion` 13,0 %, `SpatialHash::query` 5,8 %,
`perceive` 5,8 %.

### 3.3 Zoom : un pas en LOD complète

Callgrind sur 30 pas de 0,1 s avec les 254 animaux en LOD complète (`f1208ae`, instrumentation activée
seulement pendant ces 30 pas) : 3,40 G instructions, soit **113 M instructions par pas** et 446 000 par
animal et par pas (4,18 G sur `27a58e8`).

| Fonction (inclusif, % de `CreatureSystem::step`) | Part |
|---|---|
| `parallelFor` (les boucles parallèles par animal) | 99,6 % |
| `perceive` | 97,7 % |
| `SoundField::receivedLevelDb` (propagation de chaque son vers chaque auditeur) | 88,6 % |
| — `VegetationSystem::transmittance` (marche le long du rayon dans la végétation) | 41,9 % |
| — `Terrain::lineOfSight` (occultation par le relief) | 37,8 % |
| — `SoundField::absorptionDbPerM` (ISO 9613-1, `pow`/`exp` à chaque appel) | 7,8 % |
| `SoundField::collectNear` | 2,5 % |
| `integrateLocomotion` | 1,2 % |
| `CreatureSystem::neighbours` | 0,6 % |

En exclusif : `Terrain::heightAt` 34,4 %, `VegetationSystem::opticalDensity` 32,8 %,
`VegetationSystem::cellIndexAt` 6,5 %, `Terrain::lineOfSight` 5,7 %, `pow` (glibc) 5,5 %. En LOD
complète, **l'audition coûte 89 % du pas** : chaque perception recalcule, pour chaque source sonore
audible, l'absorption atmosphérique, l'atténuation par la végétation (échantillonnée tous les 8 m) et
l'occultation par le relief (échantillonnée tous les r/40 m).

## 4. Passage à l'échelle

**Mesuré** (§ 2.2, LOD complète) : ×1,88 de 1 à 4 threads avec 1,97 cœur occupé en moyenne, soit une
efficacité parallèle de 95 % rapportée aux cœurs réellement obtenus. **Estimé** (loi d'Amdahl) à
partir des fractions d'instructions exécutées dans `JobScheduler::parallelFor` :

| Régime | Fraction parallèle p | Accélération prévue à 2 / 4 / 8 cœurs |
|---|---|---|
| LOD complète (pas de 0,1 s, § 3.3) | 99,6 % | 2,0 / 3,95 / 7,8 |
| Accéléré ×100 (profil de 2 jours, § 3.1) | 90,9 % | 1,83 / 3,14 / 4,9 |

Ce sont des bornes hautes : elles supposent des cœurs libres et ignorent le coût fixe de chaque
`parallelFor` (§ 2.4), qui domine dès que le travail d'un pas descend sous la milliseconde (LOD
réduite et lointaine, régime accéléré). Le reste séquentiel du régime accéléré est la résolution des
intentions et des contacts, la vidange des empreintes (`TrackSystem::add`), la mise à jour des groupes
et la recherche (`FieldMap::reveal`).

## 5. Plan d'optimisation priorisé

Rien de ce plan n'a été appliqué : `Private/Creatures` est en cours de correction par ailleurs et
toute optimisation change les trajectoires (donc les empreintes de déterminisme). Les trois premières
lignes sont des corrections de défauts, pas des optimisations ; elles passent avant tout le reste.
Chaque étape doit être validée par `noctis_bench` et par la suite de tests (déterminisme compris, et
ThreadSanitizer pour tout ce qui touche aux phases parallèles).

| # | Cible | Part mesurée | Action | Gain attendu | Fichiers |
|---|---|---|---|---|---|
| D1 | Course de données perception / corps (§ 6.3) | — (correction) | Séparer la phase 1 de `CreatureSystem::step` en deux `parallelFor` (corps, puis perception) ; correctif validé (§ 6.3), prêt à appliquer : `Docs/patches/D1_perception_body_phase_split.diff` | déterminisme 1 / N threads garanti ; coût : un `parallelFor` de plus par pas (§ 2.4) | `Creatures/CreatureSystem.cpp` |
| D2 | Ordre d'évaluation non spécifié (§ 6.4) | — (correction) | Tirer les nombres aléatoires dans des variables locales avant l'appel (7 sites) ; correctif : `Docs/patches/D2_rng_evaluation_order.diff` | mêmes résultats GCC / Clang / MSVC | `Creatures/Behavior.cpp`, `Creatures/CreatureSystem.cpp` |
| D3 | Immigration (§ 6.5) | — (correction) | Multiplier la cible d'immigration par `SimConfig::populationScale` | populations stables à densité réduite | `Creatures/CreatureSystem.cpp` |
| P1 | Perception : audition en LOD complète, rayons à travers la végétation partout | 89 % du pas en LOD complète (§ 3.3) ; 40,5 % du régime accéléré, dont végétation sur les rayons 21,7 %, voisinage 6,1 %, ligne de vue 5,5 %, propagation sonore 3,0 % (§ 3.1) | a) rejeter tôt les sources inaudibles même en champ libre (divergence + absorption sous le seuil d'audition) avant toute marche de rayon ; b) calculer l'absorption une fois par pas et par bande de fréquence (la météo est constante pendant un pas) au lieu de `pow`/`exp` à chaque couple source-auditeur ; c) précalculer, à chaque mise à jour de la végétation, l'extinction par cellule pour les hauteurs utilisées (0,5 m et 1,5 m) — `opticalDensity` devient une lecture — et parcourir les cellules par DDA (Amanatides-Woo) au lieu d'échantillonner tous les demi-pas de cellule ; d) occultation : pyramide de hauteurs maximales (test conservatif rapide) ou cache par paire de cellules ; e) étaler les instants de perception des animaux sur plusieurs pas (phase déterministe par individu) pour supprimer les pics p95 | ×3 à ×5 sur le pas en LOD complète, p95 ramené près de la moyenne | `World/SoundField.cpp`, `World/Vegetation.cpp`, `World/Terrain.cpp`, `Creatures/Perception.cpp` (e) |
| P2 | Locomotion en LOD lointaine | 46,2 % du régime accéléré : premier poste (§ 3.1) | En LOD lointaine, un pas de 10 s est découpé en 10 sous-pas de 1 s, chacun appelant `Terrain::query` (normale = 4 lectures de hauteur, mécanique du sol, eau). Sous-pas de 5 s en LOD lointaine, pas de mécanique du sol ni d'enfoncement quand aucune empreinte n'est produite, normale tirée des 4 sommets déjà lus par `heightAt` | −50 à −70 % de ce poste | `Creatures/Locomotion.cpp`, `Creatures/CreatureSystem.cpp`, `World/Terrain.cpp` |
| P3 | `Terrain::heightAt` | 20,4 % des instructions en exclusif (34 % en LOD complète), 94 % des appels depuis la locomotion | Appelée hors ligne depuis les autres unités de compilation : la déplacer en ligne dans `Terrain.h`, sortir le test `samples_ == 0` du chemin chaud, fournir `sampleHeightNormal` (hauteur et normale en une lecture 4×4), stocker les hauteurs en `int16`/`half` relatives (16 Mo de `float` ne tiennent pas en cache L2) | −30 à −50 % de ce poste | `World/Terrain.h/.cpp` |
| P4 | Voisinage (`SpatialHash::query`) | 6,1 % du régime accéléré | Réutiliser les vecteurs de sortie (allocations visibles au profil), adapter la taille des cellules au rayon des requêtes de perception, plafonner le nombre de voisins traités | −30 % de ce poste | `Core/SpatialHash.cpp`, `Creatures/CreatureSystem.cpp` |
| P5 | `FieldMap::reveal` | 4,5 % du régime accéléré | Recalcule à chaque pas une ligne de vue vers chaque cellule du rayon, même chercheur immobile : mettre en cache l'ensemble des cellules visibles par cellule d'observation et ne le recalculer qu'au changement de cellule | ≈ ce poste entier | `Research/FieldMap.cpp` |
| P6 | Pool de threads autonome | médiane 50 à 72 µs, moyenne 157 à 314 µs par `parallelFor` à 4 threads sous charge ; temps réel ×5 plus lent à 4 threads (§ 2.3, 2.4) | Exécution séquentielle sous un seuil de travail (mesuré au pas précédent), ne réveiller que les threads nécessaires, brève attente active avant de bloquer ; dans Unreal, `ParallelFor` avec seuil de lot | supprime la pénalité | `Core/Jobs.cpp` |
| P7 | Initialisation | 73 % des 2,9 s | Paralléliser par lignes la génération de la plaine d'inondation (cellules indépendantes, donc déterministe), réduire les octaves du bruit fractal hors des zones visibles | −50 % du temps de chargement | `World/TerrainGenerator.cpp` |
| P8 | libm | 14,4 % des instructions | Remplacer `atan2`/`sincos` des caps par de l'algèbre vectorielle, mettre en cache les `pow` d'allométrie par individu | −5 % | divers |

Ordre recommandé : D1, D2, D3, puis P1 (le jeu passe l'essentiel de son temps en temps réel, où les
animaux proches sont en LOD complète), P2 et P3 (régime accéléré et CLI), P6 avant toute mesure
multi-thread, puis P5, P4, P7, P8.

## 6. Constats QA

### 6.1 Suite de tests

`noctis_tests` (cadre maison sans exceptions, `Tools/Tests/TestFramework.h` + `TestMain.cpp` :
enregistrement par macros, filtre par nom, étiquettes `short`/`long`, code de sortie non nul en cas
d'échec, 77 pour « ignoré ») contient **47 tests : 45 rapides** (étiquette CTest `short`) et **2 longs**
(étiquette `long`, lancés seulement avec `NOCTIS_LONG_TESTS=1`). Les chantiers `Private/Creatures` y
ont ajouté leurs propres tests de non-régression (attribution des causes de mort, croissance,
convergence du cap, bruit des véhicules, source sonore linéaire du tonnerre…). Sur `f1208ae` :
**45 / 45 tests rapides réussis avec GCC (41 s) et avec Clang (30 s)** (`ctest -L short -j2`, machine
chargée). Dernière exécution, sur la copie de travail de 3 h 50 (17 lignes de plus dans
`Private/Creatures` et un test ajouté, `creatures.drink.stale_water_memory_retarget`, soit 48 tests
dont 46 rapides) : **46 / 46 avec GCC (27 s) et avec Clang (29 s)**.

Corrections après la relecture QA (copie de travail de 4 h 30, mêmes 48 tests) : second programme dans
le test rapide de déterminisme (§ 6.4), valeur de `hashString("a")` figée (4953267810257967366,
`0x44bd8ad473cd9906`, recalculée en Python) au lieu de la comparaison de la fonction avec elle-même,
contrôle de l'ordre des événements transformé en assertion. CTest compte maintenant 8 entrées : les
6 familles, la garde `test_groups` (étiquette `short`) et `long`. Les familles sont tirées du préfixe
du nom des tests ; un test d'un autre préfixe compilerait sans qu'aucune entrée CTest ni aucune tâche
de CI ne le lance : `test_groups` lance `noctis_tests --label all --check-groups core,data,…` et échoue
si un test, court ou long, n'appartient à aucune famille (la liste vient de `NOCTIS_TEST_GROUPS` dans
`CMakeLists.txt`). Résultats (`ctest -L short -j2`, machine chargée, sans le correctif D1) : GCC,
**7 / 7 entrées réussies (65 s)** ; Clang, **7 / 7 (61 s)** à la dernière exécution, mais l'entrée
`sim` avait échoué à la première, sur la course D1 (second programme du test de déterminisme, § 6.4) ;
`long` est ignorée (code 77) sans `NOCTIS_LONG_TESTS`. Le test de déterminisme dure maintenant
33 à 42 s au lieu de 10 s (trois parcours de 12,5 h simulées de plus).

| Famille | Contenu | État |
|---|---|---|
| `core` (16) | maths, PCG32 (séquence de référence), SplitMix64, JSON aller-retour et erreurs, grilles, hachage spatial contre force brute (300 requêtes, 34 620 résultats identiques), calendrier, `parallelFor`, ordre des événements | OK |
| `data` (4) | les 46 fichiers JSON de `Data/` passent par `EvidenceDatabase` : 27 espèces, 2 environnements, 393 sources (327 vérifiées), 3 profils, `validate()` : 0 erreur, 0 avertissement | OK |
| `world` (10) | terrain déterministe par graine (empreintes répétées identiques, graine voisine différente), bilan de masse de l'eau (réservoir linéaire), croissance logistique bornée (≤ capacité après 2 ans), position du soleil et photopériode (15,43 h à 45° N au solstice), ISO 9613-1 (48 valeurs de la table 2 d'ISO 9613-2, écart maximal 0,89 fois la tolérance d'arrondi), divergence sphérique, tonnerre, collecte sonore concurrente | OK |
| `creatures` (6) | relation de Froude foulée/vitesse (foulée/hauteur de hanche = 2,3 Fr^0,3), sens de l'allométrie de Nagy, bornes physiologiques sur longue intégration, croissance, cap | OK |
| `research` (6) | Fisher exact (thé de Fisher p = 34/70, valeurs exactes rationnelles), Mann-Whitney (tables n = 5/5), Spearman, hypothèses jamais déclarées « vraies » (au mieux « soutenue par les données »), avertissements de confusion, bruit des véhicules | OK |
| `sim` (3 rapides + 2 longs) | déterminisme sur deux programmes (1 / 1 / N threads sans prédation, puis 1 / N / N threads avec prédation), absence de NaN et cohérence du snapshot, taux d'événements ; endurance 20 jours et déterminisme 72 h (longs) | rapides : OK, sauf échec intermittent du second programme de déterminisme tant que D1 n'est pas corrigé (§ 6.3, 6.4) ; longs : § 6.3 et § 6.5 |

Valeurs de référence vérifiées indépendamment (Python, arithmétique exacte) : p de Fisher 34/70,
41/14858, 1/126, 3571/6653325, 1063/46189 ; absorption ISO 9613-1 à 20 °C, 70 % HR : 4,978 dB/km à
1 kHz, 9,016 dB/km à 2 kHz, 76,62 dB/km à 8 kHz ; 0,411 dB/km à 125 Hz et 10 °C.

Note : `hashString` (`Core/Random.h`) a la forme de FNV-1a 64 bits mais utilise 1469598103934665603
comme base de décalage, au lieu de 14695981039346656037 (un chiffre manque). Le hachage reste
correct pour l'usage (dérivation de graines), mais n'est pas FNV-1a standard ; le corriger changerait
toutes les graines dérivées, donc tous les mondes et toutes les empreintes de référence. Le test
`core.random.splitmix_and_hash` fige la valeur actuelle.

### 6.2 Défaut corrigé : boucle d'appels d'alarme

Sur `27a58e8`, le test `sim.events.rate_sanity` échouait : sur 12,5 h simulées (graine 1), 6,4 cris
d'alarme par animal et par heure (borne 2) et 38,7 par heure pour un même *Edmontosaurus* (borne 20).
Cause : entendre l'alarme d'un congénère portait la vigilance à 0,6 (`Perception.cpp`) et un animal en
vigilance criait dès 0,55 (`Behavior.cpp`) ; dans un troupeau, le phénomène s'entretenait sans
prédateur. Le déclenchement a depuis été réécrit dans `Behavior.cpp` : sur `f1208ae`, 70 alarmes en
12,5 h, soit **0,07 par animal et par heure**, 0,8 par heure au plus pour un individu (GCC ; 66 et
1,0 avec Clang). C'était aussi, de loin, le premier poste de coût en fin de deuxième jour (§ 3.2).

### 6.3 Défaut ouvert : course de données entre perception et mise à jour du corps

**Constat.** ThreadSanitizer (GCC 13, `-DNOCTIS_SANITIZE=thread`) sur le programme du test long de
déterminisme (graine 3, 30 min en temps réel puis 72 h accélérées, 4 threads) signale 9 courses en
5 paires d'emplacements, toutes dans la phase 1 de `CreatureSystem::step`, où un même `parallelFor`
met à jour le corps de chaque animal puis le fait percevoir :

| Lecture (prédateur qui évalue une proie, `perceive`) | Écriture concurrente (la proie met à jour son propre corps) |
|---|---|
| `Perception.cpp` l. 362 : `o.injuries.healthIndex`, `o.phys.fatigue` | `Body.cpp` l. 204 (`recomputeImpairment` : `healthIndex`), l. 114 (`updatePhysiology` : `fatigue`) |
| `Perception.cpp` l. 363 : `o.injuries.defenseFactor` | `Body.cpp` l. 199 (`recomputeImpairment` : `defenseFactor`) |

Le prédateur lit donc la santé, la fatigue et la défense de sa proie avant ou après la mise à jour de
celle-ci selon l'ordonnancement des threads. À 1 thread le résultat dépend seulement de l'ordre des
indices (déterministe) ; à N threads il dépend du hasard de l'ordonnancement. Sous ThreadSanitizer (qui
ralentit et réordonne les threads), la trace à 4 threads s'écarte de la trace à 1 thread au
8 922ᵉ événement (t = 52,97 h) : la note d'opportunité d'un `HuntTargetSelected` vaut 0,5534 au lieu de
0,5421 ; la suite diverge. **Sans sanitiseur aussi** : deux exécutions du test d'endurance de 20 jours
(§ 6.5) sur le même code, même graine, 4 threads, divergent à partir du 12ᵉ jour (40 129 contre
39 617 empreintes, puis recensements différents ; 195 contre 201 animaux au 20ᵉ jour), et le test long
`sim.determinism.three_days_threads` avait échoué de façon intermittente sur les copies de travail de
0 h 58 et 1 h 25 (2 exécutions sur 4 à 4 threads, empreintes d'état ou d'événements différentes ;
1 exécution à 3 threads réussie). À l'inverse, trois exécutions successives à 4 threads du programme de
72 h sur `f1208ae` ont reproduit la trace à 1 thread (14 567 événements, identiques à l'octet) : le
défaut est intermittent. Le programme d'origine du test rapide de déterminisme (graine 11) n'exerce
pas ce chemin : aucun prédateur n'y choisit de proie (0 `HuntTargetSelected`), ThreadSanitizer n'y
signale rien et son empreinte est la même avec et sans correctif, alors que `sim.events.rate_sanity`
(graine 1, prédateurs au contact d'un troupeau dès les premières heures, 14 sélections de proie)
déclenche les mêmes 5 paires en 4 min 34 s (21 rapports). La relecture QA l'a confirmé par injection
sur une copie de l'arbre : sans sanitiseur, sur le programme de `rate_sanity`, 1 exécution à 4 threads
sur 15 a divergé de la trace à 1 thread (événements `2db9865ba718dc4a` au lieu de `08ae4fd420fdd216`),
jamais sur la graine 11 ; de même, un retour de `EventBus::flush` à un tri sur le seul temps passait
inaperçu sur la graine 11 (6 exécutions sur 6 identiques) mais faisait diverger 2 exécutions à
4 threads sur 3 sur la graine 1. Le test rapide comporte donc maintenant un second programme, celui de
`rate_sanity` (§ 6.4).

**Correction recommandée (non appliquée : `Private/Creatures` est hors du périmètre de ce travail).**
Séparer la phase 1 en deux `parallelFor` : d'abord le corps de tous les animaux (`updatePhysiology`,
`updateInjuries`, `updateMemory`), puis la perception de tous. Le correctif est dans le dépôt,
`Docs/patches/D1_perception_body_phase_split.diff` (12 lignes ajoutées, 1 modifiée ; fait sur `5eabfd4`, s'applique tel quel
à la copie de travail du 16 octobre à 4 h 30 : `git apply Docs/patches/D1_perception_body_phase_split.diff`).
Il est à appliquer par le chantier `Private/Creatures` avant la mise en service de la CI. Validé sur une
copie de l'arbre :

- ThreadSanitizer : 0 rapport sur `sim.events.rate_sanity` + `sim.no_nan_and_snapshot` (3 min 49 s) et
  0 rapport sur le programme de 72 h à 4 threads (20 min 31 s), dont la trace est identique à l'octet à
  celle du même code à 1 thread (18 923 événements) malgré l'ordonnancement perturbé par le
  sanitiseur — là où le code actuel divergeait au 8 922ᵉ événement ;
- programme de 72 h sans sanitiseur : traces à 1 et à 4 threads identiques (18 923 événements) ;
- 45 / 45 tests rapides réussis (l'empreinte du test rapide de déterminisme est inchangée,
  `b961d6c6e8ddc588` / `cbf46c9aabb4354e` ; `rate_sanity` compte 39 alarmes au lieu de 70) ;
- revalidé par la relecture QA sur la copie de travail : 0 rapport ThreadSanitizer sur
  `rate_sanity`, `no_nan_and_snapshot`, `core.jobs` et `world.sound`, et programme de la graine 1
  identique à 1 et à 4 threads dans 8 exécutions sur 8 ;
- avec le nouveau second programme du test rapide (copie de travail de 4 h 30 + correctif, GCC) :
  traces à 1 et à 4 threads identiques dans 8 comparaisons sur 8 (état `408d53619eae24c7`, événements
  `ff64874c3ce1ac70`, 685 événements dont 13 sélections de proie) ; Clang : voir § 6.4.

Le correctif change les trajectoires (chaque perception voit désormais tous les corps à jour) et coûte
un `parallelFor` de plus par pas (§ 2.4). Variante sans ce surcoût : double tampon des champs publics
lus par les autres animaux (santé, fatigue, défense, posture…), recopiés en fin de pas dans la phase
séquentielle. **Tant que ce défaut n'est pas corrigé, deux contrôles échouent par construction :**
l'entrée ThreadSanitizer de la CI (§ 6.8, marquée `continue-on-error` en attendant) et, de façon
intermittente, le second programme du test rapide `sim.determinism.same_seed_threads` (§ 6.4), qui est
fait pour cela.

### 6.4 Déterminisme

- **1 thread contre N threads** : identique bit à bit, hors la course du § 6.3. Le test rapide
  `sim.determinism.same_seed_threads` enchaîne deux programmes :
  - **A** (graine 11, départ à 21 h, 10 min en temps réel puis 6,5 h accélérées), à 1, 1 et N threads :
    sur `f1208ae`, état `b961d6c6e8ddc588`, événements `cbf46c9aabb4354e` (718 événements, GCC) ; sur la
    copie de travail de 4 h 30, état inchangé, événements `605c1427c2265702` (718 ; `Behavior.cpp` a
    encore changé après 3 h 50). Aucune sélection de proie avec GCC (une seule avec Clang) : ce
    programme ne voit ni la course D1 ni un tri d'événements partiel (§ 6.3) ;
  - **B** (graine 1, départ à 5 h, 30 min en temps réel puis 12 h accélérées : le programme de
    `sim.events.rate_sanity`), à 1 thread puis deux fois à N threads. Le test exige au moins une
    sélection de proie (`REQUIRE_GT(h.hunts, 0)`), pour que ce programme ne perde pas sa couverture sans
    que personne le voie. Copie de travail de 4 h 30 **sans** correctif D1 : GCC, état
    `b1ae97f1dce66b1b`, événements `2d9d57208e26aee3` (976 événements, 14 sélections de proie), 12
    comparaisons 1 / 4 threads sur 12 identiques ; Clang, `cc2427e28b73088f` / `b8791c9850666098`
    (1 027 événements, 17 sélections), et **4 comparaisons sur 14 ont divergé** (4 exécutions du test
    sur 7 en échec), toutes vers l'état `9890771fcdb653f9` avec les mêmes événements. C'est exactement
    l'état final du code corrigé ci-dessous : la lecture en course a vu le corps de la proie déjà mis à
    jour, ce que le correctif rend systématique. Le test attrape donc la course D1 souvent avec Clang,
    rarement avec GCC ; ThreadSanitizer reste le détecteur fiable. **Avec** le correctif D1 : GCC
    `408d53619eae24c7` / `ff64874c3ce1ac70` (685 événements, 13 sélections) et Clang
    `9890771fcdb653f9` / `422ec6c5411960d0` (1 027 événements, 17 sélections), 8 comparaisons sur 8
    identiques avec chacun. **Tant que D1 n'est pas appliqué, ce test échoue donc souvent avec Clang**
    (et la tâche `linux-clang` de la CI avec lui) : c'est le défaut qu'il doit détecter.

  Le test compare aussi les empreintes des graines 11 et 12 juste après `initialize` (même horloge, même
  nombre de pas : seule la graine peut les faire différer). L'ancienne vérification comparait la
  graine 12 sur un programme plus court (60 s + 1 h) à la graine 11 sur 600 s + 6,5 h : les empreintes,
  qui incluent l'horloge et le nombre de pas, différaient de toute façon. La clé de tri de
  `EventBus::flush` est totale (tous les champs) : l'ordre des événements émis au même instant par des
  threads différents ne dépend plus de l'ordonnancement ; `SoundField` trie de même les sons collectés
  par leur contenu. `sim.events.rate_sanity` et l'endurance de 20 jours exigent maintenant qu'aucun
  événement ne soit diffusé avec un temps antérieur à celui d'un événement précédent
  (`CHECK_EQ(rec.outOfOrder(), 0)` ; ce n'était qu'une note, 0 observé avec GCC et Clang).
- **GCC contre Clang** : les résultats diffèrent (programme A, Clang : état `c824a3112ab0021f`,
  695 événements sur `f1208ae`, `8cabc33bcfa0ef4b` sur la copie de travail de 4 h 30), parce que plusieurs expressions tirent deux nombres aléatoires dans des arguments de
  fonction ou des opérandes dont l'ordre d'évaluation n'est pas spécifié par C++ (GCC évalue de droite
  à gauche, Clang de gauche à droite). Corrigé dans `Sim/WorldSimulation.cpp` (4 instructions, en
  conservant l'ordre de GCC). Il reste 7 sites dans `Private/Creatures` (numéros de ligne de
  `f1208ae`) : `Behavior.cpp` l. 778, 1039, 1321, 1369, 1451
  (`p + Vec2::fromHeading(c.rng.range(…)) * c.rng.range(…)`) et `CreatureSystem.cpp` l. 1278, 1550
  (`spawn(…, rng_.chance(0.5f) ? … : …, …, pos + Vec2{rng_.range(…), rng_.range(…)}, …)`). Le correctif
  (tirages dans des variables locales, dans l'ordre de GCC) s'applique toujours proprement ; sur
  `27a58e8` il faisait reproduire à Clang 18 exactement les empreintes de GCC 13. Les listes entre
  accolades (`Vec2{rng, rng}`), elles, sont évaluées de gauche à droite par la norme.
- **Entre plateformes** : la libm de glibc choisit à l'exécution des variantes FMA (`__expf_fma`,
  `__powf_fma`…) ; MSVC et macOS ont leurs propres implémentations. Le déterminisme est garanti pour
  un binaire donné sur une plateforme donnée, pas entre Windows, Linux et macOS (sauvegardes
  portables : stocker l'état, pas seulement la graine).

### 6.5 Endurance (20 jours simulés, test long)

`sim.soak.20_days` sur `f1208ae` (GCC, graine 2024, monde réduit, échelle de population 0,25,
4 threads), exécuté deux fois : 13 min 39 s puis 11 min 55 s murales sous charge (10 min 16 s et
10 min 24 s de CPU). Les deux exécutions sont identiques pendant 11 jours puis divergent (course du
§ 6.3) ; les chiffres ci-dessous donnent la première, puis la seconde entre parenthèses.

- **aucune valeur non finie ni hors bornes** chez aucun animal à aucun des 20 bilans journaliers ;
- végétation stable (9,43·10⁸ à 9,45·10⁸ kg), magasins bornés (182 606 empreintes au plus, borne du
  test 2 000 000 ; 37 carcasses au plus) ; 128 913 (136 650) événements, 0,81 (0,83) vocalisation par
  animal et par heure ;
- alarmes : 1 740 (1 749) en 20 jours, soit 0,02 par animal et par heure, mais **un seul individu en
  émet 1 619 (1 530)**, soit 93 % (87 %) du total et 3,4 (3,2) par heure ; c'est un *Edmontosaurus*
  juvénile (identifié dans la seconde exécution). C'est sous la borne du test (20 par heure) mais à
  surveiller : cet animal reste coincé en vigilance pendant des jours ;
- **échec sur l'immigration** : la communauté passe de 66 à 195 (201) animaux (borne 4× respectée),
  **sans aucune naissance** ; 165 immigrants pour 36 morts (66 + 165 − 36 = 195). Adultes :
  *Edmontosaurus* 8 → 30 (31), *Didelphodon* 5 → 18 (20), *Borealosuchus* 3 → 13, *Struthiomimus*
  2 → 11, et dans la seconde exécution *Champsosaurus* 5 → 16 : au-delà de la borne du test (2× + 5),
  seule cause d'échec (4 puis 5 assertions).

Historique : sur `27a58e8` (26 min 50 s murales), aucune valeur non finie ni hors bornes, mais échec
sur la boucle d'alarmes (43,0 cris d'alarme par animal et par heure) et sur l'immigration (66 → 195
animaux) ; sur la copie de travail du 16 octobre à 1 h 10, boucle d'alarmes disparue (0,00 alarme par
animal et par heure) mais immigration toujours en excès (66 → 226 animaux).

**Défaut d'immigration.** Toute la croissance vient de l'immigration, sans aucune naissance en
20 jours. `CreatureSystem.cpp` (l. 1593 sur `f1208ae`) vise 0,9 × `initial_count` des données (45
pour *Edmontosaurus* : cible 40,5) sans tenir compte de `SimConfig::populationScale` : toute partie
lancée avec une densité réduite (option `--scale` du CLI, tests, réglage de densité) est « remplie »
jusqu'à la densité pleine en quelques jours. Correction suggérée : multiplier la cible par l'échelle de
population (à transmettre au `CreatureSystem`).

### 6.6 Mémoire, comportement indéfini, courses

- **ThreadSanitizer** : voir § 6.3. Les tests `core.jobs.parallel_for_covers_every_index_once`,
  `world.sound.field_thread_safe_collect` et `sim.determinism.same_seed_threads` dans sa version d'alors
  (programme A seul, 4 threads, 143 s) : 0 rapport. Le programme B reprend celui de `rate_sanity` et
  y déclenche donc les rapports de D1 tant que le correctif n'est pas appliqué. Avec le correctif
  (copie de l'arbre de 4 h 30, GCC 13, `-DNOCTIS_SANITIZE=thread`, 4 threads), la sélection de l'entrée
  TSan de la CI (`core.jobs`, `world.sound`, `sim.events.rate_sanity`, `sim.no_nan_and_snapshot`,
  `sim.determinism.same_seed_threads` avec ses deux programmes) donne **0 rapport**, 8 / 8 tests
  réussis en 539 s, dont 372 s pour le test de déterminisme (traces du programme B identiques à 1 et à
  4 threads sous le sanitiseur).
- **ASan + UBSan** (GCC 13, `-DNOCTIS_SANITIZE=address,undefined`, `-fno-sanitize-recover=all`,
  détection des fuites active, 4 threads) sur les 45 tests rapides de `f1208ae` : **0 rapport**
  d'erreur mémoire, de comportement indéfini ou de fuite, 45 / 45 réussis en 184,5 s (même résultat
  sur les 41 tests de `27a58e8`). Les empreintes de déterminisme sous sanitiseurs sont identiques à
  celles du build normal.
  GCC 13 émet sous sanitiseurs un faux positif `-Warray-bounds` dans `TerrainGenerator.cpp` (vecteur
  initialisé par une liste d'un élément) : dans les builds sanitiseurs seulement, ces avertissements de
  flot ne sont plus fatals (`CMakeLists.txt`).
- **valgrind memcheck** (sur `27a58e8`, exécution partielle de la suite) : 0 erreur, 0 octet perdu.

### 6.7 Portabilité

- `CMakeLists.txt` : une cible d'interface `noctis_strict` porte les drapeaux du module UE pour tous
  les exécutables. GCC / Clang / AppleClang : `-fno-exceptions -fno-rtti -Wall -Wextra -Wshadow
  -Werror`. MSVC : `/W4 /WX /permissive- /GR- /EHs-c- /utf-8 /Zc:__cplusplus`, `_HAS_EXCEPTIONS=0`,
  retrait des `/GR` et `/EHsc` ajoutés par défaut par CMake ; C4100, C4244, C4267 et C4305 sont
  désactivés comme dans Unreal. Option `NOCTIS_SANITIZE` pour ASan / UBSan / TSan.
- Compilé et testé ici : GCC 13.3 et Clang 18.1 (Linux x86-64), zéro avertissement avec `-Werror`.
  **Non compilés ici** : MSVC et AppleClang ; la CI (`.github/workflows/ci.yml`) les compile à chaque
  poussée. Relecture faite pour MSVC : attributs `format(printf)` et en-têtes POSIX sous garde,
  `getenv` couvert par `_CRT_SECURE_NO_WARNINGS`, rotation PCG32 écrite `(~rot + 1u) & 31u` (pas de
  moins unaire sur un non-signé, C4146). Points à surveiller au premier passage MSVC : avertissements de
  niveau 4 sans équivalent GCC (C4245 conversions signé/non signé de constantes, C4702 code
  inaccessible en build optimisé, C4456–C4459 masquage, proches mais pas identiques à `-Wshadow`).
- `noctis_bench` mesure la mémoire et le temps CPU avec l'API native de chaque système
  (`GetProcessMemoryInfo` / `GetProcessTimes`, `task_info`, `/proc/self/statm` et `getrusage`).

### 6.8 Intégration continue

`.github/workflows/ci.yml` :

- tâche `build-test`, matrice ubuntu-latest (GCC, Clang), windows-latest (MSVC), macos-latest
  (AppleClang) : configuration Ninja, compilation, `ctest -L short`, `noctis_bench --quick`, puis envoi
  en artefacts du JSON du benchmark, des journaux de test et du rapport JUnit. Cache ccache par chaîne
  de compilation (objets MSVC en `/Z7` pour être cachables). Le benchmark tourne même si un test
  échoue. **Tant que D1 n'est pas appliqué, `ctest -L short` échoue souvent sur `linux-clang`**
  (second programme du test de déterminisme, § 6.4) et peut échouer sur les autres chaînes : le
  correctif D1 doit être appliqué avant la mise en service du workflow ;
- tâche `long-tests` (GCC, Linux), chaque lundi (03:17 UTC) ou à la demande (`workflow_dispatch`,
  case « long_tests ») : `ctest -L long`. Tâche à part, limitée à 150 min, au-dessus de la limite de
  CTest (7 200 s) : si un test long se bloque, CTest l'arrête et le rapport JUnit est quand même écrit
  et envoyé (avec la limite de 90 min de `build-test`, c'est GitHub qui aurait tué la tâche sans
  rapport). Elle échoue aujourd'hui (immigration D3 au § 6.5 ; déterminisme de 72 h intermittent à
  cause de D1, § 6.3) et porte donc `continue-on-error: true`, à retirer une fois D1 et D3 corrigés ;
- tâche `sanitizers` (GCC, Linux) : ASan + UBSan sur tous les tests rapides, et ThreadSanitizer sur les
  tests multi-thread (`core.jobs`, `world.sound`, `sim.events.rate_sanity`, `sim.no_nan_and_snapshot`,
  `sim.determinism.same_seed_threads`). La relecture QA a reproduit l'échec attendu de l'entrée TSan :
  9 rapports aux lignes 362-363 de `Perception.cpp` et code de sortie 66 sur `sim.events.rate_sanity`
  seul, en 115 s. **L'entrée TSan échoue tant que D1 (§ 6.3) n'est pas appliqué** : elle porte
  `continue-on-error: ${{ matrix.sanitize == 'thread' }}`, à retirer avec le correctif, pour qu'un
  rouge attendu ne devienne pas l'état normal de `main`. Les deux entrées abaissent
  `vm.mmap_rnd_bits` à 28 : sur les images Ubuntu 24.04, l'entropie d'ASLR par défaut (32 bits) casse
  les runtimes TSan et ASan de GCC 13 (actions/runner-images#9491) ;
- concurrence : un nouveau commit n'annule l'exécution précédente que sur une *pull request*
  (`cancel-in-progress: ${{ github.event_name == 'pull_request' }}`) ; le groupe inclut le type
  d'événement, si bien qu'une poussée sur `main` n'annule pas l'exécution hebdomadaire des tests longs
  et n'attend pas derrière elle.

Le workflow est validé par actionlint 1.7.12 (sans shellcheck, absent ici) mais n'a pas encore tourné
sur GitHub. Le `shell: bash` par défaut des tâches lance les commandes avec `-eo pipefail` : le
`| tee` des étapes sanitiseurs ne masque pas un échec.

## 7. Reproduire

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j2
ctest --test-dir build -L short                                # tests rapides (6 familles + garde test_groups)
NOCTIS_LONG_TESTS=1 ctest --test-dir build -L long             # endurance 20 j + déterminisme 72 h
./build/noctis_tests --list --label all                        # liste des tests ; filtre : ./build/noctis_tests world.sound
git apply Docs/patches/D1_perception_body_phase_split.diff     # correctif D1 (§ 6.3) ; D2 : Docs/patches/D2_rng_evaluation_order.diff
./build/noctis_bench --out bench.json                          # benchmark complet (≈ 1 min) ; --quick pour la CI
valgrind --tool=callgrind ./build/noctis_bench --run-days 2 --regime accelerated --threads 1
cmake -S . -B build-asan -G Ninja -DNOCTIS_SANITIZE=address,undefined   # ASan + UBSan (GCC ; Clang demande compiler-rt)
cmake -S . -B build-tsan -G Ninja -DNOCTIS_SANITIZE=thread              # puis :
TSAN_OPTIONS="halt_on_error=0" ./build-tsan/noctis_tests --threads 4 sim.events.rate_sanity
```
