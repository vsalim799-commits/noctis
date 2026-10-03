// Procedural landscape generation driven by the environment definition.
//
// "meandering_floodplain": a low-gradient alluvial valley crossed by a meandering river whose
// centreline follows Langbein & Leopold's sine-generated curve, with natural levees, point bars,
// cut banks, oxbow lakes on abandoned meanders, backswamps and valley-margin terraces — the
// fluvial architecture recorded by the Hell Creek Formation's channel sandstones and floodplain
// mudstones. "dune_field": wind-aligned dunes, interdune flats and ephemeral ponds (Djadochta).
#pragma once

#include "Noctis/Science/Environment.h"
#include "Noctis/World/Terrain.h"

#include <string>
#include <vector>

namespace noctis
{
struct RiverPoint
{
    Vec2 position;
    Vec2 tangent;
    float width = 50.0f;      // bankfull width (m)
    float depth = 4.0f;       // bankfull depth (m)
    float curvature = 0.0f;   // 1/m, positive = turning left (counter-clockwise)
    float floodplainZ = 0.0f; // bankfull water surface / floodplain reference level (m)
    float distanceAlong = 0.0f;
};

struct RiverDesc
{
    std::string name;
    std::vector<RiverPoint> points;
    float meanDischargeM3s = 50.0f;
    bool perennial = true;
};

struct LakeDesc
{
    std::string name;
    Vec2 center;
    std::vector<Vec2> centerline; // for oxbows: the abandoned meander arc
    float halfWidth = 25.0f;
    float maxDepth = 2.5f;
    float spillHeight = 0.0f;     // absolute height at which the lake overflows
    bool ephemeral = false;
};

struct WorldLayout
{
    std::string generator;
    std::vector<RiverDesc> rivers;
    std::vector<LakeDesc> lakes;
    std::vector<Vec2> exposureSites; // eroding banks / deflation hollows where older strata crop out
    std::vector<Vec2> crevasseSplays;
    float axisAngle = 0.0f;
};

class NOCTIS_API TerrainGenerator
{
public:
    // Fills terrain (heights, substrates, habitats, base wetness) and returns the hydrographic layout.
    static WorldLayout generate(const EnvironmentSim& env, u64 seed, Terrain& terrain, int heightSamples = 2017, int materialCells = 1024);

    // Valley axis position used by the floodplain generator (exposed for tests and tools).
    static float valleyAxisY(float x, float size, float phase);
};
} // namespace noctis
