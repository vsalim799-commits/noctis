// noctis_bench — performance benchmark of the Noctis simulation core. Writes a JSON report.
//
//   noctis_bench [--out FILE] [--quick] [--threads N] [--seed N] [--scale F] [--env ID] [--data DIR] [--small]
//
// Measures, on the game's default world (2017² height samples, 1024² material cells, 16 m vegetation
// cells, full initial populations) unless --small:
//   * initialisation time, total and per stage (data, terrain, hydrology, vegetation, population);
//   * step cost per creature for each level of detail (Full / Near / Far forced on every animal),
//     net of the creature-free baseline step;
//   * throughput in simulated seconds per wall-clock second at 1 thread and N threads, for three
//     regimes: real time at 10 Hz, the CLI "run" regime (1 s steps, full detail near the observer),
//     and accelerated time (x100: no full detail, 10 s steps);
//   * resident memory after initialisation and peak.
// Timings are wall-clock (steady_clock) on whatever machine runs the benchmark: compare runs from
// the same runner only.
#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Json.h"
#include "Noctis/Core/Log.h"
#include "Noctis/Science/EvidenceDatabase.h"
#include "Noctis/Sim/WorldSimulation.h"
#include "Noctis/World/TerrainGenerator.h"
#include "Noctis/World/Vegetation.h"
#include "Noctis/World/Water.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#ifndef NOCTIS_SOURCE_DIR
#define NOCTIS_SOURCE_DIR "."
#endif

using namespace noctis;

namespace
{
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

// ------------------------------------------------------------------ memory

double currentRssMb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
    return -1.0;
#elif defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
    {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
    return -1.0;
#else
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f)
    {
        return -1.0;
    }
    long pagesTotal = 0;
    long pagesResident = 0;
    const int n = std::fscanf(f, "%ld %ld", &pagesTotal, &pagesResident);
    std::fclose(f);
    if (n != 2)
    {
        return -1.0;
    }
    return static_cast<double>(pagesResident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
}

double peakRssMb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
    }
    return -1.0;
#else
    rusage ru;
    std::memset(&ru, 0, sizeof(ru));
    if (getrusage(RUSAGE_SELF, &ru) != 0)
    {
        return -1.0;
    }
#if defined(__APPLE__)
    return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0); // bytes on macOS
#else
    return static_cast<double>(ru.ru_maxrss) / 1024.0; // kilobytes on Linux
#endif
#endif
}

// ------------------------------------------------------------------ options

struct Options
{
    std::string data = std::string(NOCTIS_SOURCE_DIR) + "/Data";
    std::string env = "hell_creek";
    std::string out;
    u64 seed = 7;
    int threads = 0;
    float scale = 1.0f;
    bool quick = false;
    bool smallWorld = false;
};

bool parseOptions(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--quick")
        {
            o.quick = true;
        }
        else if (a == "--small")
        {
            o.smallWorld = true;
        }
        else if ((a == "--out" || a == "--data" || a == "--env" || a == "--seed" || a == "--threads" || a == "--scale") && (v = value()) != nullptr)
        {
            if (a == "--out")
            {
                o.out = v;
            }
            else if (a == "--data")
            {
                o.data = v;
            }
            else if (a == "--env")
            {
                o.env = v;
            }
            else if (a == "--seed")
            {
                o.seed = std::strtoull(v, nullptr, 10);
            }
            else if (a == "--threads")
            {
                o.threads = std::atoi(v);
            }
            else
            {
                o.scale = static_cast<float>(std::atof(v));
            }
        }
        else
        {
            std::printf("usage: noctis_bench [--out FILE] [--quick] [--small] [--threads N] [--seed N] [--scale F] [--env ID] [--data DIR]\n");
            return false;
        }
    }
    if (o.threads <= 0)
    {
        o.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    }
    return true;
}

SimConfig makeConfig(const Options& o, int threads, bool population)
{
    SimConfig c;
    c.dataRoot = o.data;
    c.environmentId = o.env;
    c.seed = o.seed;
    c.threads = threads;
    c.populationScale = o.scale;
    c.spawnPopulation = population;
    if (o.smallWorld)
    {
        c.heightSamples = 513;
        c.materialCells = 256;
        c.vegetationCellM = 32.0f;
    }
    return c;
}

// ------------------------------------------------------------------ measurements

struct StepStats
{
    int steps = 0;
    double meanUs = 0.0;
    double p50Us = 0.0;
    double p95Us = 0.0;
    double maxUs = 0.0;
    double creaturesMean = 0.0;
};

