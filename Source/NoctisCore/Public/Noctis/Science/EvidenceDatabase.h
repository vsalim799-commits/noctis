// EvidenceDatabase — the single source of scientific truth for the game.
// Loads bibliography, species, environments, methods and reconstruction profiles from Data/,
// validates their internal consistency, and answers queries from every other system.
#pragma once

#include "Noctis/Core/FileSystem.h"
#include "Noctis/Core/Json.h"
#include "Noctis/Science/Environment.h"
#include "Noctis/Science/Species.h"

#include <map>
#include <string>
#include <vector>

namespace noctis
{
struct SourceRef
{
    std::string id;
    std::string authors;
    int year = 0;
    std::string title;
    std::string container;
    std::string volume;
    std::string pages;
    std::string doi;
    std::string url;
    std::string type;
    bool verified = false;
    std::string verification;
    std::string file;

    std::string shortCitation() const; // "Hutchinson et al. 2011"
    std::string fullCitation() const;
};

// A switchable reconstruction choice for a speculative behaviour or trait.
struct ProfileToggle
{
    std::string id;
    bool enabled = false;
    Confidence confidence = Confidence::Speculative;
    std::vector<std::string> sources;
    std::string descriptionFr;
};

struct ReconstructionProfile
{
    std::string id;
    std::string version;
    std::string nameFr;
    std::string descriptionFr;
    std::vector<ProfileToggle> toggles;
    // Taxa left out of the world under this reconstruction (e.g. a contested taxon treated as a juvenile of another).
    std::vector<std::string> excludedSpecies;

    bool enabled(const std::string& toggleId) const;
    const ProfileToggle* find(const std::string& toggleId) const;
};

struct ValidationIssue
{
    enum class Severity : u8
    {
        Info,
        Warning,
        Error
    };
    Severity severity = Severity::Warning;
    std::string file;
    std::string message;
};

class NOCTIS_API EvidenceDatabase
{
public:
    // dataRoot is the repository's Data/ directory (or the staged copy in a packaged game).
    bool load(const FileSystem& fs, const std::string& dataRoot);

    const std::vector<SpeciesDefinition>& species() const { return species_; }
    const SpeciesDefinition* findSpecies(const std::string& id) const;
    int speciesIndex(const std::string& id) const; // -1 if absent
    const SpeciesDefinition& speciesAt(int index) const { return species_[static_cast<size_t>(index)]; }

    const std::vector<EnvironmentDefinition>& environments() const { return environments_; }
    const EnvironmentDefinition* findEnvironment(const std::string& id) const;

    const std::map<std::string, SourceRef>& bibliography() const { return sources_; }
    const SourceRef* findSource(const std::string& id) const;

    const std::vector<ReconstructionProfile>& profiles() const { return profiles_; }
    const ReconstructionProfile* findProfile(const std::string& id) const;

    const JsonValue& methods() const { return methods_; }
    const JsonValue& equipment() const { return equipment_; }
    const JsonValue& simConstants() const { return simConstants_; }
    // Numeric constant from Data/Science/sim_constants.json ("physiology.assimilation_herbivore").
    float constant(const std::string& dottedPath, float fallback) const;

    // Consistency checks: unresolved source ids, unverified sources backing high confidence,
    // missing science topics, sim parameters without basis, unknown enum keys.
    std::vector<ValidationIssue> validate() const;
    const std::vector<ValidationIssue>& loadIssues() const { return loadIssues_; }

    // Species are sorted by id for deterministic indexing; this maps a list of ids to indices.
    std::vector<int> speciesIndicesFor(const std::vector<std::string>& ids) const;

private:
    bool loadBibliography(const FileSystem& fs, const std::string& dir);
    bool loadSpecies(const FileSystem& fs, const std::string& dir);
    bool loadEnvironments(const FileSystem& fs, const std::string& dir);
    bool loadProfiles(const FileSystem& fs, const std::string& dir);
    void issue(ValidationIssue::Severity s, const std::string& file, const std::string& msg);

    std::vector<SpeciesDefinition> species_;
    std::vector<EnvironmentDefinition> environments_;
    std::map<std::string, SourceRef> sources_;
    std::vector<ReconstructionProfile> profiles_;
    JsonValue methods_;
    JsonValue equipment_;
    JsonValue simConstants_;
    std::vector<ValidationIssue> loadIssues_;
};

// Parsers shared with tools/tests.
NOCTIS_API Claim parseClaim(const JsonValue& v);
NOCTIS_API JsonValue claimToJson(const Claim& c);
NOCTIS_API bool parseSpeciesJson(const JsonValue& root, SpeciesDefinition& out, std::vector<std::string>& errors);
NOCTIS_API bool parseEnvironmentJson(const JsonValue& root, EnvironmentDefinition& out, std::vector<std::string>& errors);
} // namespace noctis
