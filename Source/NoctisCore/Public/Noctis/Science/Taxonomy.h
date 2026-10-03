// Enumerations shared by the scientific data and the simulation (body plans, habitats,
// plant functional types, diets...). String keys match Data/SCHEMA.md exactly.
#pragma once

#include "Noctis/Core/Platform.h"

#include <string>

namespace noctis
{
enum class SimRole : u8
{
    Agent,      // simulated as persistent individuals
    Population  // simulated as abundances (fish, insects, small vertebrate assemblages)
};

enum class BodyPlan : u8
{
    BipedLargeTheropod,
    BipedSmallTheropod,
    BipedOrnithomimid,
    BipedOviraptorosaur,
    BipedSmallOrnithischian,
    BipedPachycephalosaur,
    FacultativeQuadrupedHadrosaur,
    QuadrupedCeratopsian,
    QuadrupedAnkylosaur,
    QuadrupedCrocodylian,
    QuadrupedSmallMammal,
    QuadrupedAzhdarchid,
    AquaticReptile,
    BirdFlyer,
    SmallReptile,
    Fish,
    Invertebrate,
    Count
};

enum class ThermoClass : u8
{
    Endotherm,
    Mesotherm,
    Ectotherm
};

enum class DietType : u8
{
    Carnivore,
    Herbivore,
    Omnivore,
    Piscivore,
    Insectivore
};

enum class HuntStyle : u8
{
    None,
    Ambush,
    StalkRush,
    Pursuit,
    ProbeForage,
    AerialGroundStalk
};

enum class NestType : u8
{
    Mound,
    OpenScrape,
    Brooded,
    Buried,
    LiveBirth,
    None
};

enum class ActivityPattern : u8
{
    Diurnal,
    Nocturnal,
    Crepuscular,
    Cathemeral
};

enum class PlantType : u8
{
    Fern,
    Horsetail,
    AngioHerb,
    AngioShrub,
    AngioTree,
    Conifer,
    Palm,
    Cycadophyte,
    Ginkgo,
    AquaticMacrophyte,
    XericShrub,
    Count
};
inline constexpr int kPlantTypeCount = static_cast<int>(PlantType::Count);

enum class Habitat : u8
{
    Channel,
    Riverbank,
    Sandbar,
    OxbowLake,
    Backswamp,
    RiparianForest,
    FloodplainOpen,
    LeveeWoodland,
    UplandConiferForest,
    CutbankCliff,
    DuneField,
    InterduneFlat,
    EphemeralPond,
    XericScrub,
    Count
};
inline constexpr int kHabitatCount = static_cast<int>(Habitat::Count);

enum class PersonalityAxis : u8
{
    Boldness,
    Caution,
    Aggression,
    Curiosity,
    Sociability,
    Vigilance,
    RiskTolerance,
    Exploration,
    FlightTendency,
    Dominance,
    Count
};
inline constexpr int kPersonalityAxisCount = static_cast<int>(PersonalityAxis::Count);

enum class Weapon : u16
{
    None = 0,
    Bite = 1 << 0,
    Horns = 1 << 1,
    Frill = 1 << 2,
    TailClub = 1 << 3,
    TailWhip = 1 << 4,
    Kick = 1 << 5,
    Claws = 1 << 6,
    HeadButt = 1 << 7,
    Beak = 1 << 8
};

enum class CallContext : u8
{
    Contact,
    Alarm,
    Distress,
    Threat,
    Courtship,
    Begging,
    Territorial,
    Other
};

NOCTIS_API const char* bodyPlanKey(BodyPlan b);
NOCTIS_API BodyPlan parseBodyPlan(const std::string& s, bool* ok = nullptr);
NOCTIS_API const char* plantTypeKey(PlantType p);
NOCTIS_API const char* plantTypeLabelFr(PlantType p);
NOCTIS_API int parsePlantType(const std::string& s); // -1 if unknown
NOCTIS_API const char* habitatKey(Habitat h);
NOCTIS_API const char* habitatLabelFr(Habitat h);
NOCTIS_API int parseHabitat(const std::string& s); // -1 if unknown
NOCTIS_API const char* personalityKey(PersonalityAxis a);
NOCTIS_API ThermoClass parseThermoClass(const std::string& s);
NOCTIS_API DietType parseDietType(const std::string& s);
NOCTIS_API HuntStyle parseHuntStyle(const std::string& s);
NOCTIS_API NestType parseNestType(const std::string& s);
NOCTIS_API ActivityPattern parseActivityPattern(const std::string& s);
NOCTIS_API u16 parseWeapon(const std::string& s);
NOCTIS_API CallContext parseCallContext(const std::string& s);
NOCTIS_API const char* callContextKey(CallContext c);
NOCTIS_API const char* dietTypeKey(DietType d);
NOCTIS_API const char* activityPatternKey(ActivityPattern a);

inline bool isBiped(BodyPlan b)
{
    switch (b)
    {
    case BodyPlan::BipedLargeTheropod:
    case BodyPlan::BipedSmallTheropod:
    case BodyPlan::BipedOrnithomimid:
    case BodyPlan::BipedOviraptorosaur:
    case BodyPlan::BipedSmallOrnithischian:
    case BodyPlan::BipedPachycephalosaur:
    case BodyPlan::BirdFlyer:
        return true;
    default:
        return false;
    }
}
} // namespace noctis
