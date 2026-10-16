# Performances, profilage et qualité du cœur de simulation

Ce document rassemble les mesures de performance du cœur `NoctisCore` (exécutable autonome, hors
Unreal), le profil d'une simulation de deux jours, le plan d'optimisation qui en découle et les
constats QA (tests, déterminisme, portabilité). Toutes les valeurs ont été mesurées le 15 octobre 2026
sur la machine de développement décrite ci-dessous ; les commandes pour les reproduire sont en fin de
document.

> **À lire d'abord — limites de ces mesures.** La machine est un conteneur Linux à 4 vCPU partagé avec
> d'autres agents (charge moyenne 6 à 16 pendant les mesures). Un test de contrôle (boucle de calcul
> pure, sans le moteur) donne le même temps mural avec 1, 2 ou 4 threads (0,30–0,31 s) et un rapport
> temps CPU / temps mural de 0,70–0,74 : le bac à sable n'accorde qu'environ un cœur à un processus,
> quel que soit son nombre de threads. **Le passage à l'échelle multi-thread ne peut donc pas être
> mesuré ici** ; il est estimé à partir de la fraction parallèle relevée au profileur (loi d'Amdahl,
> § 4). Les temps muraux absolus sont bruités (± 30 % d'une exécution à l'autre) ; les comptes
> d'instructions de callgrind, eux, ne dépendent pas de la charge. MSVC et AppleClang n'ont pas pu
> être compilés ici : seuls GCC 13.3 et Clang 18.1 l'ont été (voir § 6).

## 1. Configuration mesurée

| Élément | Valeur |
|---|---|
| Compilateurs | GCC 13.3.0 et Clang 18.1.3, `RelWithDebInfo` (`-O2 -g -DNDEBUG`), drapeaux stricts UE (`-fno-exceptions -fno-rtti -Werror`) |
| Monde | Hell Creek, graine 7, carte du jeu : 2017² échantillons de hauteur, 1024² cellules de matériau, végétation en cellules de 16 m |
| Population initiale | 254 animaux en 22 groupes (échelle de population 1,0) |
| Outil | `noctis_bench` (`Tools/Bench/bench.cpp`), rapport JSON `noctis-bench/1` |
| Profileurs | valgrind 3.22 `--tool=callgrind` (comptes d'instructions), gprof (`-pg`, échantillonnage) |

## 2. Résultats du benchmark

### 2.1 Initialisation et mémoire (1 thread)

| Étape | GCC | Clang |
|---|---|---|
| Chargement des données scientifiques | 9 ms | 9 ms |
| Génération du terrain | 2 051 ms | 2 032 ms |
| Hydrologie | 212 ms | 254 ms |
| Végétation | 346 ms | 335 ms |
| Peuplement (estimation : total − monde sans population) | ≈ 340 ms | bruit de mesure |
| **Total `WorldSimulation::initialize`** | **2 929 ms** | **2 849 ms** |
| Mémoire résidente du monde (après `malloc_trim`) | +95,5 Mo | +95,5 Mo |
| Pic de mémoire résidente du processus | 105,5 Mo | 105,7 Mo |
| Construction d'un `RenderSnapshot` (254 animaux) | 9,7 µs | 7,3 µs |

La génération du terrain représente 70 % de l'initialisation (bruit fractal de la plaine
d'inondation, § 3).

### 2.2 Coût d'un pas par animal selon le niveau de détail

Pas de 0,1 s, tous les animaux forcés dans un même niveau de détail (LOD), coût net du pas sans
animaux (21 µs) déduit.

| LOD | Pas moyen (254 animaux) | p95 | Coût par animal et par pas | Par animal et par seconde simulée |
|---|---|---|---|---|
| Complète (GCC) | 21,0 ms | 72,9 ms | 82,6 µs | 826 µs |
| Complète (Clang) | 16,5 ms | 57,7 ms | 64,9 µs | 649 µs |
| Réduite (GCC / Clang) | 0,43 / 0,31 ms | 3,1 / 2,2 ms | 1,61 / 1,10 µs | 16 / 11 µs |
| Lointaine (GCC / Clang) | 0,12 / 0,08 ms | 0,35 / 0,24 ms | 0,39 / 0,17 µs | 3,9 / 1,7 µs |

Rapport de coût complète : réduite : lointaine ≈ 210 : 4 : 1. Avec un budget de 4 ms de simulation
par pas à 10 Hz sur un seul cœur, on tient environ 50 animaux en LOD complète. Le p95 vaut 3,5 fois
la moyenne : la perception (intervalle 0,25 s) tombe pour tous les animaux sur le même pas, d'où des
pics réguliers (§ 5, P1-e).

### 2.3 Débit (secondes simulées par seconde murale)

Le monde à 1 thread et le monde à 4 threads suivent exactement la même séquence de pas ; le rapport
vérifie qu'ils finissent dans le même état (GCC : 1 053 événements et 254 animaux dans les deux cas ;
Clang : 958 et 254).

