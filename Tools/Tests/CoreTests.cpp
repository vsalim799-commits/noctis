// Core library tests: math, deterministic random numbers, JSON, grids, spatial index, calendar, jobs.
#include "TestFramework.h"

#include "Noctis/Core/Events.h"
#include "Noctis/Core/Grid2D.h"
#include "Noctis/Core/Jobs.h"
#include "Noctis/Core/Json.h"
#include "Noctis/Core/Math.h"
#include "Noctis/Core/Random.h"
#include "Noctis/Core/SpatialHash.h"
#include "Noctis/Core/Time.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

using namespace noctis;

// ---------------------------------------------------------------- Math

NOCTIS_TEST("core.math.wrap_angle")
{
    const float values[] = {0.0f, 1.0f, -1.0f, 3.0f, -3.0f, 4.0f, -4.0f, 7.0f, 20.0f, -20.0f, 100.0f};
    for (const float v : values)
    {
        const float w = wrapAngle(v);
        CHECK(w > -kPi - 1e-5f && w <= kPi + 1e-5f);
        // Same direction: cos/sin unchanged.
        CHECK_NEAR(std::cos(w), std::cos(v), 1e-4);
        CHECK_NEAR(std::sin(w), std::sin(v), 1e-4);
    }
    CHECK_NEAR(angleDelta(0.1f, kTwoPi - 0.1f), -0.2, 1e-5);
    CHECK_NEAR(angleDelta(kPi - 0.1f, -kPi + 0.1f), 0.2, 1e-5);
}

NOCTIS_TEST("core.math.vectors")
{
    const Vec2 a{3.0f, 4.0f};
    CHECK_NEAR(a.length(), 5.0, 1e-6);
    CHECK_NEAR(a.normalized().length(), 1.0, 1e-6);
    CHECK_NEAR(dot(a, a.perp()), 0.0, 1e-6);
    CHECK_NEAR(cross(Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}), 1.0, 1e-6);
    const Vec2 r = rotate(Vec2{1.0f, 0.0f}, kHalfPi);
    CHECK_NEAR(r.x, 0.0, 1e-6);
    CHECK_NEAR(r.y, 1.0, 1e-6);
    CHECK_NEAR(Vec2::fromHeading(0.7f).heading(), 0.7, 1e-6);
    CHECK_NEAR(Vec2{}.normalized().length(), 0.0, 0.0); // zero vector stays zero (no NaN)
    const Vec3 c = cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    CHECK_NEAR(c.z, 1.0, 1e-6);
    const Rect2 box{{0.0f, 0.0f}, {10.0f, 5.0f}};
    CHECK(box.contains({10.0f, 5.0f}));
    CHECK(!box.contains({10.1f, 2.0f}));
    CHECK_NEAR(box.clampPoint({-3.0f, 9.0f}).y, 5.0, 0.0);
}

NOCTIS_TEST("core.math.interpolation")
{
    CHECK_NEAR(smoothstep(0.0f, 1.0f, 0.5f), 0.5, 1e-7);
    CHECK_NEAR(smoothstep(0.0f, 1.0f, -1.0f), 0.0, 0.0);
    CHECK_NEAR(smoothstep(0.0f, 1.0f, 2.0f), 1.0, 0.0);
    CHECK_NEAR(remapClamped(5.0f, 0.0f, 10.0f, 100.0f, 200.0f), 150.0, 1e-4);
    CHECK_NEAR(remapClamped(50.0f, 0.0f, 10.0f, 100.0f, 200.0f), 200.0, 1e-4);
    CHECK_NEAR(inverseLerp(1.0f, 1.0f, 3.0f), 0.0, 0.0); // degenerate interval: no division by zero
    // approachFactor composes like an exponential: two half steps == one full step.
    const float full = approachFactor(1.0f, 0.5f);
    const float half = approachFactor(0.5f, 0.5f);
    CHECK_NEAR(1.0f - (1.0f - half) * (1.0f - half), full, 1e-6);
}

NOCTIS_TEST("core.math.decibels")
{
    // Two incoherent sources of equal level add 10 log10(2) = 3.0103 dB.
    CHECK_NEAR(dbSumPower(60.0f, 60.0f), 63.0103, 1e-3);
    CHECK_NEAR(dbToPressureRatio(20.0f), 10.0, 1e-5);
    CHECK_NEAR(pressureRatioToDb(10.0f), 20.0, 1e-5);
    CHECK_NEAR(pressureRatioToDb(0.0f), -240.0, 1e-3); // clamped at 1e-12, never -inf
}

