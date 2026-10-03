// HypothesisSystem — the player's science.
//
// A hypothesis is a testable prediction about measurable quantities in the player's own data
// (observation sessions, acoustic detections). Tests are real statistics. The system reports
// whether the data support the prediction, how strongly, and what could bias the result
// (observer distance, drone presence, time of day, pseudo-replication, sample size).
// It never says "correct": the simulated world is itself a reconstruction, and even a strongly
// supported result describes a pattern, not a mechanism.
#pragma once

#include "Noctis/Research/Observation.h"
#include "Noctis/Research/Statistics.h"

#include <string>
#include <vector>

namespace noctis
{
enum class Measure : u8
{
    GroupSpacing,
    VigilantFraction,
    FeedingFraction,
    GroupSize,
    CallRatePerMin,
    ObserverDistance,
    DroneAltitude,
    Temperature,
    Light,
    TimeOfDay,
    Count
};
NOCTIS_API const char* measureLabelFr(Measure m);
NOCTIS_API const char* measureKey(Measure m);
NOCTIS_API int parseMeasure(const std::string& key);

enum class ConditionKind : u8
{
    DroneAirborne,
    VehicleEngineOn,
    PredatorVisible,
    Night,
    ObserverWithin,   // value = metres
    TimeBetween,      // value..value2 hours
    MeasureAbove      // measure > value
};
NOCTIS_API const char* conditionLabelFr(ConditionKind c);

struct Condition
{
    ConditionKind kind = ConditionKind::DroneAirborne;
    float value = 0.0f;
    float value2 = 0.0f;
    Measure measure = Measure::GroupSpacing;
};

enum class HypothesisKind : u8
{
    EventResponse, // after a trigger, the response measure changes (vs. control windows)
    Comparison,    // the response measure differs between condition true / false
    Correlation,   // two measures co-vary
    Association    // condition and (response > threshold) co-occur
};

enum class EventTrigger : u8
{
    CallDetected,
    CallCluster,
    PredatorAppears,
    DroneOverhead
};

enum class PredictedDirection : u8
{
    Increase,
    Decrease,
    Different,
    Positive,
    Negative,
    Associated
};

enum class HypothesisStatus : u8
{
    Open,         // not enough data
    Supported,    // data support the prediction
    NotSupported, // data contradict it, or no effect with adequate power
    Inconclusive
};
NOCTIS_API const char* hypothesisStatusLabelFr(HypothesisStatus s);

// A call detected by the player's analysis of a recording (the input to acoustic hypotheses).
struct AcousticDetection
{
    double time = 0.0;
    float f0Hz = 0.0f;
    float durationS = 0.0f;
    float levelDb = 0.0f;
    int cluster = -1;
    u32 recordingId = 0;
    u32 sessionId = 0;
};

struct Hypothesis
{
    u32 id = 0;
    std::string statementFr;
    HypothesisKind kind = HypothesisKind::Comparison;
    i16 speciesIndex = -1;
    Measure response = Measure::GroupSpacing;
    Measure response2 = Measure::GroupSize;
    EventTrigger trigger = EventTrigger::CallDetected;
    int callCluster = -1;
    float windowS = 60.0f;
    Condition condition;
    PredictedDirection predicted = PredictedDirection::Different;
    HypothesisStatus status = HypothesisStatus::Open;
    TestResult result;
    std::vector<std::string> warningsFr;
    std::string methodFr;
    std::string interpretationFr;
    Confidence evidence = Confidence::Unknown;
    double created = 0.0;
    double lastTested = 0.0;
    int tests = 0;
    std::vector<u32> expeditionsSupporting;
    std::vector<u32> parentHypotheses; // the observation/hypothesis that prompted this one
};

class NOCTIS_API HypothesisSystem
{
public:
    u32 create(Hypothesis h, double now);
    // Tests the hypothesis on all sessions (optionally restricted to one expedition, 0 = all).
    void test(u32 id, const ObservationSystem& obs, const std::vector<AcousticDetection>& detections, double now, u64 seed, u32 expeditionId = 0);
    const Hypothesis* find(u32 id) const;
    Hypothesis* findMutable(u32 id);
    const std::vector<Hypothesis>& all() const { return hypotheses_; }
    std::vector<Hypothesis>& mutableAll() { return hypotheses_; }
    static std::string describe(const Hypothesis& h, const EvidenceDatabase* db);
    u32 nextId() const { return nextId_; }
    void restoreNextId(u32 v) { nextId_ = v; }

    static double measureOf(const SessionSample& s, Measure m, const std::vector<AcousticDetection>& det, u32 sessionId);
    static bool conditionHolds(const SessionSample& s, const Condition& c, const std::vector<AcousticDetection>& det, u32 sessionId);

private:
    std::vector<Hypothesis> hypotheses_;
    u32 nextId_ = 1;
};
} // namespace noctis
