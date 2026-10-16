// noctis_bench — performance benchmark of the Noctis simulation core. Writes a JSON report.
//
//   noctis_bench [--out FILE] [--quick] [--threads N] [--seed N] [--scale F] [--env ID] [--data DIR] [--small]
//   noctis_bench --run-days D [--regime accelerated|cli|realtime] [--threads N] ...   profiling driver: only
//                initialises the world and advances it D simulated days (use with callgrind/gprof, e.g.
//                valgrind --tool=callgrind --toggle-collect='noctis::WorldSimulation::advance(double, double)')
//
// Measures, on the game's default world (2017² height samples, 1024² material cells, 16 m vegetation
// cells, full initial populations) unless --small:
//   * initialisation time, total and per stage (data, terrain, hydrology, vegetation, population);
//   * step cost per creature for each level of detail (Full / Near / Far forced on every animal),
//     net of the creature-free baseline step;
//   * throughput in simulated seconds per wall-clock second at 1 thread and N threads, for three
//     regimes: real time at 10 Hz, the CLI "run" regime (1 s steps, full detail near the observer),
//     and accelerated time (x100: no full detail, 10 s steps);
//   * resident memory after initialisation and peak;
//   * fixed cost of one parallelFor on the standalone thread pool (dispatch + wake-up + join) at 1 and
//     N threads, the floor under every multi-threaded step.
// The 1-thread and N-thread worlds run the identical step sequence (the simulation is deterministic
// across thread counts), so both series measure the same workload; "consistency" in the report holds
// the event count and census at the end of each run as a check. Every timed section also records
// process CPU time / wall time ("cpu_per_wall": average busy cores) and throughput scenarios record
// their mean LOD mix, so a serial section can be told apart from a starved (shared) machine.
// Timings are wall-clock (steady_clock) on whatever machine runs the benchmark: compare runs from
// the same runner only.
#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Jobs.h"
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
#include <functional>
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
#if defined(__GLIBC__)
#include <malloc.h> // malloc_trim
#endif
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

// User + system CPU time of the whole process (all threads), in seconds. CPU / wall = average number
// of busy cores: it tells a serial section from a starved machine when wall-clock numbers are noisy.
double processCpuSeconds()
{
#if defined(_WIN32)
    FILETIME creation, exitTime, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user))
    {
        return 0.0;
    }
    auto toS = [](const FILETIME& ft) {
        const unsigned long long t = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        return static_cast<double>(t) * 1e-7; // 100 ns units
    };
    return toS(kernel) + toS(user);
#else
    rusage ru;
    std::memset(&ru, 0, sizeof(ru));
    if (getrusage(RUSAGE_SELF, &ru) != 0)
    {
        return 0.0;
    }
    return static_cast<double>(ru.ru_utime.tv_sec) + static_cast<double>(ru.ru_stime.tv_sec) +
           1e-6 * (static_cast<double>(ru.ru_utime.tv_usec) + static_cast<double>(ru.ru_stime.tv_usec));
#endif
}

