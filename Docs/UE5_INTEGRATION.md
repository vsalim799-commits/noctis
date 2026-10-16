# Noctis — intégration Unreal Engine 5

Ce document décrit la couche Unreal Engine 5 (UE 5.4 / 5.5) qui entoure le cœur de simulation `NoctisCore`,
la façon de la compiler et de la tester sur une vraie machine, et ce qui reste à vérifier.

> **Statut, sans détour.** Aucun moteur Unreal n'était disponible là où cette couche a été écrite : **aucun fichier
> de `Source/Noctis`, `Source/NoctisEditor`, `Source/NoctisCore/NoctisCore.Build.cs`, `Config/*.ini`, ni aucun script
> de `Content/Python` n'a été compilé par Unreal Build Tool ni exécuté dans l'éditeur.** Le code suit l'API UE 5.4/5.5
> telle qu'elle est documentée ; les appels dont la signature a changé entre versions sont marqués `VERIFY` dans le
> code. Ce qui a réellement été compilé et exécuté ici est listé à la section
> [Ce qui a été vérifié ici](#ce-qui-a-été-vérifié-ici). La [checklist de test](#checklist-de-test-sur-machine-réelle)
> dit quoi mesurer en premier.

---

## Sommaire

1. [Architecture](#architecture)
2. [Modules, fichiers et assets](#modules-fichiers-et-assets)
3. [Modèle de threads](#modèle-de-threads)
4. [Flux de données : simulation → instantané → acteurs → animation](#flux-de-données--simulation--instantané--acteurs--animation)
5. [Qui fait autorité ?](#qui-fait-autorité-)
6. [Audio](#audio)
7. [Environnement, eau, végétation](#environnement-eau-végétation)
8. [Outils de recherche, interface, débogage](#outils-de-recherche-interface-débogage)
9. [Sauvegarde](#sauvegarde)
10. [Pipeline d'assets (scripts Python)](#pipeline-dassets-scripts-python)
11. [Compiler et tester sur votre machine](#compiler-et-tester-sur-votre-machine)
12. [Checklist de test sur machine réelle](#checklist-de-test-sur-machine-réelle)
13. [Risques connus](#risques-connus)
14. [Ce qui a été vérifié ici](#ce-qui-a-été-vérifié-ici)

---

## Architecture

```
                       Data/ (JSON scientifiques : espèces, environnements, profils, sources)
                                     │  lu par le cœur via FNoctisUnrealFileSystem (IFileManager)
                                     ▼
 ┌───────────────────────────── module NoctisCore (C++20, sans moteur) ─────────────────────────────┐
 │ WorldSimulation : terrain, eau, végétation, météo, ciel, sons, traces, carcasses, nids,           │
 │                   créatures (perception, comportement, locomotion, tissus mous), écologie,         │
 │                   recherche (observations, photos, acoustique, capteurs, drone, véhicule, ...)     │
 │ JobScheduler ◄── FNoctisParallelForScheduler (ParallelFor du moteur)                              │
 │ Log          ──► LogNoctisCore (pont installé par NoctisCoreModule.cpp)                           │
 └───────────────────────────────────────────┬───────────────────────────────────────────────────────┘
                                             │ step(dt) / buildSnapshot()  (thread de travail UE::Tasks)
                                             ▼
 ┌──────────────────────────────── module Noctis (jeu) ──────────────────────────────────────────────┐
 │ UNoctisWorldSubsystem ── FNoctisSimulationRunner (pas fixe, accumulateur, triple tampon)          │
 │        │  OnWorldPreActorTick : publie la trame terminée, lance la suivante                       │
 │        ▼                                                                                          │
 │   FNoctisFrameOutput (RenderSnapshot + extras son/roues/chercheur + FNoctisAuxData à 1 Hz)        │
 │        │                                                                                          │
 │        ├─► ANoctisCreatureManager ─► ANoctisCreatureActor (pool) ─► UNoctisCreatureAnimInstance   │
 │        │                         └─► ISM d'imposteurs lointains                                   │
 │        ├─► ANoctisEnvironmentController ─► soleil/lune, SkyAtmosphere, fog, nuages, vent, MPC     │
 │        ├─► ANoctisWaterActor (rubans de rivière + patch d'eau local, ProceduralMesh)              │
 │        ├─► ANoctisVegetationInteraction (RT_NoctisTrails, MPC Interactor0..31, PCG)               │
 │        ├─► ANoctisAudioDirector (UNoctisCallSoundWave / sons enregistrés, arrivée physique)       │
 │        └─► UI (UMG) via UNoctisResearchLibrary, Gameplay Debugger "Noctis"                        │
 │                                                                                                   │
 │ Entrées ◄─ ANoctisResearcherCharacter, ANoctisResearchVehiclePawn (Chaos), ANoctisDronePawn,      │
 │            UNoctisResearchToolComponent (commandes exécutées entre deux pas)                       │
 └───────────────────────────────────────────────────────────────────────────────────────────────────┘
 ┌────────────── module NoctisEditor (éditeur) ──────────────┐   ┌──────── Content/Python ─────────┐
 │ validation Data/ (Message Log), validateur des DA_*,       │   │ setup_project.py                │
 │ rechargement PIE, commandlet CI, NoctisEditorLibrary       │   │ import_creatures.py             │
 └────────────────────────────────────────────────────────────┘   │ import_audio.py                 │
                                                                   └─────────────────────────────────┘
```

Principe directeur : **le cœur décide, Unreal montre**. Toute la science (où va chaque pied, qui entend quoi, quel
brouillard se forme au fond de la vallée) est calculée par `NoctisCore`, identique en jeu et dans l'outil en ligne de
commande `noctis_sim`. Unreal rend, spatialise, anime et collecte les entrées du joueur.

---

## Modules, fichiers et assets

| Élément | Rôle |
|---|---|
| `Noctis.uproject` | Modules `NoctisCore` (Runtime, PreDefault), `Noctis` (Runtime), `NoctisEditor` (Editor, Default). Plugins : EnhancedInput, PCG, ControlRig, IKRig, Metasound, ChaosVehiclesPlugin, ProceduralMeshComponent, Niagara, DataValidation, PythonScriptPlugin / EditorScriptingUtilities / ModelingToolsEditorMode (éditeur seulement). *GameplayTags* est un module du moteur (pas un plugin) : il est ajouté dans `Noctis.Build.cs`. |
| `Source/Noctis.Target.cs`, `Source/NoctisEditor.Target.cs` | `BuildSettingsVersion.V5`, `EngineIncludeOrderVersion.Latest` (UE 5.4+ ; pour 5.3 : V4 / Unreal5_3). |
| `Source/NoctisCore/NoctisCore.Build.cs` | C++20, `bEnableExceptions=false`, `bUseRTTI=false`, `PCHUsage=NoPCHs`, `bUseUnity=false`, dépend de `Core` seulement. |
| `Source/NoctisCore/Private/NoctisCoreModule.cpp` | `IMPLEMENT_MODULE`, pont de journalisation `noctis::setLogSink` → `UE_LOG(LogNoctisCore)`. Exclu de la compilation CMake. |
| `Public/Noctis/Core/Platform.h` | `NOCTISCORE_API` (défini par UBT), `NOCTIS_ASSERT` → `checkSlow`, n'inclut que `CoreTypes.h` + `AssertionMacros.h` côté UE. |
| `Source/Noctis/Public/Sim/` | `UNoctisWorldSubsystem`, `FNoctisSimulationRunner`, adaptateurs `FNoctisParallelForScheduler` / `FNoctisUnrealFileSystem`. |
| `Source/Noctis/Private/Sim/NoctisConsoleCommands.cpp` | `noctis.TimeScale`, `noctis.Pause`, `noctis.Census`, `noctis.Spawn`, `noctis.Kill`, `noctis.Weather`, `noctis.Hash` ; CVars `noctis.ShowLOD`, `noctis.Creatures.*`, `noctis.Vegetation.Interactors`. |
| `Source/Noctis/Public/Creatures/` | `ANoctisCreatureManager`, `ANoctisCreatureActor`, `UNoctisCreatureAnimInstance`, `UNoctisSpeciesVisualData`. |
| `Source/Noctis/Public/Environment/` | `ANoctisEnvironmentController`, `ANoctisWaterActor`, `ANoctisVegetationInteraction`. |
| `Source/Noctis/Public/Research/` | Chercheur, véhicule Chaos 6x6, drone, `UNoctisResearchToolComponent`, `UNoctisResearchLibrary`. |
| `Source/Noctis/Public/Audio/` | `ANoctisAudioDirector`, `UNoctisCallSoundWave` (`USoundWaveProcedural` alimenté par `SignalSynth`). |
| `Source/Noctis/Public/UI/NoctisWidgets.h` | Bases C++ des widgets : carnet, carte, spectrogramme, hypothèses, rapports. |
| `Source/Noctis/Private/Debug/` | Catégorie Gameplay Debugger `FGameplayDebuggerCategory_Noctis`. |
| `Source/Noctis/Public/Save/NoctisSaveGame.h` | Colle de sauvegarde (voir [Sauvegarde](#sauvegarde)). |
| `Source/Noctis/Private/Tests/NoctisSimulationTests.cpp` | Tests d'automatisation `Noctis.*`. |
| `Source/NoctisEditor/` | Validation des données, validateur d'assets, commande de rechargement, commandlet, `UNoctisEditorLibrary`. |
| `Tools/UECheck/` | Vérifications **sans moteur** : compilation syntaxique de la couche UE contre une doublure de l'API Unreal et les vrais en-têtes du cœur, cœur compilé comme sous UBT, miroir autonome des tests d'automatisation (voir [1 bis](#1-bis-vérifier-la-couche-unreal-sans-moteur-quelques-minutes)). |
| `Config/DefaultEngine.ini` | Lumen (GI + réflexions), Nanite, sky atmosphere, exposition physique, RVT, collisions `NoctisCreature`, réglages `[/Script/Noctis.NoctisSettings]`. Les VSM (défaut d'UE 5) et les variables par niveau sont laissées à `DefaultScalability.ini` (priorités, voir [Risques](#risques-connus)). |
| `Config/DefaultScalability.ini` | Valeurs Low/Medium/High/Epic propres à Noctis (non mesurées : à valider). |
| `Config/DefaultInput.ini` | Enhanced Input (classes par défaut, contexte `IMC_UI` automatique). |

Assets référencés par chemin dans le code (créés par les scripts Python) :

| Chemin | Créé par |
|---|---|
| `/Game/Input/IA_*`, `IMC_Researcher`, `IMC_Vehicle`, `IMC_Drone`, `IMC_Tools`, `IMC_UI` | `setup_project.py` |
| `/Game/Environment/MPC_NoctisWeather`, `/Game/Environment/Vegetation/RT_NoctisTrails`, `/Game/Environment/Water/M_NoctisWater` | `setup_project.py` |
| `/Game/UI/WBP_NoctisHUD` (+ `WBP_FieldJournal`, `WBP_FieldMap`, `WBP_Spectrogram`, `WBP_Hypotheses`, `WBP_Reports`) | `setup_project.py` (vides, mise en page à faire) |
| `/Game/Maps/HellCreek` (World Partition) | `setup_project.py` |
| `/Game/Creatures/<espèce>/DA_<espèce>`, `SK_` (+ LOD 1–3), `Animations/A_<espèce>_<clip>`, `ABP_`, `IK_`, `MI_<espèce>_<emplacement>`, `Textures/T_` ; `/Game/Creatures/_Shared/M_NoctisCreatureSkin` | `import_creatures.py` |
| `/Game/Audio/Classes/SC_*`, `Attenuation/ATT_*` (dont `ATT_Noctis_SpatialOnly`), `Concurrency/*` (dont `SCON_Noctis_Calls`), `Submixes/SM_*`, `Creatures/<espèce>/voc_*`, `Weather/SC_Thunder`, `Water/SC_Splash`, `Foley/SC_VegetationCrash`, `Foley/SC_Impact` | `import_audio.py` |

---

## Modèle de threads

```
 Thread de jeu (frame N)                                   Tâche UE::Tasks (job k)
 ───────────────────────                                   ───────────────────────
 OnWorldPreActorTick
   ├─ job k-1 terminé ? → Publish() : Previous ← Front ← Back (échange d'indices, aucune copie)
   │                       puis complétions des commandes du job (OnGameThread)
   ├─ accumulateur += dt_réel × échelle_de_temps (borné : MaxBacklogSeconds)
   └─ aucun job en vol → Launch(k) ───────────────────────►  commandes en attente (entre deux pas)
                                                             ApplyInput (chercheur, véhicule, drone)
 Tick des acteurs : lisent Previous/Front, interpolent       N × WorldSimulation::step(pas fixe)
 Rendu, animation (workers), audio                             └─ phases parallèles → ParallelFor
                                                             buildSnapshot → tampon Back
                                                             (+ FNoctisAuxData à ~1 Hz)
```

* **Pas fixe** : 0,05 s simulée (`FixedStepSeconds`) jusqu'à `FullDetailMaxTimeScale` (×8) ; au-delà, le cœur
  suspend le détail fin et le pas grandit jusqu'à `CoarseStepSeconds` (2 s). `MaxStepsPerJob` (64) borne un job.
* **Retard borné** : si le cœur ne suit pas, la dette au-delà de `MaxBacklogSeconds` est abandonnée — le temps simulé
  ralentit au lieu de partir en spirale.
* **Un seul job à la fois**, lancé uniquement depuis le thread de jeu. Le job ne touche que la simulation, le tampon
  Back et les copies d'entrées ; le thread de jeu ne lit que Front/Previous. Aucun verrou sur le chemin chaud.
* **Accès exclusif** (UI, commandes console, tests) : `UNoctisWorldSubsystem::WithSimulation(fn)` attend le job en
  cours (quelques ms), le publie, puis exécute `fn` sur le thread de jeu. À réserver à l'ouverture des panneaux.
* **Commandes** : `EnqueueCommand(SimWork, OnGameThread)` exécute `SimWork` sur le thread de simulation entre deux
  pas, puis `OnGameThread` après la publication. Les outils de recherche passent tous par là.
* **Parallélisme interne du cœur** : `FNoctisParallelForScheduler` (task graph du moteur). Le cœur garantit que
  `fn(i)` n'écrit que des données propres à l'indice `i` : l'ordre d'exécution ne change pas le résultat
  (vérifié bit à bit par `Noctis.Simulation.Determinism`). Repli possible sur le pool `std::thread` du cœur
  (`bUseParallelFor=False`).
* **Démarrage** : la génération de la vallée (terrain, hydrologie, végétation, populations) tourne sur une tâche de
  fond (`bAsyncBoot`) ; `OnSimulationReady` est diffusé quand c'est prêt. Mesuré en dehors d'Unreal : ~7 s pour la
  vallée complète (2017² échantillons, 2 threads, voir plus bas).
* **Animation** : `a.ParallelAnimUpdate=1` ; `UNoctisCreatureAnimInstance` reçoit ses entrées sur le thread de jeu
  (`NativeUpdateAnimation`) et le graphe les lit sur les workers.
* **Audio** : la synthèse des cris (`SignalSynth`) tourne sur une tâche de fond ; le thread audio sort du silence
  tant que le rendu n'est pas prêt (quelques ms).

---

## Flux de données : simulation → instantané → acteurs → animation

1. **`RenderSnapshot`** (cœur, `Sim/Snapshot.h`) : temps, ciel, météo, crue, et pour chaque animal vivant :
   position, cap/tangage/roulis, échelle, vitesse, allure et phase, longueur de foulée, facteur de service,
   jusqu'à 4 cibles de pieds + contact, décalages des tissus mous, activation musculaire, respiration, cible du
   regard, posture, comportement, vocalisation, boiterie, cicatrices, état corporel, mouillure, boue ; carcasses ;
   éclaboussures ; **nouveaux sons** (une seule fois chacun).
2. **`FNoctisFrameOutput`** (jeu) : l'instantané + pour chaque son le substrat, la distance et le niveau reçu
   calculé par le modèle de propagation du cœur ; états chercheur/véhicule/drone ; terramécanique par roue ;
   horloge ; chronométrages ; `FNoctisAuxData` (traces de pas visibles, usure des pistes, piétinement, brûlis, eau
   locale, rubans de rivière) partagé et immuable, reconstruit ~1 fois par seconde autour de la caméra.
3. **Repère** (`NoctisSpace.h`) : cœur x = est, y = nord, z = haut, mètres, cap 0 = est sens trigonométrique ;
   Unreal X = est, Y = sud, Z = haut, centimètres, yaw horaire. Le miroir de l'axe nord conserve la chiralité des
   maillages. `WorldOffsetCm` décale tout le monde si besoin. Le terrain du cœur couvre [0, 8192] m sur x et y ;
   le paysage Unreal se place donc en X = 0, Y = −819 200 cm (coin nord-ouest), voir `setup_project.py`.
4. **`ANoctisCreatureManager`** (TG_PrePhysics) : choisit qui reçoit un corps — acteurs complets dans
   `noctis.Creatures.ActorRadiusM` (au plus `MaxActors`, les plus proches d'abord, avec hystérésis), imposteurs
   instanciés jusqu'à `FarInstanceRadiusM`, rien au-delà (le cœur les simule toujours). Ce LOD est visuel ; le LOD de
   simulation (qui a des pieds, qui perçoit à pleine cadence) est décidé par le cœur autour des observateurs
   (`noctis.ShowLOD 1` l'affiche).
5. **`ANoctisCreatureActor`** : interpole Previous → Front (alpha = temps écoulé depuis la publication × échelle de
   temps / intervalle), applique transform + données primitives personnalisées (0 mouillure, 1 boue, 2 état
   corporel, 3 graine de motif, 4 nombre de cicatrices), déclenche les pas (MetaSound) au contact des pieds.
6. **`UNoctisCreatureAnimInstance`** : vitesse, allure, phase, posture, comportement, cibles des pieds (espace
   composant) et alpha de contact, tissus mous (cou, queue, ventre, gorge), respiration, cible du regard, mâchoire.
   Graphe conseillé : machine d'états/blendspace synchronisé sur `GaitPhase` → décalage du bassin → IK des pieds
   (Control Rig ou Two Bone IK, alpha = contact) → os modifiés pour les tissus mous → Look At → mâchoire.
   **Clips** : `DA_<espèce>.Clips` (rempli par `import_creatures.py` depuis les métadonnées de la fabrique) donne pour
   chaque clip sa séquence, sa boucle, la vitesse, la durée de foulée, la longueur de foulée et le facteur de service
   **auxquels il a été animé** ; `GetLocomotionSequence()` / `GetClipSequence(nom)` (sûres sur les threads
   d'animation) rendent la séquence de l'allure simulée (`walk`, `walk_fast`, `run`, `swim`, `idle`) et
   `LocomotionPlayRate` = fréquence de foulée simulée × durée de foulée du clip : les pieds animés tombent au rythme
   des pieds simulés. **Mouvement racine** : la simulation place l'acteur ; les séquences sont importées avec
   *Enable Root Motion* (racine verrouillée sur la pose de référence) et l'instance est en `IgnoreRootMotion` — le
   mouvement extrait est jeté, le maillage ne dérive pas.

---

## Qui fait autorité ?

| Domaine | Autorité | Détail |
|---|---|---|
| Animaux (position, pieds, comportement, sons) | Cœur | Unreal interpole et anime. Collision `NoctisCreature` : requêtes seulement (outils d'observation), aucune réponse physique. |
| Chercheur à pied | Unreal | `CharacterMovement` ; la position des yeux, le cap, la vitesse, la posture, la voix et la lampe frontale sont envoyés au cœur chaque trame → un émetteur que les animaux perçoivent. |
| Véhicule 6x6 | Au choix (`Authority`) | `UnrealChaos` (défaut, jeu interactif) : Chaos intègre le mouvement ; le cœur comptabilise énergie et bruit et renvoie la terramécanique de Bekker par roue (enfoncement, résistance au roulement, traction) appliquée à Chaos. `CoreTerramechanics` (parité avec `noctis_sim`, cinématiques, tests) : le `VehicleSystem` du cœur intègre les commandes, le pion est cinématique. |
| Drone | Cœur | Batterie, vent, portée radio, pilote automatique, bruit des rotors ; le pion affiche l'état publié et envoie des consignes. |
| Ciel, météo, brouillard, eau, végétation | Cœur | Unreal ne fait que rendre (aucun cycle jour/nuit propre à Unreal). |
| Résultats scientifiques | Cœur | Les outils ne décident rien : identification, mesures, enregistrements sont calculés par `ResearchSystem`. |

---

## Audio

* **Cris** : par défaut synthétisés en direct par `UNoctisCallSoundWave` (`USoundWaveProcedural`) avec le
  `SignalSynth` du cœur, à la distance réelle de l'auditeur : structure harmonique et contour de l'espèce, absorption
  ISO 9613-1 par harmonique, feuillage, vent, relief. Unreal ne fait que spatialiser
  (`ATT_Noctis_SpatialOnly` : atténuation de distance désactivée). Ce que le joueur entend et ce que son
  enregistreur capte relèvent donc de la même physique.
* **Arrivée physique** : chaque son est programmé à émission + distance / vitesse du son (température courante).
  Un tonnerre à 3 km arrive ~9 s après l'éclair.
* **Calibration** : `AudioFullScaleDbSpl` (110 dB SPL = 0 dBFS). Les sons enregistrés de `Tools/AudioFactory`
  reçoivent un volume d'asset `10^((calibration_spl_db_at_0dbfs_rms − AudioFullScaleDbSpl)/20)` (`import_audio.py`) :
  à multiplicateur 1, un cri enregistré sonne à son niveau de source à 1 m, comme un cri synthétisé. Les variantes de
  distance et le tonnerre sont calibrés sur leur niveau **reçu** (`level.received_spl_db`), les lits d'ambiance sur
  leur niveau ambiant (`level.ambient_spl_db`) : le directeur audio n'applique que l'écart entre le niveau du cœur et
  cette référence (`RainLoopAmbientDb`, `WindLoopAmbientDb`), sans compter deux fois le niveau absolu.
* **Événements enregistrés** : tonnerre (classes 0,5 / 2 / 5 / 12 km), éclaboussures et végétation brisée (classes de
  masse) viennent des bibliothèques de `Tools/AudioFactory` ; le directeur choisit la classe la plus proche
  (distance ou masse de l'émetteur), une variation au hasard, joue le son au point d'impact / à l'émetteur
  (spatialisation seule) et corrige le niveau. Le tonnerre est une source **linéique** dans le cœur
  (`SoundField::spreadingLossDb`), déclenché 80 ms avant la première arrivée (pré-roulement du fichier).
* **Hypothèses** : toutes les vocalisations de dinosaures non aviens sont des hypothèses (manifeste :
  `hypothesis = true`, classe `SC_Creature_Vocal_Hypothesis`, sous-mélange `SM_Creatures_Hypotheses`) ; les
  métadonnées `Noctis.Confidence` / `Noctis.Hypothesis` voyagent avec chaque asset.
* **Pas** : MetaSound par plan corporel avec les entrées `Footfall` (Trigger), `Mass` (Float, kg), `LevelDb`
  (Float), `Substrate` (Int32 : 0 roche, 1 gravier, 2 sable, 3 limon, 4 boue, 5 argile, 6 sol forestier, 7 tourbe),
  `InWater` (Bool). La création du graphe MetaSound n'est pas scriptée (API Builder trop mouvante entre 5.4 et 5.5).
* **Ambiances** : pluie, vent, chœur d'insectes et activité des oiseaux suivent la météo et la couche de populations.

---

## Environnement, eau, végétation

* **`ANoctisEnvironmentController`** prend dans le niveau la première `DirectionalLight` (soleil), celle portant le
  tag `Moon`, `SkyAtmosphere`, `ExponentialHeightFog`, `VolumetricCloud`, `SkyLight`, `WindDirectionalSource` et les
  pilote depuis `SkySystem`/`WeatherSystem` (latitude, obliquité, calendrier paléo, brouillard de fond de vallée par
  accumulation d'air froid, régime météo markovien). Il écrit aussi `MPC_NoctisWeather` (CloudCover, RainRate,
  GroundWetness, WindSpeed, WindDirection, FogExtinction, Illuminance, SunElevation, Temperature, Humidity,
  LightningFlash, RiverStage, Flooding, HourOfDay). Éclairement en unités physiques (lux) : l'exposition automatique
  doit couvrir la nuit sans lune comme le plein soleil (`ExtendDefaultLuminanceRange`).
* **`ANoctisWaterActor`** : section 0 = rubans de rivière de toute la vallée (largeur selon le stade par géométrie
  hydraulique), section 1 = grille exacte de surface d'eau autour de la caméra (lacs, bras morts, plaine inondée).
  Couleurs de sommets : flux (RG, 0,5 = immobile, ±2 m/s) et profondeur (B, 0–5 m). Le plugin Water d'Unreal n'est
  volontairement pas utilisé (ses plans d'eau sont des splines éditées, ici le niveau est simulé).
* **`ANoctisVegetationInteraction`** : usure des pistes (R), piétinement récent (G) et brûlis (B) copiés dans
  `RT_NoctisTrails` ; `MPC_NoctisWeather.TrailMaskRect` donne le rectangle monde. Les N animaux les plus proches
  (`noctis.Vegetation.Interactors`) sont écrits dans `Interactor0..31` (xyz + rayon) pour la flexion du feuillage.
  `OnVegetationStateUpdated(Bounds)` (Blueprint) et une liste de `UPCGComponent` régénérés périodiquement servent
  de points d'accroche PCG / RVT.

---

## Outils de recherche, interface, débogage

* **`UNoctisResearchToolComponent`** : appareil photo, jumelles, microphones, capteurs (pièges photo, enregistreurs),
  carnet ; chaque action est une commande exécutée entre deux pas, avec retour par délégué sur le thread de jeu.
* **`UNoctisResearchLibrary`** (Blueprint) : lecture des données **du joueur** uniquement (carnet, catalogue
  d'individus, hypothèses, enregistrements, carte de terrain construite depuis `FieldMap`, rapports). Jamais l'état
  caché de la simulation.
* **Widgets** (`NoctisWidgets.h`) : carnet de terrain, carte, visionneuse de spectrogramme, panneau d'hypothèses,
  visionneuse de rapports (Markdown du cœur → texte riche). Les WBP créés par `setup_project.py` sont vides : leur
  mise en page reste à faire (liaisons `BindWidgetOptional`).
* **Gameplay Debugger** (touche apostrophe, catégorie « Noctis ») : physiologie, scores de comportement, conscience
  de chaque entité perçue (stade, relation, indices, menace), blessures, groupe, LOD, cible du regard — tout ce que
  le joueur ne voit jamais.
* **Console** : `noctis.TimeScale <x>`, `noctis.Pause`, `noctis.Census`, `noctis.Spawn <espèce> [n] [m]`,
  `noctis.Kill <id|nearest> [rayon]`, `noctis.Weather clear|fair|overcast|lightrain|heavyrain|storm|fog <1/m>|nofog`,
  `noctis.Hash` (empreinte binaire de l'état publié, sons exclus car leur répartition dépend de la cadence des
  trames ; deux exécutions ne sont comparables qu'au même `tick`), `noctis.ShowLOD 1`, `noctis.Creatures.MaxActors`, … Éditeur :
  `noctis.Editor.ValidateData`, `noctis.Editor.ReloadData`.

---

## Sauvegarde

`WorldSimulation::save/load` sont **déclarées mais non implémentées** dans le cœur. La couche Unreal est construite
autour d'une future API de blob binaire versionné (`saveToBlob` / `loadFromBlob`, chunks `(tag, taille, données)`),
stocké tel quel dans `UNoctisSaveGame::CoreBlob`. En attendant, une sauvegarde conserve la **recette** du monde
(environnement, profil, graine, temps simulé) : le chargement reconstruit la même vallée et avance l'horloge à pas
grossiers. Relief, rivières et espèces sont identiques ; **les individus, observations, enregistrements et
hypothèses du joueur ne sont pas restaurés**, et l'interface le signale (`bIsApproximate`).

---

## Pipeline d'assets (scripts Python)

Exécution dans l'éditeur (`Output Log` en mode `Python`, ou `File > Execute Python Script`) :

```
py "Content/Python/setup_project.py"            # entrées, MPC, RT, eau, widgets, carte HellCreek
py "Content/Python/import_audio.py"             # classes, atténuations, concurrence, sons du manifeste
py "Content/Python/import_creatures.py"         # créatures de Build/Creatures (une ou plusieurs espèces en argument)
```

ou sans interface : `UnrealEditor-Cmd <Projet>/Noctis.uproject -run=pythonscript -script="Content/Python/setup_project.py"`.

* **`setup_project.py`** crée tout ce que le code référence par chemin, puis une carte World Partition **vide**
  (`new_level(chemin, True)`, équivalent d'*Empty Open World* — pas le modèle *Open World*, dont le paysage d'exemple
  serait pris pour le terrain Noctis) et place `PlayerStart` au camp de base calculé **par le cœur** (`unreal.NoctisEditorLibrary.
  compute_terrain_info()` génère le terrain sans animaux). L'import du Landscape n'est pas scriptable en Python :
  le script affiche les valeurs exactes (résolution, position, échelle) pour *Landscape > Import from File* avec
  `Build/Terrain/heightmap.png` produit par `noctis_sim export-terrain --out Build/Terrain` (même graine que
  `Seed`). Couches de peinture : `weight_<habitat>.png` ; densités de végétation pour PCG : `foliage_<type>.png`.
* **`import_creatures.py`** suit le contrat d'export de `Tools/CreatureFactory` (schéma
  `noctis.creature_asset/1`, `Build/Creatures/<espèce>/export/<espèce>_metadata.json`) : `SK_<espèce>.fbx` et ses
  LOD `_LOD1..3` (importés comme LOD du même maillage), une FBX par clip `A_<espèce>_<clip>.fbx`, textures
  `export/textures/T_<espèce>_<Carte>.png` (`BaseColor` sRGB, `Normal_DX` — convention UE ; à défaut `Normal`
  OpenGL importée avec *Flip Green Channel* —, `ORM` linéaire, `SSS` poids de diffusion, `Eye_BaseColor` ; `Height`,
  `Cavity`, `Masks` importées pour les artistes mais non branchées), os et chaînes de reciblage des métadonnées.
  Il crée maillage, squelette, physique, séquences (mouvement racine activé, racine verrouillée), instances du
  matériau maître `M_NoctisCreatureSkin` par emplacement du maillage (`M_Body`, `M_Eye`, `M_Claw`, `M_Teeth` →
  `MI_<espèce>_body|eye|claws|teeth`) : modèle *Subsurface*, ORM (R occlusion, G rugosité), `SSS` × `SubsurfaceAmount`
  en opacité de diffusion, `SubsurfaceColor` chair, `Tint`/`VertexColorAmount` (griffes : couleurs de sommets de la
  fabrique, base sombre → pointe claire), mouillure/boue/état corporel par données primitives (0–4).
  `ABP_<espèce>` (parent `UNoctisCreatureAnimInstance`, graphe vide), `IK_<espèce>` (racine de reciblage, chaînes
  Spine/Neck/Tail/Jaw/LeftLeg/RightLeg/LeftArm/RightArm des métadonnées, buts sur orteils et mains) et `DA_<espèce>` :
  `MeshBodyLengthM` = longueur mesurée sur la sculpture (`sizes.model.length_m`), `MeshYawOffsetDeg` (mesuré sur le
  squelette importé ; −90 par convention de la fabrique, qui regarde +Y comme le Mannequin), `Clips` (vitesse, durée
  et longueur de foulée, facteur de
  service de chaque clip), os du bassin/tête/mâchoire/pieds/cou/queue. Sans métadonnées, une recherche tolérante
  (premier fichier qui correspond) reste en place pour d'autres sources. Sous UE 5.5, l'import FBX passe par
  Interchange qui ignore `FbxImportUI` : le script bascule temporairement `Interchange.FeatureFlags.Import.FBX 0`
  (option `--interchange` pour ne pas le faire). `--resolution 2k` prend les cartes 2K de la fabrique (BaseColor,
  ORM ; sa normale 2K n'existe qu'en OpenGL, la `Normal_DX` 4K est gardée). Un matériau maître d'une version
  précédente (paramètres `Roughness`/`AO`/`Subsurface` séparés) est détecté et recréé.
* **Notes de reciblage** : l'IK des pieds vient de la simulation (cibles des pieds), pas de l'animation ; le
  reciblage (IK Retargeter `IK_<source>` → `IK_<espèce>`, chaînes de même nom) ne sert qu'à réutiliser des clips
  entre espèces de même plan corporel (théropode → théropode, cératopsien → ankylosaure avec prudence). Ne jamais
  recibler un bipède sur un quadrupède : la simulation pilote 2 ou 4 pieds selon `body_plan`.
* **`import_audio.py`** lit `Data/Audio/manifest.json`, crée la hiérarchie des classes de sons, les préréglages
  d'atténuation et de concurrence, importe les WAV de `Build/Audio` (variantes de distance comprises, pour la
  bibliothèque de référence du carnet), applique classe/atténuation/concurrence/sous-mélange/priorité/volume calibré,
  écrit les métadonnées de provenance (`Noctis.Confidence`, `Noctis.ModelConfidence`, légendes FR/EN, niveaux), construit
  les cues aléatoires des bibliothèques d'événements (`/Game/Audio/Cues/RC_<groupe>`) et en remplit les cues des réglages
  (`Weather/SC_Thunder`, `Water/SC_Splash`, `Foley/SC_VegetationCrash` ; plus de cue vide), puis lie les cris `voiceA`
  aux `DA_<espèce>` (`--prefer-baked` pour les jouer à la place de la synthèse).

---

## Compiler et tester sur votre machine

### Prérequis

* **Windows 10/11** : Unreal Engine 5.4 ou 5.5 (Epic Games Launcher ou source), Visual Studio 2022 ≥ 17.8 avec
  « Développement Desktop C++ », « Développement de jeux en C++ », SDK Windows 10/11, .NET 8 (UE 5.4/5.5).
* **Linux** : moteur compilé depuis les sources (`Setup.sh`, `GenerateProjectFiles.sh`, `make`), avec la chaîne
  clang fournie par Epic pour la version.
* Le dépôt complet : `Noctis.uproject` à la racine, `Data/` à côté (lu au démarrage), `Build/` si vous importez
  des assets.
* Les sous-modules et LFS éventuels récupérés (`git lfs pull` si l'équipe en ajoute).

### 1. Vérifier le cœur hors Unreal (2 minutes)

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build                     # suite courte
./build/noctis_sim validate --data Data    # chargement et cohérence des données
```

Si le cœur ne compile pas en mode strict CMake (`-fno-exceptions -fno-rtti -Werror`), il ne compilera pas sous UBT.

### 1 bis. Vérifier la couche Unreal sans moteur (quelques minutes)

```
python3 Tools/UECheck/check_ue_layer.py --all -j 4
```

Compile chaque fichier de `Source/Noctis` et `Source/NoctisEditor` avec clang (`-fsyntax-only`, C++20, sans
exceptions ni RTTI, `-Wshadow-all -Wundef -Werror`) contre les vrais en-têtes du cœur et une doublure de l'API UE
5.4/5.5 (`Tools/UECheck/stub`), en configuration éditeur puis sans éditeur (`WITH_EDITOR=0`, pas de tests ni de
dessin de débogage) ; compile le cœur comme UBT le fait (branche Unreal de `Platform.h`, macros `check`/`verify`/
`ensure`/`TEXT`/`IN`/`OUT`…) ; puis construit et lance le miroir autonome des tests `Noctis.Simulation.*`.
À lancer après toute modification de l'API du cœur : c'est le moyen le plus rapide de voir que la couche UE ne suit
plus. **Ce n'est pas une compilation Unreal** (voir `Tools/UECheck/README.md`) : les signatures UE de la doublure
sont celles que ce code suppose ; UBT reste juge.

### 2. Générer les fichiers de projet

* Windows : clic droit sur `Noctis.uproject` → *Generate Visual Studio project files*, ou :
  ```
  "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="D:\noctis\Noctis.uproject" -game -rocket -progress
  ```
  (moteur source : `GenerateProjectFiles.bat -project="…\Noctis.uproject" -game -engine`).
* Linux (moteur source) :
  ```
  <UE>/GenerateProjectFiles.sh -project="$PWD/Noctis.uproject" -game
  ```
* Si `EngineAssociation` (5.5) ne correspond pas à votre moteur : clic droit → *Switch Unreal Engine version*.

### 3. Compiler

```
# Windows
"<UE>\Engine\Build\BatchFiles\Build.bat" NoctisEditor Win64 Development -Project="D:\noctis\Noctis.uproject" -WaitMutex -FromMsBuild
# Linux
<UE>/Engine/Build/BatchFiles/Linux/Build.sh NoctisEditor Linux Development -Project="$PWD/Noctis.uproject"
```

Erreurs probables à la première compilation (corriger aux lignes `VERIFY`) : signatures de
`UEditorValidatorBase` (5.3 → 5.4), `ParallelFor` avec `EParallelForFlags`, `SetupGameplayDebuggerSupport`,
noms de propriétés Enhanced Input, en-têtes déplacés entre versions mineures. `NoctisCore` doit compiler sans aucune
modification (il est déjà validé en mode strict) ; s'il échoue, c'est presque toujours une collision de macro
Windows/UE — le signaler plutôt que d'ajouter un `#undef`.

### 4. Premier lancement de l'éditeur

1. Ouvrir `Noctis.uproject` (la carte `HellCreek` n'existe pas encore : l'éditeur ouvre une carte vide).
2. Vérifier `Window > Output Log` : `LogNoctisCore` doit afficher le chargement des données quand une partie démarre.
3. `Tools > Noctis > Valider les données scientifiques` → `Window > Message Log > Noctis` : 0 erreur attendue
   (les avertissements « espèce sans fiche visuelle » sont normaux tant que les créatures ne sont pas importées).
4. Lancer les scripts : `setup_project.py`, puis `import_audio.py`, puis `import_creatures.py`.
5. Importer le Landscape avec les valeurs affichées par `setup_project.py`.
6. Créer les graphes : `ABP_<espèce>` (voir l'en-tête de `NoctisCreatureAnimInstance.h`), MetaSound de pas,
   mise en page des widgets.
7. *Play* (PIE) : la vallée se construit en arrière-plan (quelques secondes), puis `noctis.Census` doit lister les
   populations et `stat unit` rester stable.

### 5. Tests d'automatisation

Dans l'éditeur : `Tools > Session Frontend > Automation`, filtre `Noctis`, *Start Tests*. En ligne de commande :

```
# Windows
"<UE>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "D:\noctis\Noctis.uproject" -ExecCmds="Automation RunTests Noctis; Quit" -unattended -nopause -nullrhi -nosplash -log -ReportExportPath="D:\noctis\Saved\AutomationReport"
# Linux
<UE>/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/Noctis.uproject" -ExecCmds="Automation RunTests Noctis; Quit" -unattended -nopause -nullrhi -nosplash -log
```

| Test | Vérifie |
|---|---|
| `Noctis.Core.CoordinateSpace` | conversions cœur ↔ Unreal (axes, cap, vitesse, attitude, aller-retour) |
| `Noctis.Core.FileSystemAdapter` | `FNoctisUnrealFileSystem` liste `Data/Species` dans le même ordre que le système de fichiers standard du cœur (les indices d'espèces en dépendent), lit les octets UTF-8 à l'identique, écrit et relit |
| `Noctis.Core.ParallelForScheduler` | chaque indice visité exactement une fois (0 à 100 003 éléments) |
| `Noctis.Simulation.Boot` | démarrage via les adaptateurs Unreal, espèces, populations, instantané cohérent, 40 pas |
| `Noctis.Simulation.Determinism` | même graine avec planificateur série, ParallelFor et pool `std::thread` : empreintes identiques bit à bit toutes les secondes pendant 30 s puis 10 min accélérées ; autre graine ≠ |
| `Noctis.Simulation.SnapshotConsistency` | 400 instantanés temps réel + 100 accélérés : ids uniques, espèces valides, valeurs finies, dans le terrain, pieds ≤ 4, échelle > 0, soleil normé, météo dans [0,1], aucun son publié deux fois, temps croissant |
| `Noctis.Simulation.Runner` | runner asynchrone (UE::Tasks, triple tampon) : trames ordonnées, alpha ∈ [0,1], commandes exécutées avant les pas et complétées sur le thread de jeu, **même état bit à bit** qu'un seul job bloquant du même nombre de pas, `WithSimulation` voit l'état publié |
| `Noctis.World.SubsystemBoot` | `UNoctisWorldSubsystem` dans un vrai `UWorld` de jeu : démarrage asynchrone, trames publiées, instantané cohérent, accélération du temps (test latent, ≤ 180 s) |

Validation des données en intégration continue :

```
UnrealEditor-Cmd "<…>/Noctis.uproject" -run=NoctisValidateData [-WarningsAsErrors]
```

### 6. Empaqueter

```
"<UE>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="D:\noctis\Noctis.uproject" -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="D:\noctis\Packaged"
```

Vérifier que `Data/` est présent à côté de l'exécutable (`RuntimeDependencies` dans `Noctis.Build.cs`), puis lancer
les tests dans le jeu packagé (`-ExecCmds="Automation RunTests Noctis.Simulation; Quit"`) : l'empreinte finale
affichée par `Noctis.Simulation.Determinism` doit être celle de l'éditeur sur la même plateforme.

---

## Checklist de test sur machine réelle

Cocher dans l'ordre ; noter la machine (CPU, GPU, RAM, pilote, résolution) avec chaque mesure.

### A. Compilation et démarrage

- [ ] `NoctisEditor` compile en Development **et** en DebugGame ; `Noctis` (jeu) compile en Shipping.
- [ ] Aucun avertissement de `NoctisCore` (même niveau d'exigence que CMake).
- [ ] L'éditeur démarre ; `Message Log > Noctis` : 0 erreur.
- [ ] Tous les tests `Noctis.*` passent (éditeur et `-nullrhi`).
- [ ] PIE : `OnSimulationReady` en moins de 15 s (vallée complète) ; le thread de jeu ne gèle pas pendant le démarrage.
- [ ] Empreinte finale de `Noctis.Simulation.Determinism` identique entre deux lancements, entre éditeur et jeu
      packagé, et (à noter, sans garantie) entre Windows et Linux.

### B. Fonctionnel

- [ ] Animaux visibles près du camp, pieds posés sur le terrain (pas de glisse : comparer cibles de pieds et
      position des orteils avec `noctis.ShowLOD 1` + Gameplay Debugger).
- [ ] `noctis.TimeScale 60` puis `1` : pas de saut visuel, les animaux reprennent leur allure.
- [ ] Aube et crépuscule : lumière, brouillard de vallée, ciel cohérents ; nuit sans lune lisible avec la lampe frontale.
- [ ] `noctis.Weather storm` : pluie, nuages, vent dans la végétation, tonnerre retardé selon la distance de l'éclair.
- [ ] Crue (attendre ou forcer la météo) : la rivière s'élargit, la plaine se couvre d'eau.
- [ ] Traces : marcher dans la boue près de la rivière, puis vérifier dans le carnet / sur le sol.
- [ ] Véhicule : enfoncement visible sur argile humide, traction réduite ; bascule `CoreTerramechanics` conforme.
- [ ] Drone : orbite, suivi, retour ; perte de liaison derrière un relief.
- [ ] Outils : photo d'un animal identifié dans le carnet ; enregistrement → spectrogramme ; hypothèse → statistiques.
- [ ] Audio : un cri lointain arrive en retard et filtré ; même niveau perçu entre un cri synthétisé et sa version
      enregistrée (`--prefer-baked`) à 50 m.

### C. Performances — cibles par niveau de scalabilité (hypothèses à confirmer)

| Niveau | Matériel de référence | Résolution (TSR) | Cible | Budget GPU | Budget thread de jeu |
|---|---|---|---|---|---|
| Low | GTX 1660 / RX 5600 XT, 6 cœurs | 1080p, 50 % | ≥ 30 i/s | ≤ 33 ms | ≤ 12 ms |
| Medium | RTX 2060 / RX 6600, 6 cœurs | 1080p, 67 % | ≥ 45 i/s | ≤ 22 ms | ≤ 10 ms |
| High | RTX 3070 / RX 6800, 8 cœurs | 1440p, 67 % | ≥ 60 i/s | ≤ 16,6 ms | ≤ 8 ms |
| Epic | RTX 4080 / RX 7900 XT, 8+ cœurs | 2160p, 50–67 % | ≥ 60 i/s | ≤ 16,6 ms | ≤ 8 ms |

Coût du cœur mesuré **hors Unreal** sur la machine de développement (4 cœurs partagés, voir
[Ce qui a été vérifié ici](#ce-qui-a-été-vérifié-ici)) : moins de 0,1 ms par pas de 50 ms avec la population par
défaut et un seul animal en détail complet, mais ≈ 0,12 ms **par animal en détail complet** et par pas. Le job de
simulation tourne en parallèle du thread de jeu ; il ne doit pas dépasser la durée d'une trame (sinon le temps
simulé ralentit) : le scénario du troupeau près de la rivière est celui qui fixe le budget.

Mesures à faire, avec les commandes :

| Quoi | Commande / outil | Seuil d'alerte |
|---|---|---|
| Temps par thread (Game, Draw, GPU, RHI) | `stat unit`, `stat unitgraph` | Game > budget ci-dessus |
| Détail GPU (Lumen, VSM, Nanite, translucidité de l'eau, brouillard volumétrique) | `stat gpu`, `ProfileGPU` (Ctrl+Maj+,) | un poste > 25 % de la trame |
| Job de simulation et présentation | Unreal Insights (`-trace=cpu,frame,bookmark,task`), marqueurs `NoctisSim_Job`, `NoctisSim_Steps`, `NoctisSim_Snapshot`, `NoctisSim_Aux`, `Noctis_SubsystemPreActorTick`, `Noctis_CreatureManager`, `Noctis_AnimUpdate`, `Noctis_AudioDirector`, `Noctis_Environment`, `Noctis_Water`, `Noctis_Vegetation` | `NoctisSim_Job` > 1 trame ; `Noctis_SubsystemPreActorTick` > 0,5 ms (attente d'un job = lockstep involontaire) |
| Chronométrage du dernier job | `UNoctisWorldSubsystem::GetLastJobMilliseconds` (HUD) | > 8 ms (High) |
| Animation | `stat anim`, `a.VisualizeLODs 1` | > 2 ms de thread de jeu pour 96 acteurs |
| Acteurs de créatures | `noctis.Creatures.MaxActors` 32 → 160 en mesurant `stat unit` | coût par acteur > 0,05 ms |
| Mémoire | `stat memory`, `memreport -full`, `stat llm` | > 12 Go (Epic), > 8 Go (Low) |
| Streaming World Partition | `wp.Runtime.ToggleDrawRuntimeHash2D`, `stat streaming` | à-coups en véhicule à vitesse max |
| Audio | `stat audio`, `au.Debug.SoundCues 1` | voix > 64, rendu procédural en retard (silence au début des cris) |
| Ombres et GI | `r.Shadow.Virtual.Visualize`, `r.Lumen.Visualize.CardPlacement` | pages VSM invalidées en continu par la végétation animée |
| Démarrage | horodatage `Simulation prête` dans le log | > 15 s |

Scénarios à mesurer : (1) camp de base à l'aube, brouillard ; (2) troupeau d'*Edmontosaurus* près de la rivière
(le plus d'acteurs complets) ; (3) orage nocturne ; (4) véhicule à vitesse max à travers la plaine ; (5) temps ×60
pendant 5 minutes ; (6) drone à 120 m au-dessus de la forêt riveraine.

---

## Risques connus

* **Jamais compilé sous UBT** : attendre une passe de corrections d'API (lignes `VERIFY`), surtout pour
  DataValidation, Enhanced Input (structure des mappings en 5.5/5.6), Gameplay Debugger, IK Rig (contrôleur Python),
  MetaSound et Interchange.
* **Scripts Python non exécutés** : noms de propriétés Python (`d_b_attenuation_at_max`, `concurrency_set` en
  `TSet`), factories (`WidgetBlueprintFactory.parent_class`, `AnimBlueprintFactory`), `new_level(chemin, True)` (carte partitionnée vide). Chaque étape
  est isolée et journalisée : une erreur n'arrête pas le reste.
* **Chaînes accentuées dans `TEXT()`** : les sources sont en UTF-8 sans BOM. Si MSVC ne compile pas en `/utf-8` sur
  votre version d'UBT, les messages français s'afficheront mal (pas d'erreur de compilation, sauf avertissement
  C4819 sur certaines pages de code) : vérifier un message accentué dans l'Output Log au premier lancement ; au
  besoin ajouter `/utf-8` (ou enregistrer les fichiers avec BOM).
* **Exceptions/RTTI** : le cœur et les modules qui l'incluent sont compilés sans exceptions ni RTTI ; une dépendance
  tierce qui les exige ne peut pas être ajoutée à ces modules.
* **Ordre des fichiers** : les indices d'espèces dépendent de l'ordre de chargement ; `FNoctisUnrealFileSystem`
  trie par octets comme le système de fichiers standard (testé), mais un pak ou un système de fichiers insensible à
  la casse peut changer des noms : garder `Data/` en fichiers libres (`NonUFS`).
* **Déterminisme flottant entre compilateurs** : MSVC, clang et gcc peuvent produire des résultats flottants
  différents (FMA, ordre des opérations, `/fp:fast`). Le déterminisme est garanti **pour un même binaire** ; comparer
  `noctis.Hash` entre plateformes n'a de sens qu'avec des options flottantes identiques (à vérifier).
* **Vallée de 8 × 8 km en World Partition** : précision flottante correcte (< 10 km de l'origine), mais le Landscape
  2017² et la végétation PCG doivent être découpés en cellules raisonnables ; HLOD à construire.
* **Translucidité de l'eau** : `M_NoctisWater` est un placeholder translucide ; le remplacer par un matériau
  *Single Layer Water* (gardant les entrées de couleurs de sommets).
* **Sauvegarde approximative** tant que le cœur n'expose pas `saveToBlob` / `loadFromBlob`.
* **Lumen et végétation dense** : la forêt riveraine animée invalide les VSM et le cache de surfaces Lumen ;
  `Low` désactive VSM et Lumen à dessein.
* **Données primitives personnalisées** : le matériau maître lit les index 0–4 ; tout autre matériau de créature
  doit respecter la même convention.
* **Interchange (5.5)** : si la bascule `Interchange.FeatureFlags.Import.FBX` disparaît, l'import FBX ignore les
  options du script (échelle, normales) : vérifier l'échelle des maillages importés (le validateur d'assets avertit
  si `MeshBodyLengthM` et la boîte englobante divergent).
* **Orientation et mouvement racine des clips** : `README` de la fabrique : « la créature regarde +Y » ; ses
  métadonnées de clips disent « +X UE après conversion » pour la translation du mouvement racine. Les deux ne peuvent
  pas être vrais ensemble. `import_creatures.py` mesure donc la direction bassin → tête sur la pose de référence du
  squelette importé (bibliothèque Python `AnimPoseExtensions`, UE 5.1+) et en déduit `MeshYawOffsetDeg` (−90 si la
  tête est sur +Y, 0 si elle est sur +X), avec un avertissement si la mesure contredit la convention de la fabrique ;
  à défaut de mesure il applique −90. Une clé `ue5.mesh_yaw_offset_deg` dans les métadonnées l'emporte sur tout. Au
  premier import, vérifier dans une partie que les animaux avancent tête la première. Le mouvement racine est de
  toute façon jeté (`IgnoreRootMotion`).
* **Priorités des variables console** : une valeur posée dans `[ConsoleVariables]` ou `[/Script/Engine.RendererSettings]`
  de `DefaultEngine.ini` est plus prioritaire que la scalabilité et figerait le réglage à tous les niveaux. C'est
  pourquoi `r.Shadow.Virtual.Enable`, `r.VolumetricFog*`, `noctis.Creatures.*` et `noctis.Vegetation.Interactors` ne
  sont réglés que dans `DefaultScalability.ini` (corrigé dans cette passe : ils étaient aussi dans `DefaultEngine.ini`).
  Vérifier au premier lancement qu'aucun message « was ignored as it is lower priority » n'apparaît pour eux.
* **Sons rares sur des simulations courtes** : sur les 500 instantanés du miroir de `SnapshotConsistency` (vallée
  réduite, 2 h simulées) aucun son n'est publié, et en suivant un animal en détail complet pendant 5 minutes il reste
  immobile (aucun pas). Les vérifications « aucun son publié deux fois » sont donc peu exercées par les tests ; la
  chaîne audio (directeur, MetaSound de pas, synthèse) doit être éprouvée en jeu, près d'un troupeau actif.
* **Bug du cœur signalé, non corrigé ici** : `AcousticRecorder` (`Private/Research/Acoustics.cpp`, calcul de `extra`
  avant `SignalSynth::render`) retranche `20 log10 d` alors que `render` applique maintenant
  `SoundField::spreadingLossDb` (source linéique pour le tonnerre) : dans les **enregistrements** du joueur, le tonnerre
  est trop fort d'environ `20 log10 d − spreadingLossDb(d)` (≈ +31 dB à 1 km). La couche UE (gain des cris synthétisés
  en jeu, `FNoctisSoundExtra::ExtraGainDb`) utilise déjà `spreadingLossDb` ; la même correction d'une ligne est à faire
  dans le cœur par ses mainteneurs.

---

## Ce qui a été vérifié ici

Environnement : Linux, gcc 13, clang 18, 4 cœurs partagés avec d'autres processus, **sans Unreal Engine**.
Aucune compilation UBT, aucun lancement de l'éditeur, aucun test d'automatisation dans Unreal, aucun script Python
dans Unreal, aucun rendu, audio, mesure GPU ou empaquetage n'a pu être fait.

* **Cœur, compilation stricte** : `cmake --build build --target noctis_sim` réussit avec
  `-fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -Werror` (mode `NOCTIS_STANDALONE`). `Platform.h` n'a pas eu
  besoin d'être modifié : sa branche Unreal (`NOCTISCORE_API`, `NOCTIS_ASSERT` → `checkSlow`, `CoreTypes.h` +
  `AssertionMacros.h` seulement) est en place, le pont de journalisation est dans `NoctisCoreModule.cpp`.
* **Cœur compilé comme sous UBT** (`Tools/UECheck/check_ue_layer.py --core`) : les 35 fichiers de
  `NoctisCore/Private` passent avec `NOCTISCORE_API` défini, les macros d'UE (`check`, `verify`, `ensure`, `TEXT`,
  `IN`, `OUT`, `OPTIONAL`, `LIKELY`…) et `-Wshadow-all -Wundef -Werror` : aucune collision de macro, aucun ombrage de
  membre ou de global (équivalents des erreurs C4456–C4459 d'UBT).
* **Couche UE contre une doublure de l'API Unreal** (`check_ue_layer.py`, clang `-fsyntax-only`, C++20, sans
  exceptions ni RTTI, `-Wall -Wextra -Wshadow-all -Wundef -Werror`) : **32/32 unités de compilation** en configuration
  éditeur (modules `Noctis`, `NoctisEditor`, `NoctisCoreModule.cpp`), **27/27** sans éditeur (`WITH_EDITOR=0`, sans
  tests ni dessin de débogage ni Gameplay Debugger). Toute l'utilisation de l'API du cœur par la couche UE est donc
  vérifiée contre les vrais en-têtes ; un test négatif (méthode du cœur mal nommée, argument UE manquant, `FString`
  passé à `UE_LOG`) est bien rejeté. Ce que la doublure ne peut pas prouver : voir `Tools/UECheck/README.md`.
* **Miroir autonome des tests d'automatisation** (`check_ue_layer.py --snapshot`, mêmes vallée réduite 513² / 256² /
  population × 0,25, pas et code d'empreinte que `NoctisSimulationTests.cpp`, planificateurs du cœur au lieu de
  `ParallelFor`), sur les sources actuelles du cœur : **tous les contrôles passent** (≈ 10 s) —
  démarrage (27 espèces chargées, 14 simulées individuellement dans `hell_creek`, 66 individus), 40 pas ;
  série / pool de 2 / pool de 3 threads : **empreintes identiques aux 41 points de contrôle** (30 s de temps réel par
  pas de 0,05 s, puis 10 min accélérées par pas de 2 s ; empreinte finale `595c05a945edbf6b` à t = 630 s), graine 43 ≠
  graine 42 ; 500 instantanés cohérents (`ValidateSnapshot` : ids, espèces, valeurs finies, bornes, pieds, soleil,
  météo ; temps croissant ; aucun son republié) jusqu'à t = 7 960 s ; découpage du runner (un job de 0 pas avec
  commande puis jobs de 1 à 2 pas à 30 i/s, 59 pas) contre un seul job de 59 pas : **même état bit à bit**.
* **Mesures antérieures (passe précédente, même machine)** : vallée complète (2017², 1024², population × 1,
  graine 7, 2 threads) : démarrage 7,2 s ; 254 individus agents ; pas de 50 ms : 0,09 ms en moyenne, p95 0,15 ms,
  max 4,6 ms (pics horaires) avec un seul animal en détail complet près du camp ; temps accéléré : 1,5 ms par pas de
  2 s ; `noctis_bench --quick --threads 2` : ≈ 0,12 ms par animal en détail complet et par pas, 0,0018 ms (Near),
  0,0004 ms (Far) ; tous les animaux forcés en Full : 29 ms par pas en moyenne. Mémoire du monde : +95 Mo.
  Instantané : 17 µs.
* **Données** : `EvidenceDatabase::load` sur `Data/` : 27 espèces, 2 environnements, 393 sources, 3 profils.
* **Contrat de la fabrique de créatures** : les fonctions pures de `import_creatures.py` (recherche des entrées,
  classement des textures, choix des normales, os, chaînes de reciblage) ont été exécutées, avec un module `unreal`
  factice, sur la sortie réelle `Build/Creatures/tyrannosaurus_rex/export` : maillage + 3 LOD, 12 clips, 9 textures
  (`Normal_DX` retenue, `Eye_BaseColor` → emplacement `eye`), 36 os, 8 chaînes de reciblage des métadonnées.
* **Contrat audio** : `Data/Audio/manifest.json` actuel (2 160 sons) contre `import_audio.py` et
  `ANoctisAudioDirector` : 0 fichier manquant dans `Build/Audio`, calibration présente pour tous les sons, les
  19 groupes aléatoires attendus (tonnerre 0,5/2/5/12 km, eau, végétation) existent, les identifiants utilisés par le
  directeur (`amb_thunder_<d>m_0N`, `fol_water_stroke_<classe>_0N`, `fol_vegetation_crash_<classe>_0N`, lits de pluie
  et de vent) existent, et les 42 types de cris des fiches d'espèces ont leur `voc_<espèce>_<cri>_voiceA`.
* **Python** : les scripts de `Content/Python` passent `python3 -m py_compile`. Aucun appel au module `unreal` n'a
  pu être exécuté.
* **`Noctis.uproject`** : JSON valide.

Corrections de cette passe (résumé) : gain des cris synthétisés pour le tonnerre (`spreadingLossDb`), composition
de rotation des imposteurs (quaternions au lieu d'une somme de rotateurs), garde contre un `PlayerController` nul
avant `CreateWidget`, retour par valeur d'un `UFUNCTION`, mouvement racine ignoré et clips de la fabrique dans
`DA_<espèce>` / l'instance d'animation, import des créatures aligné sur le contrat réel de la fabrique (métadonnées,
LOD, ORM, `Normal_DX`, poids SSS en échantillonneur *Linear Grayscale*, emplacements de matériaux, orientation mesurée
sur le squelette), conflits de priorité des variables console entre `DefaultEngine.ini` et `DefaultScalability.ini`,
outil `Tools/UECheck`.