// ---------------------------------------------------------------- Random

NOCTIS_TEST("core.random.pcg32_reference_sequence")
{
    // Reference output of O'Neill's pcg32-demo: pcg32_srandom_r(&rng, 42u, 54u), then pcg32_random_r.
    // (Values recomputed independently with a 20-line Python PCG32 for this test.)
    Rng rng(42u, 54u);
    const u32 expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (const u32 e : expected)
    {
        CHECK_EQ(rng.nextU32(), e);
    }
}

NOCTIS_TEST("core.random.splitmix_and_hash")
{
    // SplitMix64 (Steele, Lea & Flood 2014) first output from state 0.
    CHECK_EQ(mixHash64(0), 0xE220A8397B1DCDAFull);
    CHECK_EQ(mixHash64(1), 0x910A2DEC89025CC1ull);
    CHECK_NE(hashCombine(1, 2), hashCombine(2, 1));
    // hashString is FNV-1a-shaped but uses 1469598103934665603 as offset basis (the standard
    // FNV-1a 64 basis is 14695981039346656037). Changing it would reshuffle every derived seed, so
    // the test pins the current values instead of the standard ones (see Docs/PERFORMANCE.md, QA notes).
    CHECK_EQ(hashString(""), 1469598103934665603ull);
    CHECK_EQ(hashString("a"), 4953267810257967366ull); // 0x44bd8ad473cd9906, recomputed independently (Python)
    CHECK_NE(hashString("ab"), hashString("ba"));
}

NOCTIS_TEST("core.random.determinism_and_streams")
{
    Rng a(123456789u, 7u);
    Rng b(123456789u, 7u);
    Rng c(123456789u, 8u);
    int sameAsOtherStream = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const u32 va = a.nextU32();
        CHECK_EQ(va, b.nextU32());
        sameAsOtherStream += va == c.nextU32() ? 1 : 0;
    }
    CHECK_LE(sameAsOtherStream, 2);
    // Save / restore reproduces the stream exactly.
    Rng d(99u, 3u);
    for (int i = 0; i < 17; ++i)
    {
        d.nextU32();
    }
    Rng e;
    e.restore(d.stateForSave(), d.incForSave());
    for (int i = 0; i < 100; ++i)
    {
        CHECK_EQ(d.nextU32(), e.nextU32());
    }
}

NOCTIS_TEST("core.random.distributions")
{
    Rng rng(2024u, 1u);
    const int n = 200000;
    double sum = 0.0;
    double sum2 = 0.0;
    float lo = 1.0f;
    float hi = 0.0f;
    int intHits[5] = {};
    for (int i = 0; i < n; ++i)
    {
        const float u = rng.uniform();
        lo = std::min(lo, u);
        hi = std::max(hi, u);
        const int k = rng.rangeInt(-2, 2);
        CHECK(k >= -2 && k <= 2);
        ++intHits[k + 2];
    }
    CHECK_GE(lo, 0.0f);
    CHECK_LT(hi, 1.0f);
    for (const int h : intHits)
    {
        CHECK_NEAR(h, n / 5.0, 5.0 * std::sqrt(n * 0.2 * 0.8)); // 5 sigma
    }
    for (int i = 0; i < n; ++i)
    {
        const double x = rng.normal();
        sum += x;
        sum2 += x * x;
    }
    const double m = sum / n;
    const double var = sum2 / n - m * m;
    NOTE("normal(): mean %.5f, variance %.5f over %d draws", m, var, n);
    CHECK_NEAR(m, 0.0, 5.0 / std::sqrt(static_cast<double>(n)));
    CHECK_NEAR(var, 1.0, 5.0 * std::sqrt(2.0 / n));
    for (const float lambda : {3.5f, 50.0f})
    {
        double ps = 0.0;
        const int pn = 50000;
        for (int i = 0; i < pn; ++i)
        {
            const int k = rng.poisson(lambda);
            CHECK(k >= 0);
            ps += k;
        }
        NOTE("poisson(%.1f): sample mean %.4f", static_cast<double>(lambda), ps / pn);
        CHECK_NEAR(ps / pn, lambda, 5.0 * std::sqrt(lambda / pn) + 0.02);
    }
}

