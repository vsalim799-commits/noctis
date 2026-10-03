#include "Noctis/Science/Taxonomy.h"

#include "Noctis/Science/Confidence.h"

namespace noctis
{
namespace taxonomyimpl
{
const char* const kBodyPlanKeys[] = {
    "biped_large_theropod", "biped_small_theropod", "biped_ornithomimid", "biped_oviraptorosaur",
    "biped_small_ornithischian", "biped_pachycephalosaur", "facultative_quadruped_hadrosaur",
    "quadruped_ceratopsian", "quadruped_ankylosaur", "quadruped_crocodylian", "quadruped_small_mammal",
    "quadruped_azhdarchid", "aquatic_reptile", "bird_flyer", "small_reptile", "fish", "invertebrate"};

const char* const kPlantKeys[] = {"fern", "horsetail", "angio_herb", "angio_shrub", "angio_tree", "conifer",
                                  "palm", "cycadophyte", "ginkgo", "aquatic_macrophyte", "xeric_shrub"};
const char* const kPlantLabelsFr[] = {"Fougères", "Prêles", "Angiospermes herbacées", "Arbustes angiospermes",
                                      "Arbres angiospermes", "Conifères", "Palmiers", "Cycadophytes", "Ginkgos",
                                      "Macrophytes aquatiques", "Arbustes xériques"};

const char* const kHabitatKeys[] = {"channel", "riverbank", "sandbar", "oxbow_lake", "backswamp", "riparian_forest",
                                    "floodplain_open", "levee_woodland", "upland_conifer_forest", "cutbank_cliff",
                                    "dune_field", "interdune_flat", "ephemeral_pond", "xeric_scrub"};
const char* const kHabitatLabelsFr[] = {"Chenal", "Berge", "Banc de sable", "Bras mort", "Marais d'arrière-berge",
                                        "Forêt riveraine", "Plaine d'inondation ouverte", "Boisement de levée",
                                        "Forêt de conifères des terrasses", "Falaise de rive concave",
                                        "Champ de dunes", "Zone interdunaire", "Mare temporaire", "Fourré xérique"};

const char* const kPersonalityKeys[] = {"boldness", "caution", "aggression", "curiosity", "sociability",
                                        "vigilance", "risk_tolerance", "exploration", "flight_tendency", "dominance"};
} // namespace taxonomyimpl

const char* confidenceKey(Confidence c)
{
    switch (c)
    {
    case Confidence::Established: return "established";
    case Confidence::StronglySupported: return "strongly_supported";
    case Confidence::Inferred: return "inferred";
    case Confidence::Speculative: return "speculative";
    case Confidence::Unknown: return "unknown";
    }
    return "unknown";
}

const char* confidenceLabelFr(Confidence c)
{
    switch (c)
    {
    case Confidence::Established: return "ÉTABLI";
    case Confidence::StronglySupported: return "FORTEMENT ÉTAYÉ";
    case Confidence::Inferred: return "INFÉRÉ / PLAUSIBLE";
    case Confidence::Speculative: return "SPÉCULATIF";
    case Confidence::Unknown: return "INCONNU";
    }
    return "INCONNU";
}

Confidence parseConfidence(const std::string& key, bool* ok)
{
    if (ok)
    {
        *ok = true;
    }
    if (key == "established")
    {
        return Confidence::Established;
    }
    if (key == "strongly_supported")
    {
        return Confidence::StronglySupported;
    }
    if (key == "inferred")
    {
        return Confidence::Inferred;
    }
    if (key == "speculative")
    {
        return Confidence::Speculative;
    }
    if (key != "unknown" && ok)
    {
        *ok = false;
    }
    return Confidence::Unknown;
}

const char* bodyPlanKey(BodyPlan b)
{
    const int i = static_cast<int>(b);
    return (i >= 0 && i < static_cast<int>(BodyPlan::Count)) ? taxonomyimpl::kBodyPlanKeys[i] : "unknown";
}

BodyPlan parseBodyPlan(const std::string& s, bool* ok)
{
    for (int i = 0; i < static_cast<int>(BodyPlan::Count); ++i)
    {
        if (s == taxonomyimpl::kBodyPlanKeys[i])
        {
            if (ok)
            {
                *ok = true;
            }
            return static_cast<BodyPlan>(i);
        }
    }
    if (ok)
    {
        *ok = false;
    }
    return BodyPlan::BipedSmallOrnithischian;
}

const char* plantTypeKey(PlantType p)
{
    const int i = static_cast<int>(p);
    return (i >= 0 && i < kPlantTypeCount) ? taxonomyimpl::kPlantKeys[i] : "unknown";
}

const char* plantTypeLabelFr(PlantType p)
{
    const int i = static_cast<int>(p);
    return (i >= 0 && i < kPlantTypeCount) ? taxonomyimpl::kPlantLabelsFr[i] : "?";
}

int parsePlantType(const std::string& s)
{
    for (int i = 0; i < kPlantTypeCount; ++i)
    {
        if (s == taxonomyimpl::kPlantKeys[i])
        {
            return i;
        }
    }
    return -1;
}

const char* habitatKey(Habitat h)
{
    const int i = static_cast<int>(h);
    return (i >= 0 && i < kHabitatCount) ? taxonomyimpl::kHabitatKeys[i] : "unknown";
}

const char* habitatLabelFr(Habitat h)
{
    const int i = static_cast<int>(h);
    return (i >= 0 && i < kHabitatCount) ? taxonomyimpl::kHabitatLabelsFr[i] : "?";
}

int parseHabitat(const std::string& s)
{
    for (int i = 0; i < kHabitatCount; ++i)
    {
        if (s == taxonomyimpl::kHabitatKeys[i])
        {
            return i;
        }
    }
    return -1;
}

const char* personalityKey(PersonalityAxis a)
{
    const int i = static_cast<int>(a);
    return (i >= 0 && i < kPersonalityAxisCount) ? taxonomyimpl::kPersonalityKeys[i] : "unknown";
}

ThermoClass parseThermoClass(const std::string& s)
{
    if (s == "ectotherm")
    {
        return ThermoClass::Ectotherm;
    }
    if (s == "mesotherm")
    {
        return ThermoClass::Mesotherm;
    }
    return ThermoClass::Endotherm;
}

DietType parseDietType(const std::string& s)
{
    if (s == "carnivore")
    {
        return DietType::Carnivore;
    }
    if (s == "omnivore")
    {
        return DietType::Omnivore;
    }
    if (s == "piscivore")
    {
        return DietType::Piscivore;
    }
    if (s == "insectivore")
    {
        return DietType::Insectivore;
    }
    return DietType::Herbivore;
}

const char* dietTypeKey(DietType d)
{
    switch (d)
    {
    case DietType::Carnivore: return "carnivore";
    case DietType::Herbivore: return "herbivore";
    case DietType::Omnivore: return "omnivore";
    case DietType::Piscivore: return "piscivore";
    case DietType::Insectivore: return "insectivore";
    }
    return "herbivore";
}

HuntStyle parseHuntStyle(const std::string& s)
{
    if (s == "ambush")
    {
        return HuntStyle::Ambush;
    }
    if (s == "stalk_rush")
    {
        return HuntStyle::StalkRush;
    }
    if (s == "pursuit")
    {
        return HuntStyle::Pursuit;
    }
    if (s == "probe_forage")
    {
        return HuntStyle::ProbeForage;
    }
    if (s == "aerial_ground_stalk")
    {
        return HuntStyle::AerialGroundStalk;
    }
    return HuntStyle::None;
}

NestType parseNestType(const std::string& s)
{
    if (s == "mound")
    {
        return NestType::Mound;
    }
    if (s == "open_scrape")
    {
        return NestType::OpenScrape;
    }
    if (s == "brooded")
    {
        return NestType::Brooded;
    }
    if (s == "buried")
    {
        return NestType::Buried;
    }
    if (s == "live_birth")
    {
        return NestType::LiveBirth;
    }
    return NestType::None;
}

ActivityPattern parseActivityPattern(const std::string& s)
{
    if (s == "nocturnal")
    {
        return ActivityPattern::Nocturnal;
    }
    if (s == "crepuscular")
    {
        return ActivityPattern::Crepuscular;
    }
    if (s == "cathemeral")
    {
        return ActivityPattern::Cathemeral;
    }
    return ActivityPattern::Diurnal;
}

const char* activityPatternKey(ActivityPattern a)
{
    switch (a)
    {
    case ActivityPattern::Diurnal: return "diurnal";
    case ActivityPattern::Nocturnal: return "nocturnal";
    case ActivityPattern::Crepuscular: return "crepuscular";
    case ActivityPattern::Cathemeral: return "cathemeral";
    }
    return "diurnal";
}

u16 parseWeapon(const std::string& s)
{
    if (s == "bite")
    {
        return static_cast<u16>(Weapon::Bite);
    }
    if (s == "horns")
    {
        return static_cast<u16>(Weapon::Horns);
    }
    if (s == "frill")
    {
        return static_cast<u16>(Weapon::Frill);
    }
    if (s == "tail_club")
    {
        return static_cast<u16>(Weapon::TailClub);
    }
    if (s == "tail_whip")
    {
        return static_cast<u16>(Weapon::TailWhip);
    }
    if (s == "kick")
    {
        return static_cast<u16>(Weapon::Kick);
    }
    if (s == "claws")
    {
        return static_cast<u16>(Weapon::Claws);
    }
    if (s == "head_butt")
    {
        return static_cast<u16>(Weapon::HeadButt);
    }
    if (s == "beak")
    {
        return static_cast<u16>(Weapon::Beak);
    }
    return 0;
}

CallContext parseCallContext(const std::string& s)
{
    // Accept both bare keys and descriptive strings that start with a key.
    auto starts = [&](const char* k) { return s.rfind(k, 0) == 0; };
    if (starts("contact") || starts("social_contact"))
    {
        return CallContext::Contact;
    }
    if (starts("alarm"))
    {
        return CallContext::Alarm;
    }
    if (starts("distress"))
    {
        return CallContext::Distress;
    }
    if (starts("threat") || starts("agonistic") || starts("warning"))
    {
        return CallContext::Threat;
    }
    if (starts("courtship") || starts("mating"))
    {
        return CallContext::Courtship;
    }
    if (starts("begging") || starts("juvenile"))
    {
        return CallContext::Begging;
    }
    if (starts("territorial"))
    {
        return CallContext::Territorial;
    }
    return CallContext::Other;
}

const char* callContextKey(CallContext c)
{
    switch (c)
    {
    case CallContext::Contact: return "contact";
    case CallContext::Alarm: return "alarm";
    case CallContext::Distress: return "distress";
    case CallContext::Threat: return "threat";
    case CallContext::Courtship: return "courtship";
    case CallContext::Begging: return "begging";
    case CallContext::Territorial: return "territorial";
    case CallContext::Other: return "other";
    }
    return "other";
}
} // namespace noctis