| Régime | Mélange LOD moyen (complète / réduite / lointaine) | GCC 1 thread | GCC 4 threads | Clang 1 thread | Clang 4 threads |
|---|---|---|---|---|---|
| Temps réel, pas 0,1 s | 1 / 30 / 223 | ×756 | ×594 | ×1 037 | ×157 |
| Commande `run` du CLI, pas 1 s | 1 / 29 / 224 | ×456 | ×502 | ×419 | ×277 |
| Accéléré ×100, pas 10 s | 0 / 33 / 221 | ×2 544 | ×2 736 | ×2 143 | ×1 757 |

Le rapport CPU / mural reste entre 0,5 et 1,0 dans toutes les colonnes à 4 threads : les threads
n'ont pas obtenu de cœurs supplémentaires (voir l'avertissement en tête). Ces colonnes mesurent donc
surtout le **surcoût** du pool de threads sur une machine saturée : en temps réel, Clang perd un
facteur 6,6 parce que chaque pas lance trois `parallelFor` dont chacun réveille et attend tous les
threads pour moins de 0,1 ms de travail (P6). Deux jours simulés en régime accéléré (profil du § 3)
prennent 109,5 s murales à 1 thread (×1 579) sur cette machine.

## 3. Profil d'une simulation de deux jours

Exécution profilée : `noctis_bench --run-days 2 --regime accelerated --threads 1` (monde du jeu,
pas de 10 s, accélération ×100, 17 280 pas), binaire GCC.

### 3.1 Callgrind (instructions exécutées)

CALLGRIND_TABLE

### 3.2 Contrôle par échantillonnage (gprof, même exécution, 2 jours complets)

gprof échantillonne 60,4 s sur les 117 s de CPU (il ne voit ni la libm ni la libc). Ordre obtenu :
`VegetationSystem::opticalDensity` 15,1 %, `integrateLocomotion` 13,0 %, `SpatialHash::query` 5,8 %,
`perceive` 5,8 % (+ 2,7 % pour sa lambda de détection), `cellIndexAt` 3,0 %, `transmittance` 2,5 %,
`Terrain::lineOfSight` 1,8 %, `WaterSystem::query` 1,7 %, `TrackSystem::add` 1,6 %. Attention : la
première ligne du profil plat (24,9 %, attribuée à `std::__introsort_loop<SoundSource>`) est une
**erreur de symbolisation** de gprof : le graphe d'appels montre ce « tri » appelé 191 millions de
fois depuis `integrateLocomotion`, exactement comme `Terrain::heightAt` ; c'est le clone local
`heightAt [clone .part.0]` que gprof rattache au mauvais symbole. Les deux profileurs concordent une
fois cette correction faite.

### 3.3 Zoom : un pas en LOD complète

Callgrind sur 30 pas de 0,1 s avec les 254 animaux en LOD complète (`CreatureSystem::step` seul,
4,18 G instructions) :

| Fonction (inclusif) | Part |
|---|---|
| `parallelFor` (les 4 boucles parallèles par animal) | 99,4 % |
| `perceive` | 97,0 % |
| `SoundField::receivedLevelDb` (propagation de chaque son vers chaque auditeur) | 86,3 % |
| — `VegetationSystem::transmittance` (marche le long du rayon dans la végétation) | 41,4 % |
| — `Terrain::lineOfSight` (occultation par le relief) | 37,4 % |
| — `SoundField::absorptionDbPerM` (ISO 9613-1, `pow`/`exp` à chaque appel) | 7,7 % |
| `integrateLocomotion` | 1,5 % |

