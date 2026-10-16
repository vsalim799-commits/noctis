# UECheck — vérifications de la couche Unreal sans moteur

Outil de contrôle pour `Source/Noctis`, `Source/NoctisEditor` et `NoctisCore` vu par Unreal Build Tool, utilisable
là où aucun Unreal Engine n'est installé (machine de développement du cœur, intégration continue Linux).

```
python3 Tools/UECheck/check_ue_layer.py              # couche UE, configuration éditeur (WITH_EDITOR=1, tests, debug)
python3 Tools/UECheck/check_ue_layer.py --shipping   # couche UE sans éditeur ni tests (WITH_EDITOR=0, ...)
python3 Tools/UECheck/check_ue_layer.py --core       # le cœur tel que UBT le compile (NoPCH, macros UE)
python3 Tools/UECheck/check_ue_layer.py --snapshot   # miroir autonome des tests d'automatisation Noctis.Simulation.*
python3 Tools/UECheck/check_ue_layer.py --all -j 4   # tout
python3 Tools/UECheck/check_ue_layer.py Runner       # seulement les fichiers dont le chemin contient « Runner »
```

Prérequis : `clang++` ≥ 14 (C++20) ; pour `--snapshot`, une compilation CMake du cœur (`build/libnoctis_core.a`) et
`g++`. Les fichiers générés vont dans `build/uecheck/` (ignoré par git).

## Ce que ça vérifie

1. **Couche UE (`clang++ -fsyntax-only`)** : chaque `.cpp` des modules `Noctis` et `NoctisEditor` (et
   `NoctisCoreModule.cpp`) est compilé en C++20, `-fno-exceptions -fno-rtti -Wall -Wextra -Wshadow-all -Wundef
   -Werror` (équivalents clang des erreurs d'ombrage C4456–C4459 et d'identifiants non définis d'UBT), contre les
   **vrais en-têtes de NoctisCore** et une **doublure de l'API Unreal** (`stub/`, signatures UE 5.4/5.5).
   UHT est émulé : `GENERATED_BODY()` est réécrit en `Super`/`ThisClass`/`StaticClass()`, les `*.generated.h` sont
   vides. Ce contrôle attrape :
   * toute dérive entre la couche UE et l'API du cœur (méthode renommée, champ supprimé, type changé) ;
   * les erreurs C++ ordinaires du code UE (types, constance, captures, surcharges, ombrage) ;
   * `UE_LOG` / `FString::Printf` recevant un `FString` ou un `std::string` au lieu d'un `TCHAR*` (assertion statique) ;
   * les appels qui ne correspondent pas aux signatures UE telles que la doublure les déclare.
2. **Cœur en mode UBT (`--core`)** : chaque `.cpp` de `NoctisCore/Private` est compilé avec `NOCTISCORE_API` défini
   (branche Unreal de `Platform.h`) et une version légère de `CoreTypes.h` / `AssertionMacros.h` (`coreinc/`) qui
   définit les macros d'UE susceptibles d'entrer en collision : `check`, `verify`, `ensure`, `checkSlow`, `TEXT`,
   `IN`, `OUT`, `OPTIONAL`, `LIKELY`, `FORCEINLINE`… (`NoctisCore.Build.cs` compile sans PCH : c'est tout ce qu'une
   unité du cœur voit d'Unreal).
3. **Miroir des tests d'automatisation (`--snapshot`)** : `snapshot_checks_main.cpp` rejoue `Noctis.Simulation.Boot`,
   `.Determinism`, `.SnapshotConsistency` et `.Runner` avec le même code d'empreinte et de validation
   (`Source/Noctis/Private/Sim/NoctisSnapshotChecks.h`, sans dépendance Unreal), les mêmes tailles de vallée et de
   pas, mais les planificateurs du cœur (série, pools de 2 et 3 threads) à la place de `ParallelFor`.

## Ce que ça ne vérifie PAS

* **La compilation avec le vrai moteur.** La doublure est écrite d'après la documentation d'UE 5.4/5.5 : une
  hypothèse fausse sur une signature Unreal est fausse aux deux endroits. Seul UBT tranche (lignes `VERIFY`).
* UHT (spécificateurs `UPROPERTY`/`UFUNCTION`, types autorisés en Blueprint, `meta`), l'édition de liens, les
  modules et plugins, le comportement à l'exécution, les scripts Python (`Content/Python`), les assets.
* MSVC : avertissements propres à MSVC, sémantique `dllexport` (instanciation de tous les membres des classes
  `NOCTISCORE_API`), options flottantes.

## Mettre la doublure à jour

Quand le code UE utilise une nouvelle API, ajouter sa déclaration dans `stub/ue_*.h` **en recopiant la signature du
moteur** (en-tête Unreal ou documentation), jamais en l'adaptant au code appelant. Les corps sont factices
(`(void)` sur les paramètres, valeurs par défaut) : rien n'est exécuté.