// glibc keeps freed heap pages mapped, so the RSS delta of a world built after other worlds were
// destroyed would read ~0. Hand the free pages back before measuring (other allocators return large
// blocks to the system on free).
void releaseFreedMemory()
{
#if defined(__GLIBC__)
    malloc_trim(0);
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
    double runDays = 0.0;
    std::string regime = "accelerated";
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
        else if ((a == "--out" || a == "--data" || a == "--env" || a == "--seed" || a == "--threads" || a == "--scale" || a == "--run-days" ||
                  a == "--regime") &&
                 (v = value()) != nullptr)
        {
            if (a == "--run-days")
            {
                o.runDays = std::atof(v);
            }
            else if (a == "--regime")
            {
                o.regime = v;
            }
            else if (a == "--out")
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
            std::printf("usage: noctis_bench [--out FILE] [--quick] [--small] [--threads N] [--seed N] [--scale F] [--env ID] [--data DIR]\n"
                        "       noctis_bench --run-days D [--regime accelerated|cli|realtime] [--threads N] [--small] ...\n");
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
    double cpuPerWall = 0.0; // process CPU time / wall time while stepping (1.0 = one core busy)
};

StepStats measureSteps(WorldSimulation& w, double dt, double simSeconds)
{
    StepStats s;
    std::vector<double> us;
    double creatures = 0.0;
    double done = 0.0;
    const double cpu0 = processCpuSeconds();
    const auto wall0 = Clock::now();
    while (done < simSeconds - 1e-9)
    {
        const auto t0 = Clock::now();
        w.step(dt);
        us.push_back(msSince(t0) * 1000.0);
        creatures += static_cast<double>(w.creatures().aliveCount());
        done += dt;
    }
    const double wallS = msSince(wall0) / 1000.0;
    s.cpuPerWall = wallS > 0.0 ? (processCpuSeconds() - cpu0) / wallS : 0.0;
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
    j.set("cpu_per_wall", s.cpuPerWall);
    return j;
}

// Stable ASCII key for the JSON report (lodLabel() is the localised UI label).
const char* lodKey(LodLevel l)
{
    switch (l)
    {
    case LodLevel::Full: return "full";
    case LodLevel::Near: return "near";
    case LodLevel::Far: return "far";
    case LodLevel::Regional: return "regional";
    }
    return "unknown";
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
    double cpuSeconds = 0.0; // process CPU time (all threads) over the same interval
    double creaturesMean = 0.0;
    double lodFullMean = 0.0; // mean number of creatures in each level of detail (sampled per chunk)
    double lodNearMean = 0.0;
    double lodFarMean = 0.0;
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
    double lodCount[3] = {0.0, 0.0, 0.0};
    int samples = 0;
    const double cpu0 = processCpuSeconds();
    const auto t0 = Clock::now();
    double done = 0.0;
    while (done < simSeconds - 1e-9)
    {
        const double chunk = std::min(simSeconds - done, std::max(dt, 60.0));
        w.advance(chunk, dt);
        done += chunk;
        creatures += static_cast<double>(w.creatures().aliveCount());
        lodCount[0] += countLod(w, LodLevel::Full);
        lodCount[1] += countLod(w, LodLevel::Near);
        lodCount[2] += countLod(w, LodLevel::Far);
        ++samples;
    }
    t.wallSeconds = msSince(t0) / 1000.0;
    t.cpuSeconds = processCpuSeconds() - cpu0;
    const double inv = samples > 0 ? 1.0 / samples : 0.0;
    t.creaturesMean = creatures * inv;
    t.lodFullMean = lodCount[0] * inv;
    t.lodNearMean = lodCount[1] * inv;
    t.lodFarMean = lodCount[2] * inv;
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
    j.set("cpu_seconds", t.cpuSeconds);
    j.set("cpu_per_wall", t.wallSeconds > 0.0 ? t.cpuSeconds / t.wallSeconds : 0.0);
    j.set("creatures_mean", t.creaturesMean);
    JsonValue mix = JsonValue::makeObject();
    mix.set("full", t.lodFullMean);
    mix.set("near", t.lodNearMean);
    mix.set("far", t.lodFarMean);
    j.set("lod_mix_mean", mix);
    return j;
}

// Fixed cost of one JobScheduler::parallelFor on the standalone thread pool: dispatch + wake-up + join,
// measured on a trivial body (one store per index) over as many indices as creatures, with the batch
// size CreatureSystem::step uses. CreatureSystem::step issues 3 such calls per simulation step, so at
// N threads this is a floor under the step cost whatever the level of detail.
JsonValue measureSchedulerOverhead(int threads, size_t count, int calls)
{
    std::unique_ptr<JobScheduler> jobs = makeThreadPoolScheduler(threads);
    std::vector<u32> sink(count, 0u);
    const std::function<void(size_t)> body = [&](size_t i) { sink[i] += static_cast<u32>(i) + 1u; };
    for (int k = 0; k < 50; ++k) // warm-up: threads started and parked
    {
        jobs->parallelFor(count, body, 8);
    }
    std::vector<double> us;
    us.reserve(static_cast<size_t>(calls));
    const double cpu0 = processCpuSeconds();
    const auto wall0 = Clock::now();
    for (int k = 0; k < calls; ++k)
    {
        const auto t0 = Clock::now();
        jobs->parallelFor(count, body, 8);
        us.push_back(msSince(t0) * 1000.0);
    }
    const double wallS = msSince(wall0) / 1000.0;
    const double cpuPerWall = wallS > 0.0 ? (processCpuSeconds() - cpu0) / wallS : 0.0;
    double sum = 0.0;
    for (const double u : us)
    {
        sum += u;
    }
    std::sort(us.begin(), us.end());
    u64 check = 0;
    for (const u32 v : sink)
    {
        check += v;
    }
    JsonValue j = JsonValue::makeObject();
    j.set("threads", jobs->workerCount());
    j.set("count", static_cast<double>(count));
    j.set("min_batch", 8);
    j.set("calls", calls);
    j.set("us_mean", sum / static_cast<double>(us.size()));
    j.set("us_p50", us[us.size() / 2]);
    j.set("us_p95", us[std::min(us.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(us.size())))]);
    j.set("cpu_per_wall", cpuPerWall);
    j.set("checksum_ok", check == static_cast<u64>(calls + 50) * (static_cast<u64>(count) * (count + 1) / 2));
    std::printf("parallelFor(%zu, batch 8), %d thread(s): %.1f us mean, p50 %.1f, p95 %.1f (CPU/wall %.2f)\n", count, jobs->workerCount(),
                sum / static_cast<double>(us.size()), us[us.size() / 2], us[std::min(us.size() - 1, static_cast<size_t>(0.95 * static_cast<double>(us.size())))],
                cpuPerWall);
    return j;
}