En LOD complète, **l'audition coûte 86 % du temps** : chaque perception recalcule, pour chaque source
sonore audible, l'absorption atmosphérique, l'atténuation par la végétation (échantillonnée tous les
8 m) et l'occultation par le relief (échantillonnée tous les r/40 m).

## 4. Passage à l'échelle estimé (loi d'Amdahl)

Fractions parallèles mesurées par callgrind (instructions exécutées dans `JobScheduler::parallelFor`,
rapportées au pas de simulation) :

| Régime | Fraction parallèle p | Accélération prévue à 4 threads | à 8 threads |
|---|---|---|---|
| LOD complète (pas de 0,1 s) | 99,4 % | 3,9 | 7,7 |
| Accéléré ×100 (profil de 2 jours) | PAR_ACCEL | AMDAHL4 | AMDAHL8 |

Ce sont des bornes hautes : elles supposent des cœurs libres et un coût nul de synchronisation. Le
reste séquentiel du régime accéléré est la résolution des intentions et contacts, la vidange des
empreintes (`TrackSystem::add`), la mise à jour des groupes et la recherche (`FieldMap::reveal`).
Côté Unreal, `JobScheduler` doit être implémenté avec `ParallelFor` (voir `Core/Jobs.h`) ; le pool
`std::thread` autonome ne sert qu'aux outils et à la CI.

## 5. Plan d'optimisation priorisé

Rien de ce plan n'a été appliqué : `Private/Creatures` est en cours de correction par ailleurs et
toute optimisation change les trajectoires (donc les empreintes de déterminisme de référence). Gains
estimés à partir des profils ci-dessus ; chaque étape doit être validée par `noctis_bench` et par la
suite de tests (déterminisme compris).

| # | Cible | Part mesurée | Action | Gain attendu | Fichiers |
|---|---|---|---|---|---|
| P1 | Audition en LOD complète | 86 % du pas complet ; ACC_HEAR du régime accéléré | a) rejeter tôt les sources inaudibles même en champ libre (divergence + absorption sous le seuil d'audition) avant toute marche de rayon ; b) calculer l'absorption une fois par pas et par bande de fréquence (la météo est constante pendant un pas) au lieu de `pow`/`exp` à chaque couple source-auditeur ; c) précalculer, à chaque mise à jour de la végétation, l'extinction par cellule pour les hauteurs utilisées (0,5 m et 1,5 m) — `opticalDensity` devient une lecture — et parcourir les cellules par DDA (Amanatides-Woo) au lieu d'échantillonner tous les demi-pas de cellule ; d) occultation : pyramide de hauteurs maximales (test conservatif rapide) ou cache par paire de cellules ; e) étaler les instants de perception des animaux sur plusieurs pas (phase aléatoire déterministe) pour supprimer les pics p95 | ×3 à ×5 sur le pas en LOD complète, p95 ramené près de la moyenne | `World/SoundField.cpp`, `World/Vegetation.cpp`, `World/Terrain.cpp`, `Creatures/Perception.cpp` (e) |
| P2 | Locomotion en LOD lointaine | ACC_LOCO du régime accéléré | En LOD lointaine, un pas de 10 s est découpé en 10 sous-pas de 1 s, chacun appelant `Terrain::query` (normale = 4 lectures de hauteur, mécanique du sol, eau). Sous-pas de 5 s en LOD lointaine, pas de mécanique du sol ni d'enfoncement quand aucune empreinte n'est produite, normale tirée des 4 sommets déjà lus par `heightAt` | −50 à −70 % de ce poste | `Creatures/Locomotion.cpp`, `Creatures/CreatureSystem.cpp`, `World/Terrain.cpp` |
| P3 | `Terrain::heightAt` | ACC_HEIGHT exclusif | Appelée hors ligne depuis les autres unités de compilation : la déplacer en ligne dans `Terrain.h`, sortir le test `samples_ == 0` du chemin chaud, fournir `sampleHeightNormal` (hauteur et normale en une lecture 4×4), stocker les hauteurs en `int16`/`half` relatives (16 Mo de `float` ne tiennent pas en cache L2) | −30 à −50 % de ce poste | `World/Terrain.h/.cpp` |
| P4 | Voisinage (`SpatialHash::query`) | ACC_HASH | Réutiliser les vecteurs de sortie (allocations visibles au profil), adapter la taille des cellules au rayon des requêtes de perception, plafonner le nombre de voisins traités | −30 % de ce poste | `Core/SpatialHash.cpp`, `Creatures/CreatureSystem.cpp` |
| P5 | `FieldMap::reveal` | ACC_REVEAL | Recalcule à chaque pas une ligne de vue vers chaque cellule du rayon, même chercheur immobile : mettre en cache l'ensemble des cellules visibles par cellule d'observation et ne le recalculer qu'au changement de cellule | ≈ ce poste entier | `Research/FieldMap.cpp` |
| P6 | Pool de threads autonome | ×6,6 de pénalité en temps réel (Clang, machine chargée) | Exécution séquentielle sous un seuil de travail, ne réveiller que les threads nécessaires, attendre la fin des lots plutôt que l'arrivée de chaque thread ; dans Unreal, `ParallelFor` avec seuil de lot | supprime la pénalité | `Core/Jobs.cpp` |
| P7 | Initialisation | 70 % des 2,9 s | Paralléliser par lignes la génération de la plaine d'inondation (cellules indépendantes, donc déterministe), réduire les octaves du bruit fractal hors des zones visibles | −50 % du temps de chargement | `World/TerrainGenerator.cpp` |
| P8 | libm | ACC_LIBM | Remplacer `atan2`/`sincos` des caps par de l'algèbre vectorielle, mettre en cache les `pow` d'allométrie par individu | −5 % | divers |

