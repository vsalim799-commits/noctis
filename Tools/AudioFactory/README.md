# AudioFactory — bibliothèque sonore procédurale de Noctis

Générateur **reproductible** de la bibliothèque sonore du jeu (Python : numpy, scipy, soundfile, matplotlib).
Aucun échantillon enregistré n'est utilisé : chaque son est synthétisé à partir des données scientifiques du
dépôt (`Data/Species`, `Data/Environments`, `Data/Equipment`, `Data/Sources`, `Data/Profiles`) et des lois
physiques du cœur de simulation (`Source/NoctisCore`). Le même modèle sert à ce que le joueur entend et à ce
que la faune et les micros du jeu « perçoivent ».

> Règle d'or : **aucune vocalisation de dinosaure non avien, d'oiseau, d'amphibien ou d'insecte n'est connue.**
> Tous ces sons sont des **hypothèses** étiquetées (`hypothesis: true`, légende « hypothèse »), avec leur niveau
> de confiance. Le jeu ne prétend jamais savoir ce qu'un cri « voulait dire » ni à quoi il ressemblait exactement.

## Utilisation

```bash
PY=/home/user/.venvs/noctis-tools/bin/python
$PY Tools/AudioFactory/generate.py                         # toute la bibliothèque (~30 min sur 4 cœurs libres)
$PY Tools/AudioFactory/generate.py --only ambience equipment   # certains modules ; le manifeste est fusionné
$PY Tools/AudioFactory/generate.py --jobs 4 --no-png       # sans spectrogrammes
$PY Tools/AudioFactory/generate.py --prune-only            # supprime les WAV / PNG absents du manifeste (fait aussi après chaque génération)
$PY Tools/AudioFactory/generate.py --only vocal --reuse-png    # changement de métadonnées seul : garde les spectrogrammes existants
$PY Tools/AudioFactory/qa.py                               # mesures + contrôle du manifeste (code retour 1 si échec)
$PY Tools/AudioFactory/qa.py --only vocal --no-core        # une catégorie, sans comparaison au cœur C++
```

Modules : `vocal`, `footsteps`, `foley`, `ambience`, `equipment`.

### Reproductibilité

* Toute variation aléatoire provient de `common.rng(clé…)` : graine SHA-256 de l'identifiant du son et de
  `MASTER_SEED` (20261015). Mêmes versions de numpy/scipy ⇒ mêmes échantillons, bit pour bit.
* Les fichiers audio ne sont **pas** versionnés (`Build/` est dans `.gitignore`) ; seul le manifeste l'est.
* Le générateur ne lit que le dépôt ; une référence absente de `Data/Sources` est retirée automatiquement
  (et ne peut donc pas soutenir un niveau de confiance).

## Sorties