// Profiling driver: initialise, then advance D simulated days in one regime. Nothing else runs, so a
// profiler attached to the whole process (or toggled on WorldSimulation::advance) sees the simulation only.
int runDays(const Options& opt)
{
    double dt = 10.0;
    float timeScale = 100.0f;
    if (opt.regime == "cli")
    {
        dt = 1.0;
        timeScale = 1.0f;
    }
    else if (opt.regime == "realtime")
    {
        dt = 0.1;
        timeScale = 1.0f;
    }
    else if (opt.regime != "accelerated")
    {
        std::printf("unknown regime '%s' (accelerated|cli|realtime)\n", opt.regime.c_str());
        return 2;
    }
    WorldSimulation w;
    auto t0 = Clock::now();
    if (!w.initialize(makeConfig(opt, opt.threads, true)))
    {
        return 1;
    }
    const double initMs = msSince(t0);
    const size_t alive0 = w.creatures().aliveCount();
    w.setTimeScale(timeScale);
    t0 = Clock::now();
    for (int h = 0; h < static_cast<int>(opt.runDays * 24.0 + 0.5); ++h)
    {
        w.advance(3600.0, dt);
    }
    const double secs = msSince(t0) / 1000.0;
    std::printf("regime %s (dt %.1f s, x%.0f), %d thread(s): init %.0f ms; %.2f simulated days in %.1f s (x%.0f real time); creatures %zu -> %zu; "
                "peak RSS %.1f MB\n",
                opt.regime.c_str(), dt, static_cast<double>(timeScale), opt.threads, initMs, opt.runDays, secs, opt.runDays * 86400.0 / std::max(1e-9, secs),
                alive0, w.creatures().aliveCount(), peakRssMb());
    return 0;
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
    if (opt.runDays > 0.0)
    {
        return runDays(opt);
    }
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
    {
        // Before any world exists, so the measurement sees an idle process.
        JsonValue sched = JsonValue::makeArray();
        const int calls = opt.quick ? 2000 : 10000;
        sched.push(measureSchedulerOverhead(1, 254, calls));
        if (opt.threads > 1)
        {
            sched.push(measureSchedulerOverhead(opt.threads, 254, calls));
        }
        report.set("scheduler_parallel_for", sched);
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

    // --- Full world at 1 thread, then at N threads. Both worlds go through exactly the same sequence of
    // steps and the simulation is deterministic across thread counts, so the two series measure the same
    // workload; "consistency" records the event count and census at the end of each run to prove it.
    JsonValue memory = JsonValue::makeObject();
    JsonValue lodArr = JsonValue::makeArray();
    JsonValue thrArr = JsonValue::makeArray();
    JsonValue timings = JsonValue::makeObject();
    JsonValue consistency = JsonValue::makeArray();
    std::vector<int> threadCounts{1};
    if (opt.threads > 1)
    {
        threadCounts.push_back(opt.threads);
    }
    for (const int threads : threadCounts)
    {
        releaseFreedMemory();
        std::unique_ptr<WorldSimulation> w = std::make_unique<WorldSimulation>();
        const double rssBefore = currentRssMb();
        const auto t0 = Clock::now();
        if (!w->initialize(makeConfig(opt, threads, true)))
        {
            return 1;
        }
        const double initMs = msSince(t0);
        if (threads == 1)
        {
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
        else
        {
            init.set("total_ms_n_threads", initMs);
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
            j.set("lod", lodKey(l));
            j.set("threads", threads);
            j.set("creatures_in_lod", inLod);
            j.set("dt_s", 0.1);
            j.set("baseline_step_us", baseline.meanUs);
            j.set("per_creature_step_us", perCreature);
            j.set("per_creature_per_sim_second_us", perCreature / 0.1);
            lodArr.push(j);
            std::printf("LOD %-4s %d thread(s): %d/%0.f creatures, step %.0f us (p95 %.0f, CPU/wall %.2f), net %.2f us/creature/step (baseline %.0f us)\n",
                        lodKey(l), threads, inLod, s.creaturesMean, s.meanUs, s.p95Us, s.cpuPerWall, perCreature, baseline.meanUs);
        }
        const Throughput a = measureThroughput(*w, "realtime_10hz", threads, 0.1, 1.0f, realtimeSimS);
        const Throughput b = measureThroughput(*w, "cli_run_1s", threads, 1.0, 1.0f, cliSimS);
        const Throughput c = measureThroughput(*w, "accelerated_x100_10s", threads, 10.0, 100.0f, accelSimS);
        for (const Throughput* t : {&a, &b, &c})
        {
            thrArr.push(throughputJson(*t));
            std::printf("%-22s %d thread(s): x%.0f real time (%.0f s simulated in %.2f s, CPU/wall %.2f; LOD mix full %.1f / near %.1f / far %.1f)\n",
                        t->scenario.c_str(), threads, t->simSeconds / std::max(1e-9, t->wallSeconds), t->simSeconds, t->wallSeconds,
                        t->wallSeconds > 0.0 ? t->cpuSeconds / t->wallSeconds : 0.0, t->lodFullMean, t->lodNearMean, t->lodFarMean);
        }
        if (threads == 1)
        {
            const WorldSimulation::Timings& tm = w->timings();
            timings.set("environment_ms", tm.environment);
            timings.set("creatures_ms", tm.creatures);
            timings.set("research_ms", tm.research);
            timings.set("hourly_ms", tm.hourly);
            timings.set("daily_ms", tm.daily);
        }
        JsonValue cj = JsonValue::makeObject();
        cj.set("threads", threads);
        cj.set("sim_time_s", w->clock().seconds());
        cj.set("events_emitted", static_cast<double>(w->events().totalEmitted()));
        cj.set("alive", static_cast<double>(w->creatures().aliveCount()));
        consistency.push(cj);
        std::printf("end of %d-thread run: %.0f events emitted, %zu alive\n", threads, static_cast<double>(w->events().totalEmitted()),
                    w->creatures().aliveCount());
    }
    report.set("consistency", consistency);
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
