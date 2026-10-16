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
$PY Tools/AudioFactory/generate.py                         # toute la bibliothèque (~35 min sur 3 cœurs libres)
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
| `Build/Audio/<module>/<groupe>/<id>.wav` | WAV PCM 48 kHz / 24 bits, mono (sources ponctuelles, machines) ou stéréo (lits d'ambiance, tonnerre) | non |
| `Data/Audio/manifest.json` | une entrée par son : id, catégorie, espèce / source, paramètres, niveau, confiance, sources, indications UE5, légendes FR/EN, bloc `qa` | **oui** |
| `Output/audio/<groupe>/<id>.png` | spectrogramme (log-fréquence, 80 dB de dynamique) + spectre moyen, titre avec niveau de confiance | non |
| `Output/audio/sheets/*.png` | planches-contacts par groupe (vignettes) | non |
| `Output/audio/qa_report.json` | toutes les mesures et tous les contrôles | non |

### Champs principaux du manifeste (`noctis.audio_manifest/1`)

* `id`, `category` (`vocal`, `footstep`, `foley`, `ambience`, `weather`, `equipment`), `subcategory`, `module`,
  `region`, `species`, `equipment`, `variant` (voix, distance, allure, substrat, variation…).
* `confidence` (`established` … `unknown`) + `confidence_label_fr` : existence de l'événement et grandeurs documentées
  (masse, cinématique, débit, intensité de pluie…) ; `model_confidence` (+ `model_basis_fr`) : modèle de synthèse
  (timbre, loi de niveau), le plus souvent SPÉCULATIF car hypothèse de jeu ; `overall_confidence` = min des deux
  (à afficher quand une seule valeur est montrée). `hypothesis`, `disclaimer_fr`, `basis_fr` (justification en
  français), `sources` (identifiants canoniques de `Data/Sources` : les doublons marqués `alias_of` sont résolus).
* `parameters` : toutes les grandeurs physiques utilisées (f0, contour, masse, nombre de Froude, rayon de contact,
  fréquence de Minnaert, débit, intensité de pluie, distance, absorption…).
* `level` : `source_spl_db_1m` (dB SPL re 20 µPa à 1 m, convention `SoundSource::levelDb` du cœur), ou
  `ambient_spl_db` (niveau au point d'écoute pour les lits), ou `received_spl_db` (tonnerre **et toutes les variantes
  de distance** : la calibration se fait sur le niveau reçu, pas sur la source à 1 m) ; normalisation appliquée et
  `calibration_spl_db_at_0dbfs_rms` pour recalibrer dans UE5.
* `ue5` : `sound_class`, `attenuation_preset`, `looping`, `concurrency`, `priority`, `virtualization`, `submix`,
  options d'import, `random_group` (groupes de variations), `crossfade_with`, `accessibility`, notes. Les préréglages
  sont définis une seule fois en tête du manifeste (`ue5.sound_classes`, `ue5.attenuation_presets` en mètres et en cm
  — `attenuate: false` pour les sons 2D et les variantes de distance, qui ne sont que spatialisées —,
  `ue5.concurrency_presets`, `ue5.aliases` : sons volontairement non rendus car identiques à un autre fichier).
* `caption` : légende FR/EN pour l'accessibilité, **présente sur chaque son** (« (hypothèse) » ajouté une seule fois
  si la légende ne le dit pas déjà ; les cris : « Cri hypothétique d'… (contexte supposé : …) »), `preview_png` :
  spectrogramme (équivalent visuel de chaque indice sonore).
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
* Variation individuelle : 4 voix (marquage 0,15 / 0,50 / 0,85 et, depuis 1.2, 0,35 → ±5 % de hauteur, ±4 % de durée,
  timbre ; les voix A–C sont identiques bit à bit à la version 1.1) formant un `random_group` par type de cri, + un
  subadulte (10 % de la masse) pour le premier cri de contact des espèces de plus de 20 kg.
* Libellés et légendes : le cri est toujours présenté comme une hypothèse avec son **contexte supposé**
  (« Tyrannosaurus rex — cri hypothétique n° 2 (bouche fermée ; contexte supposé : menace) — voix A ») ; l'identifiant
  de type (`parameters.call_type`, p. ex. `threat_rumble`, `alarm_bellow`) n'est qu'une clé de données et n'est jamais
  affiché, car il nomme une fonction que le jeu ne prétend pas connaître.
* Infrasons : `parameters.infrasonic_energy_fraction` (énergie sous 20 Hz) et `below_30hz_energy_fraction` ; au-delà
  de 25 % d'énergie sous 30 Hz (grondements des plus grands animaux : ~79 % sous 20 Hz pour le grondement de menace de
  T. rex, voix A) `ue5.accessibility.visual_cue_required` impose la légende (« très grave, en partie infrasonore ») et le
  spectrogramme, variantes de distance comprises ; une éventuelle couche « proxy audible » est à trancher lors de la
  passe d'écoute.