// ---------------------------------------------------------------- JSON

NOCTIS_TEST("core.json.parse_types_and_escapes")
{
    const std::string text = R"({
        "name": "Edmontosaurus \"annectens\"",
        "accent": "\u00e9t\u00e9",
        "emoji": "\ud83e\udd96",
        "n": -12.5e2,
        "int": 42,
        "flags": [true, false, null],
        "nested": {"a": {"b": [1, 2, {"c": "d"}]}}
    })";
    const JsonParseResult r = parseJson(text);
    REQUIRE(r.ok);
    const JsonValue& v = r.value;
    CHECK_EQ(v.getString("name"), std::string("Edmontosaurus \"annectens\""));
    CHECK_EQ(v.getString("accent"), std::string("\xc3\xa9t\xc3\xa9"));      // "été" in UTF-8
    CHECK_EQ(v.getString("emoji"), std::string("\xf0\x9f\xa6\x96"));        // U+1F996 via surrogate pair
    CHECK_NEAR(v.get("n").asDouble(), -1250.0, 0.0);
    CHECK_EQ(v.getInt("int", 0), 42);
    CHECK_EQ(v.get("flags").size(), size_t(3));
    CHECK(v.get("flags").at(0).asBool(false));
    CHECK(v.get("flags").at(2).isNull());
    CHECK_EQ(v.path("nested.a.b").at(2).getString("c"), std::string("d"));
    CHECK(v.path("nested.missing.x").isNull());
    // Insertion order is preserved.
    REQUIRE_GE(v.members().size(), size_t(1));
    CHECK_EQ(v.members().front().first, std::string("name"));
}

NOCTIS_TEST("core.json.dump_parse_round_trip")
{
    JsonValue root = JsonValue::makeObject();
    root.set("pi", 3.141592653589793);
    root.set("third", 1.0 / 3.0);
    root.set("tiny", 1.5e-12);
    root.set("negTiny", -2.0e-10);
    root.set("big", 6.02214076e23);
    root.set("time_s", 1234567.891234);
    root.set("sum", 0.1 + 0.2);
    root.set("int", 1000000);
    root.set("neg", -7);
    root.set("text", "ligne 1\nligne 2\t\"cit\xc3\xa9\" \\ fin");
    root.set("ctrl", std::string("a\x01" "b"));
    JsonValue arr = JsonValue::makeArray();
    arr.push(1.25f);
    arr.push(JsonValue());
    arr.push(false);
    arr.push(JsonValue::makeObject());
    arr.push(JsonValue::makeArray());
    root.set("arr", arr);
    for (const bool pretty : {true, false})
    {
        const std::string d1 = root.dump(pretty);
        const JsonParseResult r = parseJson(d1);
        REQUIRE(r.ok);
        const std::string d2 = r.value.dump(pretty);
        CHECK_EQ(d1, d2); // dump is a fixed point of parse∘dump
        for (const JsonValue::Member& m : root.members())
        {
            const JsonValue& back = r.value.get(m.first);
            CHECK_EQ(static_cast<int>(back.type()), static_cast<int>(m.second.type()));
            if (m.second.isNumber())
            {
                // Exact: dump must emit enough digits for doubles to survive a save/load cycle.
                if (back.asDouble() != m.second.asDouble())
                {
                    noctistest::failf(__FILE__, __LINE__, "number '%s' not preserved: %.17g -> %.17g", m.first.c_str(), m.second.asDouble(),
                                      back.asDouble());
                }
            }
            if (m.second.isString())
            {
                CHECK_EQ(back.asString(), m.second.asString());
            }
        }
    }
    // Non-finite numbers are not representable in JSON: written as null.
    JsonValue nan = JsonValue::makeObject();
    nan.set("x", std::nan(""));
    CHECK(parseJson(nan.dump(false)).value.get("x").isNull());
}

NOCTIS_TEST("core.json.errors")
{
    const char* bad[] = {"{", "{\"a\": }", "[1, 2,]", "{\"a\" 1}", "\"unterminated", "tru", "{\"a\": 1} trailing", "[\"\\uZZZZ\"]", "-", ""};
    for (const char* t : bad)
    {
        const JsonParseResult r = parseJson(t);
        if (r.ok)
        {
            noctistest::failf(__FILE__, __LINE__, "accepted invalid JSON: %s", t);
        }
        else
        {
            CHECK(!r.error.empty());
        }
    }
    const JsonParseResult r = parseJson("{\n  \"a\": 1,\n  \"b\": ?\n}");
    CHECK(!r.ok);
    NOTE("error message: %s", r.error.c_str());
    CHECK(r.error.rfind("3:", 0) == 0); // reports line 3
}

