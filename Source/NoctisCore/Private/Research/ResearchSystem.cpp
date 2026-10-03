#include "Noctis/Research/ResearchSystem.h"

#include "Noctis/Core/Log.h"
#include "Noctis/Creatures/CreatureLogic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace noctis
{
namespace researchimpl
{
std::string fmt1(const char* f, double a)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a);
    return buf;
}

std::string speciesName(const EvidenceDatabase* db, i16 s)
{
    if (!db || s < 0)
    {
        return "espèce non identifiée";
    }
    return db->speciesAt(s).scientificName;
}
} // namespace researchimpl

void ResearchSystem::initialize(const EvidenceDatabase& db, const EnvironmentDefinition& env, const WorldLayout& layout, const std::vector<int>& agentSpecies,
                                const Rect2& bounds, const Vec2& basecamp, const Terrain& terrain, u64 seed)
{
    db_ = &db;
    env_ = &env;
    rng_.seed(seed, 0x5E5E);
    equipment_.load(db.equipment());
    vehicle_.configure(equipment_.vehicleSpec());
    drone_.configure(equipment_.droneSpec());
    map_.configure(bounds, 64.0f);
    paleo_.generateSites(layout, db, env, agentSpecies, hashCombine(seed, 0xF0));
    basecamp_ = basecamp;
    vehicle_.setTransform(Vec3{basecamp, terrain.heightAt(basecamp)}, 0.0f, 0.0f);
    const Vec2 rp = basecamp + Vec2{3.0f, 2.0f};
    researcher_.setTransform(Vec3{rp, terrain.heightAt(rp) + 1.65f}, 0.0f, Vec2{});
    drone_.state().position = vehicle_.state().position;
    map_.addFeature(MapFeatureKind::Vehicle, basecamp, 0.0, "Camp de base / véhicule");
}

void ResearchSystem::publishEmitters(std::vector<Emitter>& out) const
{
    out.push_back(researcher_.emitter());
    out.push_back(vehicle_.emitter());
    out.push_back(drone_.emitter());
    for (const Sensor& s : sensors_)
    {
        if (!s.active)
        {
            continue;
        }
        Emitter e;
        e.id = EntityId::make(EntityKind::Sensor, s.id);
        e.position = s.position;
        e.heightM = 0.4f;
        e.massKg = 2.0f;
        e.noiseDb = 0.0f;
        e.scent = 0.25f; // equipment carries human scent
        e.active = true;
        e.observer = true;
        e.observerRadiusM = 80.0f;
        out.push_back(e);
    }
}

Vec3 ResearchSystem::eyePosition() const
{
    if (researcher_.state().inVehicle)
    {
        return vehicle_.state().position + Vec3{0.0f, 0.0f, 2.8f};
    }
    const ResearcherState& r = researcher_.state();
    return Vec3{r.position.x, r.position.y, r.position.z - (r.crouched ? 0.6f : 0.0f)};
}

float ResearchSystem::viewHeading() const
{
    return researcher_.state().inVehicle ? vehicle_.state().heading : researcher_.state().heading;
}

ObserverContext ResearchSystem::observerContext(const WorldContext& ctx) const
{
    (void)ctx;
    ObserverContext o;
    o.observerNoiseDb = researcher_.state().inVehicle ? vehicle_.state().noiseDb : researcher_.state().noiseDb;
    o.crouched = researcher_.state().crouched;
    o.inVehicle = researcher_.state().inVehicle;
    o.vehicleEngineOn = vehicle_.state().engineOn && vehicle_.state().noiseDb > 0.0f;
    const DroneState& d = drone_.state();
    o.droneAirborne = d.mode != DroneMode::Docked && d.mode != DroneMode::Lost;
    o.droneAltitudeM = d.altitudeAglM;
    o.droneDistanceM = distance(d.position, eyePosition());
    return o;
}

OpticsSpec ResearchSystem::optics(OpticsKind k) const
{
    OpticsSpec o;
    switch (k)
    {
    case OpticsKind::Eye:
        o.method = ObservationMethod::NakedEye;
        o.fovDeg = 120.0f;
        o.magnification = 1.0f;
        o.rangeLimitM = 1500.0f;
        break;
    case OpticsKind::Binoculars:
    {
        o.method = ObservationMethod::Binoculars;
        const EquipmentItem* b = equipment_.find("binoculars_10x42");
        o.magnification = b ? b->specs.getFloat("magnification", 10.0f) : 10.0f;
        o.fovDeg = b ? b->specs.getFloat("fov_deg", 6.5f) : 6.5f;
        o.rangeLimitM = 3000.0f;
        o.rangefinder = b ? b->specs.getBool("rangefinder", true) : true;
        break;
    }
    case OpticsKind::DroneCamera:
        o.method = ObservationMethod::Drone;
        o.fovDeg = 70.0f;
        o.acuityRad = 2.0f * 0.0006f; // ~0.6 mrad per pixel at wide angle
        o.rangeLimitM = 600.0f;
        o.rangefinder = true;
        break;
    case OpticsKind::TrapCamera:
        o.method = ObservationMethod::CameraTrap;
        o.fovDeg = 50.0f;
        o.acuityRad = 2.0f * 0.0005f;
        o.rangeLimitM = 25.0f;
        break;
    }
    return o;
}

