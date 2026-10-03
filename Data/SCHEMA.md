# Schéma des données scientifiques — Noctis

Toutes les données biologiques, géologiques et climatiques du jeu sont des fichiers JSON
versionnés, chargés à l'exécution par le cœur de simulation (`Source/NoctisCore`).
Aucune donnée scientifique n'est codée en dur dans le C++.

Deux couches distinctes coexistent dans chaque fiche :

1. **`science`** — ce que la paléontologie réelle sait (ou ne sait pas). Chaque affirmation porte
   un niveau de confiance et des références vérifiées. C'est ce que l'encyclopédie affiche.
2. **`sim`** — les paramètres numériques dont la simulation a besoin pour faire vivre l'animal.
   Chaque paramètre a une **base** (`basis`) qui dit d'où il vient : une source, une dérivation,
   ou `game_assumption:` lorsque la science ne fournit pas de valeur. Une hypothèse de travail
   de simulation n'est **jamais** présentée comme un fait scientifique.

## Niveaux de confiance (`confidence`)

| Valeur JSON          | Affichage            | Sens |
|----------------------|----------------------|------|
| `established`        | ÉTABLI               | Directement soutenu par des preuves solides (fossiles, mesures directes). |
| `strongly_supported` | FORTEMENT ÉTAYÉ      | Plusieurs lignes de preuves convergentes. |
| `inferred`           | INFÉRÉ / PLAUSIBLE   | Déduction raisonnable : anatomie, phylogénie (EPB), biomécanique, analogues actuels. |
| `speculative`        | SPÉCULATIF           | Hypothèse possible mais non démontrée. |
| `unknown`            | INCONNU              | Données insuffisantes. Texte standard : « Information insuffisante — recherche nécessaire. » |

## Bibliographie — `Data/Sources/*.json`

```json
{
  "schema": "noctis.bibliography/1",
  "entries": [
    {
      "id": "hutchinson2011_trex_mass",
      "authors": "Hutchinson, J.R.; Bates, K.T.; Molnar, J.; Allen, V.; Makovicky, P.J.",
      "year": 2011,
      "title": "A computational analysis of limb and body dimensions in Tyrannosaurus rex with implications for locomotion, ontogeny, and growth",
      "container": "PLoS ONE",
      "volume": "6(10)",
      "pages": "e26037",
      "doi": "10.1371/journal.pone.0026037",
      "url": "https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0026037",
      "type": "article",
      "verified": true,
      "verification": "WebSearch 2026-10-03 — notice PLOS ONE et PubMed 22022500"
    }
  ]
}
```

Règles :
- `id` : `nomdupremierauteur` + `année` + `_motclé`, minuscules, ASCII, `[a-z0-9_]`.
- **Ne jamais inventer** une référence, un DOI, un volume ou des pages. Champ inconnu → `null`.
- `verified: true` uniquement si la référence a été retrouvée (titre + auteurs + année) lors d'une recherche.
- Une référence `verified: false` ne peut pas soutenir une confiance supérieure à `inferred`.

## Fiche espèce — `Data/Species/<region>/<id>.json`

```json
{
  "schema": "noctis.species/1",
  "id": "tyrannosaurus_rex",
  "reconstruction_version": "2026.10-a",
  "last_reviewed": "2026-10-03",
  "scientific_name": "Tyrannosaurus rex",
  "authority": "Osborn, 1905",
  "common_names": { "fr": "Tyrannosaure", "en": "Tyrannosaurus" },
  "regions": ["hell_creek"],

  "science": {
    "<topic>": <Claim>,
    "...": "..."
  },
  "competing_hypotheses": [
    { "topic": "growth", "summary": "...", "positions": [
        { "summary": "...", "sources": ["..."] },
        { "summary": "...", "sources": ["..."] } ] }
  ],
  "sim": { "...": "voir plus bas" },
  "sim_basis": { "<chemin.du.parametre>": "source_id | derived: ... | game_assumption: ..." }
}
```

### `Claim` (affirmation scientifique)

```json
{
  "summary": "Texte en français, factuel, nuancé.",
  "confidence": "strongly_supported",
  "sources": ["hutchinson2011_trex_mass"],
  "value": 7000, "min": 6000, "max": 9500, "unit": "kg",
  "notes": "optionnel"
}
```
`value`/`min`/`max`/`unit` sont présents uniquement pour les grandeurs numériques.
Si la donnée manque : `{"summary": "Information insuffisante — recherche nécessaire.", "confidence": "unknown", "sources": []}`.

