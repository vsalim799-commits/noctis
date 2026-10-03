#include "Noctis/Research/Paleontology.h"

#include "Noctis/Core/Random.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace noctis
{
const char* specimenElementLabelFr(SpecimenElement e)
{
    switch (e)
    {
    case SpecimenElement::Tooth: return "Dent";
    case SpecimenElement::Vertebra: return "Vertèbre";
    case SpecimenElement::Femur: return "Fémur";
    case SpecimenElement::Tibia: return "Tibia";
    case SpecimenElement::Rib: return "Côte";
    case SpecimenElement::HornCore: return "Cheville osseuse de corne";
    case SpecimenElement::FrillFragment: return "Fragment de collerette";
    case SpecimenElement::Osteoderm: return "Ostéoderme";
    case SpecimenElement::TailClub: return "Massue caudale";
    case SpecimenElement::Ungual: return "Phalange unguéale";
    case SpecimenElement::SkullFragment: return "Fragment crânien";
    case SpecimenElement::DentalBattery: return "Batterie dentaire";
    case SpecimenElement::Eggshell: return "Coquille d'œuf";
    case SpecimenElement::Coprolite: return "Coprolithe";
    case SpecimenElement::Count: break;
    }
    return "?";
}

namespace paleoimpl
{
std::vector<SpecimenElement> elementsFor(BodyPlan bp)
{
    using E = SpecimenElement;
    switch (bp)
    {
    case BodyPlan::BipedLargeTheropod:
    case BodyPlan::BipedSmallTheropod: return {E::Tooth, E::Tooth, E::Tooth, E::Vertebra, E::Femur, E::Tibia, E::Rib, E::Ungual, E::SkullFragment};
    case BodyPlan::QuadrupedCeratopsian: return {E::HornCore, E::FrillFragment, E::FrillFragment, E::Vertebra, E::Femur, E::Rib, E::Tooth};
    case BodyPlan::FacultativeQuadrupedHadrosaur: return {E::DentalBattery, E::Vertebra, E::Vertebra, E::Femur, E::Tibia, E::Rib};
    case BodyPlan::QuadrupedAnkylosaur: return {E::Osteoderm, E::Osteoderm, E::TailClub, E::Vertebra, E::Tooth};
    case BodyPlan::BipedPachycephalosaur: return {E::SkullFragment, E::SkullFragment, E::Vertebra, E::Tooth};
    case BodyPlan::QuadrupedCrocodylian:
    case BodyPlan::AquaticReptile: return {E::Osteoderm, E::Tooth, E::Tooth, E::Vertebra};
    case BodyPlan::QuadrupedSmallMammal: return {E::Tooth};
    default: return {E::Vertebra, E::Femur, E::Tooth, E::Rib};
    }
}

std::string groupLabelFr(BodyPlan bp, float massKg)
{
    switch (bp)
    {
    case BodyPlan::BipedLargeTheropod: return "Tyrannosauridae indét.";
    case BodyPlan::BipedSmallTheropod: return massKg < 100.0f ? "Dromaeosauridae indét." : "Theropoda indét.";
    case BodyPlan::BipedOrnithomimid: return "Ornithomimidae indét.";
    case BodyPlan::BipedOviraptorosaur: return "Oviraptorosauria indét.";
    case BodyPlan::QuadrupedCeratopsian: return "Ceratopsidae indét.";
    case BodyPlan::FacultativeQuadrupedHadrosaur: return "Hadrosauridae indét.";
    case BodyPlan::QuadrupedAnkylosaur: return "Ankylosauria indét.";
    case BodyPlan::BipedPachycephalosaur: return "Pachycephalosauridae indét.";
    case BodyPlan::BipedSmallOrnithischian: return "Ornithischia indét. (petit bipède)";
    case BodyPlan::QuadrupedCrocodylian: return "Crocodyliformes indét.";
    case BodyPlan::AquaticReptile: return "Reptile aquatique indét.";
    case BodyPlan::QuadrupedSmallMammal: return "Mammalia indét.";
    case BodyPlan::QuadrupedAzhdarchid: return "Azhdarchidae indét.";
    default: return "Vertebrata indét.";
    }
}

// Elements that are diagnostic at genus level for their clade.
bool diagnostic(SpecimenElement e)
{
    return e == SpecimenElement::HornCore || e == SpecimenElement::FrillFragment || e == SpecimenElement::TailClub ||
           e == SpecimenElement::DentalBattery || e == SpecimenElement::SkullFragment;
}
} // namespace paleoimpl

void PaleontologySystem::generateSites(const WorldLayout& layout, const EvidenceDatabase& db, const EnvironmentDefinition& env,
                                       const std::vector<int>& agentSpecies, u64 seed)
{
    sites_.clear();
    specimens_.clear();
    Rng rng(seed, 0xF055);
    std::vector<float> weights;
    for (const int si : agentSpecies)
    {
        const SpeciesDefinition& def = db.speciesAt(si);
        const JsonValue& w = db.simConstants().path("fossil_census_weights." + env.id + "." + def.id);
        float weight = w.isNumber() ? w.asFloat() : (w.isObject() ? w.getFloat("value", 0.0f) : -1.0f);
        if (weight < 0.0f)
        {
            // Default: abundance x preservation bias towards large, robust skeletons (game assumption).
            weight = std::sqrt(std::max(1.0f, def.sim.population.initialCount)) * std::pow(std::max(1.0f, def.sim.adultMassKg), 0.3f) * 0.2f;
        }
        weights.push_back(weight);
    }
    float sum = 0.0f;
    for (const float w : weights)
    {
        sum += w;
    }
    u32 nextSpecimen = 1;
    for (size_t s = 0; s < layout.exposureSites.size(); ++s)
    {
        FossilSite site;
        site.id = static_cast<u32>(s + 1);
        site.position = layout.exposureSites[s];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Affleurement %c", static_cast<char>('A' + static_cast<int>(s % 26)));
        site.nameFr = buf;
        const int count = rng.rangeInt(5, 14);
        for (int k = 0; k < count && sum > 0.0f; ++k)
        {
            float u = rng.uniform() * sum;
            size_t pick = 0;
            for (size_t i = 0; i < weights.size(); ++i)
            {
                u -= weights[i];
                if (u <= 0.0f)
                {
                    pick = i;
                    break;
                }
            }
            const SpeciesDefinition& def = db.speciesAt(agentSpecies[pick]);
            const auto elems = paleoimpl::elementsFor(def.sim.bodyPlan);
            FossilSpecimen sp;
            sp.id = nextSpecimen++;
            sp.siteId = site.id;
            sp.element = elems[static_cast<size_t>(rng.rangeInt(0, static_cast<int>(elems.size()) - 1))];
            if (rng.chance(0.06f))
            {
                sp.element = SpecimenElement::Eggshell;
            }
            sp.truthSpecies = static_cast<i16>(agentSpecies[pick]);
            sp.completeness = rng.range(0.15f, 0.95f);
            sp.weathering = rng.rangeInt(1, 5);
            sp.position = site.position + Vec2{rng.range(-30.0f, 30.0f), rng.range(-30.0f, 30.0f)};
            sp.sizeCm = std::cbrt(def.sim.adultMassKg) * (sp.element == SpecimenElement::Tooth ? 0.8f : 6.0f) * rng.range(0.5f, 1.1f);
            // Tooth-marked bone (feeding traces) occur on a minority of specimens.
            if (sp.element != SpecimenElement::Tooth && sp.element != SpecimenElement::Eggshell && rng.chance(0.12f))
            {
                for (const int pi : agentSpecies)
                {
                    const SpeciesDefinition& pred = db.speciesAt(pi);
                    if (pred.sim.bodyPlan == BodyPlan::BipedLargeTheropod && rng.chance(0.7f))
                    {
                        BiteMark m;
                        m.makerSpecies = static_cast<i16>(pi);
                        m.type = rng.chance(0.5f) ? BiteMarkType::Score : BiteMarkType::Puncture;
                        m.toothSpacingMm = 2.0f * pred.sim.adultLengthM * rng.range(0.6f, 1.0f);
                        m.perimortem = rng.chance(0.85f);
                        sp.marks.push_back(m);
                        break;
                    }
                }
            }
            site.specimens.push_back(sp.id);
            specimens_.push_back(sp);
        }
        // Large-theropod coprolites with bone inclusions are known from the latest Cretaceous; one per site at most.
        if (rng.chance(0.25f))
        {
            for (const int pi : agentSpecies)
            {
                if (db.speciesAt(pi).sim.bodyPlan == BodyPlan::BipedLargeTheropod)
                {
                    FossilSpecimen cop;
                    cop.id = nextSpecimen++;
                    cop.siteId = site.id;
                    cop.element = SpecimenElement::Coprolite;
                    cop.truthSpecies = static_cast<i16>(pi);
                    cop.completeness = rng.range(0.3f, 0.9f);
                    cop.position = site.position + Vec2{rng.range(-20.0f, 20.0f), rng.range(-20.0f, 20.0f)};
                    cop.sizeCm = rng.range(15.0f, 40.0f);
                    site.specimens.push_back(cop.id);
                    specimens_.push_back(cop);
                    break;
                }
            }
        }
        sites_.push_back(site);
    }
}

FossilSpecimen* PaleontologySystem::specimen(u32 id)
{
    for (FossilSpecimen& s : specimens_)
    {
        if (s.id == id)
        {
            return &s;
        }
    }
    return nullptr;
}

const FossilSite* PaleontologySystem::site(u32 id) const
{
    for (const FossilSite& s : sites_)
    {
        if (s.id == id)
        {
            return &s;
        }
    }
    return nullptr;
}

bool PaleontologySystem::excavate(u32 specimenId, float hours, float toolQuality)
{
    FossilSpecimen* s = specimen(specimenId);
    if (!s || s->collected)
    {
        return s != nullptr && s->collected;
    }
    // Larger, more fragile specimens take longer (plaster jackets etc.).
    const float hoursNeeded = 0.5f + s->sizeCm / 15.0f * (1.5f - 0.5f * s->completeness);
    s->excavation = saturate(s->excavation + hours * std::max(0.1f, toolQuality) / hoursNeeded);
    if (s->excavation >= 1.0f)
    {
        s->collected = true;
    }
    return s->collected;
}

void PaleontologySystem::identify(u32 specimenId, const EvidenceDatabase& db)
{
    FossilSpecimen* s = specimen(specimenId);
    if (!s || !s->collected || s->truthSpecies < 0)
    {
        return;
    }
    const SpeciesDefinition& def = db.speciesAt(s->truthSpecies);
    s->identified = true;
    if (s->element == SpecimenElement::Eggshell)
    {
        s->identificationFr = "Coquille d'œuf (parataxon oologique) — attribution à un producteur incertaine";
        s->identificationConfidence = Confidence::Speculative;
        return;
    }
    if (s->element == SpecimenElement::Coprolite)
    {
        s->identificationFr = "Coprolithe de grand carnivore à inclusions osseuses — producteur probable : grand théropode";
        s->identificationConfidence = Confidence::Inferred;
        return;
    }
    if (paleoimpl::diagnostic(s->element) && s->completeness > 0.4f)
    {
        s->identifiedSpecies = s->truthSpecies;
        s->identificationFr = def.scientificName + " (" + def.commonNameFr + ")";
        s->identificationConfidence = s->completeness > 0.7f ? Confidence::StronglySupported : Confidence::Inferred;
        return;
    }
    s->identificationFr = paleoimpl::groupLabelFr(def.sim.bodyPlan, def.sim.adultMassKg);
    s->identificationConfidence = s->completeness > 0.6f ? Confidence::Inferred : Confidence::Speculative;
}

std::vector<u32> PaleontologySystem::extractTrackway(u32 seedFootprint, const TrackSystem& tracks) const
{
    std::vector<u32> out;
    const Footprint* seed = tracks.find(seedFootprint);
    if (!seed)
    {
        return out;
    }
    std::vector<const Footprint*> near;
    tracks.query(seed->position, 30.0f, near);
    const Vec2 dir = Vec2::fromHeading(seed->heading);
    for (const Footprint* f : near)
    {
        if (!tracks.visible(*f) || f->manus)
        {
            continue;
        }
        const float sizeRatio = f->lengthM / std::max(0.01f, seed->lengthM);
        const float headingDiff = std::fabs(angleDelta(seed->heading, f->heading));
        const float lateral = std::fabs(cross(dir, f->position - seed->position));
        if (sizeRatio > 0.8f && sizeRatio < 1.25f && headingDiff < 0.6f && lateral < 1.0f + 1.5f * seed->lengthM)
        {
            out.push_back(f->id);
        }
    }
    return out;
}

TrackwayAnalysis PaleontologySystem::analyzeTrackway(const std::vector<u32>& footprintIds, const TrackSystem& tracks, const EvidenceDatabase& db) const
{
    TrackwayAnalysis a;
    std::vector<const Footprint*> prints;
    for (const u32 id : footprintIds)
    {
        if (const Footprint* f = tracks.find(id))
        {
            if (!f->manus)
            {
                prints.push_back(f);
            }
        }
    }
    a.footprints = static_cast<int>(prints.size());
    a.footprintIds = footprintIds;
    if (prints.size() < 3)
    {
        a.caveatsFr.push_back("Moins de trois empreintes : la foulée ne peut pas être mesurée.");
        return a;
    }
    // Principal direction of travel and ordering along it.
    Vec2 c;
    for (const Footprint* f : prints)
    {
        c += f->position;
    }
    c = c / static_cast<float>(prints.size());
    Vec2 dir;
    for (const Footprint* f : prints)
    {
        dir += Vec2::fromHeading(f->heading);
    }
    dir = dir.normalized();
    std::sort(prints.begin(), prints.end(), [&](const Footprint* x, const Footprint* y) { return dot(x->position - c, dir) < dot(y->position - c, dir); });
    float fl = 0.0f;
    float fw = 0.0f;
    float trueSpeed = 0.0f;
    for (const Footprint* f : prints)
    {
        fl += f->lengthM;
        fw += f->widthM;
        trueSpeed += f->makerSpeedMs;
    }
    a.meanFootLengthM = fl / static_cast<float>(prints.size());
    a.meanFootWidthM = fw / static_cast<float>(prints.size());
    a.trueSpeedMs = trueSpeed / static_cast<float>(prints.size());
    // Stride: same foot to same foot (every second print along the line).
    float strideSum = 0.0f;
    int strideN = 0;
    for (size_t i = 0; i + 2 < prints.size(); ++i)
    {
        strideSum += distance(prints[i]->position, prints[i + 2]->position);
        ++strideN;
    }
    a.strideM = strideN > 0 ? strideSum / static_cast<float>(strideN) : 2.0f * distance(prints[0]->position, prints[1]->position);
    float angSum = 0.0f;
    int angN = 0;
    for (size_t i = 1; i + 1 < prints.size(); ++i)
    {
        const Vec2 u = (prints[i - 1]->position - prints[i]->position).normalized();
        const Vec2 v = (prints[i + 1]->position - prints[i]->position).normalized();
        angSum += std::acos(clampf(dot(u, v), -1.0f, 1.0f)) * kRadToDeg;
        ++angN;
    }
    a.paceAngulationDeg = angN > 0 ? angSum / static_cast<float>(angN) : 0.0f;

    // Hip height from foot length (Alexander 1976; Thulborn 1990 ratios for theropods/ornithopods).
    const JsonValue* params = nullptr;
    for (const JsonValue& m : db.methods().get("methods").items())
    {
        if (m.getString("id") == "trackway_speed_alexander")
        {
            params = &m.get("parameters");
        }
    }
    float k = 4.0f;
    const bool elongated = a.meanFootLengthM > a.meanFootWidthM * 1.15f;
    if (params)
    {
        const JsonValue& th = params->get("hip_to_foot_ratio_thulborn").get("value");
        if (elongated)
        {
            k = a.meanFootLengthM > 0.25f ? th.getFloat("large_theropod", 4.9f) : th.getFloat("small_theropod", 4.5f);
        }
        else
        {
            k = th.getFloat("small_ornithopod", params->get("hip_to_foot_ratio_default").getFloat("value", 4.0f));
        }
    }
    a.hipToFootRatio = k;
    a.hipHeightEstM = k * a.meanFootLengthM;
    const float coef = params ? params->get("coef_alexander").getFloat("value", 0.25f) : 0.25f;
    const float es = params ? params->get("exp_stride").getFloat("value", 1.67f) : 1.67f;
    const float eh = params ? params->get("exp_hip").getFloat("value", -1.17f) : -1.17f;
    auto speedFor = [&](float h) { return coef * std::sqrt(kGravity) * std::pow(a.strideM, es) * std::pow(std::max(0.01f, h), eh); };
    a.speedEstMs = speedFor(a.hipHeightEstM);
    a.speedLowMs = speedFor(a.hipHeightEstM * 1.2f);
    a.speedHighMs = speedFor(a.hipHeightEstM * 0.8f);
    a.relativeStride = a.strideM / std::max(0.01f, a.hipHeightEstM);
    const float walkMax = params ? params->get("gait_walk_max_relative_stride").getFloat("value", 2.0f) : 2.0f;
    const float runMin = params ? params->get("gait_run_min_relative_stride").getFloat("value", 2.9f) : 2.9f;
    a.gaitFr = a.relativeStride < walkMax ? "Marche" : (a.relativeStride > runMin ? "Course" : "Allure intermédiaire (trot)");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s, empreinte de %.0f cm", elongated ? "Bipède tridactyle (morphotype théropodien)" : "Empreinte large (morphotype ornithopode ou quadrupède)",
                  a.meanFootLengthM * 100.0f);
    a.makerGuessFr = buf;
    a.caveatsFr.push_back("La formule d'Alexander (1976) estime la vitesse au moment du passage, pas la vitesse maximale.");
    a.caveatsFr.push_back("Une erreur de ±20 % sur la hauteur de hanche donne environ −19 % à +30 % d'erreur sur la vitesse.");
    bool soft = false;
    for (const Footprint* f : prints)
    {
        soft = soft || f->substrate == Substrate::Mud || f->substrate == Substrate::Peat || (f->substrate == Substrate::Clay && f->depthM > 0.05f);
    }
    if (soft)
    {
        a.caveatsFr.push_back("Substrat meuble : la vitesse peut être surestimée jusqu'à ~2,5× (Prescott et al. 2025).");
    }
    if (!elongated)
    {
        a.caveatsFr.push_back("La règle h ≈ k·FL est validée surtout pour des bipèdes ; elle est fragile pour les quadrupèdes.");
    }
    if (prints.size() < 5)
    {
        a.caveatsFr.push_back("Piste courte : moyenne calculée sur peu de foulées.");
    }
    a.valid = true;
    return a;
}