u32 ResearchSystem::storeObservation(ObservationRecord rec, WorldContext& ctx, CreatureSystem& creatures)
{
    (void)creatures;
    Expedition* exp = expeditions_.active();
    rec.expeditionId = exp ? exp->id : 0;
    rec.sessionId = activeSession_;
    const u32 id = observations_.store(std::move(rec));
    ObservationRecord& stored = observations_.mutableRecords().back();
    for (ObservedSubject& s : stored.subjects)
    {
        if (s.speciesIndex >= 0)
        {
            observations_.catalog().match(s, db_->speciesAt(s.speciesIndex), stored.time, s.estimatedPos, id, true);
            map_.addSighting(s.catalogId, s.speciesIndex, s.estimatedPos, stored.time, id);
            if (exp && s.catalogId != 0 && std::find(exp->catalogIds.begin(), exp->catalogIds.end(), s.catalogId) == exp->catalogIds.end())
            {
                exp->catalogIds.push_back(s.catalogId);
                const CatalogEntry* e = observations_.catalog().find(s.catalogId);
                if (e && e->observations.size() == 1)
                {
                    autolog("Nouvel individu catalogué : " + e->code + " « " + e->nickname + " » (" + researchimpl::speciesName(db_, s.speciesIndex) + ").", ctx);
                }
            }
        }
    }
    if (exp)
    {
        exp->observations.push_back(id);
    }
    SimEvent e;
    e.type = EventType::ObservationLogged;
    e.time = ctx.now;
    e.subject = researcher_.state().id;
    e.position = stored.observerPos;
    e.param = id;
    ctx.events->emit(e);
    return id;
}

u32 ResearchSystem::observeNow(OpticsKind kind, WorldContext& ctx, CreatureSystem& creatures)
{
    Vec3 eye = eyePosition();
    float heading = viewHeading();
    if (kind == OpticsKind::DroneCamera)
    {
        eye = drone_.state().position;
        heading = drone_.state().heading;
    }
    ObservationRecord rec = observations_.observe(optics(kind), eye, heading, creatures, ctx, observerContext(ctx), 0, rng_);
    return storeObservation(std::move(rec), ctx, creatures);
}

u32 ResearchSystem::takePhoto(const PhotoSettings& settings, WorldContext& ctx, CreatureSystem& creatures)
{
    PhotoRecord photo;
    ObservationRecord rec = observations_.photograph(settings, eyePosition(), viewHeading(), creatures, ctx, observerContext(ctx), 0, rng_, photo);
    const u32 obsId = storeObservation(std::move(rec), ctx, creatures);
    photo.observationId = obsId;
    const u32 pid = observations_.storePhoto(photo);
    for (ObservationRecord& r : observations_.mutableRecords())
    {
        if (r.id == obsId)
        {
            r.mediaId = pid;
        }
    }
    if (Expedition* exp = expeditions_.active())
    {
        exp->photos.push_back(pid);
    }
    SoundSource shutter;
    shutter.kind = SoundKind::Mechanical;
    shutter.emitter = researcher_.state().id;
    shutter.position = eyePosition();
    shutter.startTime = ctx.now;
    shutter.durationS = 0.05f;
    shutter.levelDb = 55.0f;
    shutter.f0Hz = 3000.0f;
    shutter.tonal = false;
    ctx.sound->emit(shutter);
    SimEvent e;
    e.type = EventType::PhotoTaken;
    e.time = ctx.now;
    e.subject = researcher_.state().id;
    e.position = eyePosition();
    e.param = pid;
    ctx.events->emit(e);
    return pid;
}

u32 ResearchSystem::startSession(ObservationMethod method, i16 speciesFocus, WorldContext& ctx)
{
    if (activeSession_ != 0)
    {
        stopSession(ctx);
    }
    Expedition* exp = expeditions_.active();
    activeSession_ = observations_.beginSession(method, speciesFocus, ctx.now, exp ? exp->id : 0);
    if (exp)
    {
        exp->sessions.push_back(activeSession_);
    }
    autolog(std::string("Début d'une session d'observation (") + observationMethodLabelFr(method) + ", " + researchimpl::speciesName(db_, speciesFocus) + ").", ctx);
    return activeSession_;
}

void ResearchSystem::stopSession(WorldContext& ctx)
{
    if (activeSession_ == 0)
    {
        return;
    }
    observations_.endSession(activeSession_, ctx.now);
    const ObservationSession* s = observations_.session(activeSession_);
    autolog(researchimpl::fmt1("Fin de session d'observation (%.0f échantillons).", s ? static_cast<double>(s->samples.size()) : 0.0), ctx);
    activeSession_ = 0;
}

void ResearchSystem::addNote(const std::string& text, WorldContext& ctx)
{
    if (Expedition* exp = expeditions_.active())
    {
        expeditions_.log(exp->id, ctx.now, ctx.clock->formatted(), text, false);
    }
    map_.addFeature(MapFeatureKind::Note, eyePosition().xy(), ctx.now, text, 0, -1, 0.0f, 1.0f);
}

void ResearchSystem::autolog(const std::string& text, WorldContext& ctx)
{
    if (Expedition* exp = expeditions_.active())
    {
        expeditions_.log(exp->id, ctx.now, ctx.clock->formatted(), text, true);
    }
}

// ---------------------------------------------------------------- Acoustics

