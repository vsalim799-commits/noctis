// ResearchSystem — everything the researcher does and knows: equipment, the researcher, the rover
// and its mobile lab, the drone, sensors, observations, recordings and their analysis, the map,
// paleontology, hypotheses, expeditions and reports. It is the single command surface used by
// Unreal (UI and input) and by the headless runner (scripted scenarios).
#pragma once

#include "Noctis/Creatures/CreatureSystem.h"
#include "Noctis/Research/Acoustics.h"
#include "Noctis/Research/Expedition.h"
#include "Noctis/Research/FieldGear.h"
#include "Noctis/Research/FieldMap.h"
#include "Noctis/Research/Hypothesis.h"
#include "Noctis/Research/Observation.h"
#include "Noctis/Research/Paleontology.h"

#include <string>
#include <vector>

namespace noctis
{
struct RecordingAnalysis
{
    u32 recordingId = 0;
    Spectrogram spectrogram;
    std::vector<AcousticEvent> events;
    bool analyzed = false;
};

// A microphone that is currently capturing (handheld, array element or autonomous recorder).
struct ActiveRecording
{
    u32 handle = 0;
    MicSpec mic;
    Vec3 position;
    float heading = 0.0f;
    double start = 0.0;
    u32 arrayId = 0;
    u32 sensorId = 0;
    std::string label;
    std::vector<SoundSource> sources;
    std::vector<u32> seen;
};

enum class OpticsKind : u8
{
    Eye,
    Binoculars,
    DroneCamera,
    TrapCamera
};

class NOCTIS_API ResearchSystem
{
public:
    void initialize(const EvidenceDatabase& db, const EnvironmentDefinition& env, const WorldLayout& layout, const std::vector<int>& agentSpecies,
                    const Rect2& bounds, const Vec2& basecamp, const Terrain& terrain, u64 seed);
    void publishEmitters(std::vector<Emitter>& out) const;
    void update(float dt, WorldContext& ctx, CreatureSystem& creatures);
    void onEvent(const SimEvent& e, WorldContext& ctx, CreatureSystem& creatures);

    // --- Observation ---
    u32 observeNow(OpticsKind kind, WorldContext& ctx, CreatureSystem& creatures);
    u32 takePhoto(const PhotoSettings& settings, WorldContext& ctx, CreatureSystem& creatures);
    u32 startSession(ObservationMethod method, i16 speciesFocus, WorldContext& ctx);
    void stopSession(WorldContext& ctx);
    u32 activeSession() const { return activeSession_; }
    void addNote(const std::string& text, WorldContext& ctx);

    // --- Acoustics ---
    u32 startRecording(const MicSpec& mic, const Vec3& position, float heading, u32 arrayId, const std::string& label, WorldContext& ctx);
    u32 stopRecording(u32 handle, WorldContext& ctx); // returns recording id
    // Deploys n microphones on a circle around 'center' (synchronised array) and starts them.
    u32 deployMicArray(const Vec2& center, float radiusM, int n, const MicSpec& mic, WorldContext& ctx);
    std::vector<u32> stopMicArray(u32 arrayId, WorldContext& ctx);
    const RecordingAnalysis* analyzeRecording(u32 recordingId, WorldContext& ctx);
    void clusterCalls(int k, u64 seed);
    LocalizationResult localizeEvent(u32 arrayId, u32 recordingId, int eventIndex, float temperatureC);

    // --- Equipment ---
    u32 placeSensor(SensorKind kind, const Vec3& position, float heading, WorldContext& ctx);
    bool retrieveSensor(u32 id);
    bool launchDrone(WorldContext& ctx);
    void enterVehicle(bool inside);
    void setVehicleControls(const VehicleControls& c) { vehicleControls_ = c; }
    void setExternalVehicle(bool external) { externalVehicle_ = external; }

    // --- Paleontology ---
    bool excavate(u32 specimenId, float hours, WorldContext& ctx);
    void identifySpecimen(u32 specimenId, WorldContext& ctx);
    u32 measureTrackway(u32 seedFootprint, WorldContext& ctx);
    u32 examineCarcass(EntityId carcass, WorldContext& ctx);