// ---------------------------------------------------------------- Grid / spatial hash

NOCTIS_TEST("core.grid.indexing_and_bilinear")
{
    Grid2D<float> g(16, 8, 2.0f, Vec2{-10.0f, 4.0f}, 0.0f);
    CHECK_EQ(g.cellCount(), size_t(128));
    int x = 0;
    int y = 0;
    g.worldToCell(Vec2{-10.0f + 5.1f, 4.0f + 3.9f}, x, y);
    CHECK_EQ(x, 2);
    CHECK_EQ(y, 1);
    const Vec2 c = g.cellCenter(2, 1);
    CHECK_NEAR(c.x, -10.0 + 5.0, 1e-6);
    CHECK_NEAR(c.y, 4.0 + 3.0, 1e-6);
    CHECK_EQ(g.clampX(-5), 0);
    CHECK_EQ(g.clampY(99), 7);
    // Bilinear interpolation reproduces a linear field exactly away from the clamped border.
    for (int j = 0; j < g.height(); ++j)
    {
        for (int i = 0; i < g.width(); ++i)
        {
            const Vec2 p = g.cellCenter(i, j);
            g.at(i, j) = 0.5f * p.x - 2.0f * p.y + 3.0f;
        }
    }
    Rng rng(5u, 1u);
    for (int k = 0; k < 200; ++k)
    {
        const Vec2 p{rng.range(-10.0f + 1.0f, -10.0f + 31.0f), rng.range(4.0f + 1.0f, 4.0f + 15.0f)};
        CHECK_NEAR(sampleBilinear(g, p), 0.5f * p.x - 2.0f * p.y + 3.0f, 1e-4);
    }
    CHECK_NEAR(g.bounds().max.x, 22.0, 1e-6);
}

NOCTIS_TEST("core.spatial_hash.matches_brute_force")
{
    const Rect2 bounds{{0.0f, 0.0f}, {1000.0f, 600.0f}};
    SpatialHash index;
    index.configure(bounds, 50.0f);
    std::vector<Vec2> pts;
    Rng rng(77u, 2u);
    for (u32 i = 0; i < 3000; ++i)
    {
        // A few points outside the bounds: they are clamped into border cells and must still be found.
        const Vec2 p{rng.range(-50.0f, 1050.0f), rng.range(-50.0f, 650.0f)};
        pts.push_back(p);
        index.insert(i, p);
    }
    index.finalize();
    CHECK_EQ(index.count(), size_t(3000));
    size_t totalFound = 0;
    for (int q = 0; q < 300; ++q)
    {
        const Vec2 center{rng.range(-20.0f, 1020.0f), rng.range(-20.0f, 620.0f)};
        const float radius = rng.range(1.0f, 180.0f);
        std::vector<u32> got;
        index.query(center, radius, got);
        std::vector<u32> expect;
        for (u32 i = 0; i < pts.size(); ++i)
        {
            if (distanceSq(pts[i], center) <= radius * radius)
            {
                expect.push_back(i);
            }
        }
        std::sort(got.begin(), got.end());
        CHECK_EQ(got.size(), expect.size());
        CHECK(got == expect);
        totalFound += got.size();
        const Rect2 box{{center.x - radius, center.y - radius * 0.5f}, {center.x + radius, center.y + radius * 0.5f}};
        std::vector<u32> gotBox;
        index.queryBox(box, gotBox);
        std::vector<u32> expectBox;
        for (u32 i = 0; i < pts.size(); ++i)
        {
            if (box.contains(pts[i]))
            {
                expectBox.push_back(i);
            }
        }
        std::sort(gotBox.begin(), gotBox.end());
        CHECK(gotBox == expectBox);
    }
    NOTE("300 radius queries returned %zu handles, identical to brute force", totalFound);
    index.clear();
    std::vector<u32> none;
    index.query(Vec2{500.0f, 300.0f}, 1000.0f, none);
    CHECK(none.empty());
}

// ---------------------------------------------------------------- Calendar