Ordre recommandé : P1 (le jeu passe l'essentiel de son temps en temps réel, où les animaux proches
sont en LOD complète), P2 et P3 (régime accéléré et CLI), puis P6 avant toute mesure multi-thread,
P5, P4, P7, P8.

## 6. Constats QA

### 6.1 Suite de tests

`noctis_tests` (cadre maison sans exceptions, `Tools/Tests/TestFramework.h`) contient 43 tests : 41
rapides (étiquette CTest `short`, ≈ 25 s sur machine calme, 50 s sous charge) et 2 longs (étiquette
`long`, lancés seulement avec `NOCTIS_LONG_TESTS=1`). Résultat actuel, identique avec GCC et Clang :
**40 / 41 tests rapides réussis**. L'échec est un vrai défaut de simulation (§ 6.2).

| Famille | Contenu | État |
|---|---|---|
| `core` (16) | maths, PCG32 (séquence de référence), JSON aller-retour, grilles, hachage spatial contre force brute, calendrier, `parallelFor`, ordre des événements | OK |
| `data` (4) | toutes les données passent par `EvidenceDatabase` avec 0 erreur, cohérence espèces/environnements | OK |
| `world` (9) | terrain déterministe par graine, bilan de masse de l'eau, croissance logistique bornée, position du soleil et photopériode (15,43 h à 45° N au solstice), ISO 9613-1 (48 valeurs de la table 2 d'ISO 9613-2, écart maximal 0,89 fois la tolérance d'arrondi) | OK |
| `creatures` (4) | relation de Froude foulée/vitesse, sens de l'allométrie de Nagy, bornes physiologiques sur longue intégration | OK |
| `research` (5) | Fisher exact (thé de Fisher p = 34/70, valeurs exactes rationnelles), Mann-Whitney (tables n = 5/5), Spearman, hypothèses jamais déclarées « vraies » | OK |
| `sim` (3 rapides + 2 longs) | déterminisme 1 thread / N threads, absence de NaN, cohérence du snapshot, taux d'événements ; 20 jours d'endurance et 3 jours de déterminisme (longs) | 1 échec (6.2) |

Valeurs de référence vérifiées indépendamment (Python, arithmétique exacte) : p de Fisher 34/70,
41/14858, 1/126, 3571/6653325, 1063/46189 ; absorption ISO 9613-1 à 20 °C, 70 % HR : 4,978 dB/km à
1 kHz, 9,016 dB/km à 2 kHz, 76,62 dB/km à 8 kHz, 0,411 dB/km à 125 Hz et 10 °C.

### 6.2 Défaut : boucle d'appels d'alarme (test `sim.events.rate_sanity` en échec)

Sur 12,5 h simulées (graine 1), les animaux émettent 6,4 cris d'alarme par animal et par heure (GCC ;
5,9 avec Clang) pour une borne de 2, et un même *Edmontosaurus* en émet 38,7 par heure (37,4) pour une
borne de 20. Cause, lue dans le code (non corrigée, `Private/Creatures` étant hors de mon périmètre) :

