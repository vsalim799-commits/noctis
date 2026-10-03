#include "Noctis/Creatures/SpeciesRuntime.h"

#include <cmath>

namespace noctis
{
const char* lodLabel(LodLevel l)
{
    switch (l)
    {
    case LodLevel::Full: return "Complète";
    case LodLevel::Near: return "Réduite";
    case LodLevel::Far: return "Lointaine";
    case LodLevel::Regional: return "Régionale";
    }
    return "?";
}

const char* bodyRegionLabelFr(BodyRegion r)
{
    switch (r)
    {
    case BodyRegion::Head: return "Tête";
    case BodyRegion::Jaw: return "Mâchoire";
    case BodyRegion::Neck: return "Cou";
    case BodyRegion::Torso: return "Tronc";
    case BodyRegion::Ribs: return "Côtes";
    case BodyRegion::Tail: return "Queue";
    case BodyRegion::ForelimbL: return "Membre antérieur gauche";
    case BodyRegion::ForelimbR: return "Membre antérieur droit";
    case BodyRegion::HindlimbL: return "Membre postérieur gauche";
    case BodyRegion::HindlimbR: return "Membre postérieur droit";
    case BodyRegion::Count: break;
    }
    return "?";
}

const char* bodyRegionKey(BodyRegion r)
{
    switch (r)
    {
    case BodyRegion::Head: return "head";
    case BodyRegion::Jaw: return "jaw";
    case BodyRegion::Neck: return "neck";
    case BodyRegion::Torso: return "torso";
    case BodyRegion::Ribs: return "ribs";
    case BodyRegion::Tail: return "tail";
    case BodyRegion::ForelimbL: return "forelimb_l";
    case BodyRegion::ForelimbR: return "forelimb_r";
    case BodyRegion::HindlimbL: return "hindlimb_l";
    case BodyRegion::HindlimbR: return "hindlimb_r";
    case BodyRegion::Count: break;
    }
    return "?";
}

const char* injuryTypeLabelFr(InjuryType t)
{
    switch (t)
    {
    case InjuryType::Laceration: return "Lacération";
    case InjuryType::Puncture: return "Perforation";
    case InjuryType::Fracture: return "Fracture";
    case InjuryType::Contusion: return "Contusion";
    case InjuryType::Sprain: return "Entorse";
    case InjuryType::Burn: return "Brûlure";
    }
    return "?";
}

const char* awarenessStageLabelFr(AwarenessStage s)
{
    switch (s)
    {
    case AwarenessStage::Unaware: return "Aucune conscience";
    case AwarenessStage::Detected: return "Stimulus détecté";
    case AwarenessStage::Oriented: return "Orientation";
    case AwarenessStage::Vigilant: return "Vigilance";
    case AwarenessStage::Identified: return "Identification";
    }
    return "?";
}

const char* relationLabelFr(Relation r)
{
    switch (r)
    {
    case Relation::Unknown: return "Inconnu";
    case Relation::GroupMate: return "Membre du groupe";
    case Relation::Conspecific: return "Congénère";
    case Relation::Offspring: return "Jeune";
    case Relation::Parent: return "Parent";
    case Relation::Mate: return "Partenaire";
    case Relation::Predator: return "Prédateur";
    case Relation::Prey: return "Proie";
    case Relation::Competitor: return "Compétiteur";
    case Relation::Harmless: return "Inoffensif";
    case Relation::Researcher: return "Chercheur";
    case Relation::Vehicle: return "Véhicule";
    case Relation::Drone: return "Drone";
    case Relation::Carcass: return "Carcasse";
    case Relation::Nest: return "Nid";
    }
    return "?";
}

const char* memoryKindLabelFr(MemoryKind k)
{
    switch (k)
    {
    case MemoryKind::Water: return "Eau";
    case MemoryKind::Food: return "Nourriture";
    case MemoryKind::Danger: return "Danger";
    case MemoryKind::SafeRest: return "Repos sûr";
    case MemoryKind::Nest: return "Nid";
    case MemoryKind::Carcass: return "Carcasse";
    case MemoryKind::ResearcherEncounter: return "Rencontre humaine";
    case MemoryKind::PredatorSighting: return "Prédateur aperçu";
    case MemoryKind::Shelter: return "Abri";
    case MemoryKind::Intrusion: return "Intrusion";
    }
    return "?";
}

const char* behaviorKey(BehaviorId b)
{
    static const char* const keys[] = {"rest", "sleep", "wander", "forage", "drink", "travel", "follow_group", "vigilance", "flee",
                                       "freeze", "defend", "defend_young", "investigate", "display", "patrol", "hunt", "feed_carcass",
                                       "scavenge", "thermoregulate", "court", "nest_tend", "care_young", "emigrate", "groom",
                                       "socialize", "fight"};
    const int i = static_cast<int>(b);
    return i >= 0 && i < kBehaviorCount ? keys[i] : "?";
}

const char* behaviorLabelFr(BehaviorId b)
{
    static const char* const labels[] = {"Repos", "Sommeil", "Déambulation", "Recherche alimentaire", "Abreuvement", "Déplacement",
                                         "Suit le groupe", "Vigilance", "Fuite", "Immobilité", "Défense", "Protection des jeunes",
                                         "Investigation", "Parade / avertissement", "Patrouille", "Chasse", "Consommation de carcasse",
                                         "Charognage", "Thermorégulation", "Parade nuptiale", "Soin du nid", "Soin des jeunes",
                                         "Départ de la vallée", "Toilettage", "Interactions sociales", "Combat"};
    const int i = static_cast<int>(b);
    return i >= 0 && i < kBehaviorCount ? labels[i] : "?";
}

const char* huntPhaseLabelFr(HuntPhase p)
{
    switch (p)
    {
    case HuntPhase::None: return "—";
    case HuntPhase::Search: return "Recherche";
    case HuntPhase::Stalk: return "Approche discrète";
    case HuntPhase::Approach: return "Approche";
    case HuntPhase::Chase: return "Poursuite";
    case HuntPhase::Attack: return "Attaque";
    case HuntPhase::Abandon: return "Abandon";
    }
    return "?";
}

const char* postureLabelFr(Posture p)
{
    switch (p)
    {
    case Posture::Normal: return "Normale";
    case Posture::HeadUp: return "Tête levée";
    case Posture::HeadDown: return "Tête baissée";
    case Posture::Crouch: return "Accroupi";
    case Posture::Display: return "Parade";
    case Posture::Lying: return "Couché";
    case Posture::Swimming: return "Nage";
    }
    return "?";
}

const char* gaitLabelFr(Gait g)
{
    switch (g)
    {
    case Gait::Stand: return "Arrêt";
    case Gait::Walk: return "Marche";
    case Gait::FastWalk: return "Marche rapide";
    case Gait::Run: return "Course";
    case Gait::Swim: return "Nage";
    case Gait::Fly: return "Vol";
    }
    return "?";
}

namespace speciesimpl
{
void setRegions(SpeciesRuntime& rt, const float mass[kBodyRegionCount], const float loco[kBodyRegionCount])
{
    for (int i = 0; i < kBodyRegionCount; ++i)
    {
        rt.regions[static_cast<size_t>(i)].massFraction = mass[i];
        rt.regions[static_cast<size_t>(i)].locomotion = loco[i];
    }
    rt.regions[static_cast<size_t>(BodyRegion::Jaw)].feeding = 0.5f;
    rt.regions[static_cast<size_t>(BodyRegion::Head)].feeding = 0.3f;
    rt.regions[static_cast<size_t>(BodyRegion::Neck)].feeding = 0.2f;
    rt.regions[static_cast<size_t>(BodyRegion::Head)].lethality = 0.9f;
    rt.regions[static_cast<size_t>(BodyRegion::Neck)].lethality = 0.85f;
    rt.regions[static_cast<size_t>(BodyRegion::Torso)].lethality = 0.6f;
    rt.regions[static_cast<size_t>(BodyRegion::Ribs)].lethality = 0.55f;
    rt.regions[static_cast<size_t>(BodyRegion::Jaw)].lethality = 0.35f;
    rt.regions[static_cast<size_t>(BodyRegion::Tail)].lethality = 0.2f;
    for (BodyRegion r : {BodyRegion::ForelimbL, BodyRegion::ForelimbR, BodyRegion::HindlimbL, BodyRegion::HindlimbR})
    {
        rt.regions[static_cast<size_t>(r)].lethality = 0.3f;
    }
}
} // namespace speciesimpl

SpeciesRuntime buildSpeciesRuntime(const SpeciesDefinition& def, int index)
{
    SpeciesRuntime rt;
    rt.index = index;
    rt.def = &def;
    const SpeciesSim& s = def.sim;
    const BodyPlan bp = s.bodyPlan;
    rt.biped = isBiped(bp);
    rt.legCount = rt.biped ? 2 : 4;
    rt.isMammal = bp == BodyPlan::QuadrupedSmallMammal;
    rt.isDinosaur = bp != BodyPlan::QuadrupedCrocodylian && bp != BodyPlan::QuadrupedSmallMammal && bp != BodyPlan::QuadrupedAzhdarchid &&
                    bp != BodyPlan::AquaticReptile && bp != BodyPlan::SmallReptile && bp != BodyPlan::Fish && bp != BodyPlan::Invertebrate;
    rt.hasTeeth = bp == BodyPlan::BipedLargeTheropod || bp == BodyPlan::BipedSmallTheropod || bp == BodyPlan::QuadrupedCrocodylian ||
                  bp == BodyPlan::AquaticReptile;
    rt.meanAdultMassKg = s.adultMassKg;

    // Region mass fractions and locomotor contributions (game assumptions informed by body proportions).
    if (rt.biped)
    {
        const float mass[kBodyRegionCount] = {0.07f, 0.02f, 0.05f, 0.33f, 0.1f, 0.17f, 0.01f, 0.01f, 0.12f, 0.12f};
        const float loco[kBodyRegionCount] = {0.0f, 0.0f, 0.0f, 0.04f, 0.02f, 0.06f, 0.0f, 0.0f, 0.44f, 0.44f};
        speciesimpl::setRegions(rt, mass, loco);
        rt.footOffsets = {Vec2{0.0f, 0.045f}, Vec2{0.0f, -0.045f}, Vec2{}, Vec2{}};
        rt.walkPhase = {0.0f, 0.5f, 0.0f, 0.0f};
        rt.runPhase = {0.0f, 0.5f, 0.0f, 0.0f};
        rt.footIsLeft = {true, false, false, false};
        rt.footIsFore = {false, false, false, false};
        rt.yawInertiaK = 0.06f;
        rt.headReach = 0.42f;
        rt.bodyRadius = 0.1f;
        rt.eyeHeightFactor = 1.35f;
    }
    else
    {
        const float mass[kBodyRegionCount] = {0.1f, 0.02f, 0.05f, 0.36f, 0.1f, 0.09f, 0.07f, 0.07f, 0.07f, 0.07f};
        const float loco[kBodyRegionCount] = {0.0f, 0.0f, 0.0f, 0.04f, 0.02f, 0.02f, 0.21f, 0.21f, 0.25f, 0.25f};
        speciesimpl::setRegions(rt, mass, loco);
        const float fore = bp == BodyPlan::QuadrupedCrocodylian || bp == BodyPlan::QuadrupedSmallMammal ? 0.18f : 0.22f;
        rt.footOffsets = {Vec2{-0.08f, 0.06f}, Vec2{fore, 0.06f}, Vec2{-0.08f, -0.06f}, Vec2{fore, -0.06f}};
        // Lateral-sequence walk (LH, LF, RH, RF) and diagonal trot.
        rt.walkPhase = {0.0f, 0.25f, 0.5f, 0.75f};
        rt.runPhase = {0.0f, 0.5f, 0.5f, 0.0f};
        rt.footIsLeft = {true, true, false, false};
        rt.footIsFore = {false, true, false, true};
        rt.yawInertiaK = 0.05f;
        rt.headReach = 0.38f;
        rt.bodyRadius = 0.13f;
        rt.eyeHeightFactor = 1.1f;
    }

    // Weapons map onto body regions used for defence.
    if (s.hasWeapon(Weapon::Horns) || s.hasWeapon(Weapon::HeadButt) || s.hasWeapon(Weapon::Frill))
    {
        rt.regions[static_cast<size_t>(BodyRegion::Head)].defense += 0.6f;
        rt.regions[static_cast<size_t>(BodyRegion::Head)].armor = std::max(rt.regions[static_cast<size_t>(BodyRegion::Head)].armor, 0.4f);
    }
    if (s.hasWeapon(Weapon::Bite) || s.hasWeapon(Weapon::Beak))
    {
        rt.regions[static_cast<size_t>(BodyRegion::Jaw)].defense += 0.5f;
    }
    if (s.hasWeapon(Weapon::TailClub) || s.hasWeapon(Weapon::TailWhip))
    {
        rt.regions[static_cast<size_t>(BodyRegion::Tail)].defense += 0.7f;
        rt.regions[static_cast<size_t>(BodyRegion::Tail)].armor = std::max(rt.regions[static_cast<size_t>(BodyRegion::Tail)].armor, 0.5f);
    }
    if (s.hasWeapon(Weapon::Kick) || s.hasWeapon(Weapon::Claws))
    {
        rt.regions[static_cast<size_t>(BodyRegion::HindlimbL)].defense += 0.3f;
        rt.regions[static_cast<size_t>(BodyRegion::HindlimbR)].defense += 0.3f;
        rt.regions[static_cast<size_t>(BodyRegion::ForelimbL)].defense += 0.15f;
        rt.regions[static_cast<size_t>(BodyRegion::ForelimbR)].defense += 0.15f;
    }
    // Generic armour (osteoderms etc.) mostly protects the dorsum and flanks.
    for (BodyRegion r : {BodyRegion::Torso, BodyRegion::Ribs, BodyRegion::Neck, BodyRegion::Tail})
    {
        RegionInfo& ri = rt.regions[static_cast<size_t>(r)];
        ri.armor = std::max(ri.armor, s.defense.armor);
    }

    // Metabolism: Nagy 2005 field metabolic rate allometries (see Data/Science/methods.json).
    switch (s.metabolism.thermo)
    {
    case ThermoClass::Endotherm:
        rt.fmrA = rt.isMammal ? 4.82f : 10.5f;
        rt.fmrB = rt.isMammal ? 0.734f : 0.681f;
        rt.fmrMesoBlend = 0.0f;
        rt.targetBodyTempC = rt.isMammal ? 36.0f : 38.0f;
        break;
    case ThermoClass::Mesotherm:
        rt.fmrA = 10.5f;
        rt.fmrB = 0.681f;
        rt.fmrMesoBlend = 0.5f; // geometric mean of avian and reptilian rates
        rt.targetBodyTempC = 34.0f;
        break;
    case ThermoClass::Ectotherm:
        rt.fmrA = 0.196f;
        rt.fmrB = 0.889f;
        rt.fmrMesoBlend = 1.0f;
        rt.targetBodyTempC = 30.0f;
        break;
    }
    const bool carnivore = s.diet.type == DietType::Carnivore || s.diet.type == DietType::Piscivore;
    rt.gutCapacityFraction = carnivore ? 0.12f : 0.035f; // carnivores gorge on meat (wet mass); herbivores hold dry matter
    rt.gutRetentionHours = carnivore ? 20.0f : 40.0f;
    rt.toothSpacingMmPerM = bp == BodyPlan::BipedLargeTheropod ? 2.0f : (bp == BodyPlan::QuadrupedCrocodylian ? 3.0f : 2.4f);

    // Soft tissue natural frequencies scale with body length (larger = slower oscillation).
    const float lenScale = std::pow(std::max(0.2f, s.adultLengthM), -0.5f);
    rt.soft[static_cast<size_t>(SoftTissueNode::BellyVertical)] = {3.2f * lenScale, 0.22f, 0.012f};
    rt.soft[static_cast<size_t>(SoftTissueNode::BellyLateral)] = {2.6f * lenScale, 0.28f, 0.008f};
    rt.soft[static_cast<size_t>(SoftTissueNode::NeckVertical)] = {2.2f * lenScale, 0.4f, 0.02f};
    rt.soft[static_cast<size_t>(SoftTissueNode::NeckLateral)] = {1.8f * lenScale, 0.45f, 0.02f};
    rt.soft[static_cast<size_t>(SoftTissueNode::TailVertical)] = {1.6f * lenScale, 0.35f, 0.03f};
    rt.soft[static_cast<size_t>(SoftTissueNode::TailLateral)] = {1.2f * lenScale, 0.35f, 0.04f};
    return rt;
}

float fieldMetabolicRateW(const SpeciesRuntime& sp, float massKg)
{
    const float grams = std::max(1.0f, massKg * 1000.0f);
    const float kJPerDay = sp.fmrA * std::pow(grams, sp.fmrB);
    float watts = kJPerDay * 1000.0f / 86400.0f;
    if (sp.fmrMesoBlend > 0.0f && sp.fmrMesoBlend < 1.0f)
    {
        const float reptile = 0.196f * std::pow(grams, 0.889f) * 1000.0f / 86400.0f;
        watts = std::exp(lerpf(std::log(watts), std::log(reptile), sp.fmrMesoBlend));
    }
    return watts;
}
} // namespace noctis