u32 ResearchSystem::startRecording(const MicSpec& mic, const Vec3& position, float heading, u32 arrayId, const std::string& label, WorldContext& ctx)
{
    ActiveRecording a;
    a.handle = nextHandle_++;
    a.mic = mic;
    a.position = position;
    a.heading = heading;
    a.start = ctx.now;
    a.arrayId = arrayId;
    a.label = label;
    active_.push_back(a);
    return a.handle;
}

void ResearchSystem::captureSounds(WorldContext& ctx)
{
    if (active_.empty())
    {
        return;
    }
    std::vector<SoundSource> fresh;
    ctx.sound->collect(ctx.now - 2.0, ctx.now + 1.0, fresh);
    for (ActiveRecording& a : active_)
    {
        for (const SoundSource& s : fresh)
        {
            if (s.startTime + s.durationS < a.start - 20.0)
            {
                continue;
            }
            if (std::find(a.seen.begin(), a.seen.end(), s.id) != a.seen.end())
            {
                continue;
            }
            a.seen.push_back(s.id);
            a.sources.push_back(s);
        }
    }
}

u32 ResearchSystem::stopRecording(u32 handle, WorldContext& ctx)
{
    captureSounds(ctx);
    for (size_t i = 0; i < active_.size(); ++i)
    {
        if (active_[i].handle != handle)
        {
            continue;
        }
        const ActiveRecording a = active_[i];
        active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(i));
        const float duration = static_cast<float>(std::max(0.5, ctx.now - a.start));
        Recording r = AcousticRecorder::record(ctx, a.mic, a.position, a.heading, a.start, std::min(duration, 600.0f), nextRecordingId_++,
                                               rng_.nextU32() ^ handle, &a.sources);
        r.arrayId = a.arrayId;
        r.label = a.label;
        Expedition* exp = expeditions_.active();
        r.expeditionId = exp ? exp->id : 0;
        if (exp)
        {
            exp->recordings.push_back(r.id);
        }
        recordings_.push_back(std::move(r));
        SimEvent e;
        e.type = EventType::RecordingMade;
        e.time = ctx.now;
        e.subject = researcher_.state().id;
        e.position = a.position;
        e.param = recordings_.back().id;
        ctx.events->emit(e);
        return recordings_.back().id;
    }
    return 0;
}

u32 ResearchSystem::deployMicArray(const Vec2& center, float radiusM, int n, const MicSpec& mic, WorldContext& ctx)
{
    const u32 arrayId = nextArrayId_++;
    n = std::max(3, n);
    for (int i = 0; i < n; ++i)
    {
        const Vec2 p = center + Vec2::fromHeading(kTwoPi * static_cast<float>(i) / static_cast<float>(n)) * radiusM;
        char label[64];
        std::snprintf(label, sizeof(label), "Réseau %u — micro %d", arrayId, i + 1);
        startRecording(mic, Vec3{p, ctx.terrain->heightAt(p) + 1.5f}, 0.0f, arrayId, label, ctx);
        map_.addFeature(MapFeatureKind::Sensor, p, ctx.now, label, arrayId, -1, 0.0f, 2.0f);
    }
    autolog(researchimpl::fmt1("Réseau de %.0f microphones déployé (localisation acoustique).", static_cast<double>(n)), ctx);
    return arrayId;
}

std::vector<u32> ResearchSystem::stopMicArray(u32 arrayId, WorldContext& ctx)
{
    std::vector<u32> handles;
    for (const ActiveRecording& a : active_)
    {
        if (a.arrayId == arrayId)
        {
            handles.push_back(a.handle);
        }
    }
    std::vector<u32> ids;
    for (const u32 h : handles)
    {
        ids.push_back(stopRecording(h, ctx));
    }
    return ids;
}

const Recording* ResearchSystem::findRecording(u32 id) const
{
    for (const Recording& r : recordings_)
    {
        if (r.id == id)
        {
            return &r;
        }
    }
    return nullptr;
}

const RecordingAnalysis* ResearchSystem::analyzeRecording(u32 recordingId, WorldContext& ctx)
{
    for (const RecordingAnalysis& a : analyses_)
    {
        if (a.recordingId == recordingId)
        {
            return &a;
        }
    }
    const Recording* r = findRecording(recordingId);
    if (!r)
    {
        return nullptr;
    }
    RecordingAnalysis a;
    a.recordingId = recordingId;
    a.spectrogram = AcousticAnalysis::spectrogram(*r, 2048, 512);
    a.events = AcousticAnalysis::detectEvents(*r, a.spectrogram, 8.0f, 12.0f, 4000.0f);
    a.analyzed = true;
    for (const AcousticEvent& ev : a.events)
    {
        AcousticDetection d;
        d.time = r->startTime + ev.startS;
        d.f0Hz = ev.f0Hz;
        d.durationS = ev.durationS;
        d.levelDb = ev.peakLevelDb;
        d.cluster = ev.cluster;
        d.recordingId = recordingId;
        // Associate with the observation session running at that time (if any).
        for (const ObservationSession& s : observations_.sessions())
        {
            if (d.time >= s.start && d.time <= (s.open ? ctx.now : s.end))
            {
                d.sessionId = s.id;
            }
        }
        detections_.push_back(d);
    }
    autolog(researchimpl::fmt1("Analyse acoustique : %.0f événements détectés.", static_cast<double>(a.events.size())), ctx);
    analyses_.push_back(std::move(a));
    return &analyses_.back();
}

