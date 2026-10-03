// ExpeditionSystem — open-ended scientific expeditions (not linear missions): the player chooses a
// question, a zone, a duration, a loadout, hypotheses and data targets; the expedition keeps a
// logbook, tracks progress and resource use, and ends with a report built from collected data.
#pragma once

#include "Noctis/Research/FieldGear.h"

#include <string>
#include <vector>

namespace noctis
{
enum class DataTargetKind : u8
{
    Recordings,
    AcousticDetections,
    Photos,
    IdentifiedIndividuals,
    SessionHours,
    Trackways,
    Specimens,
    Observations
};
NOCTIS_API const char* dataTargetLabelFr(DataTargetKind k);

struct DataTarget
{
    DataTargetKind kind = DataTargetKind::Observations;
    i16 speciesIndex = -1;
    float required = 1.0f;
    float achieved = 0.0f;
};

enum class ExpeditionStatus : u8
{
    Planned,
    Active,
    Completed,
    Aborted
};
NOCTIS_API const char* expeditionStatusLabelFr(ExpeditionStatus s);

struct LogEntry
{
    double time = 0.0;
    std::string timeText;
    std::string textFr;
    bool automatic = true;
};

struct Expedition
{
    u32 id = 0;
    int number = 0;
    std::string titleFr;
    std::string questionFr;
    std::string environmentId;
    std::string profileId;
    std::string profileVersion;
    std::string zoneNameFr;
    Rect2 zone{};
    float plannedDays = 3.0f;
    std::vector<LoadoutLine> loadout;
    LoadoutSummary loadoutSummary;
    std::vector<u32> hypotheses;
    std::vector<DataTarget> targets;
    ExpeditionStatus status = ExpeditionStatus::Planned;
    double start = 0.0;
    double end = 0.0;
    std::vector<LogEntry> log;
    std::vector<u32> observations;
    std::vector<u32> recordings;
    std::vector<u32> photos;
    std::vector<u32> sessions;
    std::vector<u32> specimens;
    std::vector<u32> trackways;
    std::vector<u32> catalogIds;
    double distanceKm = 0.0;
    double droneFlightMin = 0.0;
    int incidents = 0;
    float batteryUsedKWh = 0.0f;
};

class NOCTIS_API ExpeditionSystem
{
public:
    u32 plan(Expedition e, const EquipmentCatalog& catalog, float vehiclePayloadKg);
    bool start(u32 id, double now, const std::string& timeText);
    void end(u32 id, double now, const std::string& timeText, bool aborted);
    void log(u32 id, double now, const std::string& timeText, const std::string& text, bool automatic);
    Expedition* active();
    const Expedition* active() const;
    Expedition* find(u32 id);
    const Expedition* find(u32 id) const;
    const std::vector<Expedition>& all() const { return expeditions_; }
    std::vector<Expedition>& mutableAll() { return expeditions_; }
    u32 nextId() const { return nextId_; }
    void restoreNextId(u32 v) { nextId_ = v; }

private:
    std::vector<Expedition> expeditions_;
    u32 nextId_ = 1;
};

struct ScientificReport
{
    u32 expeditionId = 0;
    std::string titleFr;
    std::string markdown;
    JsonValue json;
    Confidence overallConfidence = Confidence::Unknown;
    double generatedAt = 0.0;
};
} // namespace noctis