BiteMarkAnalysis PaleontologySystem::analyzeBiteMarks(const std::vector<BiteMark>& marks, const EvidenceDatabase& db) const
{
    BiteMarkAnalysis a;
    float spacing = 0.0f;
    int n = 0;
    for (const BiteMark& m : marks)
    {
        ++a.marks;
        a.healedMarks += m.perimortem ? 0 : 1;
        if (m.toothSpacingMm > 0.0f)
        {
            spacing += m.toothSpacingMm;
            ++n;
        }
    }
    if (n > 0)
    {
        a.meanToothSpacingMm = spacing / static_cast<float>(n);
        // Calibration against the reference collection (tooth spacing vs body length; game assumption ~2 mm per m).
        a.predatorLengthEstM = a.meanToothSpacingMm / 2.0f;
        a.predatorLengthLowM = a.predatorLengthEstM * 0.7f;
        a.predatorLengthHighM = a.predatorLengthEstM * 1.3f;
        std::string cands;
        for (const SpeciesDefinition& d : db.species())
        {
            if (d.sim.predation.style == HuntStyle::None || d.sim.role != SimRole::Agent)
            {
                continue;
            }
            if (d.sim.adultLengthM >= a.predatorLengthLowM * 0.8f)
            {
                if (!cands.empty())
                {
                    cands += ", ";
                }
                cands += d.scientificName + (d.sim.adultLengthM > a.predatorLengthHighM * 1.3f ? " (juvénile possible)" : "");
            }
        }
        a.candidatesFr = cands.empty() ? std::string("Aucun prédateur connu de cette taille") : cands;
    }
    if (a.healedMarks > 0)
    {
        a.interpretationFr = "Des traces cicatrisées montrent que l'animal a survécu à au moins une morsure. ";
    }
    if (a.marks - a.healedMarks > 0)
    {
        a.interpretationFr += "Des traces non cicatrisées (péri- ou post-mortem) ne permettent pas, seules, de distinguer prédation et charognage.";
    }
    return a;
}