void ResearchSystem::clusterCalls(int k, u64 seed)
{
    std::vector<AcousticEvent> all;
    std::vector<std::pair<size_t, size_t>> refs;
    for (size_t i = 0; i < analyses_.size(); ++i)
    {
        for (size_t j = 0; j < analyses_[i].events.size(); ++j)
        {
            all.push_back(analyses_[i].events[j]);
            refs.push_back({i, j});
        }
    }
    AcousticAnalysis::cluster(all, k, seed);
    for (size_t n = 0; n < all.size(); ++n)
    {
        RecordingAnalysis& a = analyses_[refs[n].first];
        a.events[refs[n].second].cluster = all[n].cluster;
        const Recording* r = findRecording(a.recordingId);
        for (AcousticDetection& d : detections_)
        {
            if (r && d.recordingId == a.recordingId && std::fabs(d.time - (r->startTime + all[n].startS)) < 1e-3)
            {
                d.cluster = all[n].cluster;
            }
        }
    }
}

LocalizationResult ResearchSystem::localizeEvent(u32 arrayId, u32 recordingId, int eventIndex, float temperatureC)
{
    std::vector<const Recording*> recs;
    const Recording* ref = findRecording(recordingId);
    if (!ref)
    {
        return {};
    }
    recs.push_back(ref);
    for (const Recording& r : recordings_)
    {
        if (r.arrayId == arrayId && r.id != recordingId)
        {
            recs.push_back(&r);
        }
    }
    const RecordingAnalysis* an = nullptr;
    for (const RecordingAnalysis& a : analyses_)
    {
        if (a.recordingId == recordingId)
        {
            an = &a;
        }
    }
    if (!an || eventIndex < 0 || static_cast<size_t>(eventIndex) >= an->events.size())
    {
        return {};
    }
    const AcousticEvent& ev = an->events[static_cast<size_t>(eventIndex)];
    return AcousticAnalysis::localize(recs, ev.startS, ev.endS, 1500.0f, temperatureC);
}

// ---------------------------------------------------------------- Equipment

u32 ResearchSystem::placeSensor(SensorKind kind, const Vec3& position, float heading, WorldContext& ctx)
{
    Sensor s;
    s.id = nextSensorId_++;
    s.kind = kind;
    s.position = position;
    s.heading = heading;
    s.nextRecording = ctx.now;
    Expedition* exp = expeditions_.active();
    s.expeditionId = exp ? exp->id : 0;
    if (kind == SensorKind::AudioRecorder)
    {
        const EquipmentItem* rec = equipment_.find("audio_recorder");
        s.mic.selfNoiseDb = rec ? rec->specs.getFloat("self_noise_db", 16.0f) : 16.0f;
        s.recordDurationS = rec ? rec->specs.getFloat("record_s", 60.0f) : 60.0f;
        s.recordIntervalS = rec ? rec->specs.getFloat("interval_s", 600.0f) : 600.0f;
        s.arrayId = 0;
    }
    sensors_.push_back(s);
    map_.addFeature(MapFeatureKind::Sensor, position.xy(), ctx.now, sensorKindLabelFr(kind), s.id, -1, 0.0f, 1.0f);
    autolog(std::string(sensorKindLabelFr(kind)) + " installé.", ctx);
    return s.id;
}

bool ResearchSystem::retrieveSensor(u32 id)
{
    for (Sensor& s : sensors_)
    {
        if (s.id == id)
        {
            s.active = false;
            return true;
        }
    }
    return false;
}

bool ResearchSystem::launchDrone(WorldContext& ctx)
{
    const bool ok = drone_.launch(vehicle_.state().position, ctx.now);
    if (ok)
    {
        autolog("Drone en vol.", ctx);
    }
    return ok;
}

void ResearchSystem::enterVehicle(bool inside)
{
    ResearcherState& r = researcher_.state();
    r.inVehicle = inside;
    if (!inside)
    {
        const Vec2 p = vehicle_.state().position.xy() + Vec2::fromHeading(vehicle_.state().heading).perp() * 3.0f;
        r.position = Vec3{p, vehicle_.state().position.z + 1.65f};
    }
}

