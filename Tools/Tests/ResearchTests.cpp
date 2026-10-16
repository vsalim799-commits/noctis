// Research layer: statistics against exact textbook values, and the epistemic guarantees of the
// hypothesis system (it reports support, never truth).
#include "TestFramework.h"

#include "Noctis/Research/FieldGear.h"
#include "Noctis/Research/Hypothesis.h"
#include "Noctis/Research/Observation.h"
#include "Noctis/Research/Statistics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

using namespace noctis;

namespace
{
// Exact two-sided permutation p-value of the rank-biserial statistic by full enumeration (small n).
double exactMannWhitneyP(const std::vector<double>& a, const std::vector<double>& b)
{
    std::vector<double> all(a);
    all.insert(all.end(), b.begin(), b.end());
    const size_t n = all.size();
    const size_t na = a.size();
    auto uStat = [](const std::vector<double>& x, const std::vector<double>& y) {
        double u = 0.0;
        for (const double xi : x)
        {
            for (const double yi : y)
            {
                u += xi > yi ? 1.0 : (xi == yi ? 0.5 : 0.0);
            }
        }
        return u;
    };
    const double denom = static_cast<double>(na) * static_cast<double>(n - na);
    const double obs = std::fabs(2.0 * uStat(a, b) / denom - 1.0);
    int extreme = 0;
    int total = 0;
    for (u32 mask = 0; mask < (1u << n); ++mask)
    {
        int bits = 0;
        for (size_t i = 0; i < n; ++i)
        {
            bits += (mask >> i) & 1u ? 1 : 0;
        }
        if (bits != static_cast<int>(na))
        {
            continue;
        }
        std::vector<double> x;
        std::vector<double> y;
        for (size_t i = 0; i < n; ++i)
        {
            ((mask >> i) & 1u ? x : y).push_back(all[i]);
        }
        ++total;
        extreme += std::fabs(2.0 * uStat(x, y) / denom - 1.0) >= obs - 1e-12 ? 1 : 0;
    }
    return static_cast<double>(extreme) / static_cast<double>(total);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Words that would claim truth rather than support (French and English).
bool claimsTruth(const std::string& text, std::string* word)
{
    const std::string t = lower(text);
    for (const char* w : {"correct", "prouv", "vérité", "certitude", "est vraie", "est vrai ", "true", "proven", "confirmed"})
    {
        if (t.find(w) != std::string::npos)
        {
            if (word)
            {
                *word = w;
            }
            return true;
        }
    }
    return false;
}

SessionSample sample(double t, i16 species, float spacing, bool drone, float observerDistance, float lux)
{
    SessionSample s;
    s.time = t;
    s.speciesIndex = species;
    s.count = 8;
    s.meanSpacingM = spacing;
    s.droneAirborne = drone;
    s.droneAltitudeM = drone ? 40.0f : 0.0f;
    s.observerDistanceM = observerDistance;
    s.lux = lux;
    s.hourOfDay = 10.0f;
    s.temperatureC = 24.0f;
    return s;
}
} // namespace

NOCTIS_TEST("research.vehicle.noise_terms")
{
    // Defaults = equipment.json: the generator dominates at standstill, tyre / terrain rolling noise at speed.
    const VehicleSystem v;
    const float idle = v.noiseAt1m(0.0f, true, false);
    CHECK_NEAR(idle, 52.0, 1e-3);
    CHECK_GT(v.noiseAt1m(0.0f, true, true), idle + 25.0f);
    CHECK_NEAR(v.noiseAt1m(14.0f, false, false), 85.0, 1e-3);
    CHECK_GT(v.noiseAt1m(15.0f, true, false), 85.0f);
    CHECK_GT(v.noiseAt1m(8.0f, true, false) - v.noiseAt1m(3.0f, true, false), 10.0f);
}

NOCTIS_TEST("research.stats.fisher_exact_textbook")
{
    // Fisher's "lady tasting tea" (Fisher 1935): 3 of 4 cups right each way, two-sided p = 34/70.
    TestResult r = fisherExact(3, 1, 1, 3);
    CHECK_NEAR(r.pValue, 34.0 / 70.0, 1e-9);
    // Haldane-corrected odds ratio (3.5 * 3.5) / (1.5 * 1.5) and Woolf 95 % interval.
    CHECK_NEAR(r.effectSize, 49.0 / 9.0, 1e-9);
    CHECK_REL(r.ciLow, 0.364051, 1e-5);
    CHECK_REL(r.ciHigh, 81.42259, 1e-5);
    // Exact two-sided p-values (sum of table probabilities <= observed), computed with rational arithmetic.
    CHECK_REL(fisherExact(1, 9, 11, 3).pValue, 41.0 / 14858.0, 1e-7); // 0.0027595 (dieting example)
    CHECK_REL(fisherExact(0, 5, 5, 0).pValue, 1.0 / 126.0, 1e-7);
    CHECK_REL(fisherExact(10, 2, 3, 15).pValue, 3571.0 / 6653325.0, 1e-6);
    CHECK_REL(fisherExact(2, 7, 8, 2).pValue, 1063.0 / 46189.0, 1e-7);
    CHECK_NEAR(fisherExact(5, 5, 5, 5).pValue, 1.0, 1e-12);
    // Symmetry: transposing or swapping rows does not change a two-sided p.
    CHECK_NEAR(fisherExact(2, 7, 8, 2).pValue, fisherExact(2, 8, 7, 2).pValue, 1e-12);
    CHECK_NEAR(fisherExact(2, 7, 8, 2).pValue, fisherExact(8, 2, 2, 7).pValue, 1e-12);
    NOTE("tea: p = %.6f (34/70 = %.6f), OR = %.4f [%.4f; %.3f]", r.pValue, 34.0 / 70.0, r.effectSize, r.ciLow, r.ciHigh);
}

NOCTIS_TEST("research.stats.mann_whitney_textbook")
{
    // n1 = n2 = 5: two-sided exact p for U = 0, 2, 3 is 2/252, 8/252, 14/252 — the textbook critical
    // value at alpha = 0.05 (two-tailed) is U = 2 (Mann & Whitney 1947 tables).
    struct Case
    {
        std::vector<double> a;
        std::vector<double> b;
        double u;
        double exactP;
    };
    const Case cases[] = {
        {{1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}, 0.0, 2.0 / 252.0},
        {{1, 2, 3, 7, 4}, {5, 6, 8, 9, 10}, 2.0, 8.0 / 252.0},
        {{1, 2, 3, 5, 7}, {4, 6, 8, 9, 10}, 3.0, 14.0 / 252.0},
        {{1, 2, 2, 3}, {2, 3, 4, 5}, 2.5, 0.2}, // ties: mid-ranks
    };
    const int perms = 20000;
    for (const Case& c : cases)
    {
        const double enumerated = exactMannWhitneyP(c.a, c.b);
        CHECK_NEAR(enumerated, c.exactP, 1e-12); // the enumeration helper agrees with the tables
        const TestResult r = mannWhitney(c.a, c.b, 1234u, perms);
        // statistic = U of sample A (pairs where a > b, ties count 1/2); effect = rank-biserial 2U/(n1 n2) - 1.
        CHECK_NEAR(r.statistic, c.u, 1e-9);
        CHECK_NEAR(r.effectSize, 2.0 * c.u / (static_cast<double>(c.a.size()) * static_cast<double>(c.b.size())) - 1.0, 1e-12);
        // Monte-Carlo p: within 4.5 standard errors of the exact value (+1/(N+1) bias of the estimator).
        const double se = std::sqrt(c.exactP * (1.0 - c.exactP) / perms);
        CHECK_NEAR(r.pValue, c.exactP, 4.5 * se + 1.0 / (perms + 1.0));
        NOTE("U = %.1f: permutation p = %.5f, exact p = %.5f (+/- %.5f)", c.u, r.pValue, c.exactP, 4.5 * se);
    }
    // Decision at alpha = 0.05 matches the critical-value table.
    CHECK_LT(mannWhitney(cases[1].a, cases[1].b, 9u, perms).pValue, 0.05);
    CHECK_GT(mannWhitney(cases[2].a, cases[2].b, 9u, perms).pValue, 0.05);
    // Deterministic for a given seed.
    CHECK_NEAR(mannWhitney(cases[2].a, cases[2].b, 77u, 3000).pValue, mannWhitney(cases[2].a, cases[2].b, 77u, 3000).pValue, 0.0);
    // Degenerate input does not produce NaN.
    const TestResult empty = mannWhitney({}, {1.0, 2.0}, 1u, 100);
    CHECK_FINITE(empty.pValue);
    CHECK_FINITE(empty.effectSize);
}

NOCTIS_TEST("research.stats.spearman_and_ranks")
{
    const std::vector<double> r = ranks({10.0, 20.0, 20.0, 30.0, 5.0});
    const double expected[] = {2.0, 3.5, 3.5, 5.0, 1.0};
    for (size_t i = 0; i < r.size(); ++i)
    {
        CHECK_NEAR(r[i], expected[i], 1e-12);
    }
    // rho = 1 - 6 sum d^2 / (n (n^2 - 1)) = 1 - 6*4/120 = 0.8 (no ties).
    TestResult s = spearman({1, 2, 3, 4, 5}, {2, 1, 4, 3, 5}, 3u, 2000);
    CHECK_NEAR(s.statistic, 0.8, 1e-12);
    CHECK_NEAR(spearman({1, 2, 3, 4, 5, 6}, {2, 4, 9, 16, 25, 36}, 3u, 500).statistic, 1.0, 1e-12); // monotone, non-linear
    CHECK_NEAR(spearman({1, 2, 3, 4, 5, 6}, {6, 5, 4, 3, 2, 1}, 3u, 500).statistic, -1.0, 1e-12);
    // Permutation p for rho = 0.8, n = 5: |rho| >= 0.8 in 16 of the 120 permutations (sum d^2 <= 4 or >= 36),
    // exact two-sided p = 16/120 = 0.1333 (enumerated).
    s = spearman({1, 2, 3, 4, 5}, {2, 1, 4, 3, 5}, 5u, 20000);
    CHECK_NEAR(s.pValue, 16.0 / 120.0, 4.5 * std::sqrt((16.0 / 120.0) * (104.0 / 120.0) / 20000.0) + 1e-4);
    NOTE("spearman rho = 0.8 (n = 5): permutation p = %.4f, exact 16/120 = %.4f", s.pValue, 16.0 / 120.0);
    CHECK_NEAR(median({3.0, 1.0, 2.0, 10.0}), 2.5, 1e-12);
    CHECK_NEAR(stddev({2, 4, 4, 4, 5, 5, 7, 9}), std::sqrt(32.0 / 7.0), 1e-12); // sample (n-1) standard deviation
}

NOCTIS_TEST("research.hypothesis.reports_support_never_truth")
{
    // Status labels: none of them claims truth, and there is no "correct" status at all.
    for (int s = 0; s < 4; ++s)
    {
        std::string word;
        const char* label = hypothesisStatusLabelFr(static_cast<HypothesisStatus>(s));
        if (claimsTruth(label, &word))
        {
            noctistest::failf(__FILE__, __LINE__, "status label '%s' contains '%s'", label, word.c_str());
        }
    }
    // A strong, clean effect: group spacing much larger while the drone is airborne, many sessions.
    ObservationSystem obs;
    const i16 species = 3;
    Rng rng(42u, 1u);
    for (u32 sid = 1; sid <= 6; ++sid)
    {
        ObservationSession s;
        s.id = sid;
        s.start = sid * 10000.0;
        s.end = s.start + 3600.0;
        s.open = false;
        s.speciesFocus = species;
        for (int k = 0; k < 60; ++k)
        {
            const bool drone = (k / 10) % 2 == 1;
            const float spacing = drone ? rng.range(14.0f, 20.0f) : rng.range(4.0f, 9.0f);
            s.samples.push_back(sample(s.start + 40.0 * k, species, spacing, drone, 200.0f + rng.range(-20.0f, 20.0f), 20000.0f));
        }
        obs.mutableSessions().push_back(s);
    }
    HypothesisSystem hs;
    Hypothesis h;
    h.statementFr = "Le groupe s'espace quand le drone vole";
    h.kind = HypothesisKind::Comparison;
    h.speciesIndex = species;
    h.response = Measure::GroupSpacing;
    h.condition.kind = ConditionKind::DroneAirborne;
    h.predicted = PredictedDirection::Increase;
    const u32 id = hs.create(h, 0.0);
    hs.test(id, obs, {}, 1.0e5, 99u);
    const Hypothesis* res = hs.find(id);
    REQUIRE(res != nullptr);
    NOTE("strong effect: status '%s', p = %.4f, effect %.2f, n = %d/%d, evidence %d", hypothesisStatusLabelFr(res->status), res->result.pValue,
         res->result.effectSize, res->result.nA, res->result.nB, static_cast<int>(res->evidence));
    CHECK(res->status == HypothesisStatus::Supported);
    CHECK(res->evidence != Confidence::Established); // the system never promotes a result to "established"
    CHECK(res->interpretationFr.find("ne démontre pas le mécanisme") != std::string::npos);
    std::string word;
    if (claimsTruth(res->interpretationFr, &word) || claimsTruth(HypothesisSystem::describe(*res, nullptr), &word))
    {
        noctistest::failf(__FILE__, __LINE__, "interpretation claims truth ('%s'): %s", word.c_str(), res->interpretationFr.c_str());
    }

    // Opposite prediction on the same data: significant but in the wrong direction -> not supported.
    Hypothesis h2 = h;
    h2.predicted = PredictedDirection::Decrease;
    const u32 id2 = hs.create(h2, 0.0);
    hs.test(id2, obs, {}, 1.0e5, 99u);
    CHECK(hs.find(id2)->status == HypothesisStatus::NotSupported);

    // No difference at all (identical distributions) with plenty of data -> not supported, never supported.
    ObservationSystem flat;
    for (u32 sid = 1; sid <= 4; ++sid)
    {
        ObservationSession s;
        s.id = sid;
        s.start = sid * 10000.0;
        s.end = s.start + 3600.0;
        s.open = false;
        for (int k = 0; k < 60; ++k)
        {
            s.samples.push_back(sample(s.start + 40.0 * k, species, 6.0f + static_cast<float>(k % 5), (k / 5) % 2 == 1, 200.0f, 20000.0f));
        }
        flat.mutableSessions().push_back(s);
    }
    const u32 id3 = hs.create(h, 0.0);
    hs.test(id3, flat, {}, 1.0e5, 99u);
    NOTE("no effect: status '%s', p = %.3f", hypothesisStatusLabelFr(hs.find(id3)->status), hs.find(id3)->result.pValue);
    CHECK(hs.find(id3)->status == HypothesisStatus::NotSupported);

    // Too little data -> stays open, with a sample-size warning; one session -> pseudo-replication warning.
    ObservationSystem tiny;
    ObservationSession s;
    s.id = 1;
    s.start = 0.0;
    s.end = 600.0;
    for (int k = 0; k < 8; ++k)
    {
        s.samples.push_back(sample(40.0 * k, species, k % 2 ? 20.0f : 5.0f, k % 2 == 1, 150.0f, 20000.0f));
    }
    tiny.mutableSessions().push_back(s);
    const u32 id4 = hs.create(h, 0.0);
    hs.test(id4, tiny, {}, 1.0e4, 99u);
    const Hypothesis* small = hs.find(id4);
    CHECK(small->status == HypothesisStatus::Open);
    bool pseudoWarning = false;
    for (const std::string& wtxt : small->warningsFr)
    {
        pseudoWarning = pseudoWarning || wtxt.find("pseudo-réplication") != std::string::npos;
    }
    CHECK(pseudoWarning);
}

NOCTIS_TEST("research.hypothesis.confound_warnings")
{
    // Drone flights only at close range: the observer-distance confound must be flagged, and with
    // warnings present the evidence level cannot be "strongly supported".
    ObservationSystem obs;
    const i16 species = 1;
    Rng rng(8u, 1u);
    for (u32 sid = 1; sid <= 5; ++sid)
    {
        ObservationSession s;
        s.id = sid;
        s.start = sid * 10000.0;
        s.end = s.start + 3600.0;
        for (int k = 0; k < 60; ++k)
        {
            const bool drone = k % 2 == 1;
            s.samples.push_back(sample(s.start + 40.0 * k, species, drone ? rng.range(12.0f, 18.0f) : rng.range(4.0f, 8.0f), drone, drone ? 60.0f : 300.0f,
                                       20000.0f));
        }
        obs.mutableSessions().push_back(s);
    }
    HypothesisSystem hs;
    Hypothesis h;
    h.kind = HypothesisKind::Comparison;
    h.speciesIndex = species;
    h.response = Measure::GroupSpacing;
    h.condition.kind = ConditionKind::DroneAirborne;
    h.predicted = PredictedDirection::Increase;
    const u32 id = hs.create(h, 0.0);
    hs.test(id, obs, {}, 1.0e5, 5u);
    const Hypothesis* r = hs.find(id);
    REQUIRE(r != nullptr);
    bool observerWarning = false;
    for (const std::string& w : r->warningsFr)
    {
        observerWarning = observerWarning || w.find("Effet observateur") != std::string::npos;
    }
    CHECK(observerWarning);
    CHECK(r->evidence != Confidence::StronglySupported);
    CHECK(r->evidence != Confidence::Established);
}