- `Perception.cpp` l. 578-579 : entendre l'alarme d'un congénère porte la vigilance à
  0,6 × fraîcheur de l'alarme, soit 0,6 juste après l'avoir entendue ;
- `Behavior.cpp` l. 1029-1031 : en comportement *Vigilance*, un animal crie l'alarme dès que sa
  vigilance dépasse 0,55 (au plus une fois toutes les 20 s).

Entendre une alarme suffit donc à en émettre une : dans un troupeau, le phénomène s'entretient sans
prédateur. Correction suggérée : n'émettre l'alarme que sur une menace perçue directement (exclure la
vigilance induite par une alarme), ou plafonner cette vigilance induite sous le seuil d'émission, avec
une période réfractaire croissante.

### 6.3 Déterminisme

- **1 thread contre N threads** : identique bit à bit. Test rapide (graine 11, 10 min en temps réel
  puis 6,5 h accélérées) : état `bfbd1605b06877bb`, événements `75ca6051a8ab19c7` (601 événements) avec
  1 et 4 threads. Test long (72 h, graine 3) : état `f120589ff8b044e0`, événements `69df83527b75f971`,
  127 932 événements, identiques avec 1 et 4 threads. Une version antérieure divergeait sur l'ordre
  des événements émis au même instant par des threads différents ; la clé de tri de
  `EventBus::flush` est désormais totale.
- **GCC contre Clang** : les résultats différaient dès l'initialisation, parce que plusieurs
  expressions tirent deux nombres aléatoires dans des arguments de fonction ou des opérandes dont
  l'ordre d'évaluation n'est pas spécifié par C++ (GCC évalue de droite à gauche, Clang de gauche à
  droite). Corrigé dans `Sim/WorldSimulation.cpp` (4 instructions, en conservant l'ordre de GCC : les
  empreintes GCC sont inchangées). Il reste 7 sites dans `Private/Creatures` :
  `Behavior.cpp` l. 689, 893, 1151, 1198, 1280 (`fromHeading(rng) * rng`) et `CreatureSystem.cpp`
  l. 1199, 1471 (`spawn(…, rng.chance() ?…, …, Vec2{rng, rng})`). Avec ces 7 sites corrigés de la même
  façon (correctif prêt, vérifié dans une copie de travail), Clang 18 reproduit exactement les
  empreintes de GCC 13 (`bfbd1605b06877bb` / `75ca6051a8ab19c7`).
- **Entre plateformes** : la libm de glibc choisit à l'exécution des variantes FMA (`__expf_fma`,
  `__powf_fma`…) ; MSVC et macOS ont leurs propres implémentations. Le déterminisme est garanti pour
  un binaire donné sur une plateforme donnée, pas entre Windows, Linux et macOS (sauvegardes
  portables : stocker l'état, pas seulement la graine).

### 6.4 Endurance (20 jours simulés, test long)

`sim.soak.20_days` (GCC, graine 2024, monde réduit, échelle de population 0,25, 4 threads,
26 min 50 s murales) : **aucune valeur non finie ni hors bornes** chez aucun animal à aucun des 20
bilans journaliers, végétation stable (9,44·10⁸ à 9,46·10⁸ kg), magasins bornés (79 000 empreintes au
plus, 66 carcasses). La communauté passe de 66 à 195 animaux (minimum 66, maximum 199, borne 4×
respectée), mais le test échoue sur deux défauts réels :

1. **Immigration calibrée sur la population pleine échelle.** Aucune naissance en 20 jours ; toute
   la croissance vient de l'immigration (57 *Edmontosaurus* immigrés pour 11 au départ, adultes 8 → 40 ;
   *Champsosaurus* adultes 5 → 17 ; *Struthiomimus* 2 → 11). `CreatureSystem.cpp` l. 1499-1525 vise
   0,9 × `initial_count` des données (45 pour *Edmontosaurus* : 0,9 × 45 = 40,5) sans tenir compte de
   `SimConfig::populationScale` : toute partie lancée avec une densité réduite (option `--scale` du CLI,
   tests, réglage de densité) est « remplie » jusqu'à la densité pleine en quelques jours. Correction
   suggérée : multiplier la cible par l'échelle de population (à transmettre au `CreatureSystem`).