void ResearchSystem::updateSensors(float dt, WorldContext& ctx, CreatureSystem& creatures)
{
    (void)dt;
    for (Sensor& s : sensors_)
    {
        if (!s.active)
        {
            continue;
        }
        if (s.kind == SensorKind::CameraTrap && ctx.now - s.lastCheck >= 2.0)
        {
            s.lastCheck = ctx.now;
            if (ctx.now - s.lastTrigger < 30.0 || s.batteryWh <= 0.0f)
            {
                continue;
            }
            std::vector<u32> near;
            creatures.neighbours(s.position.xy(), 20.0f, near);
            const float air = ctx.weather->state().temperatureC;
            for (const u32 i : near)
            {
                const Creature& c = creatures.creatures()[i];
                const Vec2 rel = c.pos2() - s.position.xy();
                if (!c.alive || std::fabs(angleDelta(s.heading, rel.heading())) > 25.0f * kDegToRad || c.loco.speed < 0.15f)
                {
                    continue;
                }
                // Passive infrared detects warm moving bodies: the thermal contrast matters (ectotherms often go unrecorded).
                const float contrast = c.phys.bodyTempC - air;
                if (contrast < 1.5f || !rng_.chance(saturate(contrast / 6.0f)))
                {
                    continue;
                }
                ObservationRecord rec = observations_.observe(optics(OpticsKind::TrapCamera), s.position + Vec3{0.0f, 0.0f, 0.5f}, s.heading, creatures, ctx,
                                                              ObserverContext{}, 0, rng_);
                if (rec.subjects.empty())
                {
                    continue;
                }
                rec.tags.push_back("piège photographique");
                rec.tags.push_back("capteur " + std::to_string(s.id));
                storeObservation(std::move(rec), ctx, creatures);
                s.lastTrigger = ctx.now;
                ++s.triggers;
                s.batteryWh -= 0.02f;
                s.storageGbUsed += 0.006f;
                SimEvent e;
                e.type = EventType::SensorTriggered;
                e.time = ctx.now;
                e.subject = EntityId::make(EntityKind::Sensor, s.id);
                e.other = c.id;
                e.position = s.position;
                ctx.events->emit(e);
                break;
            }
        }
        else if (s.kind == SensorKind::AudioRecorder)
        {
            bool recording = false;
            u32 handle = 0;
            double start = 0.0;
            for (const ActiveRecording& a : active_)
            {
                if (a.sensorId == s.id)
                {
                    recording = true;
                    handle = a.handle;
                    start = a.start;
                }
            }
            if (!recording && ctx.now >= s.nextRecording && s.batteryWh > 0.0f)
            {
                const u32 h = startRecording(s.mic, s.position, s.heading, s.arrayId, "Enregistreur " + std::to_string(s.id), ctx);
                active_.back().sensorId = s.id;
                (void)h;
                s.nextRecording = ctx.now + s.recordIntervalS;
            }
            else if (recording && ctx.now - start >= s.recordDurationS)
            {
                stopRecording(handle, ctx);
                s.batteryWh -= 0.05f;
                s.storageGbUsed += s.recordDurationS * 32000.0f / 1e9f;
            }
        }
    }
}

// ---------------------------------------------------------------- Paleontology

bool ResearchSystem::excavate(u32 specimenId, float hours, WorldContext& ctx)
{
    FossilSpecimen* s = paleo_.specimen(specimenId);
    if (!s || distance(s->position, researcher_.state().position.xy()) > 20.0f)
    {
        return false;
    }
    const bool done = paleo_.excavate(specimenId, hours, 1.0f);
    if (done)
    {
        if (Expedition* exp = expeditions_.active())
        {
            if (std::find(exp->specimens.begin(), exp->specimens.end(), specimenId) == exp->specimens.end())
            {
                exp->specimens.push_back(specimenId);
            }
        }
        autolog(std::string("Spécimen collecté : ") + specimenElementLabelFr(s->element) + ".", ctx);
    }
    return done;
}

void ResearchSystem::identifySpecimen(u32 specimenId, WorldContext& ctx)
{
    paleo_.identify(specimenId, *db_);
    if (FossilSpecimen* s = paleo_.specimen(specimenId))
    {
        if (s->identified)
        {
            autolog("Identification en laboratoire : " + s->identificationFr + " [" + confidenceLabelFr(s->identificationConfidence) + "].", ctx);
        }
    }
}

u32 ResearchSystem::measureTrackway(u32 seedFootprint, WorldContext& ctx)
{
    const std::vector<u32> ids = paleo_.extractTrackway(seedFootprint, *ctx.tracks);
    TrackwayAnalysis a = paleo_.analyzeTrackway(ids, *ctx.tracks, *db_);
    trackways_.push_back(a);
    const u32 id = static_cast<u32>(trackways_.size());
    if (a.valid)
    {
        const Footprint* f = ctx.tracks->find(seedFootprint);
        if (f)
        {
            map_.addFeature(MapFeatureKind::Trackway, f->position, ctx.now, a.makerGuessFr, id, -1, a.speedEstMs, 10.0f);
        }
        autolog(researchimpl::fmt1("Piste mesurée : vitesse estimée %.1f m/s (formule d'Alexander, avec incertitude).", a.speedEstMs), ctx);
    }
    if (Expedition* exp = expeditions_.active())
    {
        exp->trackways.push_back(id);
    }
    return id;
}

u32 ResearchSystem::examineCarcass(EntityId carcass, WorldContext& ctx)
{
    const Carcass* c = ctx.carcasses->find(carcass);
    if (!c || distance(c->position, eyePosition().xy()) > 25.0f)
    {
        return 0;
    }
    examinations_.push_back(paleo_.examineCarcass(*c, ctx.now, std::max(1.0f, ctx.weather->state().temperatureC), *db_));
    map_.addFeature(MapFeatureKind::Carcass, c->position, ctx.now, "Carcasse examinée : " + researchimpl::speciesName(db_, c->speciesIndex), carcass.value,
                    c->speciesIndex, 0.0f, 10.0f);
    autolog("Carcasse examinée (" + researchimpl::speciesName(db_, c->speciesIndex) + ", " + examinations_.back().stageFr + ").", ctx);
    return static_cast<u32>(examinations_.size());
}

// ---------------------------------------------------------------- Hypotheses & expeditions

u32 ResearchSystem::createHypothesis(Hypothesis h, WorldContext& ctx)
{
    const u32 id = hypotheses_.create(h, ctx.now);
    if (Expedition* exp = expeditions_.active())
    {
        exp->hypotheses.push_back(id);
    }
    autolog("Hypothèse formulée : " + h.statementFr, ctx);
    return id;
}

