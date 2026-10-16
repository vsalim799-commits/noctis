"""Asset catalogue: geometry parameters, textures, botanical metadata and scatter rules of every asset.

Confidence vocabulary follows the project (Data/Environments/NOTES.md): ETABLI / FORTEMENT_ETAYE / INFERE /
SPECULATIF. Every asset separates
  * basis: what the asset represents and what the project data says about the group,
  * design_assumptions: choices made for the game (habit taken from modern relatives, colours, proportions),
  * speculative: elements with no direct fossil support.
Only bibliography ids that exist in Data/Sources/bib_environments.json are cited, and only for the statements
the project data attributes to them (hell_creek.json, science.flora / landscape).
"""
from __future__ import annotations

FLORA_SOURCES = ["johnson2002_hellcreek_megaflora", "wilson2021_seafood_salad_flora"]
GENERIC_COLOUR = "Couleurs de feuillage, d'écorce et de bois : valeurs plausibles de plantes actuelles (la couleur ne se fossilise pas)."
GENERIC_HERBIVORY = ("Trous et découpes marginales sur une partie des feuilles : rappel des dégâts d'insectes très "
                     "diversifiés de la flore (51 types d'associations plantes–insectes, labandeira2002_kpg_insect_herbivory) ; "
                     "fréquence et formes = choix de design.")

# --------------------------------------------------------------------------------------------- trees
CONIFER_LEAVES = dict(levels=[2, 3], spacing=0.28, start=0.1, scale=(1.0, 1.35), tilt_deg=10, spread_deg=45,
                      bend=0.10, droop=0.05, roll_deg=50)

