// Paleoenvironment definition (one per expedition region).
#pragma once

#include "Noctis/Core/Time.h"
#include "Noctis/Science/Confidence.h"
#include "Noctis/Science/Taxonomy.h"

#include <string>
#include <utility>
#include <vector>

namespace noctis
{
struct PlantTypeSpec
{
    bool present = false;
    float maxBiomassKgM2 = 0.0f;
    float growthRatePerDay = 0.0f;
    float heightM = 0.0f;
    float habitat[kHabitatCount] = {};
};

struct EnvironmentSim
{
    float latitudeDeg = 45.0f;
    float axialTiltDeg = 23.4f;
    float meanAnnualTempC = 15.0f;
    float annualTempRangeC = 12.0f;
    float diurnalTempRangeC = 8.0f;
    float annualPrecipMm = 1000.0f;
    float wetSeasonStartDoy = 60.0f;
    float wetSeasonEndDoy = 180.0f;
    float dryStrength = 0.3f;
    float humidityMean = 0.7f;
    float fogMorningProbability = 0.3f;
    float stormProbabilityPerDay = 0.05f;
    float meanWindMs = 3.0f;
    float prevailingWindDeg = 270.0f; // direction the wind blows FROM, meteorological convention
    std::string terrainGenerator = "meandering_floodplain";
    float mapSizeM = 8192.0f;
    float reliefM = 60.0f;
    std::vector<Habitat> habitats;
    PlantTypeSpec plants[kPlantTypeCount];
    std::vector<std::string> speciesIds;
    CalendarSpec calendar;
};

struct EnvironmentDefinition
{
    std::string id;
    std::string reconstructionVersion;
    std::string nameFr;
    std::string nameEn;
    std::vector<std::pair<std::string, Claim>> science;
    EnvironmentSim sim;
    std::vector<std::pair<std::string, std::string>> simBasis;
    std::string sourceFile;

    const Claim* claim(const std::string& topic) const
    {
        for (const auto& c : science)
        {
            if (c.first == topic)
            {
                return &c.second;
            }
        }
        return nullptr;
    }
    std::string basisFor(const std::string& path) const
    {
        for (const auto& b : simBasis)
        {
            if (b.first == path)
            {
                return b.second;
            }
        }
        return "game_assumption: non documenté dans sim_basis";
    }
};
} // namespace noctis