void ResearchSystem::testHypothesis(u32 id, WorldContext& ctx, bool currentExpeditionOnly)
{
    const Expedition* exp = expeditions_.active();
    hypotheses_.test(id, observations_, detections_, ctx.now, rng_.nextU32(), currentExpeditionOnly && exp ? exp->id : 0);
    if (const Hypothesis* h = hypotheses_.find(id))
    {
        autolog("Test de l'hypothèse « " + h->statementFr + " » : " + hypothesisStatusLabelFr(h->status) + ".", ctx);
    }
}

u32 ResearchSystem::planExpedition(Expedition e) { return expeditions_.plan(std::move(e), equipment_, vehicle_.payloadKg()); }

bool ResearchSystem::startExpedition(u32 id, WorldContext& ctx)
{
    Expedition* e = expeditions_.find(id);
    if (!e)
    {
        return false;
    }
    e->environmentId = env_ ? env_->id : "";
    e->profileId = ctx.profile ? ctx.profile->id : "";
    e->profileVersion = ctx.profile ? ctx.profile->version : "";
    return expeditions_.start(id, ctx.now, ctx.clock->formatted());
}

const ScientificReport* ResearchSystem::endExpedition(u32 id, WorldContext& ctx, bool aborted)
{
    if (activeSession_ != 0)
    {
        stopSession(ctx);
    }
    expeditions_.end(id, ctx.now, ctx.clock->formatted(), aborted);
    reports_.push_back(generateReport(id, ctx));
    return &reports_.back();
}

void ResearchSystem::updateExpeditionProgress(WorldContext& ctx)
{
    (void)ctx;
    Expedition* exp = expeditions_.active();
    if (!exp)
    {
        return;
    }
    for (DataTarget& t : exp->targets)
    {
        float achieved = 0.0f;
        switch (t.kind)
        {
        case DataTargetKind::Recordings: achieved = static_cast<float>(exp->recordings.size()); break;
        case DataTargetKind::AcousticDetections:
            for (const AcousticDetection& d : detections_)
            {
                for (const u32 r : exp->recordings)
                {
                    achieved += d.recordingId == r ? 1.0f : 0.0f;
                }
            }
            break;
        case DataTargetKind::Photos: achieved = static_cast<float>(exp->photos.size()); break;
        case DataTargetKind::IdentifiedIndividuals:
            for (const u32 cid : exp->catalogIds)
            {
                const CatalogEntry* e = observations_.catalog().find(cid);
                achieved += (e && (t.speciesIndex < 0 || e->speciesIndex == t.speciesIndex)) ? 1.0f : 0.0f;
            }
            break;
        case DataTargetKind::SessionHours:
            for (const u32 sid : exp->sessions)
            {
                for (const ObservationSession& s : observations_.sessions())
                {
                    if (s.id == sid)
                    {
                        achieved += static_cast<float>((s.end - s.start) / 3600.0);
                    }
                }
            }
            break;
        case DataTargetKind::Trackways: achieved = static_cast<float>(exp->trackways.size()); break;
        case DataTargetKind::Specimens: achieved = static_cast<float>(exp->specimens.size()); break;
        case DataTargetKind::Observations: achieved = static_cast<float>(exp->observations.size()); break;
        }
        t.achieved = achieved;
    }
    exp->batteryUsedKWh = vehicle_.batteryCapacityKWh() - vehicle_.state().batteryKWh;
    exp->distanceKm = vehicle_.state().odometerM / 1000.0;
}

// ---------------------------------------------------------------- Per-step update