### Sujets `science` obligatoires (clés)

`taxonomy`, `period`, `formation`, `location`, `size`, `mass`, `morphology`, `skeleton`,
`musculature`, `soft_tissue`, `integument`, `locomotion`, `speed`, `acceleration`, `diet`,
`feeding_strategy`, `predation`, `defense`, `social_behavior`, `reproduction`, `development`,
`growth`, `senses`, `communication`, `activity_pattern`, `habitat`, `climate`, `interactions`,
`fossil_record`, `documented_behavior`, `inferred_behavior`.

### Bloc `sim` (consommé par le C++)

Toutes les unités SI (m, kg, s, m/s, N, Hz, dB SPL re 20 µPa à 1 m pour les niveaux de source).

```json
"sim": {
  "role": "agent",                       // "agent" = individus simulés ; "population" = simulé en effectifs
  "body_plan": "biped_large_theropod",   // voir énumération
  "adult_mass_kg": 7000,
  "adult_length_m": 12.0,
  "hip_height_m": 3.1,
  "foot_length_m": 0.75,
  "foot_width_m": 0.6,
  "hatchling_mass_kg": 5,
  "sexual_dimorphism_mass": 1.0,         // ratio masse femelle/mâle ; 1.0 si inconnu
  "growth": {
    "asymptotic_mass_kg": 7000,
    "max_growth_rate_kg_per_year": 1790,
    "inflection_age_years": 16,
    "maturity_age_years": 18,
    "max_lifespan_years": 30
  },
  "locomotion": {
    "walk_speed_ms": 1.3,
    "max_speed_ms": 5.5,
    "max_accel_ms2": 1.5,
    "max_decel_ms2": 2.5,
    "max_yaw_rate_dps": 40,
    "max_slope_deg": 30,
    "swim": false, "swim_speed_ms": 0,
    "fly": false, "fly_speed_ms": 0,
    "duty_factor_walk": 0.65,
    "duty_factor_run": 0.5
  },
  "metabolism": {
    "thermo_class": "endotherm",          // endotherm | mesotherm | ectotherm
    "max_fasting_days": 40,
    "water_l_per_day_per_100kg": 3.0
  },
  "diet": {
    "type": "carnivore",                  // carnivore | herbivore | omnivore | piscivore | insectivore
    "plant_preferences": { "fern": 0, "horsetail": 0, "angio_herb": 0, "angio_shrub": 0,
                           "angio_tree": 0, "conifer": 0, "palm": 0, "cycadophyte": 0,
                           "ginkgo": 0, "aquatic_macrophyte": 0, "xeric_shrub": 0 },
    "browse_height_min_m": 0, "browse_height_max_m": 0,
    "prey_mass_min_kg": 50, "prey_mass_max_kg": 10000,
    "scavenges": true,
    "eats_fish": false,
    "eats_invertebrates": false,
    "eats_eggs": false
  },
  "senses": {
    "vision_range_m": 1000, "fov_deg": 270, "binocular_deg": 55, "low_light": 0.4,
    "hearing_min_hz": 20, "hearing_max_hz": 1100, "hearing_best_hz": 300, "hearing_threshold_db": 20,
    "olfaction": 0.9,                      // 0..1 relatif (inter-espèces)
    "vibration": 0.3
  },
  "social": {
    "group_size_mean": 1, "group_size_max": 2, "cohesion": 0.1,
    "territorial": true, "territory_radius_m": 4000,
    "alarm_calls": false
  },
  "defense": {
    "weapons": ["bite"],                  // bite | horns | frill | tail_club | tail_whip | kick | claws | head_butt | beak
    "armor": 0.1, "defense_strength": 0.9, "flee_preference": 0.1
  },
  "predation": {
    "hunt_style": "stalk_rush",           // none | ambush | stalk_rush | pursuit | probe_forage | aerial_ground_stalk
    "attack_strength": 1.0,               // 0..1 relatif
    "bite_force_n": 35000,
    "max_chase_s": 25
  },
  "reproduction": {
    "clutch_size_mean": 20, "clutch_size_max": 30,
    "incubation_days": 120,
    "breeding_season_doy": [90, 160],
    "parental_care": 0.5,                 // 0..1
    "nest_type": "mound"                  // mound | open_scrape | brooded | buried | live_birth | none
  },
  "vocal": {
    "closed_mouth": true,
    "f0_min_hz": 20, "f0_max_hz": 200,
    "call_types": [
      { "id": "contact", "duration_s": 2.0, "f0_hz": 40, "source_level_db": 100,
        "context": "social_contact", "confidence": "speculative" }
    ]
  },
  "activity": { "pattern": "diurnal" },   // diurnal | nocturnal | crepuscular | cathemeral
  "personality": {                         // [moyenne, écart-type] sur 0..1 — toujours game_assumption sauf preuve
    "boldness": [0.6, 0.15], "caution": [0.4, 0.15], "aggression": [0.5, 0.15],
    "curiosity": [0.4, 0.15], "sociability": [0.2, 0.1], "vigilance": [0.5, 0.15],
    "risk_tolerance": [0.5, 0.15], "exploration": [0.5, 0.15], "flight_tendency": [0.2, 0.1],
    "dominance": [0.6, 0.2]
  },
  "habitat": {
    "channel": 0, "riverbank": 0.6, "sandbar": 0.5, "oxbow_lake": 0.2, "backswamp": 0.3,
    "riparian_forest": 0.8, "floodplain_open": 0.9, "levee_woodland": 0.7,
    "upland_conifer_forest": 0.5, "cutbank_cliff": 0.1,
    "dune_field": 0, "interdune_flat": 0, "ephemeral_pond": 0, "xeric_scrub": 0
  },
  "population": { "initial_count": 2, "regional_density_per_km2": 0.02 }
}
```

