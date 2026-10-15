// Scientific data validation: every file under Data/ loads through the EvidenceDatabase with zero errors.
#include "TestFramework.h"

#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Json.h"
#include "Noctis/Science/EvidenceDatabase.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace noctis;

namespace
{
int countIssues(const std::vector<ValidationIssue>& issues, ValidationIssue::Severity s, std::string* firstMessages)
{
    int n = 0;
    for (const ValidationIssue& i : issues)
    {
        if (i.severity != s)
        {
            continue;
        }
        if (firstMessages && n < 15)
        {
            *firstMessages += "\n      " + i.file + " : " + i.message;
        }
        ++n;
    }
    return n;
}
} // namespace

NOCTIS_TEST("data.json_files_parse")
{
    std::unique_ptr<FileSystem> fs = makeStdFileSystem();
    const std::vector<std::string> files = fs->listFiles(noctistest::dataRoot(), ".json", true);
    REQUIRE_GT(files.size(), size_t(10));
    int bad = 0;
    for (const std::string& f : files)
    {
        std::string text;
        if (!fs->readText(f, text))
        {
            noctistest::fail(__FILE__, __LINE__, "cannot read " + f);
            ++bad;
            continue;
        }
        const JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            noctistest::fail(__FILE__, __LINE__, f + ": " + r.error);
            ++bad;
        }
    }
    NOTE("%zu JSON files under %s, %d unparsable", files.size(), noctistest::dataRoot().c_str(), bad);
}

NOCTIS_TEST("data.evidence_database_zero_errors")
{
    std::unique_ptr<FileSystem> fs = makeStdFileSystem();
    EvidenceDatabase db;
    REQUIRE(db.load(*fs, noctistest::dataRoot()));
    std::string loadErrors;
    const int loadErr = countIssues(db.loadIssues(), ValidationIssue::Severity::Error, &loadErrors);
    if (loadErr > 0)
    {
        noctistest::failf(__FILE__, __LINE__, "%d load error(s):%s", loadErr, loadErrors.c_str());
    }
    const std::vector<ValidationIssue> issues = db.validate();
    std::string errors;
    const int nErr = countIssues(issues, ValidationIssue::Severity::Error, &errors);
    const int nWarn = countIssues(issues, ValidationIssue::Severity::Warning, nullptr);
    const int nInfo = countIssues(issues, ValidationIssue::Severity::Info, nullptr);
    int verified = 0;
    for (const auto& s : db.bibliography())
    {
        verified += s.second.verified ? 1 : 0;
    }
    NOTE("%zu species, %zu environments, %zu sources (%d verified), %zu profiles; validate(): %d errors, %d warnings, %d infos", db.species().size(),
         db.environments().size(), db.bibliography().size(), verified, db.profiles().size(), nErr, nWarn, nInfo);
    if (nErr > 0)
    {
        noctistest::failf(__FILE__, __LINE__, "%d validation error(s):%s", nErr, errors.c_str());
    }
    CHECK_GT(db.species().size(), size_t(0));
    CHECK_GE(db.environments().size(), size_t(2));
    for (const char* p : {"standard", "conservative", "speculative"})
    {
        if (!db.findProfile(p))
        {
            noctistest::failf(__FILE__, __LINE__, "missing reconstruction profile '%s'", p);
        }
    }
}

NOCTIS_TEST("data.environments_reference_known_species")
{
    std::unique_ptr<FileSystem> fs = makeStdFileSystem();
    EvidenceDatabase db;
    REQUIRE(db.load(*fs, noctistest::dataRoot()));
    for (const EnvironmentDefinition& env : db.environments())
    {
        for (const std::string& id : env.sim.speciesIds)
        {
            if (db.speciesIndex(id) < 0)
            {
                noctistest::failf(__FILE__, __LINE__, "environment %s lists unknown species '%s'", env.id.c_str(), id.c_str());
            }
        }
        // Calendar sanity: Late Cretaceous days were shorter (more days per year) than today, never longer.
        CHECK_RANGE(env.sim.calendar.dayLengthHours, 20.0, 24.0);
        CHECK_RANGE(env.sim.calendar.dayLengthHours * env.sim.calendar.daysPerYear, 365.0 * 24.0 * 0.995, 365.25 * 24.0 * 1.005);
        CHECK_RANGE(env.sim.latitudeDeg, -90.0, 90.0);
        CHECK_GT(env.sim.mapSizeM, 1000.0f);
        NOTE("%s: day %.2f h, %.1f days/yr, lat %.1f deg, %zu species", env.id.c_str(), env.sim.calendar.dayLengthHours, env.sim.calendar.daysPerYear,
             static_cast<double>(env.sim.latitudeDeg), env.sim.speciesIds.size());
    }
    // Species indices are deterministic (sorted by id).
    for (size_t i = 1; i < db.species().size(); ++i)
    {
        CHECK(db.species()[i - 1].id < db.species()[i].id);
    }
}

NOCTIS_TEST("data.species_physical_sanity")
{
    std::unique_ptr<FileSystem> fs = makeStdFileSystem();
    EvidenceDatabase db;
    REQUIRE(db.load(*fs, noctistest::dataRoot()));
    for (const SpeciesDefinition& s : db.species())
    {
        const SpeciesSim& sim = s.sim;
        if (sim.role != SimRole::Agent)
        {
            continue;
        }
        const std::string where = " (" + s.id + ")";
        if (!(sim.adultMassKg > 0.0f && sim.adultLengthM > 0.0f && sim.hipHeightM > 0.0f && sim.hipHeightM < sim.adultLengthM * 1.5f))
        {
            noctistest::fail(__FILE__, __LINE__, "implausible body dimensions" + where);
        }
        if (!(sim.hatchlingMassKg > 0.0f && sim.hatchlingMassKg < sim.adultMassKg))
        {
            noctistest::fail(__FILE__, __LINE__, "hatchling mass must be in (0, adult mass)" + where);
        }
        if (!(sim.locomotion.walkSpeedMs > 0.0f && sim.locomotion.walkSpeedMs <= sim.locomotion.maxSpeedMs))
        {
            noctistest::fail(__FILE__, __LINE__, "walk speed must be in (0, max speed]" + where);
        }
        // Dynamic similarity: no terrestrial animal sustains Froude numbers far above ~10.
        const float frMax = sim.locomotion.maxSpeedMs * sim.locomotion.maxSpeedMs / (9.81f * sim.hipHeightM);
        if (!(frMax < 15.0f))
        {
            noctistest::failf(__FILE__, __LINE__, "max speed gives Froude %.1f%s", static_cast<double>(frMax), where.c_str());
        }
        if (!(sim.growth.maturityAgeYears > 0.0f && sim.growth.maturityAgeYears < sim.growth.maxLifespanYears))
        {
            noctistest::fail(__FILE__, __LINE__, "maturity age must be below lifespan" + where);
        }
    }
}