void ResearchSystem::autoDiscover(WorldContext& ctx, CreatureSystem& creatures)
{
    (void)creatures;
    const Vec3 eye = eyePosition();
    const Vec2 p = eye.xy();
    const float vis = std::min(1500.0f, ctx.weather->visibilityAt(eye.z - ctx.valleyFloorZ, false));
    // Water bodies within sight.
    DrinkSite site;
    if (ctx.water->findDrinkSite(p, std::min(400.0f, vis), site) &&
        ctx.terrain->lineOfSight(eye, Vec3{site.position, ctx.terrain->heightAt(site.position) + 0.5f}, 10.0f))
    {
        map_.addFeature(MapFeatureKind::Water, site.position, ctx.now, site.bodyId >= 1000 ? "Plan d'eau" : "Rivière", static_cast<u32>(site.bodyId + 1), -1,
                        0.0f, 250.0f);
    }
    // Carcasses: seen when close and visible, smelt from far downwind (large carcasses).
    std::vector<EntityId> carcasses;
    ctx.carcasses->queryNear(p, 600.0f, carcasses);
    const Vec2 wind = ctx.weather->state().wind;
    for (const EntityId id : carcasses)
    {
        const Carcass* c = ctx.carcasses->find(id);
        if (!c)
        {
            continue;
        }
        const float d = distance(c->position, p);
        const bool seen = d < std::min(150.0f, vis) && ctx.terrain->lineOfSight(eye, Vec3{c->position, ctx.terrain->heightAt(c->position) + 0.5f}, 6.0f);
        const bool downwind = wind.length() > 0.5f && dot((p - c->position).normalized(), wind.normalized()) > 0.7f;
        const bool smelt = downwind && CarcassSystem::odourStrength(*c) > 5.0f;
        if (seen)
        {
            map_.addFeature(MapFeatureKind::Carcass, c->position, ctx.now, "Carcasse", id.value, -1, 0.0f, 10.0f);
        }
        else if (smelt)
        {
            map_.addFeature(MapFeatureKind::Note, p + (c->position - p).normalized() * 50.0f, ctx.now, "Odeur de charogne (portée par le vent)", id.value, -1,
                            0.0f, 200.0f);
        }
    }
    // Nests.
    std::vector<EntityId> nests;
    ctx.nests->queryNear(p, 60.0f, nests);
    for (const EntityId id : nests)
    {
        if (const Nest* n = ctx.nests->find(id))
        {
            map_.addFeature(MapFeatureKind::Nest, n->position, ctx.now, "Nid", id.value, -1, static_cast<float>(n->eggs.size()), 5.0f);
        }
    }
    // Fossil exposures.
    for (FossilSite& s : paleo_.mutableSites())
    {
        if (!s.discovered && distance(s.position, p) < 60.0f)
        {
            s.discovered = true;
            map_.addFeature(MapFeatureKind::FossilSite, s.position, ctx.now, s.nameFr, s.id, -1, 0.0f, 20.0f);
            autolog("Affleurement fossilifère découvert : " + s.nameFr + ".", ctx);
        }
    }
    // Footprints at the researcher's feet.
    if (!researcher_.state().inVehicle)
    {
        std::vector<const Footprint*> prints;
        ctx.tracks->query(p, 12.0f, prints);
        int visible = 0;
        Vec2 c;
        for (const Footprint* f : prints)
        {
            if (ctx.tracks->visible(*f) && f->speciesIndex >= 0)
            {
                ++visible;
                c += f->position;
            }
        }
        if (visible >= 3)
        {
            map_.addFeature(MapFeatureKind::Trackway, c / static_cast<float>(visible), ctx.now, "Empreintes", 0, -1, static_cast<float>(visible), 30.0f);
        }
        if (ctx.vegetation->trail(p) > 0.5f)
        {
            map_.addFeature(MapFeatureKind::Trail, p, ctx.now, "Sentier animal", 0, -1, ctx.vegetation->trail(p), 60.0f);
        }
    }
}

void ResearchSystem::update(float dt, WorldContext& ctx, CreatureSystem& creatures)
{
    ResearcherState& r = researcher_.state();
    if (r.inVehicle)
    {
        r.position = vehicle_.state().position + Vec3{0.0f, 0.0f, 2.2f};
        r.velocity = Vec2::fromHeading(vehicle_.state().heading) * vehicle_.state().speed;
    }
    researcher_.update(dt, ctx);
    if (externalVehicle_)
    {
        vehicle_.updateAuxiliary(dt, vehicleControls_, ctx);
    }
    else
    {
        vehicle_.update(dt, vehicleControls_, ctx);
    }
    // Drone (follow lock requires the target to be visible from the drone camera).
    {
        DroneState& d = drone_.state();
        bool visible = false;
        Vec2 truth;
        if (d.mode == DroneMode::Follow)
        {
            const Creature* t = creatures.find(d.followTarget);
            if (t && t->alive)
            {
                truth = t->pos2();
                const float dist = distance(d.position, t->loco.position);
                visible = dist < 400.0f && ctx.terrain->lineOfSight(d.position, t->loco.position + Vec3{0.0f, 0.0f, t->hipHeightM}, 10.0f) &&
                          ctx.vegetation->canopyCover(t->pos2()) < 0.8f;
            }
        }
        const bool wasFlying = d.mode != DroneMode::Docked;
        drone_.update(dt, ctx, vehicle_.state().position + Vec3{0.0f, 0.0f, 2.5f}, eyePosition(), visible, truth);
        if (wasFlying && d.mode == DroneMode::Docked)
        {
            autolog("Drone de retour au véhicule.", ctx);
        }
        if (d.mode == DroneMode::Docked && d.batteryWh < drone_.batteryCapacityWh())
        {
            // Recharge from the vehicle battery at ~200 W.
            const float wh = 200.0f * dt / 3600.0f;
            if (vehicle_.drawWh(wh))
            {
                d.batteryWh = std::min(drone_.batteryCapacityWh(), d.batteryWh + wh * 0.9f);
            }
        }
        if (d.mode != DroneMode::Docked)
        {
            if (Expedition* exp = expeditions_.active())
            {
                exp->droneFlightMin += dt / 60.0;
            }
        }
    }
    captureSounds(ctx);
    // Continuous observation session sampling.
    if (activeSession_ != 0 && ctx.now - lastSessionSample_ >= 2.0)
    {
        lastSessionSample_ = ctx.now;
        const ObservationSession* s = observations_.session(activeSession_);
        OpticsKind kind = OpticsKind::Binoculars;
        if (s && s->method == ObservationMethod::Drone)
        {
            kind = OpticsKind::DroneCamera;
        }
        else if (s && s->method == ObservationMethod::NakedEye)
        {
            kind = OpticsKind::Eye;
        }
        Vec3 eye = eyePosition();
        float heading = viewHeading();
        if (kind == OpticsKind::DroneCamera)
        {
            eye = drone_.state().position;
            heading = drone_.state().heading;
        }
        else if (s && s->speciesFocus >= 0)
        {
            // The observer keeps the focal group in view (binoculars follow the animals).
            float best = 1e9f;
            for (const Creature& c : creatures.creatures())
            {
                if (c.alive && c.speciesIndex == s->speciesFocus)
                {
                    const float d = distance(c.pos2(), eye.xy());
                    if (d < best)
                    {
                        best = d;
                        heading = (c.pos2() - eye.xy()).heading();
                    }
                }
            }
        }
        ObservationRecord rec = observations_.observe(optics(kind), eye, heading, creatures, ctx, observerContext(ctx), 0, rng_);
        bool predatorVisible = false;
        for (const ObservedSubject& sub : rec.subjects)
        {
            if (sub.speciesIndex >= 0 && db_->speciesAt(sub.speciesIndex).sim.predation.style != HuntStyle::None &&
                db_->speciesAt(sub.speciesIndex).sim.adultMassKg > 50.0f)
            {
                predatorVisible = true;
            }
        }
        const ObserverContext oc = observerContext(ctx);
        const u32 id = storeObservation(std::move(rec), ctx, creatures);
        const ObservationRecord* stored = observations_.find(id);
        if (stored)
        {
            observations_.sampleSession(activeSession_, *stored, oc, predatorVisible);
        }
    }
    // Drone camera keeps the map and the archive updated while flying.
    const DroneState& d = drone_.state();
    if (d.mode != DroneMode::Docked && d.mode != DroneMode::Lost && ctx.now - lastDroneObservation_ >= 5.0)
    {
        lastDroneObservation_ = ctx.now;
        map_.reveal(d.position, std::min(500.0f, d.altitudeAglM * 6.0f + 100.0f), *ctx.terrain, ctx.now, 5.0f);
        if (activeSession_ == 0 || observations_.session(activeSession_)->method != ObservationMethod::Drone)
        {
            ObservationRecord rec = observations_.observe(optics(OpticsKind::DroneCamera), d.position, d.heading, creatures, ctx, observerContext(ctx), 0, rng_);
            if (!rec.subjects.empty())
            {
                rec.tags.push_back(d.mode == DroneMode::Survey ? "survol cartographique" : "drone");
                storeObservation(std::move(rec), ctx, creatures);
            }
        }
    }
    if (ctx.now - lastReveal_ >= 2.0)
    {
        const float elapsed = static_cast<float>(std::min(10.0, ctx.now - lastReveal_));
        lastReveal_ = ctx.now;
        const Vec3 eye = eyePosition();
        const float light = visualPerformance(ctx.sky->state().illuminanceLux, 0.15f);
        const float radius = std::min(1500.0f, ctx.weather->visibilityAt(eye.z - ctx.valleyFloorZ, false)) * (0.2f + 0.8f * light);
        map_.reveal(eye, std::max(30.0f, std::min(800.0f, radius)), *ctx.terrain, ctx.now, elapsed);
    }
    if (ctx.now - lastDiscover_ >= 3.0)
    {
        lastDiscover_ = ctx.now;
        autoDiscover(ctx, creatures);
    }
    updateSensors(dt, ctx, creatures);
    updateExpeditionProgress(ctx);
}

