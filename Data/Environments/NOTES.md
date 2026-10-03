# Environnements : incertitudes principales et choix du jeu

Passe du 2026-10-03, reconstruction `2026.10-a`. Références : `Data/Sources/bib_environments.json`.

## Hell Creek (`hell_creek.json`)

| Sujet | Ce que dit la science | Ce que fait le jeu |
|---|---|---|
| Température | TMA ~7–16 °C (LMA/CLAMP). Réchauffement de ~4 °C puis refroidissement à la fin du Maastrichtien. | Un seul état moyen : 12 °C. Pas de dérive climatique sur ~1–2 Ma. |
| Précipitations | Une seule estimation par surface foliaire (~1 940 mm), avec des erreurs énormes (~1 040–3 520 mm). | 1 600 mm, valeur abaissée (biais des flores de bord de chenal, sécheresse saisonnière contemporaine). Hypothèse de travail. |
| Saisonnalité | Réelle (CLAMP, ~49° N), mais le calendrier des pluies est inconnu. | `wet_season_doy` [100, 200] et saison sèche modérée : hypothèses de jeu. |
| Paléolatitude | ~49° N (Wilf et al. 2003). | 49° N. |
| Palmiers, cycadophytes, prêles, plantes aquatiques | Présence INFÉRÉE, sources primaires non vérifiées pour certains groupes. | Présents, avec de faibles poids d'habitat. |
| `upland_conifer_forest` | Interfluves bien drainés mal conservés dans les fossiles. | Habitat SPÉCULATIF, conservé pour la cohérence avec les fiches espèces. |
| Incendies | Probables (charbon de bois dans la Formation de Frenchman, contemporaine), fréquence inconnue. | Non simulés : le schéma n'a pas de champ pour les incendies. |
| Brouillard, orages, vent | Aucune donnée. | `game_assumption` par analogues actuels. |

## Djadochta (`djadochta.json`)

- **Bibliographie non vérifiée.** Le budget de recherche web était épuisé. Les 11 références Djadochta sont citées de mémoire (`verified: false`, volume, pages et DOI à `null`). Toutes les affirmations sont donc plafonnées à INFÉRÉ, même celles qui font consensus (dépôts éoliens, faune). **À vérifier en priorité.**
- **Flore inconnue.** Seules des traces de racines attestent la végétation. Le jeu n'active qu'un type fonctionnel générique, `xeric_shrub` ; tous les autres sont `present: false`. L'intégrateur doit vérifier que les `plant_preferences` des herbivores de Djadochta (Protoceratops…) comprennent `xeric_shrub`, sinon ils n'auront rien à manger.
- **Climat chiffré.** Toutes les valeurs (17 °C, 200 mm/an, vent d'ouest…) sont des `game_assumption` : aucune estimation publiée n'a été vérifiée. La direction du vent devrait venir des stratifications obliques publiées.
- **Paléolatitude.** ~45° N, déduite sans source vérifiée.
- **Enfouissement.** Deux hypothèses concurrentes : tempête de sable, ou glissement de sable humide après la pluie (Loope et al.). Un événement « effondrement de dune après orage » pourrait être activé par un profil spéculatif.
- **`cutbank_cliff`.** Cet habitat représente les talus crétacés (faces d'avalanche des dunes, berges d'écoulements éphémères), et non les « Flaming Cliffs » actuelles, qui sont des formes d'érosion récentes.

## Ambiguïtés du schéma relevées

- `max_biomass_kg_m2` : biomasse aérienne totale ou fourrage disponible ? Sèche ou fraîche ? Choix retenu : biomasse aérienne sèche totale.
- `growth_rate_per_day` : interprété comme un taux logistique (fraction de la biomasse maximale par jour).
- Le schéma n'a ni champ pour les incendies, ni champ pour la variabilité climatique dans le temps. Le sujet `science.fire` a été ajouté à la demande, mais il n'est pas listé dans le commentaire du schéma.
- Les PFT absents ont tous leurs champs à 0 et `habitats: {}`.
