// ScientificObservationSystem and the individual catalogue (photo-identification).
//
// An observation records only what the instrument could resolve: species (if identifiable),
// visible actions and postures (never internal motivations), resolvable features (scars, limps,
// markings), size estimates with their error, group counts and spacing, plus the full context
// (weather, light, observer distance and noise, drone/vehicle presence) so that the player can
// later test for observer effects. Identities in the catalogue come from matching observed
// features, so misidentifications are possible — as in real field biology.
#pragma once

#include "Noctis/Creatures/CreatureSystem.h"

#include <map>
#include <string>
#include <vector>

namespace noctis
{
enum class ObservationMethod : u8
{
    NakedEye,
    Binoculars,
    Photo,
    Video,
    Drone,
    CameraTrap,
    Audio,
    Fieldwork
};
NOCTIS_API const char* observationMethodLabelFr(ObservationMethod m);

enum class ObservedAction : u8
{
    Standing,
    Walking,
    Running,
    Feeding,
    Drinking,
    Lying,
    Vigilant,
    Vocalizing,
    Swimming,
    Attacking,
    Fighting,
    Displaying,
    Nesting,
    Unknown
};
NOCTIS_API const char* observedActionLabelFr(ObservedAction a);

enum class FeatureKind : u8
{
    Scar,
    Limp,
    Marking,
    Juvenile,
    Injury
};

struct ObservedFeature
{
    FeatureKind kind = FeatureKind::Scar;
    u8 region = 0;
    u32 seed = 0;     // shape seed of a scar, pattern seed of a marking
    float value = 0.0f;
};

struct ObservedSubject
{
    EntityId truth;                // hidden — debug/validation only
    i16 speciesIndex = -1;         // identified species, -1 when not identifiable
    float speciesConfidence = 0.0f;
    u32 catalogId = 0;
    float matchConfidence = 0.0f;
    std::vector<ObservedFeature> features;
    float estimatedLengthM = 0.0f;
    float lengthErrorM = 0.0f;
    ObservedAction action = ObservedAction::Unknown;
    float distanceM = 0.0f;
    float bearingDeg = 0.0f;
    Vec2 estimatedPos;
    bool lookingAtObserver = false;
    bool movingAway = false;
    float speedEstimateMs = 0.0f;
    float quality = 0.0f;
};

struct GroupObservation
{
    u32 groupTruth = 0; // hidden
    i16 speciesIndex = -1;
    int countEstimate = 0;
    float meanSpacingM = 0.0f;
    float vigilantFraction = 0.0f;
    float feedingFraction = 0.0f;
    Formation formation = Formation::Loose;
    Vec2 centroid;
};

struct EnvironmentSnapshot
{
    float temperatureC = 0.0f;
    float humidity = 0.0f;
    float cloudCover = 0.0f;
    float rainMmH = 0.0f;
    float windMs = 0.0f;
    float windFromDeg = 0.0f;
    float visibilityM = 0.0f;
    float lux = 0.0f;
    float sunElevationDeg = 0.0f;
    float hourOfDay = 12.0f;
    DayPhase phase = DayPhase::Day;
    std::string seasonFr;
};

struct ObserverContext
{
    float observerNoiseDb = 0.0f;
    bool crouched = false;
    bool inVehicle = false;
    bool vehicleEngineOn = false;
    bool droneAirborne = false;
    float droneAltitudeM = 0.0f;
    float droneDistanceM = 0.0f;
};

struct ObservationRecord
{
    u32 id = 0;
    double time = 0.0;
    std::string timeText;
    ObservationMethod method = ObservationMethod::NakedEye;
    Vec3 observerPos;
    float viewHeadingDeg = 0.0f;
    float fovDeg = 0.0f;
    float magnification = 1.0f;
    std::vector<ObservedSubject> subjects;
    std::vector<GroupObservation> groups;
    EnvironmentSnapshot env;
    ObserverContext observer;
    u32 mediaId = 0;
    u32 sessionId = 0;
    u32 expeditionId = 0;
    std::vector<std::string> tags;
    std::string note;
    float overallQuality = 0.0f;
};

struct PhotoSettings
{
    float focalMm = 400.0f;
    float sensorWidthMm = 36.0f;
    int widthPx = 8192;
    float shutterS = 1.0f / 1000.0f;
    float aperture = 5.6f;
    int iso = 800;
    bool laserRangefinder = true;
};

struct PhotoRecord
{
    u32 id = 0;
    u32 observationId = 0;
    PhotoSettings settings;
    float sceneEv = 0.0f;
    float exposureErrorEv = 0.0f;
    float motionBlurPx = 0.0f;
    float subjectCoveragePx = 0.0f;
    float sharpness = 0.0f;
    float noise = 0.0f;
    std::string imageFile; // written by the renderer (Unreal) when a real image is captured
};

struct SessionSample
{
    double time = 0.0;
    i16 speciesIndex = -1;
    int count = 0;
    float meanSpacingM = 0.0f;
    float vigilantFraction = 0.0f;
    float feedingFraction = 0.0f;
    Formation formation = Formation::Loose;
    Vec2 centroid;
    float observerDistanceM = 0.0f;
    float observerNoiseDb = 0.0f;
    bool droneAirborne = false;
    float droneAltitudeM = 0.0f;
    bool vehicleEngineOn = false;
    bool predatorVisible = false;
    float lux = 0.0f;
    float temperatureC = 0.0f;
    float hourOfDay = 12.0f;
};

struct ObservationSession
{
    u32 id = 0;
    double start = 0.0;
    double end = 0.0;
    bool open = true;
    ObservationMethod method = ObservationMethod::Binoculars;
    i16 speciesFocus = -1;
    std::vector<SessionSample> samples;
    std::vector<u32> recordingIds;
    std::vector<u32> observationIds;
    u32 expeditionId = 0;
};

// ---------------------------------------------------------------- Catalogue

struct CatalogEntry
{
    u32 id = 0;
    std::string code;     // e.g. "TRX-014"
    std::string nickname; // e.g. "Balafre"
    i16 speciesIndex = -1;
    EntityId truth;       // hidden link (debug: measures misidentification rate)
    std::vector<ObservedFeature> features;
    double firstSeen = 0.0;
    double lastSeen = 0.0;
    std::vector<u32> observations;
    std::vector<Vec2> sightings;
    std::vector<double> sightingTimes;
    float lastLengthM = 0.0f;
    int sexKnown = -1;    // -1 unknown, 0 female, 1 male — only from direct evidence (e.g. seen laying eggs)
    std::string notes;
    bool confirmedByPlayer = false;
};

class NOCTIS_API IndividualCatalog
{
public:
    // Matches the subject against existing entries; creates a new entry when the subject carries
    // enough distinctive features and nothing matches. Returns the entry id (0 = unidentified).
    u32 match(ObservedSubject& s, const SpeciesDefinition& species, double time, const Vec2& pos, u32 observationId, bool autoCreate);
    std::vector<std::pair<u32, float>> candidates(const ObservedSubject& s) const;
    void assign(ObservedSubject& s, u32 entryId, double time, const Vec2& pos, u32 observationId);
    bool rename(u32 id, const std::string& nickname);
    bool merge(u32 keep, u32 absorb);
    const CatalogEntry* find(u32 id) const;
    CatalogEntry* findMutable(u32 id);
    const CatalogEntry* findByCode(const std::string& code) const;
    const std::vector<CatalogEntry>& entries() const { return entries_; }
    std::vector<CatalogEntry>& mutableEntries() { return entries_; }
    // Debug: fraction of catalogue assignments that point to the wrong real individual.
    float misidentificationRate() const;
    void restoreCounters(u32 nextId, std::map<int, int> perSpecies) { nextId_ = nextId; perSpecies_ = std::move(perSpecies); }
    u32 nextId() const { return nextId_; }
    const std::map<int, int>& perSpeciesCounters() const { return perSpecies_; }

private:
    static float score(const CatalogEntry& e, const ObservedSubject& s);
    static std::string nicknameFor(const ObservedSubject& s, u64 seed);
    std::vector<CatalogEntry> entries_;
    std::map<int, int> perSpecies_;
    u32 nextId_ = 1;
    int assignments_ = 0;
    int wrongAssignments_ = 0;
};

// ---------------------------------------------------------------- Observation system

struct OpticsSpec
{
    ObservationMethod method = ObservationMethod::NakedEye;
    float fovDeg = 120.0f;
    float magnification = 1.0f;
    float rangeLimitM = 1500.0f;
    // Angular resolution (rad) — human eye ~1 arcmin; camera: pixel pitch / focal length.
    float acuityRad = 0.00029f;
    bool rangefinder = false;
};

class NOCTIS_API ObservationSystem
{
public:
    ObservationRecord observe(const OpticsSpec& optics, const Vec3& eye, float headingRad, const CreatureSystem& creatures, const WorldContext& ctx,
                              const ObserverContext& observer, u32 expeditionId, Rng& rng);
    // Photograph: optics from camera settings, plus exposure / blur metrics.
    ObservationRecord photograph(const PhotoSettings& settings, const Vec3& eye, float headingRad, const CreatureSystem& creatures, const WorldContext& ctx,
                                 const ObserverContext& observer, u32 expeditionId, Rng& rng, PhotoRecord& outPhoto);
    static OpticsSpec opticsFor(const PhotoSettings& s);