void ResearchSystem::onEvent(const SimEvent& e, WorldContext& ctx, CreatureSystem& creatures)
{
    const Vec2 eye = eyePosition().xy();
    switch (e.type)
    {
    case EventType::ResearcherAttacked:
    {
        if (e.other == researcher_.state().id)
        {
            researcher_.applyAttack(e.magnitude, ctx);
            map_.addFeature(MapFeatureKind::Danger, eye, ctx.now, "Attaque subie", 0, -1, e.magnitude, 50.0f);
            const Creature* c = creatures.find(e.subject);
            autolog("Le chercheur a été attaqué" + std::string(c ? " par un " + researchimpl::speciesName(db_, c->speciesIndex) : std::string()) +
                        researchimpl::fmt1(" (gravité %.2f).", e.magnitude),
                    ctx);
            if (Expedition* exp = expeditions_.active())
            {
                ++exp->incidents;
            }
        }
        else if (e.other == vehicle_.state().id)
        {
            autolog("Le véhicule a été heurté par un animal.", ctx);
        }
        break;
    }
    case EventType::Flee:
    {
        // Observable consequence of disturbance: an animal in view runs away from the researcher/vehicle/drone.
        const EntityKind k = e.other.kind();
        if (k != EntityKind::Researcher && k != EntityKind::Vehicle && k != EntityKind::Drone)
        {
            break;
        }
        const Creature* c = creatures.find(e.subject);
        if (!c)
        {
            break;
        }
        const float d = distance(c->pos2(), eye);
        if (d < 400.0f && ctx.terrain->lineOfSight(eyePosition(), c->loco.position + Vec3{0.0f, 0.0f, c->hipHeightM}, 8.0f))
        {
            autolog("Un " + researchimpl::speciesName(db_, c->speciesIndex) + researchimpl::fmt1(" s'est éloigné en courant à %.0f m de vous.", d), ctx);
        }
        break;
    }
    case EventType::Death:
    {
        const Creature* c = creatures.find(e.subject);
        if (c && distance(c->pos2(), eye) < 300.0f &&
            ctx.terrain->lineOfSight(eyePosition(), c->loco.position + Vec3{0.0f, 0.0f, c->hipHeightM}, 8.0f))
        {
            // Only what is observable: the death itself, not its internal cause.
            autolog("Mort d'un " + researchimpl::speciesName(db_, c->speciesIndex) + " observée.", ctx);
        }
        break;
    }
    default: break;
    }
}
} // namespace noctis
