#include "Noctis/Research/FieldGear.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
// ---------------------------------------------------------------- Equipment

void EquipmentCatalog::load(const JsonValue& root)
{
    items_.clear();
    for (const JsonValue& it : root.get("items").items())
    {
        EquipmentItem e;
        e.id = it.getString("id");
        e.nameFr = it.getString("name_fr", e.id);
        e.category = it.getString("category");
        e.massKg = it.getFloat("mass_kg", 0.0f);
        e.powerW = it.getFloat("power_w", 0.0f);
        e.batteryWh = it.getFloat("battery_wh", 0.0f);
        e.storageGb = it.getFloat("storage_gb", 0.0f);
        e.specs = it.get("specs");
        items_.push_back(std::move(e));
    }
    vehicle_ = root.get("vehicle");
    drone_ = root.get("drone");
}

const EquipmentItem* EquipmentCatalog::find(const std::string& id) const
{
    for (const EquipmentItem& e : items_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

LoadoutSummary summarizeLoadout(const std::vector<LoadoutLine>& lines, const EquipmentCatalog& catalog, float vehiclePayloadKg)
{
    LoadoutSummary s;
    for (const LoadoutLine& l : lines)
    {
        const EquipmentItem* e = catalog.find(l.itemId);
        if (!e)
        {
            s.warningsFr.push_back("Équipement inconnu : " + l.itemId);
            continue;
        }
        const float n = static_cast<float>(l.count);
        s.massKg += e->massKg * n;
        s.batteryWh += e->batteryWh * n;
        s.storageGb += e->storageGb * n;
        s.powerW += e->powerW * n;
        if (e->category == "camera")
        {
            s.cameras += l.count;
        }
        else if (e->category == "microphone" || e->category == "recorder")
        {
            s.microphones += l.count;
        }
        else if (e->category == "drone")
        {
            s.drones += l.count;
        }
        else if (e->category == "camera_trap")
        {
            s.cameraTraps += l.count;
        }
    }
    if (s.massKg > vehiclePayloadKg)
    {
        s.warningsFr.push_back("Charge supérieure à la capacité du véhicule.");
    }
    if (s.microphones > 0 && s.microphones < 3)
    {
        s.warningsFr.push_back("Moins de trois micros : la localisation acoustique par différence de temps d'arrivée sera impossible.");
    }
    return s;
}

// ---------------------------------------------------------------- Researcher

void ResearcherSystem::setTransform(const Vec3& position, float heading, const Vec2& velocity)
{
    state_.position = position;
    state_.heading = heading;
    state_.velocity = velocity;
}

void ResearcherSystem::walkTowards(const Vec2& target, float speed, float dt, const WorldContext& ctx)
{
    const Vec2 p = state_.position.xy();
    const Vec2 d = target - p;
    const float dist = d.length();
    if (dist < 0.3f)
    {
        state_.velocity = Vec2{};
        return;
    }
    const Vec2 dir = d / dist;
    const Vec2 next = p + dir * std::min(dist, speed * dt);
    state_.heading = dir.heading();
    state_.velocity = dir * speed;
    state_.position = Vec3{next, ctx.terrain->heightAt(next) + 1.65f};
}

void ResearcherSystem::update(float dt, const WorldContext& ctx)
{
    (void)dt;
    const float speed = state_.velocity.length();
    // Footsteps: ~40 dB slow careful steps on soft ground, ~60 dB running on gravel or through ferns.
    float steps = speed < 0.05f ? 0.0f : 42.0f + 10.0f * saturate(speed / 3.0f) + (state_.crouched ? -6.0f : 0.0f);
    if (steps > 0.0f)
    {
        const Substrate s = ctx.terrain->substrateAt(state_.position.xy());
        steps += (s == Substrate::Gravel || s == Substrate::Rock) ? 4.0f : (s == Substrate::Mud ? -2.0f : 0.0f);
        steps += 6.0f * ctx.vegetation->cover(state_.position.xy());
    }
    const float voice = state_.talking ? 58.0f : 0.0f;
    state_.noiseDb = std::max(steps, voice);
}

void ResearcherSystem::applyAttack(float severity, const WorldContext& ctx)
{
    state_.health = std::max(0.0f, state_.health - severity);
    state_.lastAttacked = ctx.now;
    ++state_.timesAttacked;
}

Emitter ResearcherSystem::emitter() const
{
    Emitter e;
    e.id = state_.id;
    e.position = state_.position;
    e.velocity = state_.velocity;
    e.heading = state_.heading;
    e.heightM = state_.crouched ? 1.0f : 1.75f;
    e.massKg = 80.0f;
    e.noiseDb = state_.noiseDb;
    e.noiseHz = state_.talking ? 250.0f : 600.0f;
    e.scent = 1.0f;
    e.lightsOn = state_.headlamp;
    e.crouched = state_.crouched;
    e.active = !state_.inVehicle && state_.health > 0.0f;
    e.observer = true;
    e.observerRadiusM = 350.0f;
    return e;
}

// ---------------------------------------------------------------- Vehicle

void VehicleSystem::configure(const JsonValue& spec)
{
    massKg_ = spec.getFloat("mass_kg", massKg_);
    payloadKg_ = spec.getFloat("payload_kg", payloadKg_);
    wheelbaseM = spec.getFloat("wheelbase_m", wheelbaseM);
    wheelRadiusM = spec.getFloat("wheel_radius_m", wheelRadiusM);
    wheelWidthM = spec.getFloat("wheel_width_m", wheelWidthM);
    wheels_ = spec.getInt("wheels", wheels_);
    maxPowerKW_ = spec.getFloat("max_power_kw", maxPowerKW_);
    maxForceN_ = spec.getFloat("max_tractive_force_n", maxForceN_);
    batteryKWh_ = spec.getFloat("battery_kwh", batteryKWh_);
    generatorKW_ = spec.getFloat("generator_kw", generatorKW_);
    fuelCapacityL_ = spec.getFloat("fuel_l", fuelCapacityL_);
    fuelLPerKWh_ = spec.getFloat("fuel_l_per_kwh", fuelLPerKWh_);
    fordingDepthM_ = spec.getFloat("fording_depth_m", fordingDepthM_);
    auxW_ = spec.getFloat("auxiliary_w", auxW_);
    noiseElectricDb_ = spec.getFloat("noise_electric_db_1m", noiseElectricDb_);
    noiseGeneratorDb_ = spec.getFloat("noise_generator_db_1m", noiseGeneratorDb_);
    dragArea_ = spec.getFloat("drag_area_m2", dragArea_);
    state_.batteryKWh = batteryKWh_ * 0.9f;
    state_.fuelL = fuelCapacityL_;
}

void VehicleSystem::wheelGround(const Terrain& terrain, const Vec2& p, float wheelLoadN, float wheelWidthM, float wheelRadiusM, float& sinkageM, float& rollingCoef,
                                float& tractionMu)
{
    const SoilMechanics s = soilMechanics(terrain.substrateAt(p), terrain.moistureAt(p));
    const float contactLen = std::max(0.15f, wheelRadiusM * 0.45f);
    const float pressureKPa = wheelLoadN / (wheelWidthM * contactLen) / 1000.0f;
    sinkageM = terrain.sinkage(p, pressureKPa, wheelWidthM);
    // Compaction resistance grows with relative sinkage (Bekker-type behaviour, simplified).
    rollingCoef = s.rollingResistance + 0.6f * sinkageM / (2.0f * wheelRadiusM);
    tractionMu = s.tractionMu * (1.0f - 0.5f * saturate(sinkageM / wheelRadiusM));
}

bool VehicleSystem::drawWh(float wh)
{
    const float kwh = wh / 1000.0f;
    if (state_.batteryKWh < kwh)
    {
        return false;
    }
    state_.batteryKWh -= kwh;
    return true;
}

void VehicleSystem::updateAuxiliary(float dt, const VehicleControls& controls, WorldContext& ctx)
{
    (void)ctx;
    state_.engineOn = controls.engineOn;
    state_.lightsOn = controls.lightsOn;
    state_.generatorOn = controls.generatorOn && state_.fuelL > 0.0f;
    const float aux = auxW_ + (state_.lightsOn ? 300.0f : 0.0f);
    state_.batteryKWh = std::max(0.0f, state_.batteryKWh - aux * dt / 3.6e6f);
    if (state_.generatorOn)
    {
        const float kwh = generatorKW_ * dt / 3600.0f;
        state_.batteryKWh = std::min(batteryKWh_, state_.batteryKWh + kwh);
        state_.fuelL = std::max(0.0f, state_.fuelL - kwh * fuelLPerKWh_);
    }
    // Noise at 1 m: electric drive + tyre noise rising with speed, the generator dominates when running.
    float noise = state_.engineOn ? noiseElectricDb_ + 12.0f * std::log10(1.0f + state_.speed / 3.0f) : 0.0f;
    if (state_.generatorOn)
    {
        noise = dbSumPower(noise, noiseGeneratorDb_);
    }
    state_.noiseDb = noise;
}

void VehicleSystem::update(float dt, const VehicleControls& controls, WorldContext& ctx)
{
    updateAuxiliary(dt, controls, ctx);
    VehicleState& s = state_;
    const Vec2 p = s.position.xy();
    const float g = kGravity;
    const float wheelLoad = massKg_ * g / static_cast<float>(wheels_);
    float sink = 0.0f;
    float crr = 0.0f;
    float mu = 0.0f;
    wheelGround(*ctx.terrain, p, wheelLoad, wheelWidthM, wheelRadiusM, sink, crr, mu);
    s.wheelSinkageM = sink;
    const WaterQuery wq = ctx.water->query(p, *ctx.terrain);
    s.waterDepth = wq.depth;
    if (wq.depth > fordingDepthM_)
    {
        s.stalled = true;
    }
    const Vec2 fwd = Vec2::fromHeading(s.heading);
    const float grade = (ctx.terrain->heightAt(p + fwd * 2.0f) - ctx.terrain->heightAt(p - fwd * 2.0f)) / 4.0f;
    const float cosSlope = 1.0f / std::sqrt(1.0f + grade * grade);
    const float sinSlope = grade * cosSlope;
    const bool drive = controls.engineOn && !s.stalled && s.batteryKWh > 0.0f;
    float force = 0.0f;
    if (drive && std::fabs(controls.throttle) > 0.01f)
    {
        const float powerLimited = maxPowerKW_ * 1000.0f * std::fabs(controls.throttle) / std::max(1.0f, std::fabs(s.speed));
        force = std::min(maxForceN_ * std::fabs(controls.throttle), powerLimited) * signf(controls.throttle);
    }
    const float tractionMax = mu * massKg_ * g * cosSlope;
    s.slip = saturate(std::fabs(force) / std::max(1.0f, tractionMax) - 1.0f);
    force = clampf(force, -tractionMax, tractionMax);
    const float resist = crr * massKg_ * g * cosSlope + 0.5f * kAirDensity * dragArea_ * s.speed * s.speed + wq.depth * 2500.0f * std::fabs(s.speed);
    const float brake = controls.brake * mu * massKg_ * g;
    float a = (force - massKg_ * g * sinSlope) / massKg_;
    const float opposing = (resist + brake) / massKg_;
    if (s.speed > 0.0f)
    {
        a -= opposing;
    }
    else if (s.speed < 0.0f)
    {
        a += opposing;
    }
    else if (std::fabs(a) < opposing)
    {
        a = 0.0f;
    }
    const float before = s.speed;
    s.speed += a * dt;
    if ((before > 0.0f && s.speed < 0.0f && controls.throttle >= 0.0f) || (before < 0.0f && s.speed > 0.0f && controls.throttle <= 0.0f))
    {
        s.speed = 0.0f;
    }
    // Stuck: deep sinkage with wheels spinning.
    s.stuck = sink > 0.35f * wheelRadiusM && std::fabs(s.speed) < 0.2f && std::fabs(controls.throttle) > 0.5f;
    if (s.stuck && controls.winch)
    {
        s.speed = 0.4f; // winching out
        s.stuck = false;
    }
    // Kinematic bicycle model, lateral grip limited.
    const float maxSteer = 0.6f;
    float yawRate = s.speed / wheelbaseM * std::tan(controls.steer * maxSteer);
    if (std::fabs(s.speed) > 0.5f)
    {
        yawRate = clampf(yawRate, -mu * g / std::fabs(s.speed), mu * g / std::fabs(s.speed));
    }
    s.yawRate = yawRate;
    s.heading = wrapAngle(s.heading + yawRate * dt);
    Vec2 next = p + Vec2::fromHeading(s.heading) * (s.speed * dt);
    next = ctx.terrain->clampToBounds(next, 5.0f);
    s.position = Vec3{next, ctx.terrain->heightAt(next) - sink * 0.5f};
    s.pitch = std::atan(grade);
    s.odometerM += static_cast<double>(std::fabs(s.speed) * dt);
    // Energy: traction power plus losses.
    const float tractionW = std::fabs(force * s.speed) / 0.85f;
    s.batteryKWh = std::max(0.0f, s.batteryKWh - tractionW * dt / 3.6e6f);
    // Ruts and trampling: the rover changes the ground like any heavy animal.
    if (std::fabs(s.speed) > 0.2f && s.odometerM - s.lastRut > 2.0)
    {
        s.lastRut = s.odometerM;
        for (const float side : {-1.0f, 1.0f})
        {
            Footprint f;
            f.position = next + Vec2::fromHeading(s.heading).perp() * (side * 1.0f);
            f.heading = s.heading;
            f.speciesIndex = -1;
            f.maker = s.id;
            f.lengthM = 0.5f;
            f.widthM = wheelWidthM;
            f.depthM = sink;
            f.sharpness = 0.8f;
            f.substrate = ctx.terrain->substrateAt(f.position);
            f.left = side < 0.0f;
            f.time = ctx.now;
            f.makerSpeedMs = std::fabs(s.speed);
            ctx.tracks->add(f);
            ctx.vegetation->trample(f.position, massKg_ / 2.0f);
        }
        if (wq.depth > 0.05f)
        {
            ctx.water->addDisturbance(next, massKg_ * 0.01f * std::fabs(s.speed), ctx.now);
        }
    }
}

void VehicleSystem::setTransform(const Vec3& position, float heading, float speed)
{
    state_.position = position;
    state_.heading = heading;
    state_.speed = speed;
}

Emitter VehicleSystem::emitter() const
{
    Emitter e;
    e.id = state_.id;
    e.position = state_.position;
    e.velocity = Vec2::fromHeading(state_.heading) * state_.speed;
    e.heading = state_.heading;
    e.heightM = 3.2f;
    e.massKg = massKg_;
    e.noiseDb = state_.noiseDb;
    e.noiseHz = state_.generatorOn ? 90.0f : 220.0f;
    e.scent = state_.generatorOn ? 3.0f : 0.8f;
    e.lightsOn = state_.lightsOn;
    e.active = true;
    e.observer = true;
    e.observerRadiusM = 400.0f;
    return e;
}

// ---------------------------------------------------------------- Drone

const char* droneModeLabelFr(DroneMode m)
{
    switch (m)
    {
    case DroneMode::Docked: return "Au dock";
    case DroneMode::Hover: return "Stationnaire";
    case DroneMode::Manual: return "Manuel";
    case DroneMode::Orbit: return "Orbite";
    case DroneMode::Follow: return "Suivi";
    case DroneMode::Survey: return "Cartographie";
    case DroneMode::ReturnHome: return "Retour";
    case DroneMode::Landing: return "Atterrissage";
    case DroneMode::Lost: return "Perdu";
    }
    return "?";
}

void DroneSystem::configure(const JsonValue& spec)
{
    massKg_ = spec.getFloat("mass_kg", massKg_);
    rotorDiameterM_ = spec.getFloat("rotor_diameter_m", rotorDiameterM_);
    rotors_ = spec.getInt("rotors", rotors_);
    batteryCapacityWh_ = spec.getFloat("battery_wh", batteryCapacityWh_);
    maxSpeedMs_ = spec.getFloat("max_speed_ms", maxSpeedMs_);
    maxWindMs_ = spec.getFloat("max_wind_ms", maxWindMs_);
    radioRangeM_ = spec.getFloat("radio_range_m", radioRangeM_);
    noiseDbAt1m_ = spec.getFloat("noise_db_1m", noiseDbAt1m_);
    bladePassHz_ = spec.getFloat("blade_pass_hz", bladePassHz_);
    rainLimitMmH_ = spec.getFloat("rain_limit_mm_h", rainLimitMmH_);
    state_.batteryWh = batteryCapacityWh_;
}

float DroneSystem::hoverPowerW() const
{
    // Actuator-disk (momentum) theory: P = T^1.5 / sqrt(2 rho A), divided by a figure of merit ~0.55.
    const float thrust = massKg_ * kGravity;
    const float area = static_cast<float>(rotors_) * kPi * square(0.5f * rotorDiameterM_);
    return std::pow(thrust, 1.5f) / std::sqrt(2.0f * kAirDensity * area) / 0.55f;
}

bool DroneSystem::launch(const Vec3& from, double now)
{
    (void)now;
    if (state_.mode != DroneMode::Docked || state_.batteryWh < batteryCapacityWh_ * 0.25f)
    {
        return false;
    }
    state_.position = from + Vec3{0.0f, 0.0f, 2.0f};
    state_.velocity = Vec3{};
    state_.mode = DroneMode::Hover;
    state_.flightTimeS = 0.0;
    state_.warningFr.clear();
    return true;
}

void DroneSystem::command(DroneMode mode)
{
    if (state_.mode == DroneMode::Docked || state_.mode == DroneMode::Lost)
    {
        return;
    }
    state_.mode = mode;
}

void DroneSystem::setManual(const Vec2& velocity, float altitude)
{
    state_.manualVelocity = velocity;
    state_.targetAltitudeM = altitude;
    command(DroneMode::Manual);
}

void DroneSystem::setOrbit(const Vec2& center, float radius, float altitude)
{
    state_.orbitCenter = center;
    state_.orbitRadiusM = radius;
    state_.targetAltitudeM = altitude;
    command(DroneMode::Orbit);
}

void DroneSystem::setFollow(EntityId target, float altitude)
{
    state_.followTarget = target;
    state_.targetAltitudeM = altitude;
    state_.followLock = true;
    state_.lostLockSince = -1.0;
    command(DroneMode::Follow);
}

void DroneSystem::setSurvey(const Rect2& area, float altitude)
{
    state_.surveyArea = area;
    state_.surveyLeg = 0;
    state_.targetAltitudeM = altitude;
    command(DroneMode::Survey);
}

void DroneSystem::dock(float rechargeFraction)
{
    state_.mode = DroneMode::Docked;
    state_.velocity = Vec3{};
    state_.batteryWh = std::min(batteryCapacityWh_, state_.batteryWh + batteryCapacityWh_ * rechargeFraction);
}

void DroneSystem::update(float dt, const WorldContext& ctx, const Vec3& home, const Vec3& controller, bool followVisible, const Vec2& followTruthPos)
{
    DroneState& d = state_;
    if (d.mode == DroneMode::Docked || d.mode == DroneMode::Lost)
    {
        d.noiseDb = 0.0f;
        d.position = d.mode == DroneMode::Docked ? home : d.position;
        return;
    }
    d.flightTimeS += dt;
    const WeatherState& w = ctx.weather->state();
    const Vec2 p = d.position.xy();
    const float ground = ctx.terrain->heightAt(p);
    d.altitudeAglM = d.position.z - ground;
    // Safety logic: battery, rain, signal.
    const float distCtrl = distance(d.position, controller);
    const bool los = ctx.terrain->lineOfSight(controller + Vec3{0.0f, 0.0f, 1.0f}, d.position, 20.0f);
    d.signal = saturate(1.0f - distCtrl / radioRangeM_) * (los ? 1.0f : 0.3f);
    d.warningFr.clear();
    if (d.batteryWh < batteryCapacityWh_ * 0.2f && d.mode != DroneMode::Landing)
    {
        d.mode = DroneMode::ReturnHome;
        d.warningFr = "Batterie faible : retour automatique.";
    }
    if (w.precipMmPerHour > rainLimitMmH_ && d.mode != DroneMode::Landing)
    {
        d.mode = DroneMode::ReturnHome;
        d.warningFr = "Pluie au-delà de la limite d'étanchéité : retour automatique.";
    }
    if (d.signal < 0.05f && d.mode != DroneMode::ReturnHome && d.mode != DroneMode::Landing)
    {
        d.mode = DroneMode::ReturnHome;
        d.warningFr = "Liaison radio perdue : retour automatique.";
    }
    // Desired horizontal velocity per mode.
    Vec2 desired;
    float targetAlt = ground + d.targetAltitudeM;
    switch (d.mode)
    {
    case DroneMode::Hover: break;
    case DroneMode::Manual: desired = d.manualVelocity; break;
    case DroneMode::Orbit:
    {
        const Vec2 rel = p - d.orbitCenter;
        const float r = std::max(1.0f, rel.length());
        const Vec2 tangent = rel.perp() / r;
        desired = tangent * std::min(8.0f, maxSpeedMs_) + rel / r * (d.orbitRadiusM - r) * 0.5f;
        break;
    }
    case DroneMode::Follow:
    {
        if (followVisible)
        {
            d.followTargetPos = followTruthPos;
            d.followLock = true;
            d.lostLockSince = -1.0;
        }
        else if (d.lostLockSince < 0.0)
        {
            d.lostLockSince = ctx.now;
        }
        if (d.lostLockSince >= 0.0 && ctx.now - d.lostLockSince > 30.0)
        {
            d.followLock = false;
            d.mode = DroneMode::Hover;
            d.warningFr = "Cible perdue de vue : vol stationnaire.";
        }
        const Vec2 offset = d.followTargetPos - p;
        desired = offset.normalized() * std::min(maxSpeedMs_, std::max(0.0f, offset.length() - 25.0f) * 0.5f);
        break;
    }
    case DroneMode::Survey:
    {
        // Lawnmower pattern with 80 m line spacing.
        const Rect2& a = d.surveyArea;
        const float spacing = 80.0f;
        const int legs = std::max(1, static_cast<int>((a.max.y - a.min.y) / spacing));
        const int leg = std::min(d.surveyLeg, legs);
        const float y = a.min.y + static_cast<float>(leg) * spacing;
        const float x = (leg % 2 == 0) ? a.max.x : a.min.x;
        const Vec2 wp{x, y};
        if (distance(p, wp) < 10.0f)
        {
            ++d.surveyLeg;
            if (d.surveyLeg > legs)
            {
                d.mode = DroneMode::ReturnHome;
            }
        }
        desired = (wp - p).normalized() * std::min(10.0f, maxSpeedMs_);
        break;
    }
    case DroneMode::ReturnHome:
    {
        const Vec2 off = home.xy() - p;
        desired = off.normalized() * std::min(maxSpeedMs_, off.length() * 0.5f + 1.0f);
        targetAlt = std::max(ground + 30.0f, home.z + 25.0f);
        if (off.length() < 5.0f)
        {
            d.mode = DroneMode::Landing;
        }
        break;
    }
    case DroneMode::Landing:
        targetAlt = home.z;
        desired = (home.xy() - p) * 0.5f;
        if (d.position.z - home.z < 0.5f)
        {
            dock(0.0f);
            return;
        }
        break;
    default: break;
    }
    // Wind drift beyond the drone's authority.
    const float windExcess = std::max(0.0f, w.windSpeedMs + w.gustMs - maxWindMs_);
    const Vec2 windDrift = w.wind.normalized() * windExcess;
    if (desired.length() > maxSpeedMs_)
    {
        desired = desired.normalized() * maxSpeedMs_;
    }
    const Vec2 vh = Vec2{d.velocity.x, d.velocity.y};
    const Vec2 dv = desired + windDrift - vh;
    const float maxA = 4.0f;
    const Vec2 a = dv.length() > maxA * dt ? dv.normalized() * maxA : dv / std::max(dt, 1e-3f);
    const Vec2 nv = vh + a * dt;
    float vz = clampf((targetAlt - d.position.z) * 0.8f, -3.0f, 4.0f);
    // Never fly into terrain.
    if (d.position.z + vz * dt < ground + 3.0f)
    {
        vz = std::max(vz, 2.0f);
    }
    d.velocity = Vec3{nv.x, nv.y, vz};
    d.position += d.velocity * dt;
    if (nv.lengthSq() > 0.25f)
    {
        d.heading = nv.heading();
    }
    // Power: hover + forward-flight/wind penalty + climb.
    const Vec2 airVel = nv - w.wind;
    const float airSpeed = airVel.length();
    d.powerW = hoverPowerW() * (1.0f + 0.25f * square(airSpeed / maxSpeedMs_)) + std::max(0.0f, massKg_ * kGravity * vz / 0.6f);
    d.batteryWh = std::max(0.0f, d.batteryWh - d.powerW * dt / 3600.0f);
    if (d.batteryWh <= 0.0f)
    {
        d.mode = DroneMode::Lost;
        d.warningFr = "Batterie épuisée : drone posé en urgence.";
        d.position.z = ground;
    }
    // Rotor noise rises with power demand.
    d.noiseDb = noiseDbAt1m_ + 10.0f * std::log10(std::max(0.2f, d.powerW / hoverPowerW()));
}

Emitter DroneSystem::emitter() const
{
    Emitter e;
    e.id = state_.id;
    e.position = state_.position;
    e.velocity = Vec2{state_.velocity.x, state_.velocity.y};
    e.heading = state_.heading;
    e.heightM = 0.5f;
    e.massKg = massKg_;
    e.noiseDb = state_.noiseDb;
    e.noiseHz = bladePassHz_;
    e.scent = 0.0f;
    e.airborne = true;
    e.active = state_.mode != DroneMode::Docked && state_.mode != DroneMode::Lost;
    e.observer = e.active;
    e.observerRadiusM = 300.0f;
    return e;
}

const char* sensorKindLabelFr(SensorKind k)
{
    switch (k)
    {
    case SensorKind::CameraTrap: return "Piège photographique";
    case SensorKind::AudioRecorder: return "Enregistreur acoustique";
    case SensorKind::WeatherStation: return "Station météo";
    }
    return "?";
}
} // namespace noctis