* Variantes de distance : 50, 200 et 800 m en terrain découvert (+ 200 m à travers la forêt riveraine à Hell Creek),
  absorption ISO 9613-1 et feuillage ISO 9613-2 du cœur (`propagation.py`, portage ligne à ligne de
  `SoundField.cpp`), réponse normalisée à f0 ; la perte de niveau est dans `parameters.propagation` et dans
  `level.received_spl_db`, sur lequel le fichier est calibré (préréglage `ATT_Vocal_DistanceBaked` : spatialisation seule).

### 2. Pas — `footsteps.py`

* Toutes les espèces à pattes des deux formations + le chercheur (80 kg, bottes) ; 2 allures (marche, allure
  rapide) × substrats × **4 variations**.
* Substrats : Hell Creek — boue, limon, sable, eau peu profonde, litière de feuilles, couvert de fougères ;
  Djadochta — sable, limon, eau peu profonde (mares éphémères ; ni fougères ni litière de feuillus).
* Cinématique identique au cœur (`Locomotion.cpp`) : nombre de Froude, facteur de service, longueur de foulée
  (similitude dynamique, Alexander), temps de contact, force maximale `m g part (1 + 0,6 min(Fr, 2))`,
  niveau `35 + 12 log10 m + 6 √min(Fr, 2)` + correction de substrat (le terme de Froude est borné comme la vitesse
  d'impact de la synthèse, qui sature à Fr ≥ 1 ; sans borne, un Kryptobaatar de 40 g gagnait 24 dB à la course).
  **À reporter dans `Locomotion.cpp` (`footfallSound`)** par l'agent qui travaille sur `Creatures`.
* Synthèse : contact masse-ressort amorti (fréquence `600 m^-0,25 + 30 Hz` × √raideur relative du substrat,
  amortissement par substrat) + texture physique : micro-impacts de grains (sable), effritement (limon),
  claquement puis succion de la cavité au lever du pied à la fréquence de Minnaert (boue), couronne + nuage de
  bulles + gouttes retombant après 2v/g (eau), ruptures de feuilles (litière), frondes frôlées puis cassées (fougères).
* Les trains de micro-impacts (grains, effritement, ruptures de feuilles) suivent une **relaxation exponentielle**
  (taux et taille des événements ∝ exp(−t/τc)) : la texture s'éteint au lieu de s'arrêter net ; les gouttes retombent
  selon une loi gamma (dispersion des vitesses d'éjection). Tampons rendus longs, coupés à −60 dB du maximum.
* Confiance : `confidence` = cinématique et existence (≤ INFÉRÉ) ; `model_confidence` = SPÉCULATIF (loi de niveau,
  fréquence de contact, raideurs et textures sont des hypothèses de jeu).

### 3. Foley corporel — `foley.py`

* **Respiration** (repos / effort, 4 variations) — loi d'échelle documentée : fréquence `f = 0,89 M^-0,26 Hz`
  × (0,8 + 1,6 charge), soit 53,5 M^-0,26 /min (Stahl 1967, mammifères ; c'est la loi de `Body.cpp`), sauf pour
  l'oiseau (`bird_flyer`) qui suit la loi aviaire de Calder 1968 (182 W^-0,33 /min, W en g : ~23 /min au lieu de 61 pour
  0,5 kg) — **`Body.cpp` doit appliquer la même règle** pour rester synchrone avec le thorax animé ; volume courant
  `V_T = 7,69 M^1,04 mL` (Stahl 1967 ; exposant vérifié, constante non revérifiée — signalé). Les fichiers dont le
  niveau à 1 m serait sous 0 dB SPL (inaudibles) ne sont pas produits. Bruit d'écoulement turbulent dans une voie nasale de section isométrique,
  intensité ~ A U^6 (calibration de jeu : 25 dB à 1 m pour 70 kg au repos), résonances du conduit. SPÉCULATIF
  pour les non-mammifères (extrapolation), INFÉRÉ pour les mammifères.
* Contact corporel selon le tégument de la fiche espèce (écailles : frottement collé-glissé à l'espacement des
  écailles ; ostéodermes : modes de plaques ; carapace ; plumes ; pelage / pycnofibres ; peau nue).
* Queue dans la végétation (fougères à Hell Creek, broussailles xériques en Djadochta), alimentation selon le
  régime (cueillette, broyage par batterie dentaire, cisaillement, arrachage de chair, os broyés…), boisson
  (puiser-relever pour les archosaures, lapement, aspiration), éclaboussures par classe de masse (entrée, nage,
  sortie : cavité d'air, bulles de Minnaert, gouttes), végétation brisée, gorgée d'air du lépisosté.
* **Vol** (oiseau, azhdarchidé) : envol et battements d'ailes (4 variations) — poussée d'air à chaque abattée, froissement
  des plumes ou claquement de la membrane alaire, fréquence de battement par similitude isométrique `f ∝ m^-1/6`
  (hypothèse de jeu) ; SPÉCULATIF pour le ptérosaure (acoustique de l'aile inconnue).
* **Chercheur** (joueur, `SC_Player_Foley`) : respiration au repos / essoufflée (même modèle que les animaux, 80 kg),
  chute avec souffle coupé (blessure), sac à dos, vêtements, **page du carnet** et **crayon** (le carnet est le seul
  dispositif d'apprentissage : ses sons existent), 4 variations chacun.

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
  * **pluie entendue depuis le véhicule-refuge** (faible / forte) : gouttes sur le toit (modes de panneau), lit de
    tambourinement, pluie extérieure assourdie (`rainNoiseDb` − 6 dB, hypothèse de jeu) ;
  * **feu** (rare, `FireIgnition` / `VegetationSystem::updateFire`) : feu couvant (crépitements épars, vapeur) et front
    de feu (crépitements denses, éclats de résine, grondement de combustion ~150 Hz, ruptures de branches) — physique
    INFÉRÉE (Bamforth et al. 2014 pour l'existence des feux), modèle de jeu SPÉCULATIF ;
  * anoures isolés **de jour** (conditions diurnes) pour le mixage de jour du marais ;
  * Djadochta : vent sur les dunes calme / moyen / fort avec **saltation du sable** au-delà de 6,5 m/s, insectes épars.
* **Mixages pré-calculés jour / nuit** (berge de rivière, marais à conifères, nuit sur les dunes), chaque couche à son
  niveau ambiant ; pour chaque mixage contenant de la biophonie hypothétique, une version « géophonie seule »
  (`_abiotic`), ou, si elle se réduit à une seule couche, un renvoi vers cette couche (`parameters.abiotic_equivalent`).
  Un mixage d'une seule couche (jour sur les dunes) n'est pas rendu : `ue5.aliases` renvoie à `amb_dj_wind_moderate`.
* Mastering : les boucles sont **normalisées en crête à −1 dBFS** et leur RMS tombe où il tombe (le niveau absolu est
  porté par `calibration_spl_db_at_0dbfs_rms`, le 24 bits laisse toute la marge). Seules les vraies valeurs aberrantes
  sont limitées : seuil à 30 dB au-dessus du RMS médian des trames de 50 ms, jamais plus de 6 dB de réduction ni plus de
  1 % des échantillons touchés (sinon le seuil remonte) ; les transitoires des gouttes, les rafales en U³ et les chocs de
  feuilles gardent leur dynamique (le limiteur de crête à 17 dB de la version 1.1, qui réduisait de 15–27 dB 30–69 % des
  échantillons, a été retiré). Statistiques dans `parameters…mastering_limiter`, vérifiées par la QA.
* **Tonnerre** à 0,5 / 2 / 5 / 12 km (4 variations chacun) : canal de foudre tortueux et ramifié, onde en N par
  segment, allongement r^1/4, absorption ISO par tranche de distance, diffusion par la vallée. **Niveau reçu** = somme
  énergétique des segments du canal rendu (source linéique : ~−3 dB par doublement tant que la distance est inférieure à
  la longueur du canal), rapportée au niveau linéique du cœur (111 dB à 1 m de 1 m de canal ; `SoundField::spreadingLossDb`,
  canal vertical de 4 km) : ~86 dB à 500 m, ~78 dB à 2 km, ~72 dB à 5 km, ~64 dB à 12 km. Calibration absolue :
  « Information insuffisante — recherche nécessaire. » (aucune mesure de tonnerre dans `Data/Sources`) ; contrainte de
  cohérence interne vérifiée par la QA : tonnerre à 2 km ou moins plus fort que la pluie forte (70 dB). Le fichier commence
  80 ms avant la première arrivée : le déclencher `ue5.schedule_delay_after_flash_s` (= `file_starts_s_after_flash`)
  après l'éclair ; il doit être spatialisé par le directeur audio au point d'impact (la direction de l'éclair).

### 5. Matériel — `equipment.py`

* Véhicule 6×6 hybride : traction électrique (arrêt / 3 / 8 / 15 m/s : raies électriques et d'engrènement, MLI de
  l'onduleur) + **bruit de roulement pneus / terrain séparé** `85 + 35 log10(v / 14 m/s)` dB à 1 m (sculptures, chocs de
  suspension ; ordre de grandeur des mesures au passage des véhicules routiers, hypothèse de conception, aucune source
  dans `Data/Sources`), somme énergétique : 52 dB à l'arrêt, ~63 dB à 3 m/s, ~77 dB à 8 m/s, ~86 dB à 15 m/s ;
  **groupe électrogène** (80 dB à 1 m : 28 dB de plus que l'électrique à l'arrêt, perturbation acoustique simulée),
  variantes 50 / 200 / 800 m, démarrage / arrêt, portes. Même loi dans `FieldGear.cpp` (`VehicleSystem::noiseAt1m`) et
  `equipment.json`.
* Machines rendues en **mono** (sources ponctuelles spatialisées par UE5 ; l'ancienne « stéréo » par copie retardée de
  0,7 ms était un filtre en peigne). Concurrence : `CON_Vehicle_Drive` (2, fondu entre vitesses), `CON_Vehicle_Generator`
  (2, passage démarrage → régime), `CON_Vehicle_Foley` (portes), `CON_Drone_Rotor` (2) : une porte n'arrête plus le
  moteur, groupe et traction jouent ensemble.
* Drone : **fréquence de passage des pales 160 Hz** (equipment.json) en vol stationnaire, croisière et montée
  (BPF ∝ puissance^1/3, niveau +10 log10 P/P0), battements entre rotors, variantes 50 / 200 / 800 m, décollage, atterrissage.
* Obturateur (vue unique, rafale 10 i/s), enregistreur (bouton, manipulation), radio du camp de base (alternat,
  queue de squelch, bip, souffle et parasites d'orage).
* Ce matériel est fictif ; son niveau de confiance (SPÉCULATIF) ne porte pas sur une question paléontologique.

## Contenu généré (version 1.2.0 du générateur)

| Catégorie | Sons | Détail |
|---|---:|---|
| `vocal` | 309 | tous les types de cris des 27 fiches espèces : 4 voix (groupe aléatoire) + subadulte + variantes 50 / 200 / 800 m (et 200 m en forêt à Hell Creek) ; les 267 sons de 1.1 sont inchangés bit à bit, 42 voix D ajoutées |
| `footstep` | 1 056 | 24 espèces à pattes + chercheur × substrats × 2 allures × 4 variations |
| `foley` | 676 | respiration (172 : la respiration au repos de 5 petites espèces, sous 0 dB SPL, n'est pas produite), contact corporel, queue, alimentation, boisson, vol (oiseau, azhdarchidé : 16), gorgée d'air du lépisosté, 60 éclaboussures / végétation, chercheur (28 : respiration, chute, sac, vêtements, carnet, crayon) |
| `ambience` | 29 | 21 couches de 60 s (dont feu couvant / front de feu et anoures de jour) + 5 mixages jour / nuit + 3 versions « géophonie seule » ; le mixage jour des dunes est un alias |
| `weather` | 22 | 4 couches de pluie + 2 pluies entendues depuis le véhicule (60 s) + 16 tonnerres (4 distances × 4 variations) |
| `equipment` | 68 | véhicule 24, drone 14, obturateur 8, enregistreur 8, radio 14 (machines en mono) |
| **Total** | **2 160** | 5 391 s d'audio (~90 min), ~1,1 Go de WAV, 51 boucles ; `confidence` : 840 INFÉRÉS, 1 320 SPÉCULATIFS ; `model_confidence` : 2 160 SPÉCULATIFS ; 320 hypothèses étiquetées |

Dernier passage de `qa.py` (2026-10-16, générateur 1.2.0) : 2 160 / 2 160 sons conformes (33 359 contrôles) et les 12
contrôles de bibliothèque au vert ; aucun fichier orphelin ; absorption conforme à la table 2 de l'ISO 9613-2 ; portage
Python identique au cœur C++ (écart relatif max. 1·10⁻⁷) ; 411 bandes de fonctions de transfert de distance vérifiées
(écart max. 0,01 dB) ; f0 mesurée sur 278 cris tonals (YIN sur 254, pic spectral de la porteuse sur les 24 grondements
modulés) ; limiteur des boucles : réduction max. 5,9 dB sur < 0,9 % des échantillons (15 boucles sur 35 touchées) ;
tonnerre : niveau géométrique à ± 2,4 dB du modèle linéique du cœur (écart moyen 0,0 dB) ; queue des one-shots ≤ −47,7 dB.

## Niveaux, normalisation et calibration

Les one-shots et les boucles d'ambiance sont normalisés en crête à −1 dBFS, les boucles de machines en RMS (−18 à
−24 dBFS, plafond de crête −1 dBFS). Le niveau physique n'est donc pas « dans » le fichier mais dans `level` : dans UE5,
régler le volume d'un son pour qu'un RMS de 0 dBFS corresponde à `calibration_spl_db_at_0dbfs_rms` (`import_audio.py`),
puis laisser l'atténuation par la distance agir — ou, pour les variantes de distance et le tonnerre, calibrés sur leur
niveau reçu, ne faire que spatialiser (préréglages `*_DistanceBaked` : `attenuate: false`).

Garde de bande (tout sauf les vocalisations, inchangées depuis 1.1) : passe-haut 5 Hz, passe-bas à phase nulle 18–20 kHz
(plus d'énergie inaudible au-dessus de 20 kHz). Les one-shots sont rendus dans des tampons assez longs pour les queues
de leurs événements puis coupés quand l'enveloppe reste sous −60 dB du maximum ; seules les transitions vers une
boucle (démarrage du groupe électrogène, décollage du drone : `qa.transition`) finissent volontairement à plein niveau.

## Import dans Unreal Engine 5.4

* Créer les Sound Classes, Sound Attenuation et Sound Concurrency nommées dans `manifest.ue5` (hiérarchie
  `SC_Noctis_Master` → …). Les vocalisations hypothétiques ont leur propre classe et sous-mix
  (`SC_Creature_Vocal_Hypothesis`, `SM_Creatures_Hypotheses`) pour pouvoir être coupées ou signalées selon le profil.
* Les entrées partageant `ue5.random_group` forment un nœud Random (≥ 4 variations, vérifié par la QA) ;
  `import_audio.py` construit les cues `RC_<groupe>` des bibliothèques d'événements (tonnerre par distance,
  éclaboussures et végétation brisée par masse) et remplit avec elles les cues des réglages (`SC_Thunder`, `SC_Splash`,
  `SC_VegetationCrash`).
* Directeur audio : il choisit la classe la plus proche de l'événement et n'applique que l'écart entre le niveau prédit
  par le cœur et le niveau de référence de l'asset ; les lits de pluie et de vent sont corrigés par
  `coreDb − level.ambient_spl_db` (`RainLoopAmbientDb`, `WindLoopAmbientDb`), pas par une référence fixe.
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

Contrôles ajoutés en 1.2 (chacun correspond à un défaut trouvé en revue) : légende FR/EN présente partout et contenant
« hypoth » (une seule fois) pour les hypothèses ; `model_confidence` présent ; limiteur des boucles (réduction max < 6 dB,
< 1 % des échantillons) ; queue avant le fondu final < −40 dB du maximum pour les one-shots hors transitions ; absence de
coupure nette des textures granulaires denses (sable, limon : enveloppe 2–12 kHz moyennée sur les 4 variations, chute
< 12 dB) ; composante continue < −80 dBFS ; énergie au-dessus de 20 kHz < −30 dB (hors vocalisations : les harmoniques des cris aigus de petits mammifères, ~10,5 kHz pour la détresse de Kryptobaatar, font partie du modèle analysé par l'enregistreur du jeu) ; variantes de distance calibrées sur
le niveau reçu ; tonnerre : niveau géométrique vs modèle linéique du cœur (± 4 dB) et tonnerre ≤ 2 km > pluie forte ;
préréglages d'atténuation importables (`attenuate: false` ou tous les champs numériques) ; aucun son à fondu enchaîné
dans un groupe de concurrence à une seule place, et cibles `crossfade_with` qui désignent bien des sons du manifeste
(les lits de pluie et de vent citaient des clés de couche sans le préfixe `amb_`, ce qui rendait le contrôle précédent
inopérant) ; alias résolus ; toutes les voix de cris dans un groupe aléatoire ; libellés des cris sans identifiant de
type (qui nomme une fonction) et avec « contexte supposé ».

## Limites connues

* Aucune écoute humaine n'a été faite pendant la génération : la validation repose uniquement sur des mesures
  et sur l'inspection des spectrogrammes. Une passe d'écoute par un·e sound designer reste nécessaire.
* Les vocalisations sont des hypothèses par construction ; les répertoires (`sim.vocal`) sont eux-mêmes SPÉCULATIFS.
* La respiration utilise la loi mammalienne du cœur pour toutes les espèces sauf l'oiseau (probablement trop rapide
  pour les ectothermes et les grands archosaures).
* **Changements du cœur à reporter par l'agent `Creatures`** (fichiers hors du périmètre de la fabrique) : borne du
  terme de Froude du niveau des pas (`Locomotion.cpp`, `6 √min(Fr, 2)`) et loi aviaire de la respiration pour
  `bird_flyer` (`Body.cpp`). Tant qu'ils ne sont pas faits, les niveaux de `level.source_spl_db_1m` des pas de petits
  animaux rapides et le rythme respiratoire de l'oiseau diffèrent de la simulation.
* Le contrôle automatique des coupures nettes ne s'applique pas aux textures faites d'événements discrets épars
  (craquements de litière, gouttes, bulles), dont l'extinction naturelle est rapide ; leur loi de temps est lisse par
  construction (relaxation exponentielle / loi gamma).
* Tonnerre : calibration absolue non sourcée (voir plus haut) ; vocalisations très graves (infrasons) quasi inaudibles
  sur la plupart des systèmes — décision de la passe d'écoute.
* Les niveaux de foley et de biophonie, les rythmes, les densités de chœurs et les constantes mécaniques
  (rapports de réduction, nombre de pales, pas de sculpture…) sont des hypothèses de jeu, documentées dans
  `parameters`.
* Propagation : divergence sphérique + absorption + feuillage ; le vent, le relief, le sol (effet de sol) et la
  réfraction ne sont pas dans les fichiers (le cœur applique vent et relief au niveau, pas au timbre).
* Ambiances : un seul lit de 60 s par condition ; les grenouilles n'ont pas de fiche espèce (présence via
  l'assemblage de lissamphibiens, référence non vérifiée) ; aucune couche nocturne spécifique pour la Djadochta
  au-delà d'insectes épars ; Djadochta sans pluie, feu ni tonnerre spécifiques (le tonnerre est indépendant de la région).
* « Information insuffisante — recherche nécessaire. » pour : cris réels de tout dinosaure non avien, appels des
  anoures et des insectes du Maastrichtien, végétation de la Djadochta.

## Références utilisées

Toutes proviennent de `Data/Sources` (listées avec leur statut `verified` dans `manifest.sources_used`) :
Riede et al. 2016, Clarke et al. 2016 (vocalisation à bouche fermée, syrinx) ; Fletcher 2004 (échelle de
fréquence) ; ISO 9613-1:1993, ISO 9613-2:1996 (propagation) ; Alexander 1976, 1983 (vitesses, similitude
dynamique) ; Minnaert 1933 (bulles) ; Prosperetti & Oguz 1993 (bruit de pluie) ; Marshall & Palmer 1948
(gouttes) ; Stahl 1967, Calder 1968 (respiration) ; Gu et al. 2012 (stridulation d'un katydide jurassique) ;
Gardner & DeMar 2013 (lissamphibiens, non vérifiée) ; fiches espèces et environnements (flore, hydrologie,
précipitations, faune, incendies : Bamforth et al. 2014). Aucune nouvelle référence n'a été ajoutée en 1.2. Les
doublons de `Data/Sources` (même article sous deux identifiants, parfois avec des statuts `verified` contradictoires)
portent désormais `alias_of` vers une entrée canonique dont ils reprennent le statut ; le manifeste ne cite que les
identifiants canoniques. Les copies d'un même identifiant dans deux fichiers thématiques ont aussi été alignées
(`dashzeveg1995_ukhaa_tolgod` et `loope1998_dune_field` : vérifiées dans `bib_djadochta.json`, non vérifiées dans
`bib_environments.json` ; la copie de `bib_environments.json` reprend désormais l'entrée vérifiée).
