// Epistemic status of every scientific statement in the game.
#pragma once

#include "Noctis/Core/Platform.h"

#include <string>
#include <vector>

namespace noctis
{
enum class Confidence : u8
{
    Established = 0,       // ÉTABLI
    StronglySupported = 1, // FORTEMENT ÉTAYÉ
    Inferred = 2,          // INFÉRÉ / PLAUSIBLE
    Speculative = 3,       // SPÉCULATIF
    Unknown = 4            // INCONNU
};

NOCTIS_API const char* confidenceKey(Confidence c);     // "strongly_supported"
NOCTIS_API const char* confidenceLabelFr(Confidence c); // "FORTEMENT ÉTAYÉ"
NOCTIS_API Confidence parseConfidence(const std::string& key, bool* ok = nullptr);
// The weaker of two confidences (used when a conclusion depends on several claims).
inline Confidence weakest(Confidence a, Confidence b) { return static_cast<u8>(a) > static_cast<u8>(b) ? a : b; }

inline constexpr const char* kInsufficientDataFr = "Information insuffisante — recherche nécessaire.";

// A scientific claim, as stored in species/environment files.
struct Claim
{
    std::string summary;
    Confidence confidence = Confidence::Unknown;
    std::vector<std::string> sources;
    bool hasValue = false;
    double value = 0.0;
    bool hasMin = false;
    double min = 0.0;
    bool hasMax = false;
    double max = 0.0;
    std::string unit;
    std::string notes;
};

struct CompetingPosition
{
    std::string summary;
    std::vector<std::string> sources;
};

struct CompetingHypothesis
{
    std::string topic;
    std::string summary;
    std::vector<CompetingPosition> positions;
};
} // namespace noctis