StepStats measureSteps(WorldSimulation& w, double dt, double simSeconds)
{
    StepStats s;
    std::vector<double> us;
    double creatures = 0.0;
    double done = 0.0;
    while (done < simSeconds - 1e-9)
    {
        const auto t0 = Clock::now();
        w.step(dt);
        us.push_back(msSince(t0) * 1000.0);
        creatures += static_cast<double>(w.creatures().aliveCount());
        done += dt;
    }
    if (us.empty())
    {
        return s;
    }
    s.steps = static_cast<int>(us.size());
    double sum = 0.0;
    for (const double u : us)
    {
        sum += u;
    }
    s.meanUs = sum / static_cast<double>(us.size());
    s.creaturesMean = creatures / static_cast<double>(us.size());
    std::sort(us.begin(), us.end());
    s.p50Us = us[us.size() / 2];
    s.p95Us = us[std::min(us.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(us.size())))];
    s.maxUs = us.back();
    return s;
}

JsonValue stepJson(const StepStats& s)
{
    JsonValue j = JsonValue::makeObject();
    j.set("steps", s.steps);
    j.set("step_us_mean", s.meanUs);
    j.set("step_us_p50", s.p50Us);
    j.set("step_us_p95", s.p95Us);
    j.set("step_us_max", s.maxUs);
    j.set("creatures_mean", s.creaturesMean);
    return j;
}

void forceLod(WorldSimulation& w, LodLevel l)
{
    w.setTimeScale(1.0f);
    LodSettings& lod = w.creatures().lod();
    lod.allowFull = l == LodLevel::Full;
    lod.fullRadiusM = l == LodLevel::Full ? 1.0e7f : 350.0f;
    lod.nearRadiusM = l == LodLevel::Near ? 1.0e7f : (l == LodLevel::Far ? 0.0f : 1500.0f);
}

int countLod(const WorldSimulation& w, LodLevel l)
{
    int n = 0;
    for (const Creature& c : w.creatures().creatures())
    {
        n += (c.alive && c.lod == l) ? 1 : 0;
    }
    return n;
}

struct Throughput
{
    std::string scenario;
    int threads = 1;
    double dt = 0.0;
    float timeScale = 1.0f;
    double simSeconds = 0.0;
    double wallSeconds = 0.0;
    double creaturesMean = 0.0;
};

Throughput measureThroughput(WorldSimulation& w, const char* scenario, int threads, double dt, float timeScale, double simSeconds)
{
    Throughput t;
    t.scenario = scenario;
    t.threads = threads;
    t.dt = dt;
    t.timeScale = timeScale;
    t.simSeconds = simSeconds;
    w.setTimeScale(timeScale);
    LodSettings& lod = w.creatures().lod();
    lod.fullRadiusM = 350.0f;
    lod.nearRadiusM = 1500.0f;
    w.step(dt); // apply the LOD regime
    double creatures = 0.0;
    int samples = 0;
    const auto t0 = Clock::now();
    double done = 0.0;
    while (done < simSeconds - 1e-9)
    {
        const double chunk = std::min(simSeconds - done, std::max(dt, 60.0));
        w.advance(chunk, dt);
        done += chunk;
        creatures += static_cast<double>(w.creatures().aliveCount());
        ++samples;
    }
    t.wallSeconds = msSince(t0) / 1000.0;
    t.creaturesMean = samples > 0 ? creatures / samples : 0.0;
    return t;
}

JsonValue throughputJson(const Throughput& t)
{
    JsonValue j = JsonValue::makeObject();
    j.set("scenario", t.scenario);
    j.set("threads", t.threads);
    j.set("dt_s", t.dt);
    j.set("time_scale", t.timeScale);
    j.set("sim_seconds", t.simSeconds);
    j.set("wall_seconds", t.wallSeconds);
    j.set("sim_s_per_wall_s", t.wallSeconds > 0.0 ? t.simSeconds / t.wallSeconds : 0.0);
    j.set("creatures_mean", t.creaturesMean);
    return j;
}

const char* compilerName()
{
#if defined(__clang__) && defined(__apple_build_version__)
    return "AppleClang " __clang_version__;
#elif defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(_MSC_VER)
#define NOCTIS_BENCH_STR2(x) #x
#define NOCTIS_BENCH_STR(x) NOCTIS_BENCH_STR2(x)
    return "MSVC " NOCTIS_BENCH_STR(_MSC_FULL_VER);
#else
    return "unknown";
#endif
}

const char* osName()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}
} // namespace