CarcassExamination PaleontologySystem::examineCarcass(const Carcass& c, double now, float meanTempC, const EvidenceDatabase& db) const
{
    CarcassExamination e;
    e.stageFr = decayStageLabelFr(c.stage);
    // Observed total body score (with scoring error) inverted through Megyesi et al. 2005 (± 388 ADD).
    const float tbs = CarcassSystem::totalBodyScore(c.accumulatedDegreeDays);
    const float add = std::pow(10.0f, 0.002f * tbs * tbs + 1.81f);
    const float t = std::max(1.0f, meanTempC);
    e.timeSinceDeathDays = add / t;
    e.timeSinceDeathLowDays = std::max(0.0f, (add - 388.16f) / t);
    e.timeSinceDeathHighDays = (add + 388.16f) / t;
    e.weatheringStage = c.weatheringStage;
    e.shedTeeth = c.shedTeeth;
    e.scatter = c.scatter;
    e.softTissueKg = c.softTissueKg;
    e.estimatedMassKg = c.massAtDeathKg * (0.8f + 0.4f * static_cast<float>((c.id.serial() * 2654435761u) % 100u) / 100.0f);
    e.bites = analyzeBiteMarks(c.marks, db);
    e.notesFr.push_back("Estimation du délai depuis la mort : méthode des degrés-jours (Megyesi et al. 2005), établie sur des restes humains — à utiliser comme ordre de grandeur.");
    if (c.shedTeeth > 0)
    {
        e.notesFr.push_back("Dents isolées autour de la carcasse : des théropodes ou crocodyliformes s'y sont nourris (remplacement dentaire).");
    }
    if (c.scatter > 0.5f)
    {
        e.notesFr.push_back("Squelette largement désarticulé : forte activité de charognards.");
    }
    (void)now;
    return e;
}
} // namespace noctis
