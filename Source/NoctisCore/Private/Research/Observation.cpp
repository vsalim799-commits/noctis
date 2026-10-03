#include "Noctis/Research/Observation.h"

#include "Noctis/Creatures/CreatureLogic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace noctis
{
const char* observationMethodLabelFr(ObservationMethod m)
{
    switch (m)
    {
    case ObservationMethod::NakedEye: return "Œil nu";
    case ObservationMethod::Binoculars: return "Jumelles";
    case ObservationMethod::Photo: return "Photographie";
    case ObservationMethod::Video: return "Vidéo";
    case ObservationMethod::Drone: return "Drone";
    case ObservationMethod::CameraTrap: return "Piège photographique";
    case ObservationMethod::Audio: return "Enregistrement sonore";
    case ObservationMethod::Fieldwork: return "Relevé de terrain";
    }
    return "?";
}

const char* observedActionLabelFr(ObservedAction a)
{
    switch (a)
    {
    case ObservedAction::Standing: return "Immobile";
    case ObservedAction::Walking: return "Marche";
    case ObservedAction::Running: return "Course";
    case ObservedAction::Feeding: return "S'alimente";
    case ObservedAction::Drinking: return "Boit";
    case ObservedAction::Lying: return "Couché";
    case ObservedAction::Vigilant: return "Tête levée, balayage visuel";
    case ObservedAction::Vocalizing: return "Vocalise";
    case ObservedAction::Swimming: return "Nage";
    case ObservedAction::Attacking: return "Attaque";
    case ObservedAction::Fighting: return "Combat";
    case ObservedAction::Displaying: return "Posture de parade";
    case ObservedAction::Nesting: return "Au nid";
    case ObservedAction::Unknown: return "Indéterminé";
    }
    return "?";
}

// ---------------------------------------------------------------- Catalogue

float IndividualCatalog::score(const CatalogEntry& e, const ObservedSubject& s)
{
    if (e.speciesIndex != s.speciesIndex)
    {
        return 0.0f;
    }
    int matches = 0;
    int mismatches = 0;
    for (const ObservedFeature& f : s.features)
    {
        if (f.kind != FeatureKind::Scar && f.kind != FeatureKind::Marking)
        {
            continue;
        }
        bool found = false;
        bool regionSeen = false;
        for (const ObservedFeature& g : e.features)
        {
            if (g.kind == f.kind && g.region == f.region)
            {
                regionSeen = true;
                if (g.seed == f.seed)
                {
                    found = true;
                }
            }
        }
        if (found)
        {
            ++matches;
        }
        else if (regionSeen && f.kind == FeatureKind::Marking)
        {
            ++mismatches; // a different marking pattern in the same place
        }
    }
    if (matches == 0)
    {
        return 0.0f;
    }
    float sc = static_cast<float>(matches) / (static_cast<float>(matches) + 0.6f * static_cast<float>(mismatches) + 0.4f);
    if (e.lastLengthM > 0.0f && s.estimatedLengthM > 0.0f)
    {
        // Animals grow; allow for it, but big discrepancies lower the match.
        const float rel = std::fabs(s.estimatedLengthM - e.lastLengthM) / e.lastLengthM;
        sc *= std::exp(-std::max(0.0f, rel - 0.15f) * 3.0f);
    }
    return saturate(sc);
}

std::string IndividualCatalog::nicknameFor(const ObservedSubject& s, u64 seed)
{
    Rng r(seed, 0x1D);
    bool scarHead = false;
    bool scarTail = false;
    bool scarLimb = false;
    bool limp = false;
    int scars = 0;
    for (const ObservedFeature& f : s.features)
    {
        if (f.kind == FeatureKind::Scar)
        {
            ++scars;
            const BodyRegion reg = static_cast<BodyRegion>(f.region);
            scarHead = scarHead || reg == BodyRegion::Head || reg == BodyRegion::Jaw || reg == BodyRegion::Neck;
            scarTail = scarTail || reg == BodyRegion::Tail;
            scarLimb = scarLimb || reg == BodyRegion::HindlimbL || reg == BodyRegion::HindlimbR || reg == BodyRegion::ForelimbL || reg == BodyRegion::ForelimbR;
        }
        limp = limp || f.kind == FeatureKind::Limp;
    }
    if (limp)
    {
        return "Boiteux";
    }
    if (scars >= 3)
    {
        return "Trois-Cicatrices";
    }
    if (scarHead)
    {
        return r.chance(0.5f) ? "Balafre" : "Scar";
    }
    if (scarTail)
    {
        return "Queue-Marquée";
    }
    if (scarLimb)
    {
        return "Patte-Griffée";
    }
    static const char* const names[] = {"Brume", "Silex", "Ocre", "Fougère", "Grès", "Argile", "Saule", "Cendre", "Orage", "Aube", "Ambre", "Basalte"};
    return names[r.rangeInt(0, 11)];
}

std::vector<std::pair<u32, float>> IndividualCatalog::candidates(const ObservedSubject& s) const
{
    std::vector<std::pair<u32, float>> out;
    for (const CatalogEntry& e : entries_)
    {
        const float sc = score(e, s);
        if (sc > 0.05f)
        {
            out.push_back({e.id, sc});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

void IndividualCatalog::assign(ObservedSubject& s, u32 entryId, double time, const Vec2& pos, u32 observationId)
{
    CatalogEntry* e = findMutable(entryId);
    if (!e)
    {
        return;
    }
    s.catalogId = e->id;
    for (const ObservedFeature& f : s.features)
    {
        if (f.kind == FeatureKind::Juvenile)
        {
            continue;
        }
        bool known = false;
        for (const ObservedFeature& g : e->features)
        {
            known = known || (g.kind == f.kind && g.region == f.region && g.seed == f.seed);
        }
        if (!known)
        {
            e->features.push_back(f);
        }
    }
    e->lastSeen = time;
    e->observations.push_back(observationId);
    e->sightings.push_back(pos);
    e->sightingTimes.push_back(time);
    if (s.estimatedLengthM > 0.0f)
    {
        e->lastLengthM = s.estimatedLengthM;
    }
    ++assignments_;
    if (e->truth.valid() && s.truth.valid() && e->truth != s.truth)
    {
        ++wrongAssignments_;
    }
}

u32 IndividualCatalog::match(ObservedSubject& s, const SpeciesDefinition& species, double time, const Vec2& pos, u32 observationId, bool autoCreate)
{
    if (s.speciesIndex < 0)
    {
        return 0;
    }
    const auto cands = candidates(s);
    if (!cands.empty() && cands.front().second >= 0.55f)
    {
        s.matchConfidence = cands.front().second;
        assign(s, cands.front().first, time, pos, observationId);
        return s.catalogId;
    }
    int distinctive = 0;
    for (const ObservedFeature& f : s.features)
    {
        distinctive += (f.kind == FeatureKind::Scar || f.kind == FeatureKind::Marking) ? 1 : 0;
    }
    if (!autoCreate || distinctive == 0)
    {
        return 0; // not identifiable from this observation
    }
    CatalogEntry e;
    e.id = nextId_++;
    const int n = ++perSpecies_[s.speciesIndex];
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s-%03d", species.catalogPrefix.c_str(), n);
    e.code = buf;
    e.nickname = nicknameFor(s, static_cast<u64>(e.id) * 2654435761u);
    e.speciesIndex = s.speciesIndex;
    e.truth = s.truth;
    e.firstSeen = time;
    entries_.push_back(e);
    s.matchConfidence = 1.0f;
    assign(s, e.id, time, pos, observationId);
    return e.id;
}

bool IndividualCatalog::rename(u32 id, const std::string& nickname)
{
    CatalogEntry* e = findMutable(id);
    if (!e)
    {
        return false;
    }
    e->nickname = nickname;
    e->confirmedByPlayer = true;
    return true;
}

bool IndividualCatalog::merge(u32 keep, u32 absorb)
{
    CatalogEntry* a = findMutable(keep);
    CatalogEntry* b = findMutable(absorb);
    if (!a || !b || a == b || a->speciesIndex != b->speciesIndex)
    {
        return false;
    }
    a->features.insert(a->features.end(), b->features.begin(), b->features.end());
    a->observations.insert(a->observations.end(), b->observations.begin(), b->observations.end());
    a->sightings.insert(a->sightings.end(), b->sightings.begin(), b->sightings.end());
    a->sightingTimes.insert(a->sightingTimes.end(), b->sightingTimes.begin(), b->sightingTimes.end());
    a->firstSeen = std::min(a->firstSeen, b->firstSeen);
    a->lastSeen = std::max(a->lastSeen, b->lastSeen);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const CatalogEntry& e) { return e.id == absorb; }), entries_.end());
    return true;
}

const CatalogEntry* IndividualCatalog::find(u32 id) const
{
    for (const CatalogEntry& e : entries_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

CatalogEntry* IndividualCatalog::findMutable(u32 id)
{
    for (CatalogEntry& e : entries_)
    {
        if (e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

const CatalogEntry* IndividualCatalog::findByCode(const std::string& code) const
{
    for (const CatalogEntry& e : entries_)
    {
        if (e.code == code)
        {
            return &e;
        }
    }
    return nullptr;
}

float IndividualCatalog::misidentificationRate() const
{
    return assignments_ > 0 ? static_cast<float>(wrongAssignments_) / static_cast<float>(assignments_) : 0.0f;
}

// ---------------------------------------------------------------- Observation system

ObservedAction ObservationSystem::actionOf(const Creature& c, double now)
{
    if (now - c.behavior.lastCall < 3.0)
    {
        return ObservedAction::Vocalizing;
    }
    if (c.loco.swimming)
    {
        return ObservedAction::Swimming;
    }
    if (c.behavior.current == BehaviorId::Hunt && c.behavior.hunt == HuntPhase::Attack)
    {
        return ObservedAction::Attacking;
    }
    if (c.behavior.current == BehaviorId::Fight)
    {
        return ObservedAction::Fighting;
    }
    if (c.behavior.posture == Posture::Display)
    {
        return ObservedAction::Displaying;
    }
    if (c.behavior.current == BehaviorId::NestTend && c.loco.speed < 0.2f)
    {
        return ObservedAction::Nesting;
    }
    if (c.behavior.posture == Posture::Lying)
    {
        return ObservedAction::Lying;
    }
    if (c.behavior.posture == Posture::HeadDown)
    {
        return c.behavior.current == BehaviorId::Drink ? ObservedAction::Drinking : ObservedAction::Feeding;
    }
    if (c.behavior.posture == Posture::HeadUp && c.loco.speed < 0.5f)
    {
        return ObservedAction::Vigilant;
    }
    if (c.loco.gait == Gait::Run || c.loco.gait == Gait::FastWalk)
    {
        return ObservedAction::Running;
    }
    if (c.loco.speed > 0.15f)
    {
        return ObservedAction::Walking;
    }
    return ObservedAction::Standing;
}

EnvironmentSnapshot ObservationSystem::snapshotEnvironment(const WorldContext& ctx)
{
    EnvironmentSnapshot e;
    const WeatherState& w = ctx.weather->state();
    e.temperatureC = w.temperatureC;
    e.humidity = w.humidity;
    e.cloudCover = w.cloudCover;
    e.rainMmH = w.precipMmPerHour;
    e.windMs = w.windSpeedMs;
    e.windFromDeg = w.windFromDeg;
    e.visibilityM = w.visibilityM;
    e.lux = ctx.sky->state().illuminanceLux;
    e.sunElevationDeg = ctx.sky->state().sunElevationDeg;
    e.phase = ctx.sky->state().phase;
    e.hourOfDay = static_cast<float>(ctx.clock->hourOfDay());
    e.seasonFr = ctx.season->state().labelFr;
    return e;
}

OpticsSpec ObservationSystem::opticsFor(const PhotoSettings& s)
{
    OpticsSpec o;
    o.method = ObservationMethod::Photo;
    o.fovDeg = 2.0f * std::atan(s.sensorWidthMm / (2.0f * s.focalMm)) * kRadToDeg;
    o.magnification = 1.0f;
    o.acuityRad = 2.0f * (s.sensorWidthMm / static_cast<float>(s.widthPx)) / s.focalMm;
    o.rangeLimitM = 3000.0f;
    o.rangefinder = s.laserRangefinder;
    return o;
}

ObservationRecord ObservationSystem::observe(const OpticsSpec& optics, const Vec3& eye, float headingRad, const CreatureSystem& creatures,
                                             const WorldContext& ctx, const ObserverContext& observer, u32 expeditionId, Rng& rng)
{
    ObservationRecord rec;
    rec.time = ctx.now;
    rec.timeText = ctx.clock->formatted();
    rec.method = optics.method;
    rec.observerPos = eye;
    rec.viewHeadingDeg = headingRad * kRadToDeg;
    rec.fovDeg = optics.fovDeg;
    rec.magnification = optics.magnification;
    rec.env = snapshotEnvironment(ctx);
    rec.observer = observer;
    rec.expeditionId = expeditionId;

    const float acuity = optics.acuityRad / std::max(1.0f, optics.magnification);
    // Human scotopic vision is poor; optics gather some light.
    const float light = visualPerformance(ctx.sky->state().illuminanceLux * (optics.magnification > 1.0f ? 1.6f : 1.0f), 0.15f);
    const float halfFov = 0.5f * optics.fovDeg * kDegToRad;
    std::vector<u32> idx;
    creatures.neighbours(eye.xy(), optics.rangeLimitM, idx);
    std::sort(idx.begin(), idx.end());
    std::map<u32, std::vector<size_t>> byGroup;
    float qualitySum = 0.0f;
    for (const u32 i : idx)
    {
        const Creature& c = creatures.creatures()[i];
        if (!c.alive)
        {
            continue;
        }
        const Vec2 rel = c.pos2() - eye.xy();
        const float d = std::max(0.5f, rel.length());
        if (std::fabs(angleDelta(headingRad, rel.heading())) > halfFov)
        {
            continue;
        }
        const float sizeM = std::max(c.hipHeightM * 1.3f, c.lengthM * 0.6f);
        const float angular = sizeM / d;
        const float sizeInAcuity = angular / acuity;
        const float targetH = std::min(1.6f, c.hipHeightM * 0.8f);
        const float foliageT = ctx.vegetation->transmittance(eye.xy(), c.pos2(), targetH);
        const float fogT = std::exp(-ctx.weather->fogExtinctionAt(eye.z - ctx.valleyFloorZ, false) * d - 3.912f / 40000.0f * d);
        const Vec3 target{c.pos2(), c.loco.position.z + c.hipHeightM};
        const float los = (d > 20.0f && !ctx.terrain->lineOfSight(eye, target, std::max(4.0f, d / 40.0f))) ? 0.0f : 1.0f;
        const float detect = saturate((sizeInAcuity - 5.0f) / 20.0f) * light * foliageT * fogT * los;
        if (detect <= 0.0f || rng.uniform() > detect)
        {
            continue;
        }
        ObservedSubject s;
        s.truth = c.id;
        s.quality = saturate(sizeInAcuity / 150.0f) * light * foliageT * fogT;
        const SpeciesRuntime& sp = creatures.speciesOf(c);
        if (s.quality > 0.12f)
        {
            s.speciesIndex = c.speciesIndex;
            s.speciesConfidence = saturate(s.quality * 1.5f);
            // Poor views of similar-looking taxa can be confused (e.g. a small tyrannosaurid).
            if (s.quality < 0.45f && rng.chance(0.3f * (0.45f - s.quality) / 0.45f))
            {
                float best = 1e9f;
                for (const SpeciesRuntime& other : creatures.species())
                {
                    if (other.index == sp.index || other.sim().bodyPlan != sp.sim().bodyPlan || other.sim().role != SimRole::Agent)
                    {
                        continue;
                    }
                    const float ratio = std::fabs(std::log(other.sim().adultMassKg / std::max(0.01f, c.massKg)));
                    if (ratio < best && ratio < 0.7f)
                    {
                        best = ratio;
                        s.speciesIndex = static_cast<i16>(other.index);
                        s.speciesConfidence = saturate(s.quality);
                    }
                }
            }
        }
        // Resolvable features.
        for (const Scar& scar : c.injuries.scars)
        {
            const float scarSize = scar.size * c.lengthM * 0.12f;
            if ((scarSize / d) / acuity > 3.0f && s.quality > 0.25f && rng.chance(0.65f))
            {
                s.features.push_back({FeatureKind::Scar, static_cast<u8>(scar.region), scar.shapeSeed, scar.size});
            }
        }
        for (const Injury& inj : c.injuries.active)
        {
            if (inj.severity > 0.3f && s.quality > 0.3f && (inj.type == InjuryType::Laceration || inj.type == InjuryType::Puncture))
            {
                s.features.push_back({FeatureKind::Injury, static_cast<u8>(inj.region), 0u, inj.severity});
            }
        }
        const float limpThreshold = optics.method == ObservationMethod::Video ? 0.2f : 0.35f;
        if (c.injuries.limp > limpThreshold && c.loco.speed > 0.3f && s.quality > 0.3f)
        {
            s.features.push_back({FeatureKind::Limp, 0, 0u, c.injuries.limp});
        }
        if (ctx.toggle("individual_markings") && s.quality > 0.5f)
        {
            s.features.push_back({FeatureKind::Marking, 0, static_cast<u32>(c.markingSeed * 1e6f), 0.0f});
        }
        if (c.massKg < sp.sim().adultMassKg * 0.3f)
        {
            s.features.push_back({FeatureKind::Juvenile, 0, 0u, c.massKg / sp.sim().adultMassKg});
        }
        const float err = (optics.rangefinder ? 0.06f : 0.22f) * (1.0f - 0.5f * s.quality);
        s.estimatedLengthM = std::max(0.05f, c.lengthM * (1.0f + rng.normal(0.0f, err)));
        s.lengthErrorM = c.lengthM * err;
        s.action = actionOf(c, ctx.now);
        s.distanceM = d * (1.0f + rng.normal(0.0f, optics.rangefinder ? 0.005f : 0.08f));
        s.bearingDeg = rel.heading() * kRadToDeg;
        s.estimatedPos = c.pos2() + Vec2{rng.normal(0.0f, d * 0.02f), rng.normal(0.0f, d * 0.02f)};
        s.lookingAtObserver = std::fabs(angleDelta(c.loco.heading, (eye.xy() - c.pos2()).heading())) < 0.4f;
        s.movingAway = dot(c.loco.velocity, rel.normalized()) > 0.5f;
        s.speedEstimateMs = std::max(0.0f, c.loco.speed * (1.0f + rng.normal(0.0f, 0.2f)));
        qualitySum += s.quality;
        if (c.social.groupId != 0)
        {
            byGroup[c.social.groupId].push_back(rec.subjects.size());
        }
        rec.subjects.push_back(std::move(s));
    }
    for (const auto& g : byGroup)
    {
        if (g.second.size() < 2)
        {
            continue;
        }
        GroupObservation go;
        go.groupTruth = g.first;
        go.speciesIndex = rec.subjects[g.second.front()].speciesIndex;
        go.countEstimate = static_cast<int>(g.second.size());
        float nnSum = 0.0f;
        float lenSum = 0.0f;
        int vig = 0;
        int feed = 0;
        Vec2 centroid;
        for (const size_t a : g.second)
        {
            const ObservedSubject& sa = rec.subjects[a];
            float nn = 1e9f;
            for (const size_t b : g.second)
            {
                if (a != b)
                {
                    nn = std::min(nn, distance(sa.estimatedPos, rec.subjects[b].estimatedPos));
                }
            }
            nnSum += nn;
            lenSum += sa.estimatedLengthM;
            vig += sa.action == ObservedAction::Vigilant ? 1 : 0;
            feed += (sa.action == ObservedAction::Feeding || sa.action == ObservedAction::Drinking) ? 1 : 0;
            centroid += sa.estimatedPos;
        }
        const float n = static_cast<float>(g.second.size());
        go.meanSpacingM = nnSum / n;
        go.vigilantFraction = static_cast<float>(vig) / n;
        go.feedingFraction = static_cast<float>(feed) / n;
        go.centroid = centroid / n;
        const float meanLen = lenSum / n;
        int running = 0;
        for (const size_t a : g.second)
        {
            running += rec.subjects[a].action == ObservedAction::Running ? 1 : 0;
        }
        if (static_cast<float>(running) >= 0.4f * n)
        {
            go.formation = Formation::Fleeing;
        }
        else if (go.meanSpacingM < 1.8f * meanLen)
        {
            go.formation = Formation::Compact;
        }
        else if (go.meanSpacingM < 5.0f * meanLen)
        {
            go.formation = Formation::Loose;
        }
        else
        {
            go.formation = Formation::Dispersed;
        }
        rec.groups.push_back(go);
    }
    rec.overallQuality = rec.subjects.empty() ? 0.0f : qualitySum / static_cast<float>(rec.subjects.size());
    return rec;
}

ObservationRecord ObservationSystem::photograph(const PhotoSettings& settings, const Vec3& eye, float headingRad, const CreatureSystem& creatures,
                                                const WorldContext& ctx, const ObserverContext& observer, u32 expeditionId, Rng& rng, PhotoRecord& outPhoto)
{
    const OpticsSpec optics = opticsFor(settings);
    ObservationRecord rec = observe(optics, eye, headingRad, creatures, ctx, observer, expeditionId, rng);
    outPhoto = PhotoRecord{};
    outPhoto.settings = settings;
    const float radPerPx = (settings.sensorWidthMm / static_cast<float>(settings.widthPx)) / settings.focalMm;
    // Exposure: scene EV100 from illuminance (EV = log2(lux / 2.5)), camera EV from N, t, ISO.
    outPhoto.sceneEv = std::log2(std::max(1e-4f, ctx.sky->state().illuminanceLux) / 2.5f);
    const float cameraEv = std::log2(settings.aperture * settings.aperture / settings.shutterS) - std::log2(static_cast<float>(settings.iso) / 100.0f);
    outPhoto.exposureErrorEv = outPhoto.sceneEv - cameraEv;
    outPhoto.noise = saturate(std::log2(static_cast<float>(settings.iso) / 100.0f) / 8.0f);
    float bestCoverage = 0.0f;
    float blurAtBest = 0.0f;
    for (ObservedSubject& s : rec.subjects)
    {
        const Creature* c = creatures.find(s.truth);
        if (!c)
        {
            continue;
        }
        const float d = std::max(1.0f, s.distanceM);
        const float coverage = c->lengthM / d / radPerPx;
        const Vec2 rel = (c->pos2() - eye.xy()).normalized();
        const float lateral = std::fabs(cross(rel, c->loco.velocity));
        const float blurPx = (lateral / d) * settings.shutterS / radPerPx;
        const float exposureOk = std::exp(-square(outPhoto.exposureErrorEv) / 4.0f);
        const float sharp = 1.0f / (1.0f + blurPx / 3.0f);
        s.quality *= sharp * exposureOk * (1.0f - 0.4f * outPhoto.noise);
        if (coverage > bestCoverage)
        {
            bestCoverage = coverage;
            blurAtBest = blurPx;
        }
    }
    outPhoto.subjectCoveragePx = bestCoverage;
    outPhoto.motionBlurPx = blurAtBest;
    outPhoto.sharpness = 1.0f / (1.0f + blurAtBest / 3.0f);
    return rec;
}

u32 ObservationSystem::beginSession(ObservationMethod m, i16 speciesFocus, double now, u32 expeditionId)
{
    ObservationSession s;
    s.id = nextSession_++;
    s.start = now;
    s.end = now;
    s.method = m;
    s.speciesFocus = speciesFocus;
    s.expeditionId = expeditionId;
    sessions_.push_back(s);
    return s.id;
}

ObservationSession* ObservationSystem::session(u32 id)
{
    for (ObservationSession& s : sessions_)
    {
        if (s.id == id)
        {
            return &s;
        }
    }
    return nullptr;
}

void ObservationSystem::sampleSession(u32 sessionId, const ObservationRecord& rec, const ObserverContext& observer, bool predatorVisible)
{
    ObservationSession* s = session(sessionId);
    if (!s || !s->open)
    {
        return;
    }
    s->end = rec.time;
    s->observationIds.push_back(rec.id);
    auto fill = [&](SessionSample& smp) {
        smp.time = rec.time;
        smp.observerNoiseDb = observer.observerNoiseDb;
        smp.droneAirborne = observer.droneAirborne;
        smp.droneAltitudeM = observer.droneAltitudeM;
        smp.vehicleEngineOn = observer.vehicleEngineOn;
        smp.predatorVisible = predatorVisible;
        smp.lux = rec.env.lux;
        smp.temperatureC = rec.env.temperatureC;
        smp.hourOfDay = rec.env.hourOfDay;
    };
    bool any = false;
    for (const GroupObservation& g : rec.groups)
    {
        if (s->speciesFocus >= 0 && g.speciesIndex != s->speciesFocus)
        {
            continue;
        }
        SessionSample smp;
        fill(smp);
        smp.speciesIndex = g.speciesIndex;
        smp.count = g.countEstimate;
        smp.meanSpacingM = g.meanSpacingM;
        smp.vigilantFraction = g.vigilantFraction;
        smp.feedingFraction = g.feedingFraction;
        smp.formation = g.formation;
        smp.centroid = g.centroid;
        smp.observerDistanceM = distance(g.centroid, rec.observerPos.xy());
        s->samples.push_back(smp);
        any = true;
    }
    if (!any)
    {
        // Solitary animals: one sample from the focus-species subjects.
        int n = 0;
        int vig = 0;
        int feed = 0;
        float dist = 0.0f;
        Vec2 centroid;
        i16 species = s->speciesFocus;
        for (const ObservedSubject& sub : rec.subjects)
        {
            if (s->speciesFocus >= 0 && sub.speciesIndex != s->speciesFocus)
            {
                continue;
            }
            species = sub.speciesIndex;
            ++n;
            vig += sub.action == ObservedAction::Vigilant ? 1 : 0;
            feed += sub.action == ObservedAction::Feeding ? 1 : 0;
            dist += sub.distanceM;
            centroid += sub.estimatedPos;
        }
        if (n > 0)
        {
            SessionSample smp;
            fill(smp);
            smp.speciesIndex = species;
            smp.count = n;
            smp.vigilantFraction = static_cast<float>(vig) / static_cast<float>(n);
            smp.feedingFraction = static_cast<float>(feed) / static_cast<float>(n);
            smp.observerDistanceM = dist / static_cast<float>(n);
            smp.centroid = centroid / static_cast<float>(n);
            s->samples.push_back(smp);
        }
    }
}

void ObservationSystem::endSession(u32 sessionId, double now)
{
    if (ObservationSession* s = session(sessionId))
    {
        s->open = false;
        s->end = now;
    }
}

u32 ObservationSystem::store(ObservationRecord rec)
{
    rec.id = nextRecord_++;
    records_.push_back(std::move(rec));
    return records_.back().id;
}

u32 ObservationSystem::storePhoto(PhotoRecord p)
{
    p.id = nextPhoto_++;
    photos_.push_back(p);
    return p.id;
}

const ObservationRecord* ObservationSystem::find(u32 id) const
{
    for (const ObservationRecord& r : records_)
    {
        if (r.id == id)
        {
            return &r;
        }
    }
    return nullptr;
}
} // namespace noctis