    // Continuous observation sessions (binocular watch, video, drone follow).
    u32 beginSession(ObservationMethod m, i16 speciesFocus, double now, u32 expeditionId);
    void sampleSession(u32 sessionId, const ObservationRecord& rec, const ObserverContext& observer, bool predatorVisible);
    void endSession(u32 sessionId, double now);
    ObservationSession* session(u32 id);
    const std::vector<ObservationSession>& sessions() const { return sessions_; }
    std::vector<ObservationSession>& mutableSessions() { return sessions_; }

    // Archive.
    u32 store(ObservationRecord rec);
    const std::vector<ObservationRecord>& records() const { return records_; }
    std::vector<ObservationRecord>& mutableRecords() { return records_; }
    const ObservationRecord* find(u32 id) const;
    const std::vector<PhotoRecord>& photos() const { return photos_; }
    std::vector<PhotoRecord>& mutablePhotos() { return photos_; }
    u32 storePhoto(PhotoRecord p);
    IndividualCatalog& catalog() { return catalog_; }
    const IndividualCatalog& catalog() const { return catalog_; }
    void restoreCounters(u32 nextRecord, u32 nextPhoto, u32 nextSession)
    {
        nextRecord_ = nextRecord;
        nextPhoto_ = nextPhoto;
        nextSession_ = nextSession;
    }
    u32 nextRecord() const { return nextRecord_; }
    u32 nextPhoto() const { return nextPhoto_; }
    u32 nextSession() const { return nextSession_; }

    static ObservedAction actionOf(const Creature& c, double now);
    static EnvironmentSnapshot snapshotEnvironment(const WorldContext& ctx);

private:
    std::vector<ObservationRecord> records_;
    std::vector<PhotoRecord> photos_;
    std::vector<ObservationSession> sessions_;
    IndividualCatalog catalog_;
    u32 nextRecord_ = 1;
    u32 nextPhoto_ = 1;
    u32 nextSession_ = 1;
};
} // namespace noctis
