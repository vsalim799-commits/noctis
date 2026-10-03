// noctis_sim — headless runner for the Noctis paleo-ecological simulation.
//
//   noctis_sim validate                      check scientific data consistency
//   noctis_sim run   [--days N] [--out DIR]  run the valley, write census, events and maps
//   noctis_sim demo  [--out DIR]             vertical-slice field expedition (observation, acoustics, drone,
//                                            hypotheses, report) driven by the live simulation
//   noctis_sim export-terrain [--out DIR]    heightmap and weightmaps for Unreal Landscape import
// Common options: --data DIR --env ID --profile ID --seed N --threads N --scale F
#include "Render.h"

#include "Noctis/Core/Log.h"
#include "Noctis/Creatures/CreatureLogic.h"
#include "Noctis/Sim/WorldSimulation.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef NOCTIS_SOURCE_DIR
#define NOCTIS_SOURCE_DIR "."
#endif

using namespace noctis;
using namespace noctistools;

namespace
{
struct Args
{
    std::string mode = "run";
    std::string data = std::string(NOCTIS_SOURCE_DIR) + "/Data";
    std::string env = "hell_creek";
    std::string profile = "standard";
    std::string out = "Output/run";
    u64 seed = 7;
    int threads = 0;
    float scale = 1.0f;
    double days = 2.0;
    double hours = 2.0;
};

Args parseArgs(int argc, char** argv)
{
    Args a;
    if (argc > 1)
    {
        a.mode = argv[1];
    }
    for (int i = 2; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (k == "--data")
        {
            a.data = next();
        }
        else if (k == "--env")
        {
            a.env = next();
        }
        else if (k == "--profile")
        {
            a.profile = next();
        }
        else if (k == "--out")
        {
            a.out = next();
        }
        else if (k == "--seed")
        {
            a.seed = std::stoull(next());
        }
        else if (k == "--threads")
        {
            a.threads = std::stoi(next());
        }
        else if (k == "--scale")
        {
            a.scale = std::stof(next());
        }
        else if (k == "--days")
        {
            a.days = std::stod(next());
        }
        else if (k == "--hours")
        {
            a.hours = std::stod(next());
        }
    }
    return a;
}

void writeText(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary);
    f << text;
}

int cmdValidate(const Args& a)
{
    auto fs = makeStdFileSystem();
    EvidenceDatabase db;
    db.load(*fs, a.data);
    const std::vector<ValidationIssue> issues = db.validate();
    int errors = 0;
    int warnings = 0;
    std::map<std::string, int> perFile;
    for (const ValidationIssue& i : issues)
    {
        const char* sev = i.severity == ValidationIssue::Severity::Error ? "ERROR" : (i.severity == ValidationIssue::Severity::Warning ? "WARN " : "INFO ");
        errors += i.severity == ValidationIssue::Severity::Error ? 1 : 0;
        warnings += i.severity == ValidationIssue::Severity::Warning ? 1 : 0;
        if (i.severity != ValidationIssue::Severity::Info)
        {
            std::printf("%s %s : %s\n", sev, i.file.c_str(), i.message.c_str());
        }
        ++perFile[i.file];
    }
    int verified = 0;
    for (const auto& s : db.bibliography())
    {
        verified += s.second.verified ? 1 : 0;
    }
    std::printf("\nEspèces : %zu | environnements : %zu | références : %zu (%d vérifiées) | profils : %zu\n", db.species().size(), db.environments().size(),
                db.bibliography().size(), verified, db.profiles().size());
    std::printf("Erreurs : %d | avertissements : %d\n", errors, warnings);
    return errors == 0 ? 0 : 1;
}

void printCensus(WorldSimulation& w, FILE* out)
{
    const double now = w.clock().seconds();
    std::fprintf(out, "%-30s %6s %6s %6s %6s %6s %6s %6s\n", "Espèce", "vivant", "juv.", "naiss.", "morts", "préd.", "immig.", "émigr.");
    for (const int si : w.speciesInWorld())
    {
        const SpeciesDefinition& d = w.db().speciesAt(si);
        if (d.sim.role != SimRole::Agent)
        {
            continue;
        }
        const SpeciesCensus c = w.creatures().census(si, now);
        std::string causes;
        for (int k = 0; k < 11; ++k)
        {
            if (c.deathsByCause[k] > 0)
            {
                causes += std::string(" ") + deathCauseLabelFr(static_cast<DeathCause>(k)) + "=" + std::to_string(c.deathsByCause[k]);
            }
        }
        std::fprintf(out, "%-30s %6d %6d %6d %6d %6d %6d %6d %s\n", d.scientificName.c_str(), c.alive, c.juveniles, c.births, c.deaths,
                     c.deathsByCause[static_cast<int>(DeathCause::Predation)], c.immigrants, c.emigrants, causes.c_str());
    }
    for (const PopulationPool& p : w.ecology().pools())
    {
        std::fprintf(out, "%-30s %14.0f individus (population)\n", w.db().speciesAt(p.speciesIndex).scientificName.c_str(), p.abundance);
    }
}