NOCTIS_TEST("core.time.paleo_calendar")
{
    CalendarSpec spec;
    spec.dayLengthHours = 23.5;  // Late Cretaceous: shorter day ...
    spec.daysPerYear = 372.0;    // ... more days per year
    spec.synodicMonthDays = 29.0;
    SimClock clock;
    clock.configure(spec, 140.0, 5.0, 0);
    CHECK_NEAR(clock.dayOfYear(), 140.0 + 5.0 / 23.5, 1e-9);
    CHECK_NEAR(clock.hourOfDay(), 5.0, 1e-9);
    CHECK_EQ(clock.year(), 0);
    CHECK_EQ(clock.formatted(), std::string("An 0, jour 141, 05:00"));
    // One paleo-day later: same hour, next day.
    clock.advance(23.5 * 3600.0);
    CHECK_NEAR(clock.hourOfDay(), 5.0, 1e-6);
    CHECK_NEAR(std::floor(clock.dayOfYear()), 141.0, 0.0);
    // Year rollover after daysPerYear days.
    clock.setSeconds((372.0 - 140.0) * 23.5 * 3600.0);
    CHECK_EQ(clock.year(), 1);
    CHECK_NEAR(clock.dayOfYear(), 5.0 / 23.5, 1e-6);
    CHECK_NEAR(clock.yearLengthSeconds(), 372.0 * 23.5 * 3600.0, 1e-3);
    // Moon phase has the synodic period and stays in [0, 1).
    clock.setSeconds(0.0);
    const double p0 = clock.moonPhase();
    clock.advance(29.0 * 23.5 * 3600.0);
    CHECK_NEAR(clock.moonPhase(), p0, 1e-6);
    clock.advance(14.5 * 23.5 * 3600.0);
    CHECK_NEAR(std::fmod(clock.moonPhase() - p0 + 1.0, 1.0), 0.5, 1e-6);
}

// ---------------------------------------------------------------- Jobs / events

NOCTIS_TEST("core.jobs.parallel_for_covers_every_index_once")
{
    for (const int threads : {1, 2, 4})
    {
        std::unique_ptr<JobScheduler> jobs = makeThreadPoolScheduler(threads);
        CHECK_EQ(jobs->workerCount(), threads);
        for (const size_t count : {size_t(0), size_t(1), size_t(7), size_t(1000), size_t(10007)})
        {
            std::vector<std::atomic<int>> hits(count);
            for (auto& h : hits)
            {
                h.store(0);
            }
            for (int rep = 0; rep < 5; ++rep)
            {
                jobs->parallelFor(count, [&](size_t i) { hits[i].fetch_add(1); }, 8);
            }
            int wrong = 0;
            for (auto& h : hits)
            {
                wrong += h.load() == 5 ? 0 : 1;
            }
            CHECK_EQ(wrong, 0);
        }
    }
}

NOCTIS_TEST("core.events.deterministic_order")
{
    // Emission order differs, dispatch order must not.
    auto run = [](bool reversed) {
        EventBus bus(100);
        std::vector<u32> seen;
        bus.subscribe([&](const SimEvent& e) { seen.push_back(e.subject.value * 100u + static_cast<u32>(e.type)); });
        std::vector<SimEvent> evs;
        for (u32 i = 0; i < 20; ++i)
        {
            SimEvent e;
            e.type = static_cast<EventType>(i % 5);
            e.time = 10.0 + static_cast<double>(i % 3);
            e.subject = EntityId::make(EntityKind::Creature, 20u - i);
            evs.push_back(e);
        }
        if (reversed)
        {
            std::reverse(evs.begin(), evs.end());
        }
        for (const SimEvent& e : evs)
        {
            bus.emit(e);
        }
        bus.flush();
        return seen;
    };
    CHECK(run(false) == run(true));
    // Chronicle is bounded and chronological.
    EventBus bus(10);
    for (int i = 0; i < 25; ++i)
    {
        SimEvent e;
        e.type = EventType::Drink;
        e.time = static_cast<double>(i);
        bus.emit(e);
        bus.flush();
    }
    const std::vector<SimEvent> chron = bus.chronicle();
    REQUIRE_EQ(chron.size(), size_t(10));
    CHECK_NEAR(chron.front().time, 15.0, 0.0);
    CHECK_NEAR(chron.back().time, 24.0, 0.0);
    CHECK_EQ(bus.totalEmitted(), size_t(25));
}