### Énumération `body_plan`

`biped_large_theropod`, `biped_small_theropod`, `biped_ornithomimid`, `biped_oviraptorosaur`,
`biped_small_ornithischian`, `biped_pachycephalosaur`, `facultative_quadruped_hadrosaur`,
`quadruped_ceratopsian`, `quadruped_ankylosaur`, `quadruped_crocodylian`, `quadruped_small_mammal`,
`quadruped_azhdarchid`, `aquatic_reptile`, `bird_flyer`, `small_reptile`, `fish`, `invertebrate`.

### `sim_basis`

Clé = chemin pointé dans `sim` (ex. `"locomotion.max_speed_ms"`). Valeur :
- `"<source_id>"` ou `"<source_id>; <source_id>"` — valeur tirée directement de la littérature ;
- `"derived: <explication>"` — calcul à partir de données publiées (préciser lesquelles) ;
- `"game_assumption: <raison>"` — valeur choisie pour faire tourner la simulation faute de données.

Tout paramètre non listé dans `sim_basis` est considéré comme `game_assumption` par le validateur
et signalé dans l'encyclopédie.

## Environnement — `Data/Environments/<id>.json`

```json
{
  "schema": "noctis.environment/1",
  "id": "hell_creek",
  "reconstruction_version": "2026.10-a",
  "name": { "fr": "...", "en": "..." },
  "science": { "<topic>": <Claim> },        // geology, age, paleolatitude, climate, precipitation,
                                             // seasonality, hydrology, flora, fauna, taphonomy, landscape
  "sim": {
    "latitude_deg": 50, "axial_tilt_deg": 23.4,
    "mean_annual_temp_c": 12, "annual_temp_range_c": 15, "diurnal_temp_range_c": 8,
    "annual_precip_mm": 1200, "wet_season_doy": [60, 180], "dry_season_strength": 0.3,
    "relative_humidity_mean": 0.7, "fog_morning_probability": 0.3, "storm_probability_per_day": 0.05,
    "mean_wind_ms": 3, "prevailing_wind_deg": 270,
    "terrain_generator": "meandering_floodplain",   // meandering_floodplain | dune_field
    "map_size_m": 8192, "relief_m": 60,
    "habitats": ["channel", "riverbank", "..."],
    "plant_functional_types": {
      "fern": { "present": true, "max_biomass_kg_m2": 0.8, "growth_rate_per_day": 0.02,
                "height_m": 0.8, "habitats": {"floodplain_open": 1.0, "riparian_forest": 0.6} }
    },
    "species": [ { "id": "tyrannosaurus_rex", "role": "agent" } ]
  },
  "sim_basis": { "...": "..." }
}
```

## Profils de reconstruction — `Data/Profiles/*.json`

Un profil active ou désactive des comportements **spéculatifs** dans la simulation
(ex. chasse en groupe chez T. rex, cris d'alarme chez Edmontosaurus). Le profil actif est
enregistré dans chaque sauvegarde et chaque rapport scientifique produit par le joueur, pour que
le joueur sache toujours sous quelle reconstruction ses observations ont été faites.