ASSETS = {
    # ------------------------------------------------------------------ Cupressaceae s.l.
    "conifer_metasequoia": dict(
        ue_name="SM_Conifer_Metasequoia_A", label_fr="Grand conifère taxodiacé (habitus de type Metasequoia)",
        category="tree", pft="conifer", builder="tree", bark="cupressaceae", atlas="metasequoia", seed=101,
        spec=dict(
            trunk=dict(height=28.0, radius=0.62, tip_radius=0.03, sinuosity=0.015, seg_m=0.9, flare=(0.6, 1.2),
                       buttress=(6, 0.18, 1.6), sink=0.3, taper_pow=0.8),
            levels=[dict(per_m=3.0, range=(0.16, 0.985), down=(104, 58), down_var=7, length=7.8, shape="conical",
                         len_var=0.25, rad=0.30, tip=0.15, up=0.05, noise=0.05, seg_m=0.6, droop_end=-0.02),
                    dict(per_m=3.2, range=(0.08, 0.98), rot="planar", down=(58, 48), down_var=10, length=1.6,
                         shape="taper", rad=0.5, tip=0.3, up=0.02, noise=0.12, seg_m=0.35, min_rad=0.006),
                    dict(per_m=2.2, range=(0.2, 0.9), rot="planar", down=(55, 50), length=0.55, shape="taper",
                         rad=0.55, tip=0.4, noise=0.1, seg_m=0.3, min_rad=0.004, geom=False)],
            leaves=dict(CONIFER_LEAVES, levels=[1, 2, 3], spacing=0.3),
            lod=dict(lod1_levels=2, lod2_levels=1)),
        botany=dict(
            basis="Conifère Cupressaceae s.l. (anciennes « Taxodiaceae ») : Metasequoia est signalé dans la Hell Creek ; conifères abondants mais peu diversifiés dans la flore de Seafood Salad.",
            confidence="FORTEMENT_ETAYE", confidence_subject="présence de Cupressaceae (dont Metasequoia) dans la flore",
            habit_confidence="INFERE",
            design_assumptions=["Port conique élancé, base cannelée et écorce fibreuse rougeâtre repris du Metasequoia actuel (analogue moderne).",
                                "Hauteur 28 m, rameaux distiques de feuilles linéaires plates de ~2 cm.", GENERIC_COLOUR],
            speculative=["Aucun arbre fossile entier : architecture complète extrapolée de l'actuel.",
                         "Feuillage caduc ou non à ~49° N : non représenté (feuillage vert d'été)."],
            sources=FLORA_SOURCES),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    "conifer_taxodium": dict(
        ue_name="SM_Conifer_Taxodioid_B", label_fr="Vieux conifère taxodiacé à cime aplatie et base contreforcée",
        category="tree", pft="conifer", builder="tree", bark="cupressaceae", atlas="metasequoia", seed=202,
        spec=dict(
            trunk=dict(height=26.0, radius=0.85, tip_radius=0.08, sinuosity=0.03, seg_m=0.9, flare=(1.0, 1.4),
                       buttress=(7, 0.3, 2.2), sink=0.35, taper_pow=0.7, up=0.0),
            levels=[dict(per_m=1.7, range=(0.40, 1.0), down=(95, 50), down_var=12, length=10.0, shape="flat_top",
                         len_var=0.35, rad=0.34, tip=0.15, up=0.10, noise=0.10, seg_m=0.7),
                    dict(per_m=2.6, range=(0.1, 0.98), rot="planar", down=(60, 45), down_var=14, length=2.6,
                         shape="taper", rad=0.45, tip=0.25, up=0.03, noise=0.15, seg_m=0.4, min_rad=0.008),
                    dict(per_m=2.4, range=(0.15, 0.95), rot="planar", down=(55, 50), length=0.7, shape="taper",
                         rad=0.5, tip=0.4, noise=0.12, seg_m=0.3, min_rad=0.004, geom=False)],
            envelope=(0.0, 0.0, 20.0, 11.0, 11.0, 8.0),
            leaves=dict(CONIFER_LEAVES, levels=[1, 2, 3], spacing=0.26),
            lod=dict(lod1_levels=2, lod2_levels=1)),
        botany=dict(
            basis="Conifère Cupressaceae s.l. des marais arrière et des berges.",
            confidence="FORTEMENT_ETAYE", confidence_subject="Cupressaceae dans la flore", habit_confidence="SPECULATIF",
            design_assumptions=["Habitus de vieux Taxodium actuel (cime aplatie, contreforts) choisi comme analogue moderne ; le genre Taxodium n'est pas nommé dans les données du projet.",
                                "Même feuillage distique que la variante A (texture partagée).", GENERIC_COLOUR],
            speculative=["Contreforts et cime aplatie : traits d'arbres actuels de milieux inondés, non observés sur fossiles."],
            sources=FLORA_SOURCES),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    "conifer_glyptostrobus": dict(
        ue_name="SM_Conifer_Glyptostrobus_C", label_fr="Conifère de marais (habitus de type Glyptostrobus)",
        category="tree", pft="conifer", builder="tree", bark="cupressaceae", atlas="glyptostrobus", seed=303,
        spec=dict(
            trunk=dict(height=19.0, radius=0.42, tip_radius=0.03, sinuosity=0.02, seg_m=0.8, flare=(0.7, 1.0),
                       buttress=(5, 0.22, 1.2), sink=0.25),
            levels=[dict(per_m=3.4, range=(0.14, 0.99), down=(80, 40), down_var=10, length=3.6, shape="columnar",
                         len_var=0.3, rad=0.3, tip=0.2, up=0.18, noise=0.08, seg_m=0.5),
                    dict(per_m=4.0, range=(0.1, 0.98), down=(50, 40), down_var=14, length=1.1, shape="taper",
                         rad=0.5, tip=0.3, up=0.08, noise=0.15, seg_m=0.35, min_rad=0.005, geom=False)],
            leaves=dict(CONIFER_LEAVES, levels=[1, 2], spacing=0.18, tilt_deg=25, cards_per=1),
            lod=dict(lod1_levels=1, lod2_levels=1)),
        botany=dict(
            basis="Glyptostrobus est signalé parmi les gymnospermes de la Hell Creek ; feuillage dimorphe (pousses à écailles et pousses à feuilles linéaires) comme chez le genre actuel.",
            confidence="FORTEMENT_ETAYE", confidence_subject="présence de Glyptostrobus", habit_confidence="INFERE",
            design_assumptions=["Port étroit colonnaire, rameaux ascendants : habitus du Glyptostrobus actuel.", GENERIC_COLOUR],
            speculative=["Pneumatophores (« genoux ») non modélisés faute de preuve fossile dans les données."],
            sources=FLORA_SOURCES),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    "conifer_young": dict(
        ue_name="SM_Conifer_Metasequoia_Young", label_fr="Jeune conifère taxodiacé (6 m)",
        category="tree", pft="conifer", builder="tree", bark="cupressaceae", atlas="metasequoia", seed=404,
        spec=dict(
            trunk=dict(height=6.0, radius=0.09, tip_radius=0.01, sinuosity=0.03, seg_m=0.4, sink=0.1),
            levels=[dict(per_m=5.0, range=(0.06, 0.98), down=(98, 55), down_var=8, length=2.2, shape="conical",
                         len_var=0.2, rad=0.45, tip=0.2, up=0.06, noise=0.06, seg_m=0.3, min_rad=0.006),
                    dict(per_m=3.0, range=(0.15, 0.95), rot="planar", down=(55, 45), length=0.5, shape="taper",
                         rad=0.55, tip=0.4, noise=0.1, seg_m=0.25, min_rad=0.003)],
            leaves=dict(CONIFER_LEAVES, levels=[1, 2], spacing=0.26, scale=(0.6, 0.85)),
            lod=dict(lod1_levels=1, lod2_levels=1)),
        botany=dict(
            basis="Stade juvénile du conifère taxodiacé (régénération en lisière et clairières).",
            confidence="FORTEMENT_ETAYE", confidence_subject="Cupressaceae dans la flore", habit_confidence="INFERE",
            design_assumptions=["Cime conique jusqu'au sol typique des jeunes Metasequoia actuels.", GENERIC_COLOUR],
            speculative=[], sources=FLORA_SOURCES),
        collision="trunk_small", cull_cm=(0, 25000), lod_screen_size=(1.0, 0.3, 0.1)),
    # ------------------------------------------------------------------ angiosperm trees
    "broadleaf_canopy_a": dict(
        ue_name="SM_Broadleaf_Canopy_A", label_fr="Grand arbre angiosperme de canopée (feuilles entières)",
        category="tree", pft="angio_tree", builder="tree", bark="angio_smooth", atlas="broadleaf_entire", seed=505,
        spec=dict(
            trunk=dict(height=13.0, radius=0.48, tip_radius=0.2, sinuosity=0.04, seg_m=0.8, flare=(0.35, 0.8), sink=0.25),
            levels=[dict(count=(5, 6), range=(0.6, 1.0), down=(50, 32), down_var=10, length=11.0, shape="uniform",
                         len_var=0.2, rad=0.62, tip=0.25, up=0.06, noise=0.12, seg_m=0.7),
                    dict(per_m=2.1, range=(0.12, 1.0), down=(60, 45), down_var=15, length=5.5, shape="taper",
                         len_var=0.3, rad=0.48, tip=0.25, up=0.02, noise=0.18, seg_m=0.5),
                    dict(per_m=3.4, range=(0.1, 1.0), down=(55, 45), down_var=15, length=1.5, shape="taper",
                         rad=0.5, tip=0.35, up=0.0, noise=0.2, seg_m=0.35, min_rad=0.006, geom=False)],
            envelope=(0.0, 0.0, 17.5, 11.5, 11.5, 7.5),
            leaves=dict(levels=[2, 3], spacing=0.17, start=0.12, scale=(1.05, 1.45), tilt_deg=20, spread_deg=50,
                        bend=0.12, roll_deg=45),
            lod=dict(lod1_levels=2, lod2_levels=1)),
        botany=dict(
            basis="Arbre angiosperme (dicotylédone) : les angiospermes dominent la mégaflore (~380 morphotypes foliaires ; arbres angiospermes dominants à Seafood Salad).",
            confidence="ETABLI", confidence_subject="dominance des arbres angiospermes", habit_confidence="INFERE",
            design_assumptions=["Morphotype foliaire générique elliptique à marge entière et nervation pennée (aucune espèce précise).",
                                "Fût de 13 m puis 4–5 charpentières, cime en dôme de ~21 m de large, hauteur ~24 m (cf. hauteur max du type fonctionnel angio_tree = 25 m).",
                                GENERIC_HERBIVORY, GENERIC_COLOUR],
            speculative=["Architecture d'arbre complète : aucun arbre angiosperme entier de la formation n'est reconstitué."],
            sources=FLORA_SOURCES + ["labandeira2002_kpg_insect_herbivory"]),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    "broadleaf_canopy_b": dict(
        ue_name="SM_Broadleaf_Lobed_B", label_fr="Arbre angiosperme à feuilles palmatilobées dentées",
        category="tree", pft="angio_tree", builder="tree", bark="angio_rough", atlas="broadleaf_lobed", seed=606,
        spec=dict(
            trunk=dict(height=7.0, radius=0.40, tip_radius=0.2, sinuosity=0.05, seg_m=0.7, flare=(0.3, 0.6), sink=0.25),
            levels=[dict(count=(4, 6), range=(0.55, 1.0), down=(55, 32), down_var=12, length=9.5, shape="uniform",
                         len_var=0.25, rad=0.6, tip=0.25, up=0.05, noise=0.15, seg_m=0.6),
                    dict(per_m=1.6, range=(0.15, 1.0), down=(55, 40), down_var=18, length=4.0, shape="taper",
                         len_var=0.3, rad=0.48, tip=0.25, up=0.03, noise=0.2, seg_m=0.45),
                    dict(per_m=3.4, range=(0.1, 1.0), down=(55, 45), down_var=15, length=1.3, shape="taper",
                         rad=0.5, tip=0.35, noise=0.2, seg_m=0.3, min_rad=0.006, geom=False)],
            envelope=(0.0, 0.0, 11.5, 8.5, 8.5, 6.2),
            leaves=dict(levels=[2, 3], spacing=0.2, start=0.15, scale=(1.0, 1.35), tilt_deg=22, spread_deg=50,
                        bend=0.12, roll_deg=45),
            lod=dict(lod1_levels=2, lod2_levels=1)),
        botany=dict(
            basis="Arbre angiosperme (dicotylédone), morphotype à feuilles lobées et dentées.",
            confidence="ETABLI", confidence_subject="arbres angiospermes dominants et diversité de morphotypes foliaires", habit_confidence="INFERE",
            design_assumptions=["Feuille palmatilobée dentée générique (aucun genre attribué).",
                                "Cime arrondie basse (~17 m), fût court, écorce fissurée.", GENERIC_HERBIVORY, GENERIC_COLOUR],
            speculative=["Architecture complète extrapolée."],
            sources=FLORA_SOURCES + ["labandeira2002_kpg_insect_herbivory"]),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    "broadleaf_riparian_c": dict(
        ue_name="SM_Broadleaf_Riparian_C", label_fr="Arbre angiosperme riverain penché, multi-troncs, feuilles lancéolées",
        category="tree", pft="angio_tree", builder="tree", bark="angio_smooth", atlas="broadleaf_lanceolate", seed=707,
        spec=dict(
            trunk=dict(height=11.0, radius=0.26, tip_radius=0.05, sinuosity=0.08, seg_m=0.6, count=2, splay_deg=22,
                       flare=(0.25, 0.5), sink=0.2, up=0.01),
            levels=[dict(per_m=1.3, range=(0.3, 1.0), down=(55, 35), down_var=14, length=5.0, shape="dome",
                         len_var=0.3, rad=0.5, tip=0.2, up=-0.02, noise=0.18, seg_m=0.45, droop_end=-0.08),
                    dict(per_m=3.2, range=(0.1, 1.0), down=(50, 40), down_var=15, length=1.8, shape="taper",
                         rad=0.5, tip=0.3, up=-0.06, noise=0.2, seg_m=0.3, droop_end=-0.15, min_rad=0.005)],
            leaves=dict(levels=[1, 2], spacing=0.11, start=0.1, scale=(1.05, 1.4), tilt_deg=-5, spread_deg=45,
                        bend=0.15, droop=0.25, roll_deg=45),
            lod=dict(lod1_levels=1, lod2_levels=1)),
        botany=dict(
            basis="Arbre angiosperme de berge (forêt riveraine) ; les flores de bord de chenal sont bien représentées dans les dépôts.",
            confidence="ETABLI", confidence_subject="arbres angiospermes dans les forêts riveraines", habit_confidence="SPECULATIF",
            design_assumptions=["Port penché vers l'eau, rameaux retombants, deux troncs : choix de design pour lire la berge.",
                                "Feuille lancéolée finement dentée générique.", GENERIC_HERBIVORY, GENERIC_COLOUR],
            speculative=["Port retombant : pas d'indice fossile direct."],
            sources=FLORA_SOURCES),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    "ginkgo_tree": dict(
        ue_name="SM_Ginkgo_A", label_fr="Ginkgo (arbre à rameaux courts)",
        category="tree", pft="ginkgo", builder="tree", bark="angio_rough", atlas="ginkgo", seed=808,
        spec=dict(
            trunk=dict(height=16.0, radius=0.38, tip_radius=0.04, sinuosity=0.05, seg_m=0.8, flare=(0.3, 0.8), sink=0.25),
            levels=[dict(per_m=1.1, range=(0.25, 0.97), down=(62, 38), down_var=12, length=6.5, shape="flame",
                         len_var=0.35, rad=0.32, tip=0.2, up=0.07, noise=0.12, seg_m=0.6),
                    dict(per_m=2.6, range=(0.1, 1.0), down=(55, 45), down_var=20, length=1.7, shape="taper",
                         rad=0.5, tip=0.35, up=0.05, noise=0.2, seg_m=0.4, min_rad=0.006)],
            leaves=dict(levels=[1, 2], spacing=0.13, start=0.1, scale=(1.1, 1.45), tilt_deg=15, spread_deg=55, bend=0.08, roll_deg=50),
            lod=dict(lod1_levels=1, lod2_levels=1)),
        botany=dict(
            basis="Ginkgo est signalé dans la Hell Creek (Johnson 2002 ; Wilson et al. 2021), relativement rare à Seafood Salad.",
            confidence="FORTEMENT_ETAYE", confidence_subject="présence de Ginkgo", habit_confidence="INFERE",
            design_assumptions=["Port clairsemé et rameaux courts du Ginkgo biloba actuel.", "Feuilles en éventail bilobées.", GENERIC_COLOUR],
            speculative=["Espèce fossile et port exact inconnus ; 16 m."], sources=FLORA_SOURCES),
        collision="trunk", cull_cm=(0, 0), lod_screen_size=(1.0, 0.35, 0.12)),
    # ------------------------------------------------------------------ shrubs
    "shrub_ovate": dict(
        ue_name="SM_Shrub_Ovate_A", label_fr="Arbuste angiosperme multicaule (feuilles ovées)",
        category="shrub", pft="angio_shrub", builder="tree", bark="angio_smooth", atlas="shrub_ovate", seed=909,
        spec=dict(
            trunk=dict(height=2.3, radius=0.035, tip_radius=0.008, sinuosity=0.12, seg_m=0.25, count=6, splay_deg=24, sink=0.05),
            levels=[dict(per_m=4.0, range=(0.25, 1.0), down=(45, 35), down_var=12, length=0.9, shape="taper",
                         rad=0.6, tip=0.3, up=0.05, noise=0.25, seg_m=0.2, min_rad=0.004)],
            leaves=dict(levels=[0, 1], spacing=0.08, start=0.3, scale=(0.95, 1.25), tilt_deg=20, spread_deg=55, bend=0.08, roll_deg=45),
            lod=dict(lod1_levels=1, lod2_levels=0)),
        botany=dict(
            basis="Arbuste angiosperme du sous-bois et des milieux ouverts (type fonctionnel angio_shrub, hauteur max 3 m).",
            confidence="INFERE", confidence_subject="arbustes angiospermes dans les milieux ouverts et le sous-bois", habit_confidence="SPECULATIF",
            design_assumptions=["Port multicaule de 2,3 m, feuilles ovées acuminées génériques.", GENERIC_HERBIVORY, GENERIC_COLOUR],
            speculative=["Port arbustif : les arbustes sont peu identifiables dans les flores fossiles."], sources=FLORA_SOURCES),
        collision="none", cull_cm=(0, 12000), lod_screen_size=(1.0, 0.3, 0.1)),
    "shrub_small_leaf": dict(
        ue_name="SM_Shrub_SmallLeaf_B", label_fr="Arbuste bas dense à petites feuilles",
        category="shrub", pft="angio_shrub", builder="tree", bark="angio_rough", atlas="shrub_small", seed=1010,
        spec=dict(
            trunk=dict(height=1.3, radius=0.02, tip_radius=0.006, sinuosity=0.15, seg_m=0.15, count=9, splay_deg=38, sink=0.04),
            levels=[dict(per_m=6.0, range=(0.2, 1.0), down=(50, 35), down_var=15, length=0.5, shape="taper",
                         rad=0.6, tip=0.35, up=0.04, noise=0.3, seg_m=0.15, min_rad=0.003)],
            leaves=dict(levels=[0, 1], spacing=0.065, start=0.25, scale=(0.95, 1.25), tilt_deg=25, spread_deg=60, bend=0.06, roll_deg=50),
            lod=dict(lod1_levels=1, lod2_levels=0)),
        botany=dict(
            basis="Arbuste angiosperme bas des levées et lisières.", confidence="INFERE",
            confidence_subject="arbustes angiospermes", habit_confidence="SPECULATIF",
            design_assumptions=["Touffe dense de 1,3 m à petites feuilles elliptiques.", GENERIC_COLOUR],
            speculative=["Port et taille."], sources=FLORA_SOURCES),
        collision="none", cull_cm=(0, 10000), lod_screen_size=(1.0, 0.3, 0.1)),
    # ------------------------------------------------------------------ palms
    "palm_sabalites_tall": dict(
        ue_name="SM_Palm_Sabalites_Tall", label_fr="Palmier à feuilles costapalmées (type Sabalites), stipe de 6 m",
        category="palm", pft="palm", builder="palm", bark="palm_trunk", atlas="palm_fan", seed=1111,
        spec=dict(trunk_h=5.4, trunk_r=0.2, leaves=20, petiole=1.4, blade=2.1, droop=0.35),
        botany=dict(
            basis="Palmiers costapalmés (Sabalites) confirmés au Campanien–Maastrichtien plus au nord (Alberta, Saskatchewan) ; présence dans la Hell Creek même : INFÉRÉE.",
            confidence="INFERE", confidence_subject="palmiers dans la Hell Creek", habit_confidence="SPECULATIF",
            design_assumptions=["Limbe costapalmé plissé à segments fendus, d'après la morphologie du genre de feuilles Sabalites.",
                                "Stipe à bases foliaires persistantes et couronne de ~17 feuilles : port du Sabal actuel (analogue moderne).",
                                GENERIC_COLOUR],
            speculative=["Port arborescent et hauteur (6 m) : seules des feuilles sont connues."],
            sources=["greenwood2022_cretaceous_palms", "johnson2002_hellcreek_megaflora"]),
        collision="trunk", cull_cm=(0, 30000), lod_screen_size=(1.0, 0.3, 0.1)),
    "palm_sabalites_short": dict(
        ue_name="SM_Palm_Sabalites_Acaulescent", label_fr="Palmier acaule à feuilles costapalmées (sous-bois)",
        category="palm", pft="palm", builder="palm", bark="palm_trunk", atlas="palm_fan", seed=1212,
        spec=dict(trunk_h=0.25, trunk_r=0.16, leaves=12, petiole=0.9, blade=1.7, droop=0.2),
        botany=dict(
            basis="Variante basse du palmier à feuilles de type Sabalites.", confidence="INFERE",
            confidence_subject="palmiers dans la Hell Creek", habit_confidence="SPECULATIF",
            design_assumptions=["Port acaule (stipe souterrain) des Sabal de sous-bois actuels.", GENERIC_COLOUR],
            speculative=["Existence d'un palmier acaule dans la formation."],
            sources=["greenwood2022_cretaceous_palms"]),
        collision="none", cull_cm=(0, 15000), lod_screen_size=(1.0, 0.3, 0.1)),
    # ------------------------------------------------------------------ ferns
    "fern_tufted": dict(
        ue_name="SM_Fern_Tufted_A", label_fr="Fougère en touffe dressée (frondes pennées lobées)",
        category="fern", pft="fern", builder="fern", atlas="fern", seed=1313,
        spec=dict(fronds=14, cells=[0, 0, 0, 3], length=(0.9, 1.2), arch=0.45, rise=(55, 75), rosette_r=0.06),
        botany=dict(
            basis="Fougères présentes dans la flore (FORTEMENT ÉTAYÉ), relativement peu abondantes à Seafood Salad ; dans les milieux ouverts, fougères, prêles, herbacées et arbustes remplaçaient les prairies.",
            confidence="FORTEMENT_ETAYE", confidence_subject="présence de fougères", habit_confidence="INFERE",
            design_assumptions=["Touffe en entonnoir de frondes pennées à pennes lobées ; 1 m (hauteur du type fonctionnel fern).", GENERIC_COLOUR],
            speculative=["Aucune espèce attribuée."], sources=FLORA_SOURCES),
        collision="none", cull_cm=(0, 6000), lod_screen_size=(1.0, 0.25, 0.08)),
    "fern_lace": dict(
        ue_name="SM_Fern_Bipinnate_B", label_fr="Fougère basse étalée (frondes bipennées)",
        category="fern", pft="fern", builder="fern", atlas="fern", seed=1414,
        spec=dict(fronds=12, cells=[1, 1, 1, 3], length=(0.6, 0.8), arch=0.5, rise=(45, 65), rosette_r=0.05),
        botany=dict(basis="Fougère du sous-bois humide.", confidence="FORTEMENT_ETAYE", confidence_subject="présence de fougères",
                    habit_confidence="INFERE", design_assumptions=["Frondes bipennées étalées de 0,6 m.", GENERIC_COLOUR],
                    speculative=["Aucune espèce attribuée."], sources=FLORA_SOURCES),
        collision="none", cull_cm=(0, 5000), lod_screen_size=(1.0, 0.25, 0.08)),
    "fern_marsh": dict(
        ue_name="SM_Fern_Marsh_C", label_fr="Fougère de marais (frondes étroites dressées)",
        category="fern", pft="fern", builder="fern", atlas="fern", seed=1515,
        spec=dict(fronds=18, cells=[2, 2, 2, 2, 3], length=(1.0, 1.35), arch=0.25, rise=(68, 82), rosette_r=0.12, spread=0.25),
        botany=dict(basis="Fougère des sols engorgés (marais arrière, berges).", confidence="FORTEMENT_ETAYE",
                    confidence_subject="présence de fougères", habit_confidence="SPECULATIF",
                    design_assumptions=["Frondes pennées étroites dressées en colonie lâche.", GENERIC_COLOUR],
                    speculative=["Écologie de marais pour cette forme : choix de design."], sources=FLORA_SOURCES),
        collision="none", cull_cm=(0, 6000), lod_screen_size=(1.0, 0.25, 0.08)),
    # ------------------------------------------------------------------ horsetails
    "horsetail_unbranched": dict(
        ue_name="SM_Horsetail_Stand_A", label_fr="Peuplement de prêles à tiges simples (avec strobiles)",
        category="horsetail", pft="horsetail", builder="horsetail", bark="horsetail_stem", seed=1616,
        spec=dict(stems=40, radius=0.55, height=(0.8, 1.45), stem_r=(0.006, 0.0095), internode=(0.05, 0.09),
                  branched=False, strobili=0.35),
        botany=dict(
            basis="Prêles (sphénopsides, Equisetum) : INFÉRÉ (résumé de Johnson 1993 non vérifié) ; type fonctionnel horsetail des berges, bancs de sable et marais.",
            confidence="INFERE", confidence_subject="présence de prêles", habit_confidence="INFERE",
            design_assumptions=["Tiges articulées creuses à gaines dentées et strobiles terminaux : morphologie d'Equisetum actuel.",
                                "Peuplement clonal de ~0,9 m² (rhizomes).", GENERIC_COLOUR],
            speculative=["Densité et hauteur (1,2 m, type fonctionnel)."], sources=["johnson2002_hellcreek_megaflora"]),
        collision="none", cull_cm=(0, 5000), lod_screen_size=(1.0, 0.25, 0.08)),
    "horsetail_branched": dict(
        ue_name="SM_Horsetail_Stand_B", label_fr="Peuplement de prêles à verticilles de rameaux",
        category="horsetail", pft="horsetail", builder="horsetail", bark="horsetail_stem", seed=1717,
        spec=dict(stems=20, radius=0.5, height=(0.75, 1.3), stem_r=(0.0055, 0.008), internode=(0.06, 0.09),
                  branched=True, strobili=0.0, whorl_scale=1.9, whorl_droop=0.55, lean=0.05),
        botany=dict(basis="Prêle à rameaux verticillés.", confidence="INFERE", confidence_subject="présence de prêles",
                    habit_confidence="INFERE", design_assumptions=["Verticilles de rameaux fins et étalés à chaque nœud (port « en goupillon » d'Equisetum actuel) ; tiges peu nombreuses et droites pour ne jamais lire comme une touffe de graminée.", GENERIC_COLOUR],
                    speculative=["Forme ramifiée : choix de variété."], sources=["johnson2002_hellcreek_megaflora"]),
        collision="none", cull_cm=(0, 4500), lod_screen_size=(1.0, 0.25, 0.08)),
    # ------------------------------------------------------------------ aquatic
    "emergent_broadleaf": dict(
        ue_name="SM_Aquatic_Emergent_Broad", label_fr="Émergente aquatique à limbes larges pétiolés",
        category="aquatic", pft="aquatic_macrophyte", builder="emergent", atlas="emergent_broad", bark="green_stem", seed=1818,
        spec=dict(leaves=13, petiole=(0.45, 0.8), blade=(0.28, 0.38), radius=0.25, kind="broad"),
        botany=dict(
            basis="Macrophytes aquatiques des eaux calmes (type fonctionnel aquatic_macrophyte : bras morts, marais arrière).",
            confidence="INFERE", confidence_subject="plantes aquatiques", habit_confidence="SPECULATIF",
            design_assumptions=["Émergente générique à limbes cordés sur longs pétioles (aucun taxon attribué).", GENERIC_COLOUR],
            speculative=["Forme de croissance émergente à larges feuilles."], sources=["johnson2002_hellcreek_megaflora"]),
        collision="none", cull_cm=(0, 5000), lod_screen_size=(1.0, 0.25, 0.08)),
    "emergent_strap": dict(
        ue_name="SM_Aquatic_Emergent_Strap", label_fr="Émergente à feuilles rubanées (port de jonc, non graminée)",
        category="aquatic", pft="aquatic_macrophyte", builder="emergent", atlas="strap", bark="green_stem", seed=1919,
        spec=dict(leaves=30, length=(0.8, 1.35), radius=0.22, kind="strap", fans=4, leaves_per_fan=6,
                  width_scale=2.6, fan_spread_deg=16, stiff=True),
        botany=dict(
            basis="Macrophyte émergent des rives calmes.", confidence="INFERE", confidence_subject="plantes aquatiques",
            habit_confidence="SPECULATIF",
            design_assumptions=["Éventails équitants de larges feuilles rubanées à nervation parallèle (port de rubanier / iris actuel, monocotylédone aquatique générique, aucun taxon attribué). Ce n'est PAS une graminée : pas d'épillets, pas de prairie ; la disposition en éventails évite la lecture « touffe d'herbe ».", GENERIC_COLOUR],
            speculative=["Groupe et forme exacts inconnus."], sources=["johnson2002_hellcreek_megaflora"]),
        collision="none", cull_cm=(0, 5000), lod_screen_size=(1.0, 0.25, 0.08)),
    "floating_rosettes": dict(
        ue_name="SM_Aquatic_Floating_Rosettes", label_fr="Rosettes flottantes (inspiration Quereuxia)",
        category="aquatic", pft="aquatic_macrophyte", builder="floating", atlas="floating", bark="green_stem", seed=2020,
        spec=dict(rosettes=9, patch=1.8),
        botany=dict(
            basis="Plante aquatique flottante Quereuxia angulata signalée dans des dépôts d'étang du Dakota du Sud (INFÉRÉ, référence primaire non vérifiée).",
            confidence="INFERE", confidence_subject="présence de plantes flottantes de type Quereuxia", habit_confidence="SPECULATIF",
            design_assumptions=["Rosettes de petites feuilles rhomboïdales dentées à plat sur l'eau ; taille 25–45 cm.", GENERIC_COLOUR],
            speculative=["Disposition et taille exactes des rosettes."], sources=["johnson2002_hellcreek_megaflora"]),
        collision="none", cull_cm=(0, 6000), lod_screen_size=(1.0, 0.25, 0.08), on_water=True),
    # ------------------------------------------------------------------ minor groups present in the data
    "cycadophyte": dict(
        ue_name="SM_Cycadophyte_A", label_fr="Cycadophyte à tronc court",
        category="shrub", pft="cycadophyte", builder="cycad", atlas="cycad", bark="palm_trunk", seed=2121,
        spec=dict(trunk_h=0.6, trunk_r=0.17, leaves=16, length=(1.1, 1.5)),
        botany=dict(basis="Cycadophytes rares (INFÉRÉ dans les données).", confidence="INFERE",
                    confidence_subject="présence de cycadophytes", habit_confidence="SPECULATIF",
                    design_assumptions=["Tronc court cuirassé de bases foliaires, couronne de feuilles pennées rigides (cycadale actuelle comme analogue).", GENERIC_COLOUR],
                    speculative=["Groupe (cycadale ou autre) et port."], sources=["johnson2002_hellcreek_megaflora"]),
        collision="trunk_small", cull_cm=(0, 8000), lod_screen_size=(1.0, 0.25, 0.08)),
    "herb_dicot": dict(
        ue_name="SM_Herb_Dicot_A", label_fr="Herbacée dicotylédone basse (touffe de feuilles)",
        category="herb", pft="angio_herb", builder="herb", atlas="herb", bark="green_stem", seed=2222,
        spec=dict(leaves=16, radius=0.18),
        botany=dict(basis="Herbacées des milieux ouverts (type fonctionnel angio_herb, 0,5 m) ; sous-représentées dans les fossiles (biais taphonomique).",
                    confidence="INFERE", confidence_subject="herbacées angiospermes", habit_confidence="SPECULATIF",
                    design_assumptions=["Touffe de feuilles pétiolées sans floraison voyante : ni graminée ni prairie fleurie.", GENERIC_COLOUR],
                    speculative=["Forme générique."], sources=FLORA_SOURCES),
        collision="none", cull_cm=(0, 3500), lod_screen_size=(1.0, 0.25, 0.08)),
    # ------------------------------------------------------------------ deadwood
    "deadwood_log": dict(
        ue_name="SM_Deadwood_FallenLog", label_fr="Tronc tombé (écorce partielle, branches cassées)",
        category="deadwood", pft="deadwood", builder="log", bark="cupressaceae", seed=2323,
        spec=dict(length=9.0, radius=0.38),
        botany=dict(basis="Bois mort de forêt riveraine et laisses de crue (crues répétées enregistrées par les dépôts de débordement).",
                    confidence="INFERE", confidence_subject="bois flotté et chablis en plaine d'inondation",
                    design_assumptions=["Tronc de conifère taxodiacé à moitié écorcé.", GENERIC_COLOUR], speculative=[],
                    sources=["fastovsky2016_hellcreek_primer"]),
        collision="hulls", cull_cm=(0, 15000), lod_screen_size=(1.0, 0.3, 0.1), nanite=True),
    "deadwood_stump": dict(
        ue_name="SM_Deadwood_Stump", label_fr="Chicot cassé avec racines",
        category="deadwood", pft="deadwood", builder="stump", bark="angio_rough", seed=2424,
        spec=dict(height=1.4, radius=0.45),
        botany=dict(basis="Arbre cassé (tempêtes, crues).", confidence="INFERE", confidence_subject="chablis",
                    design_assumptions=["Cassure en esquilles, racines traçantes.", GENERIC_COLOUR], speculative=[],
                    sources=["fastovsky2016_hellcreek_primer"]),
        collision="hulls", cull_cm=(0, 12000), lod_screen_size=(1.0, 0.3, 0.1), nanite=True),
    "deadwood_rootwad": dict(
        ue_name="SM_Deadwood_RootWad", label_fr="Souche déracinée (galette racinaire)",
        category="deadwood", pft="deadwood", builder="rootwad", bark="angio_smooth", seed=2525,
        spec=dict(radius=1.5, log_length=3.0, log_radius=0.35),
        botany=dict(basis="Arbre déraciné par l'érosion de berge (berges concaves des méandres).", confidence="INFERE",
                    confidence_subject="arbres déracinés sur berges érodées",
                    design_assumptions=["Galette racinaire verticale avec motte de limon.", GENERIC_COLOUR], speculative=[],
                    sources=["fastovsky2016_hellcreek_primer"]),
        collision="hulls", cull_cm=(0, 15000), lod_screen_size=(1.0, 0.3, 0.1), nanite=True),
    "deadwood_branches": dict(
        ue_name="SM_Deadwood_BranchLitter", label_fr="Branches mortes au sol",
        category="deadwood", pft="deadwood", builder="branches", bark="deadwood", seed=2626,
        spec=dict(count=9, area=3.0),
        botany=dict(basis="Litière ligneuse de forêt.", confidence="INFERE", confidence_subject="litière ligneuse",
                    design_assumptions=["Branches grises cassées, sans écorce.", GENERIC_COLOUR], speculative=[], sources=[]),
        collision="none", cull_cm=(0, 4000), lod_screen_size=(1.0, 0.25, 0.08)),
}

# Bark/stem texture used by each builder that needs a second wood material (logs: inner wood).
DEADWOOD_INNER = "deadwood"

# --------------------------------------------------------------------------------------------- scatter rules
# Densities are GAME ASSUMPTIONS (instances per hectare where the simulation density mask = 1), chosen for
# plausible stand structure (closed riparian forest ~250-400 canopy stems/ha). Masks: Build/Environment/sim.
PFT_RULES = {
    "conifer": dict(mask="foliage_conifer", per_ha=220, min_spacing_m=5.5, slope_max_deg=30, water_buffer_m=4,
                    cluster_m=120, cluster_amp=0.6,
                    assets={"conifer_metasequoia": 0.40, "conifer_taxodium": 0.15, "conifer_glyptostrobus": 0.25, "conifer_young": 0.20},
                    habitat_bias={"backswamp": {"conifer_glyptostrobus": 2.0, "conifer_taxodium": 1.6}}),
    "angio_tree": dict(mask="foliage_angio_tree", per_ha=260, min_spacing_m=6.0, slope_max_deg=30, water_buffer_m=3,
                       cluster_m=90, cluster_amp=0.5,
                       assets={"broadleaf_canopy_a": 0.42, "broadleaf_canopy_b": 0.38, "broadleaf_riparian_c": 0.20},
                       habitat_bias={"riverbank": {"broadleaf_riparian_c": 4.0}, "levee_woodland": {"broadleaf_riparian_c": 1.5}}),
    "ginkgo": dict(mask="foliage_ginkgo", per_ha=40, min_spacing_m=8.0, slope_max_deg=30, water_buffer_m=4,
                   cluster_m=150, cluster_amp=0.8, assets={"ginkgo_tree": 1.0}),
    "palm": dict(mask="foliage_palm", per_ha=160, min_spacing_m=3.0, slope_max_deg=30, water_buffer_m=3,
                 cluster_m=60, cluster_amp=0.9, assets={"palm_sabalites_tall": 0.45, "palm_sabalites_short": 0.55}),
    "angio_shrub": dict(mask="foliage_angio_shrub", per_ha=900, min_spacing_m=1.6, slope_max_deg=35, water_buffer_m=2,
                        cluster_m=30, cluster_amp=0.7, assets={"shrub_ovate": 0.55, "shrub_small_leaf": 0.45}),
    "cycadophyte": dict(mask="foliage_cycadophyte", per_ha=120, min_spacing_m=2.0, slope_max_deg=30, water_buffer_m=4,
                        cluster_m=40, cluster_amp=0.9, assets={"cycadophyte": 1.0}),
    "fern": dict(mask="foliage_fern", per_ha=4000, min_spacing_m=0.8, slope_max_deg=40, water_buffer_m=1,
                 cluster_m=12, cluster_amp=0.8, assets={"fern_tufted": 0.45, "fern_lace": 0.35, "fern_marsh": 0.20},
                 habitat_bias={"backswamp": {"fern_marsh": 4.0}, "riverbank": {"fern_marsh": 2.0}}),
    "horsetail": dict(mask="foliage_horsetail", per_ha=2600, min_spacing_m=0.9, slope_max_deg=35, water_buffer_m=-1.5,
                      cluster_m=10, cluster_amp=0.9, assets={"horsetail_unbranched": 0.6, "horsetail_branched": 0.4}),
    "angio_herb": dict(mask="foliage_angio_herb", per_ha=3000, min_spacing_m=0.5, slope_max_deg=40, water_buffer_m=1,
                       cluster_m=8, cluster_amp=0.8, assets={"herb_dicot": 1.0}),
    "aquatic_macrophyte": dict(mask="foliage_aquatic_macrophyte", per_ha=3000, min_spacing_m=0.8, slope_max_deg=20,
                               water_buffer_m=-6.0, cluster_m=10, cluster_amp=0.9, shoreline_per_ha=900,
                               assets={"emergent_strap": 0.45, "emergent_broadleaf": 0.35, "floating_rosettes": 0.20}),
}
# Deadwood is not simulated as a plant type: habitat-driven placement (GAME ASSUMPTION), denser near the river.
DEADWOOD_RULES = {
    "deadwood_log": dict(habitats={"riparian_forest": 12, "levee_woodland": 10, "backswamp": 8, "upland_conifer_forest": 8,
                                   "sandbar": 6, "riverbank": 10}, min_spacing_m=8),
    "deadwood_stump": dict(habitats={"riparian_forest": 5, "levee_woodland": 5, "backswamp": 6, "riverbank": 4}, min_spacing_m=8),
    "deadwood_rootwad": dict(habitats={"riverbank": 6, "sandbar": 5, "cutbank_cliff": 8}, min_spacing_m=12),
    "deadwood_branches": dict(habitats={"riparian_forest": 60, "levee_woodland": 50, "upland_conifer_forest": 40,
                                        "backswamp": 25, "floodplain_open": 8, "sandbar": 15, "riverbank": 15}, min_spacing_m=3),
}


def asset_ids(category=None):
    return [k for k, v in ASSETS.items() if category is None or v["category"] == category]
