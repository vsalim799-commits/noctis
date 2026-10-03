// Paleontology: fossil exposures, excavation, specimen identification, trackway analysis,
// bite-mark analysis and carcass examination. The player reads living-world traces with the same
// methods paleontologists apply to the fossil record — and can test those methods against
// animals they have actually filmed (e.g. calibrate Alexander's trackway-speed formula).
#pragma once

#include "Noctis/Creatures/CreatureSystem.h"
#include "Noctis/World/TerrainGenerator.h"

#include <string>
#include <vector>

namespace noctis
{
enum class SpecimenElement : u8
{
    Tooth,
    Vertebra,
    Femur,
    Tibia,
    Rib,
    HornCore,
    FrillFragment,
    Osteoderm,
    TailClub,
    Ungual,
    SkullFragment,
    DentalBattery,
    Eggshell,
    Coprolite,
    Count
};
NOCTIS_API const char* specimenElementLabelFr(SpecimenElement e);

struct FossilSpecimen
{
    u32 id = 0;
    u32 siteId = 0;
    SpecimenElement element = SpecimenElement::Vertebra;
    i16 truthSpecies = -1; // hidden
    float completeness = 0.5f;
    int weathering = 2;
    float excavation = 0.0f; // 0..1
    bool collected = false;
    bool identified = false;
    std::string identificationFr;
    Confidence identificationConfidence = Confidence::Unknown;
    i16 identifiedSpecies = -1;
    std::vector<BiteMark> marks;
    Vec2 position;
    float sizeCm = 10.0f;
};

struct FossilSite
{
    u32 id = 0;
    Vec2 position;
    std::string nameFr;
    std::vector<u32> specimens;
    bool discovered = false;
};

struct TrackwayAnalysis
{
    bool valid = false;
    int footprints = 0;
    float meanFootLengthM = 0.0f;
    float meanFootWidthM = 0.0f;
    float strideM = 0.0f;
    float paceAngulationDeg = 0.0f;
    float hipHeightEstM = 0.0f;
    float hipToFootRatio = 4.0f;
    float relativeStride = 0.0f;
    float speedEstMs = 0.0f;
    float speedLowMs = 0.0f;
    float speedHighMs = 0.0f;
    std::string gaitFr;
    std::string makerGuessFr;
    std::vector<std::string> caveatsFr;
    std::vector<u32> footprintIds;
    float trueSpeedMs = 0.0f; // hidden, revealed only in calibration studies / debug
};

struct BiteMarkAnalysis
{
    int marks = 0;
    int healedMarks = 0;
    float meanToothSpacingMm = 0.0f;
    float predatorLengthEstM = 0.0f;
    float predatorLengthLowM = 0.0f;
    float predatorLengthHighM = 0.0f;
    std::string candidatesFr;
    std::string interpretationFr;
};

struct CarcassExamination
{
    std::string stageFr;
    float timeSinceDeathDays = 0.0f;
    float timeSinceDeathLowDays = 0.0f;
    float timeSinceDeathHighDays = 0.0f;
    int weatheringStage = 0;
    int shedTeeth = 0;
    float scatter = 0.0f;
    float softTissueKg = 0.0f;
    float estimatedMassKg = 0.0f;
    BiteMarkAnalysis bites;
    std::vector<std::string> notesFr;
};

class NOCTIS_API PaleontologySystem
{
public:
    void generateSites(const WorldLayout& layout, const EvidenceDatabase& db, const EnvironmentDefinition& env, const std::vector<int>& agentSpecies,
                       u64 seed);
    // Progresses excavation; returns true when the specimen is freed and collected.
    bool excavate(u32 specimenId, float hours, float toolQuality);
    // Lab identification: resolution depends on the diagnostic value of the element.
    void identify(u32 specimenId, const EvidenceDatabase& db);
    TrackwayAnalysis analyzeTrackway(const std::vector<u32>& footprintIds, const TrackSystem& tracks, const EvidenceDatabase& db) const;
    // Groups aligned footprints of similar size starting from a seed footprint (what the player selects in the field).
    std::vector<u32> extractTrackway(u32 seedFootprint, const TrackSystem& tracks) const;
    BiteMarkAnalysis analyzeBiteMarks(const std::vector<BiteMark>& marks, const EvidenceDatabase& db) const;
    CarcassExamination examineCarcass(const Carcass& c, double now, float meanTempC, const EvidenceDatabase& db) const;

    const std::vector<FossilSite>& sites() const { return sites_; }
    std::vector<FossilSite>& mutableSites() { return sites_; }
    const std::vector<FossilSpecimen>& specimens() const { return specimens_; }
    std::vector<FossilSpecimen>& mutableSpecimens() { return specimens_; }
    FossilSpecimen* specimen(u32 id);
    const FossilSite* site(u32 id) const;

private:
    std::vector<FossilSite> sites_;
    std::vector<FossilSpecimen> specimens_;
};
} // namespace noctis
