// Field equipment: catalogue and loadouts, the researcher, the research vehicle (VehicleSystem),
// the drone (DroneSystem) and passive sensors. Everything that makes noise, smell or light is
// published as an Emitter, so animals perceive the researcher's activity through the same senses
// as everything else — observer effects are part of the science, not a hidden modifier.
#pragma once

#include "Noctis/Core/Json.h"
#include "Noctis/Research/Acoustics.h"

#include <string>
#include <vector>

namespace noctis
{
// ---------------------------------------------------------------- Equipment

struct EquipmentItem
{
    std::string id;
    std::string nameFr;
    std::string category;
    float massKg = 0.0f;
    float powerW = 0.0f;
    float batteryWh = 0.0f;
    float storageGb = 0.0f;
    JsonValue specs;
};

class NOCTIS_API EquipmentCatalog
{
public:
    void load(const JsonValue& root);
    const EquipmentItem* find(const std::string& id) const;
    const std::vector<EquipmentItem>& items() const { return items_; }
    const JsonValue& vehicleSpec() const { return vehicle_; }
    const JsonValue& droneSpec() const { return drone_; }

private:
    std::vector<EquipmentItem> items_;
    JsonValue vehicle_;
    JsonValue drone_;
};

struct LoadoutLine
{
    std::string itemId;
    int count = 1;
};

struct LoadoutSummary
{
    float massKg = 0.0f;
    float batteryWh = 0.0f;
    float storageGb = 0.0f;
    float powerW = 0.0f;
    int cameras = 0;
    int microphones = 0;
    int drones = 0;
    int cameraTraps = 0;
    std::vector<std::string> warningsFr;
};

NOCTIS_API LoadoutSummary summarizeLoadout(const std::vector<LoadoutLine>& lines, const EquipmentCatalog& catalog, float vehiclePayloadKg);

// ---------------------------------------------------------------- Researcher

struct ResearcherState
{
    EntityId id = EntityId::make(EntityKind::Researcher, 1);
    Vec3 position;
    float heading = 0.0f;
    Vec2 velocity;
    bool crouched = false;
    bool inVehicle = false;
    bool talking = false;
    bool headlamp = false;
    float health = 1.0f;
    int timesAttacked = 0;
    double lastAttacked = -1e9;
    float noiseDb = 0.0f;
};

class NOCTIS_API ResearcherSystem
{
public:
    void setTransform(const Vec3& position, float heading, const Vec2& velocity);
    // Headless autopilot: walk towards a target at a given speed (used by tests and scripted scenarios).
    void walkTowards(const Vec2& target, float speed, float dt, const WorldContext& ctx);
    void update(float dt, const WorldContext& ctx);
    void applyAttack(float severity, const WorldContext& ctx);
    Emitter emitter() const;
    ResearcherState& state() { return state_; }
    const ResearcherState& state() const { return state_; }

private:
    ResearcherState state_;
};

// ---------------------------------------------------------------- Vehicle

struct VehicleControls
{
    float throttle = 0.0f; // -1..1 (negative: reverse)
    float brake = 0.0f;    // 0..1
    float steer = 0.0f;    // -1..1
    bool engineOn = true;
    bool generatorOn = false;
    bool lightsOn = false;
    bool winch = false;
};

struct VehicleState
{
    EntityId id = EntityId::make(EntityKind::Vehicle, 1);
    Vec3 position;
    float heading = 0.0f;
    float speed = 0.0f;
    float yawRate = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float batteryKWh = 0.0f;
    float fuelL = 0.0f;
    float wheelSinkageM = 0.0f;
    float slip = 0.0f;
    float waterDepth = 0.0f;
    bool stuck = false;
    bool stalled = false;
    bool engineOn = true;
    bool generatorOn = false;
    bool lightsOn = false;
    double odometerM = 0.0;
    double lastRut = 0.0;
    float noiseDb = 0.0f;
};

class NOCTIS_API VehicleSystem
{
public:
    void configure(const JsonValue& spec);
    // Headless dynamics (Unreal drives the vehicle with Chaos and calls setTransform instead).
    void update(float dt, const VehicleControls& controls, WorldContext& ctx);
    void setTransform(const Vec3& position, float heading, float speed);
    // Energy and noise bookkeeping when Unreal is authoritative for motion.
    void updateAuxiliary(float dt, const VehicleControls& controls, WorldContext& ctx);
    Emitter emitter() const;
    // Bekker-based wheel/ground interaction terms (exported to Unreal to tune tyre friction per substrate).
    static void wheelGround(const Terrain& terrain, const Vec2& p, float wheelLoadN, float wheelWidthM, float wheelRadiusM, float& sinkageM, float& rollingCoef,
                            float& tractionMu);
    VehicleState& state() { return state_; }
    const VehicleState& state() const { return state_; }
    float massKg() const { return massKg_; }
    float payloadKg() const { return payloadKg_; }
    float batteryCapacityKWh() const { return batteryKWh_; }
    void rechargeWh(float wh) { state_.batteryKWh = std::min(batteryKWh_, state_.batteryKWh + wh / 1000.0f); }
    bool drawWh(float wh);

private:
    VehicleState state_;
    float massKg_ = 7200.0f;
    float payloadKg_ = 1200.0f;
    float wheelbaseM = 3.8f;
    float wheelRadiusM = 0.6f;
    float wheelWidthM = 0.42f;
    int wheels_ = 6;
    float maxPowerKW_ = 240.0f;
    float maxForceN_ = 60000.0f;
    float batteryKWh_ = 200.0f;
    float generatorKW_ = 60.0f;
    float fuelCapacityL_ = 220.0f;
    float fuelLPerKWh_ = 0.3f;
    float fordingDepthM_ = 1.0f;
    float auxW_ = 900.0f;
    float noiseElectricDb_ = 52.0f;
    float noiseGeneratorDb_ = 80.0f;
    float dragArea_ = 4.5f;
};

// ---------------------------------------------------------------- Drone

enum class DroneMode : u8
{
    Docked,
    Hover,
    Manual,
    Orbit,
    Follow,
    Survey,
    ReturnHome,
    Landing,
    Lost
};
NOCTIS_API const char* droneModeLabelFr(DroneMode m);

struct DroneState
{
    EntityId id = EntityId::make(EntityKind::Drone, 1);
    DroneMode mode = DroneMode::Docked;
    Vec3 position;
    Vec3 velocity;
    float heading = 0.0f;
    float batteryWh = 0.0f;
    float powerW = 0.0f;
    float altitudeAglM = 0.0f;
    float targetAltitudeM = 40.0f;
    Vec2 manualVelocity;
    Vec2 orbitCenter;
    float orbitRadiusM = 60.0f;
    EntityId followTarget;
    Vec2 followTargetPos;
    bool followLock = false;
    double lostLockSince = -1.0;
    Rect2 surveyArea{};
    int surveyLeg = 0;
    float signal = 1.0f;
    float noiseDb = 0.0f;
    std::string warningFr;
    double flightTimeS = 0.0;
};

class NOCTIS_API DroneSystem
{
public:
    void configure(const JsonValue& spec);
    bool launch(const Vec3& from, double now);
    void command(DroneMode mode);
    void setManual(const Vec2& velocity, float altitude);
    void setOrbit(const Vec2& center, float radius, float altitude);
    void setFollow(EntityId target, float altitude);
    void setSurvey(const Rect2& area, float altitude);
    // home: where the drone returns (vehicle or researcher). followVisible: the follow target is visible to the drone camera.
    void update(float dt, const WorldContext& ctx, const Vec3& home, const Vec3& controller, bool followVisible, const Vec2& followTruthPos);
    Emitter emitter() const;
    DroneState& state() { return state_; }
    const DroneState& state() const { return state_; }
    float batteryCapacityWh() const { return batteryCapacityWh_; }
    float hoverPowerW() const;
    void dock(float rechargeFraction);

private:
    DroneState state_;
    float massKg_ = 4.5f;
    float rotorDiameterM_ = 0.38f;
    int rotors_ = 4;
    float batteryCapacityWh_ = 280.0f;
    float maxSpeedMs_ = 16.0f;
    float maxWindMs_ = 12.0f;
    float radioRangeM_ = 6000.0f;
    float noiseDbAt1m_ = 78.0f;
    float bladePassHz_ = 160.0f;
    float rainLimitMmH_ = 4.0f;
};

// ---------------------------------------------------------------- Sensors

enum class SensorKind : u8
{
    CameraTrap,
    AudioRecorder,
    WeatherStation
};
NOCTIS_API const char* sensorKindLabelFr(SensorKind k);

struct Sensor
{
    u32 id = 0;
    SensorKind kind = SensorKind::CameraTrap;
    Vec3 position;
    float heading = 0.0f;
    bool active = true;
    float batteryWh = 50.0f;
    float storageGbUsed = 0.0f;
    double lastTrigger = -1e9;
    double lastCheck = -1e9;
    int triggers = 0;
    u32 arrayId = 0;
    MicSpec mic;
    float recordDurationS = 60.0f;
    float recordIntervalS = 600.0f;
    double nextRecording = 0.0;
    u32 expeditionId = 0;
};
} // namespace noctis