| Chemin | Contenu | Versionné |
|---|---|---|
| `Build/Audio/<module>/<groupe>/<id>.wav` | WAV PCM 48 kHz / 24 bits, mono (sources ponctuelles) ou stéréo (lits d'ambiance, tonnerre, machines) | non |
| `Data/Audio/manifest.json` | une entrée par son : id, catégorie, espèce / source, paramètres, niveau, confiance, sources, indications UE5, légendes FR/EN, bloc `qa` | **oui** |
| `Output/audio/<groupe>/<id>.png` | spectrogramme (log-fréquence, 80 dB de dynamique) + spectre moyen, titre avec niveau de confiance | non |
| `Output/audio/sheets/*.png` | planches-contacts par groupe (vignettes) | non |
| `Output/audio/qa_report.json` | toutes les mesures et tous les contrôles | non |

### Champs principaux du manifeste (`noctis.audio_manifest/1`)

* `id`, `category` (`vocal`, `footstep`, `foley`, `ambience`, `weather`, `equipment`), `subcategory`, `module`,
  `region`, `species`, `equipment`, `variant` (voix, distance, allure, substrat, variation…).
* `confidence` (`established` … `unknown`) + `confidence_label_fr`, `hypothesis`, `disclaimer_fr`,
  `basis_fr` (justification en français), `sources` (identifiants de `Data/Sources`).
* `parameters` : toutes les grandeurs physiques utilisées (f0, contour, masse, nombre de Froude, rayon de contact,
  fréquence de Minnaert, débit, intensité de pluie, distance, absorption…).
* `level` : `source_spl_db_1m` (dB SPL re 20 µPa à 1 m, convention `SoundSource::levelDb` du cœur), ou
  `ambient_spl_db` (niveau au point d'écoute pour les lits), ou `received_spl_db` (tonnerre) ; normalisation
  appliquée et `calibration_spl_db_at_0dbfs_rms` pour recalibrer dans UE5.
* `ue5` : `sound_class`, `attenuation_preset`, `looping`, `concurrency`, `priority`, `virtualization`, `submix`,
  options d'import, `random_group` (groupes de variations), notes. Les préréglages sont définis une seule fois
  en tête du manifeste (`ue5.sound_classes`, `ue5.attenuation_presets` en mètres et en cm, `ue5.concurrency_presets`).
* `caption` : légende FR/EN pour l'accessibilité (suffixe « (hypothèse) » automatique), `preview_png` : spectrogramme
  (équivalent visuel de chaque indice sonore).
* `profile_toggle` / `profile_states` : interrupteur de profil de reconstruction (cris de contact / d'alarme).
* `qa` : valeurs attendues vérifiées par `qa.py`.

## Contenu et modèles

### 1. Vocalisations (hypothèses) — `vocal.py`

* Une entrée par type de cri de `sim.vocal.call_types` de chaque espèce des deux formations.
* La synthèse reproduit `SignalSynth` (`Research/Acoustics.cpp`) : contours de fréquence par contexte, enveloppe
  12 % / 25 %, modulation d'amplitude à 12 Hz pour la menace, gigue de 4 % pour la détresse.
* **Bouche fermée** (quasi sinusoïdal, harmoniques en h^-2,5) uniquement pour les archosaures marqués
  `closed_mouth` et seulement tant que Riede et al. 2016 est présent dans `Data/Sources` (avec Clarke et al. 2016 :
  absence de syrinx connu chez les non-aviens) : SPÉCULATIF. Sinon bouche ouverte (source harmonique + formants
  d'un conduit vocal de longueur isométrique 0,17 (M/70)^(1/3) m, hypothèse de jeu), souffle ou expiration nasale.
* Échelle allométrique du cœur : `pitchShift = sizeRatio^-0,4` (Fletcher 2004), niveau `+5 log10(sizeRatio)`.
* Variation individuelle : 3 voix (marquage 0,15 / 0,50 / 0,85 → ±5 % de hauteur, ±4 % de durée, timbre) +
  un subadulte (10 % de la masse) pour le premier cri de contact des espèces de plus de 20 kg.
* Variantes de distance : 50, 200 et 800 m en terrain découvert (+ 200 m à travers la forêt riveraine à Hell Creek),
  absorption ISO 9613-1 et feuillage ISO 9613-2 du cœur (`propagation.py`, portage ligne à ligne de
  `SoundField.cpp`), réponse normalisée à f0 ; la perte de niveau est dans `parameters.propagation`.

### 2. Pas — `footsteps.py`

* Toutes les espèces à pattes des deux formations + le chercheur (80 kg, bottes) ; 2 allures (marche, allure
  rapide) × substrats × **4 variations**.
* Substrats : Hell Creek — boue, limon, sable, eau peu profonde, litière de feuilles, couvert de fougères ;
  Djadochta — sable, limon, eau peu profonde (mares éphémères ; ni fougères ni litière de feuillus).
* Cinématique identique au cœur (`Locomotion.cpp`) : nombre de Froude, facteur de service, longueur de foulée
  (similitude dynamique, Alexander), temps de contact, force maximale `m g part (1 + 0,6 min(Fr, 2))`,
  niveau `35 + 12 log10 m + 6 √Fr` + correction de substrat.
* Synthèse : contact masse-ressort amorti (fréquence `600 m^-0,25 + 30 Hz` × √raideur relative du substrat,
  amortissement par substrat) + texture physique : micro-impacts de grains (sable), effritement (limon),
  claquement puis succion de la cavité au lever du pied à la fréquence de Minnaert (boue), couronne + nuage de
  bulles + gouttes retombant après 2v/g (eau), ruptures de feuilles (litière), frondes frôlées puis cassées (fougères).

### 3. Foley corporel — `foley.py`

* **Respiration** (repos / effort, 4 variations) — loi d'échelle documentée : fréquence `f = 0,89 M^-0,26 Hz`
  × (0,8 + 1,6 charge), soit 53,5 M^-0,26 /min (Stahl 1967, mammifères ; c'est la loi de `Body.cpp`, gardée pour
  rester synchrone avec l'animation du thorax) ; volume courant `V_T = 7,69 M^1,04 mL` (Stahl 1967 ; exposant
  vérifié, constante non revérifiée — signalé) ; la loi aviaire de Calder 1968 (182 W^-0,33 /min) est donnée en
  alternative dans le manifeste. Bruit d'écoulement turbulent dans une voie nasale de section isométrique,
  intensité ~ A U^6 (calibration de jeu : 25 dB à 1 m pour 70 kg au repos), résonances du conduit. SPÉCULATIF
  pour les non-mammifères (extrapolation), INFÉRÉ pour les mammifères.
* Contact corporel selon le tégument de la fiche espèce (écailles : frottement collé-glissé à l'espacement des
  écailles ; ostéodermes : modes de plaques ; carapace ; plumes ; pelage / pycnofibres ; peau nue).
* Queue dans la végétation (fougères à Hell Creek, broussailles xériques en Djadochta), alimentation selon le
  régime (cueillette, broyage par batterie dentaire, cisaillement, arrachage de chair, os broyés…), boisson
  (puiser-relever pour les archosaures, lapement, aspiration), éclaboussures par classe de masse (entrée, nage,
  sortie : cavité d'air, bulles de Minnaert, gouttes), végétation brisée, gorgée d'air du lépisosté.

### 4. Ambiances — `ambience.py`

* **Boucles de 60 s parfaitement continues**, stéréo, rendues de façon circulaire (STFT périodique, contrôles en
  série de Fourier, queues d'événements repliées) : pas de fondu enchaîné, la dernière valeur continue la première.
* Couches séparées (Hell Creek d'abord, tranche verticale) :
  * vent dans les **conifères** (tons éoliens des aiguilles, f = 0,2 U/d, d = 0,7–3 mm) vs **feuillus** (chocs
    feuille contre feuille, taux ~U³) à 1,5 / 3 / 8 m/s ; niveau = `windNoiseDb` du cœur ;
  * eau courante par **débit** : ruisseau 1,5 m³/s, étiage 18, moyen 60, crue 300 m³/s (grondement, grappes de bulles
    de Minnaert, clapotis, débris en crue) ; niveau = `WaterSystem::noiseLevelDb` ;
  * pluie **faible (2 mm/h) et forte (25 mm/h)** en forêt et sur l'eau : distribution de Marshall & Palmer, impacts,
    bulles de type I (~14 kHz) et II (Prosperetti & Oguz) ; niveau = `rainNoiseDb` ;
  * biophonie (HYPOTHÈSES) : chœur nocturne d'insectes stridulants à ton pur (~6,4 kHz, reconstitution jurassique de
    Gu et al. 2012), passages d'insectes volants (Doppler), chœur d'anoures, oiseaux du Maastrichtien (paramètres
    `sim.vocal` de `hell_creek_avifauna`, syrinx attesté chez Vegavis, Clarke et al. 2016) ;
  * Djadochta : vent sur les dunes calme / moyen / fort avec **saltation du sable** au-delà de 6,5 m/s, insectes épars.
* **Mixages pré-calculés jour / nuit** (berge de rivière, marais à conifères, dunes), chaque couche à son niveau
  ambiant ; pour chaque mixage contenant de la biophonie hypothétique, une version « géophonie seule » (`_abiotic`),
  ou, si elle se réduit à une seule couche, un renvoi vers cette couche (`parameters.abiotic_equivalent`).
* Mastering : limiteur de crête circulaire et transparent (seuil 17 dB au-dessus du niveau RMS médian des trames de
  50 ms pour la géophonie, 22 dB pour la biophonie), car les niveaux physiques d'événements proches (goutte à 40 cm,
  grenouille à 3 m) donnent des facteurs de crête de 30–40 dB ; la réduction appliquée est dans
  `parameters…mastering_limiter`.
* **Tonnerre** à 0,5 / 2 / 5 / 12 km (4 variations chacun) : canal de foudre tortueux et ramifié, onde en N par
  segment, allongement r^1/4, absorption ISO par tranche de distance, diffusion par la vallée. Le silence initial
  (`delay_after_flash_s`) n'est pas dans le fichier : UE5 doit déclencher le son avec ce retard après l'éclair.

### 5. Matériel — `equipment.py`

* Véhicule 6×6 hybride : traction électrique (arrêt / 3 / 8 / 15 m/s : raies électriques et d'engrènement, MLI de
  l'onduleur, bruit de roulement, sculptures, chocs de suspension), **groupe électrogène** (80 dB à 1 m contre 52 dB
  en électrique : perturbation acoustique simulée), variantes 50 / 200 / 800 m, démarrage / arrêt, portes.
* Drone : **fréquence de passage des pales 160 Hz** (equipment.json) en vol stationnaire, croisière et montée
  (BPF ∝ puissance^1/3, niveau +10 log10 P/P0), battements entre rotors, variantes 50 / 200 / 800 m, décollage, atterrissage.
* Obturateur (vue unique, rafale 10 i/s), enregistreur (bouton, manipulation), radio du camp de base (alternat,
  queue de squelch, bip, souffle et parasites d'orage).
* Ce matériel est fictif ; son niveau de confiance (SPÉCULATIF) ne porte pas sur une question paléontologique.

## Contenu généré (version 1.1.0 du générateur)

| Catégorie | Sons | Détail |
|---|---:|---|
| `vocal` | 267 | tous les types de cris des 27 fiches espèces : 3 voix + subadulte + variantes 50 / 200 / 800 m (et 200 m en forêt à Hell Creek) |
| `footstep` | 1 056 | 24 espèces à pattes + chercheur × substrats × 2 allures × 4 variations |
| `foley` | 652 | respiration, contact corporel, queue, alimentation, boisson (24 espèces), gorgée d'air du lépisosté + 60 éclaboussures / végétation |
| `ambience` | 27 | 18 couches de 60 s + 6 mixages jour / nuit + 3 versions « géophonie seule » |
| `weather` | 20 | 4 couches de pluie de 60 s + 16 tonnerres (4 distances × 4 variations) |
| `equipment` | 68 | véhicule 24, drone 14, obturateur 8, enregistreur 8, radio 14 |
| **Total** | **2 090** | 5 038 s d'audio (~84 min), ~1 Go de WAV, 47 boucles ; 833 sons INFÉRÉS, 1 257 SPÉCULATIFS ; 277 hypothèses étiquetées |

Dernier passage de `qa.py` : 2 090 / 2 090 sons conformes ; aucun fichier orphelin ; absorption conforme à la
table 2 de l'ISO 9613-2 ; portage Python identique au cœur C++ (écart relatif max. 1·10⁻⁷) ; 411 bandes de
fonctions de transfert de distance vérifiées (écart max. 0,01 dB) ; f0 mesurée par YIN sur 241 cris tonals.

## Niveaux, normalisation et calibration

Les one-shots sont normalisés en crête à −1 dBFS, les boucles en RMS (−20 dBFS, plafond de crête −1 dBFS).
Le niveau physique n'est donc pas « dans » le fichier mais dans `level` : dans UE5, régler le volume d'un son pour
qu'un RMS de 0 dBFS corresponde à `calibration_spl_db_at_0dbfs_rms`, puis laisser l'atténuation par la distance
agir (ou utiliser les variantes de distance précalculées, dont les préréglages `*_DistanceBaked` désactivent le
filtre passe-bas d'UE5 pour ne pas compter deux fois l'absorption).

## Import dans Unreal Engine 5.4

* Créer les Sound Classes, Sound Attenuation et Sound Concurrency nommées dans `manifest.ue5` (hiérarchie
  `SC_Noctis_Master` → …). Les vocalisations hypothétiques ont leur propre classe et sous-mix
  (`SC_Creature_Vocal_Hypothesis`, `SM_Creatures_Hypotheses`) pour pouvoir être coupées ou signalées selon le profil.
* Les entrées partageant `ue5.random_group` forment un nœud Random (≥ 4 variations, vérifié par la QA).
* Boucles : `looping: true`, virtualisation `PlayWhenSilent`, chargement à la demande.
* Accessibilité : afficher `caption.fr`/`caption.en` (avec « hypothèse » pour la biophonie) et, sur demande,
  le spectrogramme `preview_png` — équivalent visuel de chaque indice acoustique.

## Contrôle qualité — `qa.py`

Pour chaque son : format (48 kHz, PCM 24 bits, canaux, nombre d'échantillons), durée, crête, RMS, RMS actif
(cohérence avec le manifeste à 0,05 dB), centroïde spectral, pic spectral, rapport d'énergie de bande, centroïde
de la bande basse (pas), **f0 par YIN** pour les cris tonals (pic spectral de la porteuse pour les grondements
modulés en amplitude), durée active, **continuité des boucles** (saut et énergie haute fréquence à la jointure
comparés au reste du fichier), **fonction de transfert des variantes de distance** (rapports de bandes mesurés vs
prédiction ISO 9613-1 + feuillage), sources connues, indications UE5, présence du spectrogramme.
Au niveau de la bibliothèque : fichiers orphelins / manquants, ≥ 4 variations par groupe aléatoire, absorption
comparée à la **table 2 de l'ISO 9613-2** et au **cœur C++ compilé** (`validation/soundfield_probe.cpp` lié à
`build/libnoctis_core.a` ; écart relatif < 2·10⁻⁴, le C++ calcule en float32).

## Limites connues

* Aucune écoute humaine n'a été faite pendant la génération : la validation repose uniquement sur des mesures
  et sur l'inspection des spectrogrammes. Une passe d'écoute par un·e sound designer reste nécessaire.
* Les vocalisations sont des hypothèses par construction ; les répertoires (`sim.vocal`) sont eux-mêmes SPÉCULATIFS.
* La respiration utilise la loi mammalienne du cœur pour toutes les espèces (probablement trop rapide pour les
  ectothermes et les grands archosaures).
* Les niveaux de foley et de biophonie, les rythmes, les densités de chœurs et les constantes mécaniques
  (rapports de réduction, nombre de pales, pas de sculpture…) sont des hypothèses de jeu, documentées dans
  `parameters`.
* Propagation : divergence sphérique + absorption + feuillage ; le vent, le relief, le sol (effet de sol) et la
  réfraction ne sont pas dans les fichiers (le cœur applique vent et relief au niveau, pas au timbre).
* Ambiances : un seul lit de 60 s par condition ; les grenouilles n'ont pas de fiche espèce (présence via
  l'assemblage de lissamphibiens, référence non vérifiée) ; aucune couche nocturne spécifique pour la Djadochta
  au-delà d'insectes épars ; Djadochta sans pluie ni tonnerre spécifiques (le tonnerre est indépendant de la région).
* « Information insuffisante — recherche nécessaire. » pour : cris réels de tout dinosaure non avien, appels des
  anoures et des insectes du Maastrichtien, végétation de la Djadochta.

## Références utilisées

Toutes proviennent de `Data/Sources` (listées avec leur statut `verified` dans `manifest.sources_used`) :
Riede et al. 2016, Clarke et al. 2016 (vocalisation à bouche fermée, syrinx) ; Fletcher 2004 (échelle de
fréquence) ; ISO 9613-1:1993, ISO 9613-2:1996 (propagation) ; Alexander 1976, 1983 (vitesses, similitude
dynamique) ; Minnaert 1933 (bulles) ; Prosperetti & Oguz 1993 (bruit de pluie) ; Marshall & Palmer 1948
(gouttes) ; Stahl 1967, Calder 1968 (respiration) ; Gu et al. 2012 (stridulation d'un katydide jurassique) ;
Gardner & DeMar 2013 (lissamphibiens, non vérifiée) ; fiches espèces et environnements (flore, hydrologie,
précipitations, faune). Aucune nouvelle référence n'a été ajoutée par le module d'ambiance ni par le module matériel.