    // --- Hypotheses, expeditions, reports ---
    u32 createHypothesis(Hypothesis h, WorldContext& ctx);
    void testHypothesis(u32 id, WorldContext& ctx, bool currentExpeditionOnly);
    u32 planExpedition(Expedition e);
    bool startExpedition(u32 id, WorldContext& ctx);
    const ScientificReport* endExpedition(u32 id, WorldContext& ctx, bool aborted);
    ScientificReport generateReport(u32 expeditionId, const WorldContext& ctx) const;

    // --- State access ---
    const EquipmentCatalog& equipment() const { return equipment_; }
    ResearcherSystem& researcher() { return researcher_; }
    const ResearcherSystem& researcher() const { return researcher_; }
    VehicleSystem& vehicle() { return vehicle_; }
    const VehicleSystem& vehicle() const { return vehicle_; }
    DroneSystem& drone() { return drone_; }
    const DroneSystem& drone() const { return drone_; }
    std::vector<Sensor>& sensors() { return sensors_; }
    const std::vector<Sensor>& sensors() const { return sensors_; }
    ObservationSystem& observations() { return observations_; }
    const ObservationSystem& observations() const { return observations_; }
    FieldMap& map() { return map_; }
    const FieldMap& map() const { return map_; }
    PaleontologySystem& paleontology() { return paleo_; }
    const PaleontologySystem& paleontology() const { return paleo_; }
    HypothesisSystem& hypotheses() { return hypotheses_; }
    const HypothesisSystem& hypotheses() const { return hypotheses_; }
    ExpeditionSystem& expeditions() { return expeditions_; }
    const ExpeditionSystem& expeditions() const { return expeditions_; }
    const std::vector<Recording>& recordings() const { return recordings_; }
    std::vector<Recording>& mutableRecordings() { return recordings_; }
    const std::vector<RecordingAnalysis>& analyses() const { return analyses_; }
    const std::vector<AcousticDetection>& detections() const { return detections_; }
    std::vector<AcousticDetection>& mutableDetections() { return detections_; }
    const std::vector<TrackwayAnalysis>& trackways() const { return trackways_; }
    std::vector<TrackwayAnalysis>& mutableTrackways() { return trackways_; }
    const std::vector<CarcassExamination>& examinations() const { return examinations_; }
    const std::vector<ScientificReport>& reports() const { return reports_; }
    const std::vector<ActiveRecording>& activeRecordings() const { return active_; }
    const VehicleControls& vehicleControls() const { return vehicleControls_; }
    const Recording* findRecording(u32 id) const;
    ObserverContext observerContext(const WorldContext& ctx) const;
    Vec3 eyePosition() const;
    float viewHeading() const;
    const EvidenceDatabase* db() const { return db_; }

private:
    u32 storeObservation(ObservationRecord rec, WorldContext& ctx, CreatureSystem& creatures);
    void autoDiscover(WorldContext& ctx, CreatureSystem& creatures);
    void updateSensors(float dt, WorldContext& ctx, CreatureSystem& creatures);
    void captureSounds(WorldContext& ctx);
    void updateExpeditionProgress(WorldContext& ctx);
    void autolog(const std::string& text, WorldContext& ctx);
    OpticsSpec optics(OpticsKind k) const;

    const EvidenceDatabase* db_ = nullptr;
    const EnvironmentDefinition* env_ = nullptr;
    EquipmentCatalog equipment_;
    ResearcherSystem researcher_;
    VehicleSystem vehicle_;
    VehicleControls vehicleControls_;
    bool externalVehicle_ = false;
    DroneSystem drone_;
    std::vector<Sensor> sensors_;
    ObservationSystem observations_;
    FieldMap map_;
    PaleontologySystem paleo_;
    HypothesisSystem hypotheses_;
    ExpeditionSystem expeditions_;
    std::vector<Recording> recordings_;
    std::vector<RecordingAnalysis> analyses_;
    std::vector<AcousticDetection> detections_;
    std::vector<TrackwayAnalysis> trackways_;
    std::vector<CarcassExamination> examinations_;
    std::vector<ScientificReport> reports_;
    std::vector<ActiveRecording> active_;
    u32 activeSession_ = 0;
    double lastSessionSample_ = -1e9;
    double lastReveal_ = -1e9;
    double lastDiscover_ = -1e9;
    double lastDroneObservation_ = -1e9;
    u32 nextRecordingId_ = 1;
    u32 nextHandle_ = 1;
    u32 nextArrayId_ = 1;
    u32 nextSensorId_ = 1;
    Rng rng_;
    Vec2 basecamp_;
};
} // namespace noctis