int main(int argc, char** argv)
{
    Options opt;
    if (!parseOptions(argc, argv, opt))
    {
        return 2;
    }
    setLogLevel(LogLevel::Warning);
    const double rssStart = currentRssMb();
    JsonValue report = JsonValue::makeObject();
    report.set("schema", "noctis-bench/1");
    {
        JsonValue b = JsonValue::makeObject();
        b.set("compiler", compilerName());
        b.set("os", osName());
        b.set("hardware_threads", static_cast<int>(std::thread::hardware_concurrency()));
#if defined(NDEBUG)
        b.set("ndebug", true);
#else
        b.set("ndebug", false);
#endif
        report.set("build", b);
    }
    {
        JsonValue c = JsonValue::makeObject();
        c.set("environment", opt.env);
        c.set("seed", static_cast<double>(opt.seed));
        c.set("population_scale", opt.scale);
        c.set("threads_n", opt.threads);
        c.set("quick", opt.quick);
        const SimConfig sc = makeConfig(opt, 1, true);
        c.set("height_samples", sc.heightSamples);
        c.set("material_cells", sc.materialCells);
        c.set("vegetation_cell_m", sc.vegetationCellM);
        report.set("config", c);
    }
    const double lodSimS = opt.quick ? 8.0 : 30.0;
    const double realtimeSimS = opt.quick ? 20.0 : 60.0;
    const double cliSimS = opt.quick ? 600.0 : 3600.0;
    const double accelSimS = (opt.quick ? 2.0 : 6.0) * 3600.0;

    // --- Initialisation stages measured in isolation (same code paths as WorldSimulation::initialize).
    JsonValue init = JsonValue::makeObject();
    {
        std::unique_ptr<FileSystem> fs = makeStdFileSystem();
        EvidenceDatabase db;
        auto t0 = Clock::now();
        if (!db.load(*fs, opt.data))
        {
            std::printf("cannot load data from %s\n", opt.data.c_str());
            return 1;
        }
        init.set("data_load_ms", msSince(t0));
        const EnvironmentDefinition* env = db.findEnvironment(opt.env);
        if (!env)
        {
            std::printf("unknown environment %s\n", opt.env.c_str());
            return 1;
        }
        const SimConfig sc = makeConfig(opt, 1, true);
        Terrain terrain;
        t0 = Clock::now();
        const WorldLayout layout = TerrainGenerator::generate(env->sim, hashCombine(opt.seed, 0x7E), terrain, sc.heightSamples, sc.materialCells);
        init.set("terrain_ms", msSince(t0));
        WaterSystem water;
        t0 = Clock::now();
        water.build(layout, terrain);
        init.set("water_ms", msSince(t0));
        VegetationSystem veg;
        t0 = Clock::now();
        veg.build(env->sim, terrain, hashCombine(opt.seed, 0x9E), sc.vegetationCellM);
        init.set("vegetation_ms", msSince(t0));
    }
    double initNoPopMs = 0.0;
    StepStats baseline;
    {
        std::unique_ptr<WorldSimulation> w = std::make_unique<WorldSimulation>();
        const auto t0 = Clock::now();
        if (!w->initialize(makeConfig(opt, 1, false)))
        {
            return 1;
        }
        initNoPopMs = msSince(t0);
        w->setTimeScale(1.0f);
        w->advance(5.0, 0.1);
        baseline = measureSteps(*w, 0.1, lodSimS);
    }
    init.set("world_without_population_ms", initNoPopMs);

    // --- Full world, 1 thread: init, memory, per-LOD step cost, throughput.
    JsonValue memory = JsonValue::makeObject();
    JsonValue lodArr = JsonValue::makeArray();
    JsonValue thrArr = JsonValue::makeArray();
    JsonValue timings = JsonValue::makeObject();
    {
        std::unique_ptr<WorldSimulation> w = std::make_unique<WorldSimulation>();
        const double rssBefore = currentRssMb();
        const auto t0 = Clock::now();
        if (!w->initialize(makeConfig(opt, 1, true)))
        {
            return 1;
        }
        const double initMs = msSince(t0);
        init.set("total_ms", initMs);
        init.set("population_spawn_ms_estimate", initMs - initNoPopMs);
        init.set("creatures", static_cast<double>(w->creatures().aliveCount()));
        init.set("groups", static_cast<double>(w->creatures().groups().size()));
        const double rssAfter = currentRssMb();
        memory.set("rss_before_world_mb", rssBefore);
        memory.set("rss_after_init_mb", rssAfter);
        memory.set("world_rss_delta_mb", rssAfter - rssBefore);
        std::printf("init: %.0f ms (%zu creatures), RSS +%.1f MB\n", initMs, w->creatures().aliveCount(), rssAfter - rssBefore);

        // Snapshot cost (what the renderer pays per frame).
        {
            RenderSnapshot snap;
            w->buildSnapshot(snap);
            const auto s0 = Clock::now();
            for (int i = 0; i < 20; ++i)
            {
                w->buildSnapshot(snap);
            }
            memory.set("snapshot_creatures", static_cast<double>(snap.creatures.size()));
            report.set("snapshot_us", msSince(s0) * 1000.0 / 20.0);
        }

        const LodLevel levels[] = {LodLevel::Full, LodLevel::Near, LodLevel::Far};
        for (const LodLevel l : levels)
        {
            forceLod(*w, l);
            w->advance(2.0, 0.1); // reassign LOD and let promotions settle
            const int inLod = countLod(*w, l);
            const StepStats s = measureSteps(*w, 0.1, lodSimS);
            const double net = std::max(0.0, s.meanUs - baseline.meanUs);
            const double perCreature = s.creaturesMean > 0.0 ? net / s.creaturesMean : 0.0;
            JsonValue j = stepJson(s);
            j.set("lod", lodLabel(l));
            j.set("creatures_in_lod", inLod);
            j.set("dt_s", 0.1);
            j.set("baseline_step_us", baseline.meanUs);
            j.set("per_creature_step_us", perCreature);
            j.set("per_creature_per_sim_second_us", perCreature / 0.1);
            lodArr.push(j);
            std::printf("LOD %-4s: %d/%0.f creatures, step %.0f us (p95 %.0f), net %.2f us/creature/step (baseline %.0f us)\n", lodLabel(l), inLod,
                        s.creaturesMean, s.meanUs, s.p95Us, perCreature, baseline.meanUs);
        }
        const Throughput a = measureThroughput(*w, "realtime_10hz", 1, 0.1, 1.0f, realtimeSimS);
        const Throughput b = measureThroughput(*w, "cli_run_1s", 1, 1.0, 1.0f, cliSimS);
        const Throughput c = measureThroughput(*w, "accelerated_x100_10s", 1, 10.0, 100.0f, accelSimS);
        for (const Throughput* t : {&a, &b, &c})
        {
            thrArr.push(throughputJson(*t));
            std::printf("%-22s 1 thread : x%.0f real time (%.0f s simulated in %.2f s)\n", t->scenario.c_str(), t->simSeconds / std::max(1e-9, t->wallSeconds),
                        t->simSeconds, t->wallSeconds);
        }
        const WorldSimulation::Timings& tm = w->timings();
        timings.set("environment_ms", tm.environment);
        timings.set("creatures_ms", tm.creatures);
        timings.set("research_ms", tm.research);
        timings.set("hourly_ms", tm.hourly);
        timings.set("daily_ms", tm.daily);
    }

    // --- Full world, N threads.
    if (opt.threads > 1)
    {
        std::unique_ptr<WorldSimulation> w = std::make_unique<WorldSimulation>();
        const auto t0 = Clock::now();
        if (!w->initialize(makeConfig(opt, opt.threads, true)))
        {
            return 1;
        }
        init.set("total_ms_n_threads", msSince(t0));
        // Same LOD experiment for Full only (the parallel phase that matters most).
        forceLod(*w, LodLevel::Full);
        w->advance(2.0, 0.1);
        const StepStats s = measureSteps(*w, 0.1, lodSimS);
        JsonValue j = stepJson(s);
        j.set("lod", "full");
        j.set("threads", opt.threads);
        j.set("dt_s", 0.1);
        report.set("full_lod_n_threads", j);
        std::printf("LOD full, %d threads: step %.0f us (p95 %.0f)\n", opt.threads, s.meanUs, s.p95Us);
        const Throughput a = measureThroughput(*w, "realtime_10hz", opt.threads, 0.1, 1.0f, realtimeSimS);
        const Throughput b = measureThroughput(*w, "cli_run_1s", opt.threads, 1.0, 1.0f, cliSimS);
        const Throughput c = measureThroughput(*w, "accelerated_x100_10s", opt.threads, 10.0, 100.0f, accelSimS);
        for (const Throughput* t : {&a, &b, &c})
        {
            thrArr.push(throughputJson(*t));
            std::printf("%-22s %d threads: x%.0f real time (%.0f s simulated in %.2f s)\n", t->scenario.c_str(), opt.threads,
                        t->simSeconds / std::max(1e-9, t->wallSeconds), t->simSeconds, t->wallSeconds);
        }
    }
    memory.set("rss_start_mb", rssStart);
    memory.set("rss_end_mb", currentRssMb());
    memory.set("peak_rss_mb", peakRssMb());
    report.set("init", init);
    report.set("lod", lodArr);
    report.set("throughput", thrArr);
    report.set("smoothed_timings_ms", timings);
    report.set("memory", memory);
    std::printf("peak RSS %.1f MB\n", peakRssMb());
    const std::string text = report.dump(true);
    if (!opt.out.empty())
    {
        std::unique_ptr<FileSystem> fs = makeStdFileSystem();
        if (!fs->writeText(opt.out, text))
        {
            std::printf("cannot write %s\n", opt.out.c_str());
            return 1;
        }
        std::printf("report written to %s\n", opt.out.c_str());
    }
    else
    {
        std::printf("%s\n", text.c_str());
    }
    return 0;
}
