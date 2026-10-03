// Physical locomotion and soft-tissue secondary motion.
//
// Mass is felt because it is simulated: acceleration and braking are capped by leg force and by
// ground traction (substrate mechanics), yaw acceleration by rotational inertia (I = k m L^2,
// cf. Carrier et al. 2001), slopes and mud cost speed and energy, gait follows dynamic similarity
// (Froude number; stride/hip = 2.3 Fr^0.3, Alexander), and every footfall loads the substrate,
// sinks to a depth given by Bekker's pressure–sinkage law, leaves a footprint and makes a sound.
#include "Noctis/Creatures/CreatureLogic.h"

#include <cmath>

namespace noctis
{
namespace locomotionimpl
{
float frac(float v) { return v - std::floor(v); }

void footfallSound(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, const Vec3& pos, float froude, Substrate sub, bool inWater)
{
    SoundSource s;
    s.kind = inWater ? SoundKind::Splash : SoundKind::Footfall;
    s.emitter = c.id;
    s.speciesIndex = static_cast<i16>(sp.index);
    s.position = pos;
    s.startTime = ctx.now;
    s.durationS = 0.25f;
    float level = 35.0f + 12.0f * std::log10(std::max(0.01f, c.massKg)) + 6.0f * std::sqrt(std::max(0.0f, froude));
    if (sub == Substrate::Mud || sub == Substrate::Peat)
    {
        level -= 6.0f;
    }
    else if (sub == Substrate::Gravel || sub == Substrate::Rock)
    {
        level += 3.0f;
    }
    if (ctx.vegetation && ctx.vegetation->cover(pos.xy()) > 0.4f)
    {
        level += 4.0f; // brushing and snapping vegetation
    }
    if (inWater)
    {
        level += 4.0f;
    }
    s.levelDb = level;
    s.f0Hz = inWater ? 400.0f : 600.0f * std::pow(std::max(0.01f, c.massKg), -0.25f) + 30.0f;
    s.bandwidthHz = s.f0Hz * 2.0f;
    s.tonal = false;
    s.seed = c.rng.nextU32();
    ctx.sound->emit(s);
}
} // namespace locomotionimpl

void integrateLocomotion(Creature& c, const SpeciesRuntime& sp, const WorldContext& ctx, float dt, bool withFeet)
{
    using namespace locomotionimpl;
    if (!c.alive || dt <= 0.0f)
    {
        return;
    }
    LocomotionState& L = c.loco;
    const SpeciesSim& s = sp.sim();
    const SpeciesSim::Locomotion& lo = s.locomotion;
    const Vec2 p = c.pos2();
    const float m = std::max(0.01f, c.massKg);
    const float hip = std::max(0.03f, c.hipHeightM);
    const float len = std::max(0.05f, c.lengthM);

    const TerrainQuery tq = ctx.terrain->query(p);
    const SoilMechanics soil = soilMechanics(tq.substrate, tq.moisture);
    const WaterQuery wq = ctx.water ? ctx.water->query(p, *ctx.terrain) : WaterQuery{};
    L.waterDepth = wq.depth;
    const bool canSwim = lo.swim && lo.swimSpeedMs > 0.0f;
    L.swimming = canSwim && wq.depth > hip * 0.9f;
    float mu = soil.tractionMu * (wq.depth > 0.05f ? 0.85f : 1.0f);
    if (L.swimming)
    {
        mu = 0.4f; // propulsion against water rather than ground
    }

    // --- Speed capacity ---
    const float sizeScale = std::sqrt(hip / std::max(0.03f, s.hipHeightM));
    float vmax = lo.maxSpeedMs * sizeScale * c.injuries.speedFactor * (1.0f - 0.45f * c.phys.fatigue) *
                 (0.6f + 0.4f * saturate(c.phys.condition * 2.0f));
    vmax *= 1.0f - 0.7f * saturate(L.sinkageM / (0.4f * hip)); // deep mud
    if (L.swimming)
    {
        vmax = lo.swimSpeedMs * sizeScale * c.injuries.speedFactor;
    }
    else if (wq.depth > 0.05f)
    {
        vmax *= 1.0f - 0.6f * saturate(wq.depth / hip); // wading drag
    }
    const Vec2 fwd = Vec2::fromHeading(L.heading);
    const float probe = std::max(1.0f, len * 0.3f);
    const float grade = (ctx.terrain->heightAt(p + fwd * probe) - ctx.terrain->heightAt(p - fwd * probe)) / (2.0f * probe);
    const float maxGrade = std::tan(lo.maxSlopeDeg * kDegToRad);
    if (grade > 0.0f && !L.swimming)
    {
        vmax *= 1.0f - 0.6f * saturate(grade / std::max(0.05f, maxGrade));
    }

    // --- Yaw: rotational inertia and traction limit turning ---
    const float desiredSpeed = std::min(c.behavior.desiredSpeed, std::max(0.0f, vmax));
    const float desiredHeading = c.behavior.desiredDir.lengthSq() > 1e-6f ? c.behavior.desiredDir.heading() : L.heading;
    const float alphaMax = std::max(0.15f, mu * kGravity * 0.3f * hip / (sp.yawInertiaK * len * len));
    const float speedFrac = vmax > 0.0f ? saturate(L.speed / vmax) : 0.0f;
    float yawMax = lo.maxYawRateDps * kDegToRad * lerpf(1.0f, 0.5f, speedFrac);
    if (L.speed > 0.5f)
    {
        yawMax = std::min(yawMax, mu * kGravity * 0.7f / L.speed); // centripetal traction limit
    }
    const float err = angleDelta(L.heading, desiredHeading);
    const float targetYawRate = clampf(err * 2.5f, -yawMax, yawMax);
    L.yawRate += clampf(targetYawRate - L.yawRate, -alphaMax * dt, alphaMax * dt);
    L.heading = wrapAngle(L.heading + L.yawRate * dt);

    // --- Forward speed: leg force and traction limit acceleration and braking ---
    const float turnFactor = std::max(0.15f, std::cos(std::min(std::fabs(err), kHalfPi)));
    const float targetSpeed = desiredSpeed * turnFactor;
    const float aMax = std::min(lo.maxAccelMs2 * c.injuries.speedFactor * (1.0f - 0.4f * c.phys.fatigue), mu * kGravity);
    const float dMax = std::min(lo.maxDecelMs2, mu * kGravity * 1.1f);
    const float requested = (targetSpeed - L.speed) / dt;
    float a = clampf(requested, -dMax, aMax);
    if (!L.swimming && L.speed > 0.1f)
    {
        a -= kGravity * grade * 0.5f; // partial compensation of slope
    }
    // Slipping: the animal asks more of the ground than traction allows (mud, slopes).
    L.slip = std::max(0.0f, L.slip - dt * 2.0f);
    if (std::fabs(requested) > mu * kGravity * 1.15f && L.speed > 1.5f && !L.swimming)
    {
        L.slip = saturate(L.slip + dt * (std::fabs(requested) / (mu * kGravity) - 1.0f));
        if (L.slip > 0.6f && c.rng.chance(dt * 0.5f))
        {
            SimEvent e;
            e.type = EventType::Slip;
            e.time = ctx.now;
            e.subject = c.id;
            e.position = L.position;
            e.magnitude = L.slip;
            ctx.events->emit(e);
        }
    }
    const float oldSpeed = L.speed;
    L.speed = clampf(L.speed + a * dt, 0.0f, std::max(0.0f, vmax * 1.05f + (oldSpeed > vmax ? oldSpeed - vmax : 0.0f)));
    const Vec2 oldVel = L.velocity;
    Vec2 next = p + Vec2::fromHeading(L.heading) * (L.speed * dt);

    // --- Constraints: water too deep for non-swimmers, impassable slopes, world bounds ---
    if (ctx.water && !canSwim)
    {
        const float d = ctx.water->depthAt(next, *ctx.terrain);
        if (d > hip * 0.95f && d > wq.depth)
        {
            next = p;
            L.speed *= 0.3f;
        }
    }
    if (!L.swimming)
    {
        const float rise = ctx.terrain->heightAt(next) - tq.height;
        const float stepLen = std::max(0.01f, distance(next, p));
        if (rise > 0.0f && rise / stepLen > maxGrade * 1.6f)
        {
            next = p;
            L.speed *= 0.3f;
        }
    }
    next = ctx.terrain->clampToBounds(next, 2.0f);
    L.velocity = (next - p) / dt;
    L.acceleration = (L.velocity - oldVel) / dt;

    // --- Vertical placement ---
    const float groundZ = ctx.terrain->heightAt(next);
    if (L.swimming && ctx.water)
    {
        const WaterQuery nq = ctx.water->query(next, *ctx.terrain);
        L.position = Vec3{next, nq.surfaceZ - 0.55f * hip};
    }
    else
    {
        L.position = Vec3{next, groundZ - L.sinkageM * 0.5f};
    }

    // --- Body attitude from terrain and inertial lean ---
    const Vec3 n = ctx.terrain->normalAt(next);
    const Vec2 left = Vec2::fromHeading(L.heading).perp();
    const float slopeAlong = -(n.x * std::cos(L.heading) + n.y * std::sin(L.heading)) / std::max(0.2f, n.z);
    const float slopeAcross = -(n.x * left.x + n.y * left.y) / std::max(0.2f, n.z);
    const float aFwd = dot(L.acceleration, Vec2::fromHeading(L.heading));
    const float targetPitch = std::atan(slopeAlong) * 0.8f - 0.12f * clampf(aFwd / kGravity, -1.0f, 1.0f);
    const float targetRoll = std::atan(slopeAcross) * 0.6f + 0.4f * clampf(L.speed * L.yawRate / kGravity, -0.8f, 0.8f);
    L.pitch = lerpf(L.pitch, targetPitch, approachFactor(dt, 0.25f));
    L.roll = lerpf(L.roll, targetRoll, approachFactor(dt, 0.25f));

    // --- Gait by dynamic similarity ---
    const float v = L.speed;
    L.froude = v * v / (kGravity * hip);
    const float runBlend = saturate((L.froude - 0.15f) / 0.85f);
    L.dutyFactor = lerpf(lo.dutyFactorWalk, lo.dutyFactorRun, runBlend);
    if (L.swimming)
    {
        L.gait = Gait::Swim;
    }
    else if (v < 0.05f)
    {
        L.gait = Gait::Stand;
    }
    else if (L.froude < 0.15f)
    {
        L.gait = Gait::Walk;
    }
    else if (L.dutyFactor >= 0.5f)
    {
        L.gait = Gait::FastWalk; // grounded locomotion: no aerial phase
    }
    else
    {
        L.gait = Gait::Run;
    }
    const float relStride = std::max(0.5f, 2.3f * std::pow(std::max(1e-4f, L.froude), 0.3f));
    L.strideLengthM = relStride * hip;
    L.strideFrequencyHz = v > 0.05f ? v / L.strideLengthM : 0.0f;
    const float prevPhase = L.gaitPhase;
    L.gaitPhase = frac(L.gaitPhase + L.strideFrequencyHz * dt);

    // --- Feet, footfalls, footprints ---
    const int legs = sp.legCount;
    L.footCount = static_cast<u8>(legs);
    const float footArea = std::max(1e-4f, s.footLengthM * s.footWidthM * sp.footAreaFill) * std::pow(hip / std::max(0.03f, s.hipHeightM), 2.0f);
    const float dynamicFactor = 1.0f + 0.6f * std::min(L.froude, 2.0f);
    int stanceCount = 0;
    for (int i = 0; i < legs; ++i)
    {
        const float off = lerpf(sp.walkPhase[static_cast<size_t>(i)], sp.runPhase[static_cast<size_t>(i)], runBlend);
        const float ph = frac(L.gaitPhase + off);
        if (ph < L.dutyFactor || L.gait == Gait::Stand)
        {
            ++stanceCount;
        }
    }
    stanceCount = std::max(1, stanceCount);
    const float footScale = hip / std::max(0.03f, s.hipHeightM);
    for (int i = 0; i < legs; ++i)
    {
        FootState& f = L.feet[static_cast<size_t>(i)];
        f.left = sp.footIsLeft[static_cast<size_t>(i)];
        f.fore = sp.footIsFore[static_cast<size_t>(i)];
        const float off = lerpf(sp.walkPhase[static_cast<size_t>(i)], sp.runPhase[static_cast<size_t>(i)], runBlend);
        f.phaseOffset = off;
        const float ph = frac(L.gaitPhase + off);
        const float prevPh = frac(prevPhase + off);
        const bool stance = ph < L.dutyFactor || L.gait == Gait::Stand || L.gait == Gait::Swim;
        const Vec2 local = sp.footOffsets[static_cast<size_t>(i)] * len;
        const float lead = 0.5f * L.strideLengthM * L.dutyFactor;
        const Vec2 placement = next + rotate(Vec2{local.x + lead, local.y}, L.heading);
        // Touchdown when phase wraps into stance.
        const bool touchdown = stance && v > 0.05f && (prevPh > ph || prevPh >= L.dutyFactor) && L.gait != Gait::Swim;
        if (touchdown)
        {
            const float share = f.fore ? 0.45f : (legs == 2 ? 1.0f : 0.55f);
            const float area = footArea * (f.fore ? 0.6f : 1.0f);
            const float pressureKPa = m * kGravity * share * dynamicFactor / area / 1000.0f;
            const float width = s.footWidthM * footScale * (f.fore ? 0.7f : 1.0f);
            const float sink = ctx.terrain->sinkage(placement, pressureKPa, width);
            L.sinkageM = lerpf(L.sinkageM, sink, 0.5f);
            f.planted = Vec3{placement, ctx.terrain->heightAt(placement) - sink};
            f.contactForceN = m * kGravity * share * dynamicFactor;
            const bool inWater = ctx.water && ctx.water->depthAt(placement, *ctx.terrain) > 0.05f;
            if (withFeet || c.lod == LodLevel::Near)
            {
                Footprint fp;
                fp.position = placement;
                fp.heading = L.heading;
                fp.speciesIndex = static_cast<i16>(sp.index);
                fp.maker = c.id;
                fp.lengthM = s.footLengthM * footScale * (f.fore ? 0.6f : 1.0f);
                fp.widthM = width;
                fp.depthM = inWater ? sink * 0.5f : sink;
                fp.sharpness = saturate(0.4f + soil.trackRetention * 0.6f);
                fp.substrate = ctx.terrain->substrateAt(placement);
                fp.left = f.left;
                fp.manus = f.fore;
                fp.time = ctx.now;
                fp.makerSpeedMs = v;
                c.pendingFootprints.push_back(fp);
            }
            if (withFeet)
            {
                footfallSound(c, sp, ctx, f.planted, L.froude, tq.substrate, inWater);
            }
        }
        if (stance)
        {
            f.inContact = true;
            f.swing = 0.0f;
            if (L.gait == Gait::Stand)
            {
                f.planted = Vec3{next + rotate(local, L.heading), groundZ};
                f.contactForceN = m * kGravity / static_cast<float>(legs);
            }
            else
            {
                f.contactForceN = m * kGravity * dynamicFactor / static_cast<float>(stanceCount);
            }
            f.target = f.planted;
        }
        else
        {
            f.inContact = false;
            f.contactForceN = 0.0f;
            f.swing = saturate((ph - L.dutyFactor) / std::max(0.05f, 1.0f - L.dutyFactor));
            const Vec2 nextPlacement = next + rotate(Vec2{local.x + lead + L.strideLengthM * (1.0f - f.swing) * 0.5f, local.y}, L.heading);
            f.target = Vec3{nextPlacement, ctx.terrain->heightAt(nextPlacement)};
        }
        c.soft.muscleActivation[static_cast<size_t>(i)] = saturate(f.contactForceN / (m * kGravity));
    }
    // Far LOD: no feet, but animals still wear trails (zero-depth pseudo footprints, one per ~15 m).
    if (!withFeet && c.lod == LodLevel::Far && v > 0.1f)
    {
        const double before = L.distanceTravelled;
        if (std::floor((before + v * dt) / 15.0) > std::floor(before / 15.0))
        {
            Footprint fp;
            fp.position = next;
            fp.maker = c.id;
            fp.speciesIndex = static_cast<i16>(sp.index);
            fp.depthM = 0.0f;
            fp.time = ctx.now;
            c.pendingFootprints.push_back(fp);
        }
    }

    // Vertical COM oscillation (inverted-pendulum walking, bouncing running).
    const float stepsPerStride = legs == 2 ? 2.0f : 2.0f;
    const float bobAmp = hip * (L.gait == Gait::Run ? 0.05f : 0.025f) * saturate(v / std::max(0.1f, lo.walkSpeedMs));
    L.bodyBob = bobAmp * std::cos(kTwoPi * stepsPerStride * L.gaitPhase) * (L.gait == Gait::Run ? -1.0f : 1.0f);
    L.comHeight = hip * 1.05f + L.bodyBob;

    // --- Energetics (Taylor, Heglund & Maloiy 1982 net cost of transport; slope work; mud/water penalty) ---
    const float cot = 10.7f * std::pow(m, -0.316f); // J kg^-1 m^-1
    const float mudFactor = 1.0f + 2.0f * saturate(L.sinkageM / (0.5f * hip)) + (wq.depth > 0.05f ? saturate(wq.depth / hip) : 0.0f);
    const float climb = std::max(0.0f, m * kGravity * v * grade) / 0.25f;
    L.metabolicCostW = cot * m * v * mudFactor * (L.swimming ? 1.5f : 1.0f) + climb;
    L.distanceTravelled += static_cast<double>(v * dt);
}

void updateSoftTissue(Creature& c, const SpeciesRuntime& sp, float dt)
{
    const LocomotionState& L = c.loco;
    SoftTissueState& st = c.soft;
    const Vec2 fwd = Vec2::fromHeading(L.heading);
    const Vec2 left = fwd.perp();
    const float aF = dot(L.acceleration, fwd);
    const float aL = dot(L.acceleration, left) + L.speed * L.yawRate;
    // Vertical acceleration of the trunk from the COM oscillation (second derivative of bob).
    const float stepHz = L.strideFrequencyHz * 2.0f;
    const float aV = -square(kTwoPi * stepHz) * L.bodyBob;
    const float sway = std::sin(kTwoPi * L.gaitPhase) * std::min(L.speed, 6.0f) * 0.6f;
    const float fat = 0.6f + 0.8f * saturate(c.phys.condition);
    const std::array<float, kSoftTissueNodeCount> input = {aV * fat, aL * fat, aV + 0.5f * aF, aL, aV, aL + sway};
    for (int i = 0; i < kSoftTissueNodeCount; ++i)
    {
        const SoftTissueSpec& spec = sp.soft[static_cast<size_t>(i)];
        const float w = kTwoPi * spec.naturalHz;
        const int sub = std::max(1, static_cast<int>(std::ceil(dt * w / 0.3f)));
        const float h = dt / static_cast<float>(sub);
        float x = st.offset[static_cast<size_t>(i)];
        float vel = st.velocity[static_cast<size_t>(i)];
        for (int k = 0; k < sub; ++k)
        {
            const float acc = -w * w * x - 2.0f * spec.damping * w * vel - spec.gain * input[static_cast<size_t>(i)];
            vel += acc * h;
            x += vel * h;
        }
        // Physical bound on displacement (tissue cannot stretch indefinitely).
        const float limit = c.lengthM * 0.05f;
        st.offset[static_cast<size_t>(i)] = clampf(x, -limit, limit);
        st.velocity[static_cast<size_t>(i)] = vel;
    }
}
} // namespace noctis