2. **Boucle d'alarmes (§ 6.2) sur la durée** : 43,4 vocalisations par animal et par heure, dont 43,0
   cris d'alarme ; un même individu crie 152 fois par heure (toutes les 24 s, soit la période
   réfractaire de 20 s plus le délai de décision) pendant 20 jours.

Le test long de déterminisme (72 h, 1 contre 4 threads) passe (§ 6.3).

### 6.5 Mémoire et comportement indéfini

- **ASan + UBSan** (GCC 13, `-DNOCTIS_SANITIZE=address,undefined`, `-fno-sanitize-recover=all`,
  détection des fuites active) sur les 41 tests rapides : **0 rapport** d'erreur mémoire, de
  comportement indéfini ou de fuite (186 s). Les résultats numériques sont identiques à ceux du build
  normal (mêmes taux d'alarme au centième près), ce qui confirme au passage le déterminisme.
  Clang n'a pas pu servir ici (bibliothèques compiler-rt absentes de la machine). GCC 13 émet sous
  sanitiseurs un faux positif `-Warray-bounds` dans `TerrainGenerator.cpp` (vecteur initialisé par une
  liste d'un élément) : dans les builds sanitiseurs seulement, ces avertissements de flot ne sont plus
  fatals (`CMakeLists.txt`).
- **valgrind memcheck** (exécution partielle de la suite : familles core, data, world, research,
  creatures et une partie de sim, avant arrêt du processus) : 0 erreur, 0 octet perdu.

### 6.6 Portabilité

- `CMakeLists.txt` : une cible d'interface `noctis_strict` porte les drapeaux du module UE pour tous
  les exécutables. GCC/Clang/AppleClang : `-fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -Werror`.
  MSVC : `/W4 /WX /permissive- /GR- /EHs-c- /utf-8 /Zc:__cplusplus`, `_HAS_EXCEPTIONS=0`, retrait des
  `/GR` et `/EHsc` ajoutés par défaut par CMake ; C4100, C4244, C4267 et C4305 sont désactivés comme
  dans Unreal. Option `NOCTIS_SANITIZE` pour ASan/UBSan.
- Compilé et testé ici : GCC 13.3 et Clang 18.1 (Linux x86-64), zéro avertissement avec `-Werror`.
  **Non compilés ici** : MSVC et AppleClang ; la CI (`.github/workflows/ci.yml`) les compile à chaque
  poussée. Points à surveiller au premier passage MSVC : avertissements de niveau 4 sans équivalent
  GCC (C4245 conversions signé/non signé de constantes, C4702 code inaccessible en build optimisé).
- `noctis_bench` mesure la mémoire et le temps CPU avec l'API native de chaque système
  (`GetProcessMemoryInfo`/`GetProcessTimes`, `task_info`, `/proc/self/statm` et `getrusage`).

### 6.7 Intégration continue

`.github/workflows/ci.yml` (validé par actionlint 1.7.12) : matrice ubuntu-latest (GCC, Clang),
windows-latest (MSVC), macos-latest (AppleClang) ; configuration Ninja, compilation, `ctest -L short`,
`noctis_bench --quick`, puis envoi en artefacts du JSON du benchmark, des journaux de test et du
rapport JUnit. Cache ccache par chaîne de compilation (objets MSVC en `/Z7` pour être cachables). Le
benchmark tourne même si un test échoue. Les tests longs tournent chaque lundi (03:17 UTC) ou à la
demande (`workflow_dispatch`, case « long_tests »). Le workflow n'a pas encore tourné sur GitHub.

## 7. Reproduire

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j2
ctest --test-dir build -L short                                # tests rapides
NOCTIS_LONG_TESTS=1 ctest --test-dir build -L long             # endurance + déterminisme 72 h
./build/noctis_tests --list --label all                        # liste des tests ; filtre : ./build/noctis_tests world.sound
./build/noctis_bench --out bench.json                          # benchmark complet (≈ 1 min) ; --quick pour la CI
valgrind --tool=callgrind ./build/noctis_bench --run-days 2 --regime accelerated --threads 1
cmake -S . -B build-asan -DNOCTIS_SANITIZE=address,undefined   # ASan + UBSan (GCC ; Clang demande compiler-rt)
```