bool initWorld(WorldSimulation& w, const Args& a)
{
    SimConfig cfg;
    cfg.dataRoot = a.data;
    cfg.environmentId = a.env;
    cfg.profileId = a.profile;
    cfg.seed = a.seed;
    cfg.threads = a.threads;
    cfg.populationScale = a.scale;
    const auto t0 = std::chrono::steady_clock::now();
    if (!w.initialize(cfg))
    {
        return false;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("Monde initialisé en %.0f ms\n", ms);
    return true;
}

int cmdRun(const Args& a)
{
    WorldSimulation w;
    if (!initWorld(w, a))
    {
        return 1;
    }
    std::filesystem::create_directories(a.out);
    // Count notable events.
    std::map<EventType, int> counts;
    std::vector<std::string> notable;
    std::map<std::string, int> calls;
    w.events().subscribe([&](const SimEvent& e) {
        ++counts[e.type];
        if (e.type == EventType::Vocalization)
        {
            const Creature* c = w.creatures().find(e.subject);
            if (c)
            {
                ++calls[w.db().speciesAt(c->speciesIndex).id + ":" + callContextKey(static_cast<CallContext>(e.param)) + ":" +
                        behaviorKey(c->behavior.current)];
            }
        }
        if (e.type == EventType::HuntKill || e.type == EventType::Hatch || e.type == EventType::EggsLaid || e.type == EventType::Mating ||
            e.type == EventType::FireIgnition || e.type == EventType::Immigration || e.type == EventType::Emigration ||
            (e.type == EventType::Death && e.param != static_cast<u32>(DeathCause::Unknown)))
        {
            char buf[256];
            const Creature* c = w.creatures().find(e.subject);
            std::snprintf(buf, sizeof(buf), "%s  %-18s %-30s mag=%.1f", w.clock().formatted().c_str(), eventTypeName(e.type),
                          c ? w.db().speciesAt(c->speciesIndex).scientificName.c_str() : "", e.magnitude);
            notable.push_back(buf);
        }
    });
    MapOptions mo;
    mo.title = "NOCTIS - " + w.environment().nameFr;
    writePng(a.out + "/map_t0.png", renderMap(w, mo));
    const auto t0 = std::chrono::steady_clock::now();
    const double total = a.days * 86400.0;
    double done = 0.0;
    int day = 0;
    while (done < total)
    {
        const double chunk = std::min(3600.0, total - done);
        w.advance(chunk, 1.0);
        done += chunk;
        if (static_cast<int>(done / 86400.0) > day)
        {
            day = static_cast<int>(done / 86400.0);
            std::printf("\n=== %s ===\n", w.clock().formatted().c_str());
            printCensus(w, stdout);
            mo.title = "NOCTIS - JOUR " + std::to_string(day);
            writePng(a.out + "/map_day" + std::to_string(day) + ".png", renderMap(w, mo));
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%.1f jours simulés en %.1f s (×%.0f temps réel)\n", a.days, secs, total / std::max(1e-3, secs));
    const WorldSimulation::Timings& tm = w.timings();
    std::printf("Coût moyen par pas : environnement %.2f ms, créatures %.2f ms, recherche %.2f ms ; horaire %.1f ms ; journalier %.1f ms\n", tm.environment,
                tm.creatures, tm.research, tm.hourly, tm.daily);
    std::printf("Événements : ");
    for (const auto& c : counts)
    {
        std::printf("%s=%d ", eventTypeName(c.first), c.second);
    }
    std::printf("\nVocalisations par espèce:contexte:comportement :\n");
    for (const auto& c : calls)
    {
        if (c.second > 20)
        {
            std::printf("  %-60s %d\n", c.first.c_str(), c.second);
        }
    }
    std::printf("\nPrécipitations cumulées : %.1f mm ; empreintes en mémoire : %zu ; carcasses : %zu\n", w.weather().accumulatedPrecipMm(), w.tracks().count(),
                w.carcasses().all().size());
    std::string log;
    for (const std::string& s : notable)
    {
        log += s + "\n";
    }
    writeText(a.out + "/events.txt", log);
    FILE* f = std::fopen((a.out + "/census.txt").c_str(), "w");
    if (f)
    {
        printCensus(w, f);
        std::fclose(f);
    }
    mo.title = "NOCTIS - FIN";
    writePng(a.out + "/map_end.png", renderMap(w, mo));
    return 0;
}

int cmdExportTerrain(const Args& a)
{
    WorldSimulation w;
    Args b = a;
    b.scale = 0.0f;
    SimConfig cfg;
    cfg.dataRoot = a.data;
    cfg.environmentId = a.env;
    cfg.profileId = a.profile;
    cfg.seed = a.seed;
    cfg.spawnPopulation = false;
    if (!w.initialize(cfg))
    {
        return 1;
    }
    std::filesystem::create_directories(a.out);
    const Grid2D<float>& h = w.terrain().heights();
    const float lo = w.terrain().minHeight();
    const float hi = w.terrain().maxHeight();
    std::vector<u16> r16(h.cellCount());
    std::vector<u8> raw;
    raw.reserve(h.cellCount() * 2);
    // Unreal landscapes import row 0 as the northern edge (+Y in our frame maps to -Y in UE).
    for (int y = 0; y < h.height(); ++y)
    {
        for (int x = 0; x < h.width(); ++x)
        {
            const float v = (h.at(x, h.height() - 1 - y) - lo) / std::max(0.01f, hi - lo);
            const u16 s = static_cast<u16>(std::lround(saturate(v) * 65535.0f));
            r16[static_cast<size_t>(y) * static_cast<size_t>(h.width()) + static_cast<size_t>(x)] = s;
            raw.push_back(static_cast<u8>(s & 0xFF));
            raw.push_back(static_cast<u8>(s >> 8));
        }
    }
    writePng16Gray(a.out + "/heightmap.png", h.width(), h.height(), r16);
    w.fileSystem().writeBinary(a.out + "/heightmap.r16", raw);
    // Weightmaps per habitat (8-bit).
    const Grid2D<u8>& hab = w.terrain().habitats();
    for (int hi2 = 0; hi2 < kHabitatCount; ++hi2)
    {
        Image img(hab.width(), hab.height());
        bool any = false;
        for (int y = 0; y < hab.height(); ++y)
        {
            for (int x = 0; x < hab.width(); ++x)
            {
                const bool on = hab.at(x, hab.height() - 1 - y) == hi2;
                any = any || on;
                const u8 v = on ? 255 : 0;
                img.at(x, y) = Rgb{v, v, v};
            }
        }
        if (any)
        {
            writePng(a.out + "/weight_" + habitatKey(static_cast<Habitat>(hi2)) + ".png", img);
        }
    }
    // Foliage density masks per plant functional type (drive procedural foliage).
    for (int t = 0; t < kPlantTypeCount; ++t)
    {
        const std::vector<float> d = w.vegetation().densityMap(static_cast<PlantType>(t));
        float mx = 0.0f;
        for (const float v : d)
        {
            mx = std::max(mx, v);
        }
        if (mx <= 0.0f)
        {
            continue;
        }
        const int vw = w.vegetation().width();
        const int vh = w.vegetation().height();
        Image img(vw, vh);
        for (int y = 0; y < vh; ++y)
        {
            for (int x = 0; x < vw; ++x)
            {
                const u8 v = static_cast<u8>(saturate(d[static_cast<size_t>(vh - 1 - y) * static_cast<size_t>(vw) + static_cast<size_t>(x)]) * 255.0f);
                img.at(x, y) = Rgb{v, v, v};
            }
        }
        writePng(a.out + "/foliage_" + plantTypeKey(static_cast<PlantType>(t)) + ".png", img);
    }
    JsonValue meta = JsonValue::makeObject();
    meta.set("environment", w.environment().id);
    meta.set("seed", static_cast<double>(a.seed));
    meta.set("size_m", w.terrain().size());
    meta.set("samples", h.width());
    meta.set("min_height_m", lo);
    meta.set("max_height_m", hi);
    meta.set("ue_scale_xy_cm", w.terrain().spacing() * 100.0f);
    meta.set("ue_scale_z", (hi - lo) * 100.0f / 512.0f);
    meta.set("ue_location_z_cm", (lo + (hi - lo) * 0.5f) * 100.0f);
    meta.set("note", "Unreal: Landscape > Import from File > heightmap.png (16-bit). Scale Z = (max-min) m * 100 / 512. Layers from weight_*.png.");
    writeText(a.out + "/terrain.json", meta.dump(true));
    std::printf("Terrain exporté : %dx%d, relief %.1f m -> %s\n", h.width(), h.height(), hi - lo, a.out.c_str());
    return 0;
}

// ----------------------------------------------------------------------------- demo

const Group* largestGroup(WorldSimulation& w, int speciesIndex)
{
    const Group* best = nullptr;
    for (const Group& g : w.creatures().groups())
    {
        if (g.speciesIndex == speciesIndex && (!best || g.members.size() > best->members.size()))
        {
            best = &g;
        }
    }
    return best;
}

int cmdDemo(const Args& a)
{
    WorldSimulation w;
    if (!initWorld(w, a))
    {
        return 1;
    }
    std::filesystem::create_directories(a.out);
    WorldContext& ctx = w.context();
    ResearchSystem& rs = w.research();
    const int edmo = w.db().speciesIndex("edmontosaurus_annectens");
    if (edmo < 0)
    {
        std::printf("Edmontosaurus absent des données.\n");
        return 1;
    }
    // Let the valley run a few hours before dawn so animals settle into their routines.
    w.setTimeScale(100.0f);
    w.advance(3.0 * 3600.0, 2.0);
    w.setTimeScale(1.0f);
    const Group* herd = largestGroup(w, edmo);
    if (!herd)
    {
        std::printf("Aucun groupe d'Edmontosaurus.\n");
        return 1;
    }
    const Vec2 herdPos = herd->centroid;
    // Park the rover ~220 m from the herd on dry ground, facing it.
    Vec2 park = herdPos + Vec2{220.0f, 0.0f};
    for (int k = 0; k < 16; ++k)
    {
        const Vec2 cand = herdPos + Vec2::fromHeading(kTwoPi * static_cast<float>(k) / 16.0f) * 220.0f;
        if (w.water().depthAt(cand, w.terrain()) <= 0.0f && w.terrain().slopeDegAt(cand) < 10.0f && w.terrain().contains(cand))
        {
            park = cand;
            break;
        }
    }
    rs.vehicle().setTransform(Vec3{park, w.terrain().heightAt(park)}, (herdPos - park).heading(), 0.0f);
    VehicleControls vc;
    vc.engineOn = false;
    rs.setVehicleControls(vc);
    rs.enterVehicle(true);
    // Morning fog (scenario setting) for the first ninety minutes.
    w.weather().forceFog(0.012f);

    Expedition e;
    e.titleFr = "Vocalisations et cohésion des groupes d'Edmontosaurus";
    e.questionFr = "Les groupes d'Edmontosaurus annectens changent-ils d'organisation spatiale après une vocalisation détectée, "
                   "et la présence du drone modifie-t-elle leur vigilance ?";
    e.zoneNameFr = "Plaine d'inondation centrale";
    e.zone = Rect2{herdPos - Vec2{800.0f, 800.0f}, herdPos + Vec2{800.0f, 800.0f}};
    e.plannedDays = 1.0f;
    e.loadout = {{"camera_body_ff", 1}, {"lens_100_400", 1}, {"video_camera", 1}, {"binoculars_10x42", 1}, {"omni_mic", 4},
                 {"audio_recorder", 2}, {"camera_trap", 2}, {"drone_quad", 1}, {"drone_battery", 3}, {"gps_unit", 1}, {"excavation_kit", 1}};
    e.targets = {{DataTargetKind::SessionHours, static_cast<i16>(edmo), 1.5f, 0.0f}, {DataTargetKind::Recordings, -1, 8.0f, 0.0f},
                 {DataTargetKind::IdentifiedIndividuals, static_cast<i16>(edmo), 3.0f, 0.0f}};
    const u32 expId = rs.planExpedition(e);
    rs.startExpedition(expId, ctx);
    Hypothesis h1;
    h1.statementFr = "Après une vocalisation détectée, l'espacement entre individus du groupe diminue dans la minute qui suit.";
    h1.kind = HypothesisKind::EventResponse;
    h1.speciesIndex = static_cast<i16>(edmo);
    h1.response = Measure::GroupSpacing;
    h1.trigger = EventTrigger::CallDetected;
    h1.windowS = 60.0f;
    h1.predicted = PredictedDirection::Decrease;
    const u32 h1id = rs.createHypothesis(h1, ctx);
    Hypothesis h2;
    h2.statementFr = "La proportion d'individus vigilants est plus élevée quand le drone est en vol.";
    h2.kind = HypothesisKind::Comparison;
    h2.speciesIndex = static_cast<i16>(edmo);
    h2.response = Measure::VigilantFraction;
    h2.condition.kind = ConditionKind::DroneAirborne;
    h2.predicted = PredictedDirection::Increase;
    const u32 h2id = rs.createHypothesis(h2, ctx);
    Hypothesis h3;
    h3.statementFr = "Plus le groupe observé est grand, plus la proportion d'individus vigilants est faible (effet « plus d'yeux »).";
    h3.kind = HypothesisKind::Correlation;
    h3.speciesIndex = static_cast<i16>(edmo);
    h3.response = Measure::GroupSize;
    h3.response2 = Measure::VigilantFraction;
    h3.predicted = PredictedDirection::Negative;
    const u32 h3id = rs.createHypothesis(h3, ctx);

    rs.startSession(ObservationMethod::Binoculars, static_cast<i16>(edmo), ctx);
    MicSpec mic;
    const Vec2 arrayCentre = lerp(park, herdPos, 0.45f);
    u32 arrayId = rs.deployMicArray(arrayCentre, 70.0f, 4, mic, ctx);
    rs.placeSensor(SensorKind::CameraTrap, Vec3{lerp(park, herdPos, 0.8f), w.terrain().heightAt(lerp(park, herdPos, 0.8f)) + 0.5f}, (herdPos - park).heading(), ctx);
    DrinkSite site;
    if (w.water().findDrinkSite(herdPos, 1500.0f, site))
    {
        rs.placeSensor(SensorKind::AudioRecorder, Vec3{site.position, w.terrain().heightAt(site.position) + 1.0f}, 0.0f, ctx);
        rs.placeSensor(SensorKind::CameraTrap, Vec3{site.position, w.terrain().heightAt(site.position) + 0.5f}, 0.0f, ctx);
    }
    std::printf("Expédition démarrée à %s, troupeau de %zu individus à %.0f m.\n", w.clock().formatted().c_str(), herd->members.size(), distance(park, herdPos));

    MapOptions close;
    close.size = 1024;
    close.window = Rect2{herdPos - Vec2{450.0f, 450.0f}, herdPos + Vec2{450.0f, 450.0f}};
    close.title = "EXPEDITION 1 - AUBE, BROUILLARD";
    writePng(a.out + "/demo_01_dawn_closeup.png", renderMap(w, close));

    const double sessionSeconds = a.hours * 3600.0;
    double t = 0.0;
    double lastPhoto = -1e9;
    double lastArrayCycle = 0.0;
    bool droneUp = false;
    int droneFlights = 0;
    while (t < sessionSeconds)
    {
        w.step(0.1);
        t += 0.1;
        if (t > 5400.0)
        {
            w.weather().forceFog(-1.0f);
        }
        // Keep the herd in the binoculars: the researcher turns towards the focal group.
        if (const Group* g = largestGroup(w, edmo))
        {
            rs.researcher().state().heading = (g->centroid - rs.eyePosition().xy()).heading();
            rs.vehicle().state().heading = rs.researcher().state().heading;
        }
        if (t - lastPhoto > 300.0)
        {
            lastPhoto = t;
            PhotoSettings ps;
            ps.focalMm = 400.0f;
            ps.shutterS = 1.0f / 1000.0f;
            ps.iso = w.sky().state().illuminanceLux < 1000.0f ? 3200 : 800;
            rs.takePhoto(ps, ctx, w.creatures());
        }
        // Mic array recordings in 10-minute files.
        if (t - lastArrayCycle > 600.0)
        {
            lastArrayCycle = t;
            rs.stopMicArray(arrayId, ctx);
            arrayId = rs.deployMicArray(arrayCentre, 70.0f, 4, mic, ctx);
        }
        // Two drone flights over the herd (alternating with drone-free periods for hypothesis H2).
        const bool droneWindow = (t > 1800.0 && t < 2700.0) || (t > 4500.0 && t < 5400.0);
        if (droneWindow && !droneUp && rs.drone().state().mode == DroneMode::Docked && droneFlights < 2)
        {
            if (rs.launchDrone(ctx))
            {
                droneUp = true;
                ++droneFlights;
                if (const Group* g = largestGroup(w, edmo))
                {
                    rs.drone().setOrbit(g->centroid, 70.0f, 35.0f);
                }
            }
        }
        if (droneUp && !droneWindow)
        {
            rs.drone().command(DroneMode::ReturnHome);
            droneUp = false;
        }
        if (droneUp && static_cast<int>(t) % 60 == 0)
        {
            if (const Group* g = largestGroup(w, edmo))
            {
                rs.drone().state().orbitCenter = g->centroid;
            }
        }
        if (static_cast<int>(t * 10.0) % 18000 == 0)
        {
            std::printf("  %s  session %.0f min, observations %zu, enregistrements %zu, drone %s\n", w.clock().formatted().c_str(), t / 60.0,
                        rs.observations().records().size(), rs.recordings().size(), droneModeLabelFr(rs.drone().state().mode));
        }
        if (std::fabs(t - 2100.0) < 0.05)
        {
            close.title = "EXPEDITION 1 - DRONE EN ORBITE";
            if (const Group* g = largestGroup(w, edmo))
            {
                close.window = Rect2{g->centroid - Vec2{450.0f, 450.0f}, g->centroid + Vec2{450.0f, 450.0f}};
            }
            writePng(a.out + "/demo_02_drone_orbit.png", renderMap(w, close));
        }
    }
    rs.stopMicArray(arrayId, ctx);
    rs.stopSession(ctx);
    // Lab work in the rover.
    std::printf("Analyse au laboratoire mobile...\n");
    size_t bestRec = 0;
    size_t bestEvents = 0;
    for (const Recording& r : rs.recordings())
    {
        const RecordingAnalysis* an = rs.analyzeRecording(r.id, ctx);
        if (an && an->events.size() > bestEvents)
        {
            bestEvents = an->events.size();
            bestRec = r.id;
        }
    }
    rs.clusterCalls(3, 99);
    if (bestRec != 0)
    {
        const Recording* r = rs.findRecording(static_cast<u32>(bestRec));
        for (const RecordingAnalysis& an : rs.analyses())
        {
            if (an.recordingId == bestRec && r)
            {
                writePng(a.out + "/demo_03_spectrogram.png", renderSpectrogram(*r, an, "SPECTROGRAMME - " + r->label));
                w.fileSystem().writeBinary(a.out + "/recording_best.wav", AcousticAnalysis::toWav(*r));
                if (r->arrayId != 0 && !an.events.empty())
                {
                    const LocalizationResult loc = rs.localizeEvent(r->arrayId, r->id, 0, w.weather().state().temperatureC);
                    if (loc.valid)
                    {
                        std::printf("Localisation acoustique de l'événement 0 : (%.0f, %.0f) ± %.0f m, résidu %.2f ms\n", loc.position.x, loc.position.y,
                                    loc.errorRadiusM, loc.residualMs);
                        rs.addNote("Source sonore localisée par réseau de micros.", ctx);
                    }
                }
            }
        }
    }
    // Field paleontology near the herd: measure a trackway.
    {
        std::vector<const Footprint*> prints;
        w.tracks().query(herdPos, 300.0f, prints);
        const Footprint* seed = nullptr;
        for (const Footprint* f : prints)
        {
            if (f->speciesIndex == edmo && w.tracks().visible(*f) && !f->manus)
            {
                seed = f;
                break;
            }
        }
        if (seed)
        {
            rs.enterVehicle(false);
            rs.researcher().setTransform(Vec3{seed->position, w.terrain().heightAt(seed->position) + 1.65f}, 0.0f, Vec2{});
            const u32 tw = rs.measureTrackway(seed->id, ctx);
            const TrackwayAnalysis& ta = rs.trackways()[tw - 1];
            std::printf("Piste : %d empreintes, foulée %.2f m, vitesse estimée %.2f m/s (%.2f–%.2f) ; vitesse réelle du pisteur (cachée) %.2f m/s\n", ta.footprints,
                        ta.strideM, ta.speedEstMs, ta.speedLowMs, ta.speedHighMs, ta.trueSpeedMs);
            rs.enterVehicle(true);
        }
    }
    for (const u32 id : {h1id, h2id, h3id})
    {
        rs.testHypothesis(id, ctx, true);
        const Hypothesis* h = rs.hypotheses().find(id);
        std::printf("H%u : %s | n=%d p=%.3f effet=%.2f\n", id, hypothesisStatusLabelFr(h->status), h->result.n, h->result.pValue, h->result.effectSize);
    }
    close.title = "EXPEDITION 1 - FIN DE SESSION";
    if (const Group* g = largestGroup(w, edmo))
    {
        close.window = Rect2{g->centroid - Vec2{450.0f, 450.0f}, g->centroid + Vec2{450.0f, 450.0f}};
    }
    writePng(a.out + "/demo_04_end_closeup.png", renderMap(w, close));
    MapOptions full;
    full.title = "NOCTIS - VALLEE DE HELL CREEK";
    writePng(a.out + "/demo_05_valley.png", renderMap(w, full));
    full.explored = true;
    full.title = "CARTE DU CHERCHEUR (ZONES EXPLOREES)";
    writePng(a.out + "/demo_06_explored.png", renderMap(w, full));
    const ScientificReport* rep = rs.endExpedition(expId, ctx, false);
    if (rep)
    {
        writeText(a.out + "/rapport_expedition_1.md", rep->markdown);
        writeText(a.out + "/rapport_expedition_1.json", rep->json.dump(true));
    }
    std::printf("Catalogue : %zu individus ; observations : %zu ; photos : %zu ; détections acoustiques : %zu\n", rs.observations().catalog().entries().size(),
                rs.observations().records().size(), rs.observations().photos().size(), rs.detections().size());
    std::printf("Taux de mauvaise identification (débogage) : %.1f %%\n", rs.observations().catalog().misidentificationRate() * 100.0f);
    std::printf("Rapport : %s/rapport_expedition_1.md\n", a.out.c_str());
    return 0;
}

// Binary field blob for the WebGL preview: arrays are appended and described in the JSON manifest.
struct BlobWriter
{
    std::vector<uint8_t> bytes;
    JsonValue fields = JsonValue::makeObject();
    void align4()
    {
        while (bytes.size() % 4u != 0u)
        {
            bytes.push_back(0);
        }
    }
    void addF32(const std::string& name, const std::vector<float>& v)
    {
        align4();
        JsonValue d = JsonValue::makeObject();
        d.set("offset", static_cast<double>(bytes.size()));
        d.set("count", static_cast<double>(v.size()));
        d.set("type", "f32");
        fields.set(name, d);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(v.data());
        bytes.insert(bytes.end(), p, p + v.size() * sizeof(float));
    }
    // Quantizes [0,1] values to bytes.
    void addUnit8(const std::string& name, const std::vector<float>& v)
    {
        JsonValue d = JsonValue::makeObject();
        d.set("offset", static_cast<double>(bytes.size()));
        d.set("count", static_cast<double>(v.size()));
        d.set("type", "u8");
        fields.set(name, d);
        for (const float x : v)
        {
            bytes.push_back(static_cast<uint8_t>(std::lround(saturate(x) * 255.0f)));
        }
    }
    void addU8(const std::string& name, const std::vector<uint8_t>& v)
    {
        JsonValue d = JsonValue::makeObject();
        d.set("offset", static_cast<double>(bytes.size()));
        d.set("count", static_cast<double>(v.size()));
        d.set("type", "u8raw");
        fields.set(name, d);
        bytes.insert(bytes.end(), v.begin(), v.end());
    }
};

// Exports the live world for the WebGL preview: a detailed window that contains the largest
// Edmontosaurus herd and the nearest river reach, the whole valley at low resolution for the
// horizon, and every animal in the window with its simulated pose (feet, soft tissue).
int cmdPreview(const Args& a)
{
    WorldSimulation w;
    if (!initWorld(w, a))
    {
        return 1;
    }
    std::filesystem::create_directories(a.out);
    const int edmo = w.db().speciesIndex("edmontosaurus_annectens");
    w.setTimeScale(100.0f);
    w.advance(a.hours * 3600.0, 2.0);
    w.setTimeScale(1.0f);
    Vec2 focus = w.basecamp();
    if (const Group* g = largestGroup(w, edmo))
    {
        focus = g->centroid;
    }
    // Nearest river reach to the herd: the window is shifted so both are in view.
    Vec2 riverPt = focus;
    float riverDist = 1e9f;
    for (const RiverDesc& r : w.layout().rivers)
    {
        for (const RiverPoint& rp : r.points)
        {
            const float d = (rp.position - focus).length();
            if (d < riverDist)
            {
                riverDist = d;
                riverPt = rp.position;
            }
        }
    }
    const float half = 1024.0f;
    Vec2 center = focus;
    if (riverDist < 1500.0f)
    {
        center = focus + (riverPt - focus) * 0.5f;
    }
    const Rect2 tb = w.terrain().bounds();
    center.x = clampf(center.x, tb.min.x + half, tb.max.x - half);
    center.y = clampf(center.y, tb.min.y + half, tb.max.y - half);
    const Rect2 win{center - Vec2{half, half}, center + Vec2{half, half}};
    // Bring the rover close so the herd is simulated at full detail (feet, soft tissue).
    const Vec2 park = focus + Vec2{180.0f, -120.0f};
    w.research().vehicle().setTransform(Vec3{park, w.terrain().heightAt(park)}, (focus - park).heading(), 0.0f);
    w.research().enterVehicle(true);
    w.advance(90.0, 0.05);

    auto density = [&](const Vec2& p, PlantType t) {
        const int ci = w.vegetation().cellIndexAt(p);
        const PlantTypeSpec& sp = w.vegetation().spec(t);
        if (ci < 0 || !sp.present || sp.maxBiomassKgM2 <= 0.0f)
        {
            return 0.0f;
        }
        return saturate(w.vegetation().biomass(ci, t) / sp.maxBiomassKgM2);
    };
    BlobWriter blob;
    // Detailed window (4 m spacing).
    const int n = 513;
    {
        const size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);
        std::vector<float> heights(count), water(count), canopy(count), fern(count), conifer(count), angio(count), palm(count), horsetail(count),
            shrub(count), trail(count), moisture(count);
        std::vector<uint8_t> habitat(count), substrate(count);
        for (int j = 0; j < n; ++j)
        {
            for (int i = 0; i < n; ++i)
            {
                const size_t k = static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i);
                const Vec2 p{lerpf(win.min.x, win.max.x, static_cast<float>(i) / static_cast<float>(n - 1)),
                             lerpf(win.min.y, win.max.y, static_cast<float>(j) / static_cast<float>(n - 1))};
                heights[k] = w.terrain().heightAt(p);
                habitat[k] = static_cast<uint8_t>(w.terrain().habitatAt(p));
                substrate[k] = static_cast<uint8_t>(w.terrain().substrateAt(p));
                water[k] = w.water().query(p, w.terrain()).depth;
                canopy[k] = w.vegetation().canopyCover(p);
                fern[k] = density(p, PlantType::Fern);
                conifer[k] = density(p, PlantType::Conifer);
                angio[k] = density(p, PlantType::AngioTree);
                palm[k] = density(p, PlantType::Palm);
                horsetail[k] = density(p, PlantType::Horsetail);
                shrub[k] = std::max(density(p, PlantType::AngioShrub), density(p, PlantType::Cycadophyte));
                trail[k] = w.vegetation().trail(p);
                moisture[k] = w.terrain().moistureAt(p);
            }
        }
        blob.addF32("heights", heights);
        blob.addF32("water", water);
        blob.addU8("habitat", habitat);
        blob.addU8("substrate", substrate);
        blob.addUnit8("canopy", canopy);
        blob.addUnit8("fern", fern);
        blob.addUnit8("conifer", conifer);
        blob.addUnit8("angio", angio);
        blob.addUnit8("palm", palm);
        blob.addUnit8("horsetail", horsetail);
        blob.addUnit8("shrub", shrub);
        blob.addUnit8("trail", trail);
        blob.addUnit8("moisture", moisture);
    }
    // Whole valley at low resolution for the horizon.
    const int farN = 257;
    {
        const size_t count = static_cast<size_t>(farN) * static_cast<size_t>(farN);
        std::vector<float> heights(count), water(count), canopy(count), conifer(count), angio(count);
        for (int j = 0; j < farN; ++j)
        {
            for (int i = 0; i < farN; ++i)
            {
                const size_t k = static_cast<size_t>(j) * static_cast<size_t>(farN) + static_cast<size_t>(i);
                const Vec2 p{lerpf(tb.min.x, tb.max.x, static_cast<float>(i) / static_cast<float>(farN - 1)),
                             lerpf(tb.min.y, tb.max.y, static_cast<float>(j) / static_cast<float>(farN - 1))};
                heights[k] = w.terrain().heightAt(p);
                water[k] = w.water().query(p, w.terrain()).depth;
                canopy[k] = w.vegetation().canopyCover(p);
                conifer[k] = density(p, PlantType::Conifer);
                angio[k] = density(p, PlantType::AngioTree);
            }
        }
        blob.addF32("farHeights", heights);
        blob.addF32("farWater", water);
        blob.addUnit8("farCanopy", canopy);
        blob.addUnit8("farConifer", conifer);
        blob.addUnit8("farAngio", angio);
    }

    JsonValue root = JsonValue::makeObject();
    auto rectJson = [](const Rect2& r) {
        JsonValue j = JsonValue::makeArray();
        j.push(r.min.x);
        j.push(r.min.y);
        j.push(r.max.x);
        j.push(r.max.y);
        return j;
    };
    root.set("format", "noctis-preview-2");
    root.set("window", rectJson(win));
    root.set("n", n);
    root.set("world", rectJson(tb));
    root.set("farN", farN);
    root.set("fields", blob.fields);
    root.set("time", w.clock().formatted());
    root.set("hour", w.clock().hourOfDay());
    root.set("weather", weatherRegimeLabelFr(w.weather().state().regime));
    root.set("environment", w.environment().nameFr);
    const SkyState& sky = w.sky().state();
    JsonValue sun = JsonValue::makeArray();
    sun.push(sky.sunDirection.x);
    sun.push(sky.sunDirection.y);
    sun.push(sky.sunDirection.z);
    root.set("sun", sun);
    root.set("lux", sky.illuminanceLux);
    root.set("fog", w.weather().fogExtinctionAt(0.0f, false));
    root.set("cloud", w.weather().state().cloudCover);
    root.set("wind", w.weather().state().windSpeedMs);
    JsonValue focusJ = JsonValue::makeArray();
    focusJ.push(focus.x);
    focusJ.push(focus.y);
    root.set("focus", focusJ);
    // River centrelines (bankfull width) for camera placement.
    JsonValue rivers = JsonValue::makeArray();
    for (const RiverDesc& r : w.layout().rivers)
    {
        JsonValue pts = JsonValue::makeArray();
        for (size_t k = 0; k < r.points.size(); k += 4)
        {
            const RiverPoint& rp = r.points[k];
            JsonValue q = JsonValue::makeArray();
            q.push(rp.position.x);
            q.push(rp.position.y);
            q.push(rp.width);
            pts.push(q);
        }
        rivers.push(pts);
    }
    root.set("rivers", rivers);
    JsonValue species = JsonValue::makeArray();
    for (const SpeciesDefinition& d : w.db().species())
    {
        JsonValue s = JsonValue::makeObject();
        s.set("id", d.id);
        s.set("name", d.scientificName);
        s.set("plan", bodyPlanKey(d.sim.bodyPlan));
        s.set("length", d.sim.adultLengthM);
        s.set("hip", d.sim.hipHeightM);
        species.push(s);
    }
    root.set("species", species);
    JsonValue creatures = JsonValue::makeArray();
    for (const Creature& c : w.creatures().creatures())
    {
        if (!c.alive || !win.contains(c.pos2()))
        {
            continue;
        }
        JsonValue o = JsonValue::makeObject();
        o.set("id", c.id.value);
        o.set("s", c.speciesIndex);
        o.set("x", c.loco.position.x);
        o.set("y", c.loco.position.y);
        o.set("z", c.loco.position.z);
        o.set("h", c.loco.heading);
        o.set("pitch", c.loco.pitch);
        o.set("roll", c.loco.roll);
        o.set("len", c.lengthM);
        o.set("hip", c.hipHeightM);
        o.set("mass", c.massKg);
        o.set("speed", c.loco.speed);
        o.set("gait", gaitLabelFr(c.loco.gait));
        o.set("posture", static_cast<int>(c.behavior.posture));
        o.set("behavior", behaviorLabelFr(c.behavior.current));
        o.set("lod", static_cast<int>(c.lod));
        o.set("group", static_cast<double>(c.social.groupId));
        JsonValue feet = JsonValue::makeArray();
        for (int f = 0; f < c.loco.footCount; ++f)
        {
            const FootState& fs = c.loco.feet[static_cast<size_t>(f)];
            JsonValue jf = JsonValue::makeArray();
            const Vec3 fp = fs.inContact ? fs.planted : fs.target;
            jf.push(fp.x);
            jf.push(fp.y);
            jf.push(fp.z);
            jf.push(fs.inContact ? 1 : 0);
            jf.push(fs.fore ? 1 : 0);
            feet.push(jf);
        }
        o.set("feet", feet);
        JsonValue soft = JsonValue::makeArray();
        for (const float v : c.soft.offset)
        {
            soft.push(v);
        }
        o.set("soft", soft);
        creatures.push(o);
    }
    root.set("creatures", creatures);
    JsonValue tracks = JsonValue::makeArray();
    w.tracks().forEach([&](const Footprint& f) {
        if (win.contains(f.position) && f.depthM > 0.01f && f.speciesIndex >= 0)
        {
            JsonValue t = JsonValue::makeArray();
            t.push(f.position.x);
            t.push(f.position.y);
            t.push(f.lengthM);
            t.push(f.heading);
            t.push(f.depthM);
            tracks.push(t);
        }
    });
    root.set("tracks", tracks);
    JsonValue carcasses = JsonValue::makeArray();
    for (const Carcass& c : w.carcasses().all())
    {
        if (c.active && win.contains(c.position))
        {
            JsonValue t = JsonValue::makeArray();
            t.push(c.position.x);
            t.push(c.position.y);
            t.push(c.heading);
            t.push(c.massAtDeathKg);
            t.push(c.softTissueKg / std::max(1.0f, c.massAtDeathKg));
            carcasses.push(t);
        }
    }
    root.set("carcasses", carcasses);
    const VehicleState& vs = w.research().vehicle().state();
    JsonValue veh = JsonValue::makeArray();
    veh.push(vs.position.x);
    veh.push(vs.position.y);
    veh.push(vs.position.z);
    veh.push(vs.heading);
    root.set("vehicle", veh);
    writeText(a.out + "/preview.json", root.dump(false));
    {
        std::ofstream f(a.out + "/preview.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(blob.bytes.data()), static_cast<std::streamsize>(blob.bytes.size()));
    }
    std::printf("Prévisualisation exportée : %d animaux dans la fenêtre (%.0f m), rivière à %.0f m du troupeau, %s\n", static_cast<int>(creatures.size()),
                win.size().x, riverDist, w.clock().formatted().c_str());
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    const Args a = parseArgs(argc, argv);
    if (a.mode == "validate")
    {
        return cmdValidate(a);
    }
    if (a.mode == "run")
    {
        return cmdRun(a);
    }
    if (a.mode == "demo")
    {
        return cmdDemo(a);
    }
    if (a.mode == "preview")
    {
        return cmdPreview(a);
    }
    if (a.mode == "export-terrain")
    {
        return cmdExportTerrain(a);
    }
    std::printf("Usage: noctis_sim validate|run|demo|export-terrain [--data DIR] [--env ID] [--profile ID] [--seed N] [--days N] [--hours N] [--out DIR]\n");
    return 1;
}
