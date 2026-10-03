#include "Noctis/World/TerrainGenerator.h"

#include "PolylineField.h"

#include "Noctis/Core/Log.h"
#include "Noctis/Core/Noise.h"
#include "Noctis/Core/Random.h"

#include <cmath>
#include <limits>

namespace noctis
{
namespace terraingenimpl
{
using namespace polylinefield;

Polyline makeSineGeneratedRiver(const GradientNoise2D& noise, float size, float axisPhase, float omega, float arcWavelength,
                                float phase0, Vec2 start, float stepM)
{
    Polyline line;
    Vec2 pos = start;
    float s = 0.0f;
    float phase = phase0;
    const float beltHalf = size * 0.14f;
    for (int step = 0; step < 200000 && pos.x < size + 300.0f; ++step)
    {
        line.p.push_back(pos);
        const float axisY = TerrainGenerator::valleyAxisY(pos.x, size, axisPhase);
        const float axisSlope = (TerrainGenerator::valleyAxisY(pos.x + 10.0f, size, axisPhase) - axisY) / 10.0f;
        const float thetaAxis = std::atan(axisSlope);
        const float w = omega * (1.0f + 0.25f * noise.sample(s / 3000.0f, 3.7f));
        const float m = arcWavelength * (1.0f + 0.2f * noise.sample(s / 4000.0f, 11.3f));
        phase += kTwoPi * stepM / m;
        float theta = thetaAxis + w * std::sin(phase) - 0.35f * (pos.y - axisY) / beltHalf;
        // Never flow backwards out of the valley (keeps the generated river well-posed).
        theta = clampf(theta, -2.6f, 2.6f);
        pos += Vec2::fromHeading(theta) * stepM;
        s += stepM;
    }
    line.finalize();
    return line;
}
} // namespace terraingenimpl

float TerrainGenerator::valleyAxisY(float x, float size, float phase)
{
    return size * 0.5f + size * 0.07f * std::sin(kTwoPi * x / (1.35f * size) + phase);
}

namespace terraingenimpl
{
WorldLayout generateFloodplain(const EnvironmentSim& env, u64 seed, Terrain& terrain)
{
    WorldLayout layout;
    layout.generator = "meandering_floodplain";
    Rng rng(seed, 101);
    const GradientNoise2D noise(hashCombine(seed, 1));
    const GradientNoise2D noiseB(hashCombine(seed, 2));
    const float size = terrain.size();
    const float axisPhase = rng.range(0.0f, kTwoPi);

    // --- Channel geometry (game assumptions documented in Data/Science/sim_constants.json "fluvial") ---
    const float width = 55.0f;
    const float depth = 4.0f;
    const float omega = 1.55f;                        // max deflection angle (rad) of the sine-generated curve
    const float wavelength = 12.0f * width;           // meander wavelength ~ 10–14 channel widths
    const float sinuosity = 2.0f;                     // ~1/J0(omega) for omega ~ 1.5 rad
    const float arcWavelength = wavelength * sinuosity;
    const float gradient = 0.0004f;                   // valley slope (m/m), low-gradient coastal-plain river
    const float relief = std::max(10.0f, env.reliefM);
    const float baseZ = 20.0f;

    auto floodplainZ = [&](float x) { return baseZ + (size - x) * gradient; };

    Polyline river = makeSineGeneratedRiver(noise, size, axisPhase, omega, arcWavelength, rng.range(0.0f, kTwoPi),
                                            {-300.0f, TerrainGenerator::valleyAxisY(-300.0f, size, axisPhase)}, 2.0f);

    // Material grid spec (cell centres) for distance fields.
    const Grid2D<float>& spec = terrain.moisture();
    DistField riverField;
    buildDistField(river, spec, riverField);

    // --- Abandoned meanders -> oxbow lakes ---
    Polyline paleo = makeSineGeneratedRiver(noiseB, size, axisPhase, omega * 1.08f, arcWavelength * 0.9f, rng.range(0.0f, kTwoPi),
                                            {-300.0f, TerrainGenerator::valleyAxisY(-300.0f, size, axisPhase) + rng.range(-120.0f, 120.0f)}, 2.0f);
    Polyline oxbows;
    std::vector<int> oxbowLakeIndex;
    {
        const float arcHalf = arcWavelength * 0.2f;
        const size_t n = paleo.p.size();
        size_t i = 1;
        while (i + 1 < n && layout.lakes.size() < 5)
        {
            const bool apex = std::fabs(paleo.kappa[i]) > 0.0025f && std::fabs(paleo.kappa[i]) >= std::fabs(paleo.kappa[i - 1]) &&
                              std::fabs(paleo.kappa[i]) >= std::fabs(paleo.kappa[i + 1]);
            if (!apex)
            {
                ++i;
                continue;
            }
            // Collect arc around apex and check clearance from the active channel.
            std::vector<Vec2> arc;
            bool ok = true;
            for (size_t k = i; k > 0 && paleo.s[i] - paleo.s[k] < arcHalf; --k)
            {
                arc.insert(arc.begin(), paleo.p[k]);
            }
            for (size_t k = i + 1; k < n && paleo.s[k] - paleo.s[i] < arcHalf; ++k)
            {
                arc.push_back(paleo.p[k]);
            }
            for (const Vec2& q : arc)
            {
                if (!terrain.bounds().contains(q) || q.x < 150.0f || q.y < 150.0f || q.x > size - 150.0f || q.y > size - 150.0f)
                {
                    ok = false;
                    break;
                }
                if (riverField.dist.sampleNearest(q) < width * 2.5f + 60.0f)
                {
                    ok = false;
                    break;
                }
                for (const Vec2& existing : oxbows.p)
                {
                    if (distanceSq(existing, q) < 250.0f * 250.0f)
                    {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                {
                    break;
                }
            }
            if (ok && arc.size() > 20)
            {
                LakeDesc lake;
                lake.name = "Bras mort " + std::to_string(layout.lakes.size() + 1);
                lake.centerline = arc;
                lake.center = arc[arc.size() / 2];
                lake.halfWidth = 22.0f + rng.range(-4.0f, 6.0f);
                lake.maxDepth = 2.2f + rng.range(-0.4f, 0.6f);
                lake.ephemeral = false;
                const int lakeId = static_cast<int>(layout.lakes.size());
                layout.lakes.push_back(lake);
                for (const Vec2& q : arc)
                {
                    oxbows.p.push_back(q);
                    oxbowLakeIndex.push_back(lakeId);
                }
                i += arc.size();
            }
            else
            {
                i += 10;
            }
        }
        oxbows.finalize();
    }
    DistField oxbowField;
    if (!oxbows.p.empty())
    {
        buildDistField(oxbows, spec, oxbowField);
    }

    // --- Tributary creek from the northern valley margin ---
    Polyline creek;
    {
        Vec2 pos{size * rng.range(0.3f, 0.7f), size - 20.0f};
        float phase = rng.range(0.0f, kTwoPi);
        float s = 0.0f;
        for (int step = 0; step < 4000; ++step)
        {
            creek.p.push_back(pos);
            if (riverField.dist.sampleNearest(pos) < width * 0.4f || !terrain.bounds().contains(pos))
            {
                break;
            }
            phase += kTwoPi * 2.0f / 320.0f;
            const float theta = -kHalfPi + 0.85f * std::sin(phase) + 0.2f * noise.sample(s / 500.0f, 5.5f);
            pos += Vec2::fromHeading(theta) * 2.0f;
            s += 2.0f;
        }
        creek.finalize();
    }
    DistField creekField;
    buildDistField(creek, spec, creekField);
    const float creekWidth = 9.0f;
    const float creekDepth = 1.4f;

    // --- Exposure sites: eroding outer banks at strongly curved bends ---
    std::vector<std::pair<Vec2, Vec2>> bluffs; // position, outward normal
    {
        for (size_t i = 5; i + 5 < river.p.size(); i += 5)
        {
            const float k = river.kappa[i];
            if (std::fabs(k) < 0.004f || std::fabs(k) < std::fabs(river.kappa[i - 5]) || std::fabs(k) < std::fabs(river.kappa[i + 5]))
            {
                continue;
            }
            const Vec2 left = river.t[i].perp();
            const Vec2 outward = k > 0.0f ? -left : left; // outer bank is opposite the turn direction
            const Vec2 site = river.p[i] + outward * (width * 0.5f + 6.0f);
            if (!terrain.bounds().contains(site) || site.x < 200.0f || site.x > size - 200.0f)
            {
                continue;
            }
            bool farFromOthers = true;
            for (const auto& b : bluffs)
            {
                if (distanceSq(b.first, site) < 900.0f * 900.0f)
                {
                    farFromOthers = false;
                    break;
                }
            }
            if (farFromOthers && bluffs.size() < 7)
            {
                bluffs.push_back({site, outward});
                layout.exposureSites.push_back(site + outward * 8.0f);
            }
        }
    }

    // --- Crevasse splays ---
    for (int c = 0; c < 3 && river.p.size() > 100; ++c)
    {
        const size_t i = static_cast<size_t>(rng.rangeInt(50, static_cast<int>(river.p.size()) - 50));
        const Vec2 left = river.t[i].perp();
        const Vec2 outward = river.kappa[i] > 0.0f ? -left : left;
        const Vec2 site = river.p[i] + outward * (width * 0.5f + 40.0f);
        if (terrain.bounds().contains(site))
        {
            layout.crevasseSplays.push_back(site);
        }
    }

    // --- Heights ---
    Grid2D<float>& heights = terrain.heights();
    const int hs = heights.width();
    const float hspacing = terrain.spacing();
    const float leveeHeight = 1.3f;
    const float leveeLength = 130.0f;
    const float floodHalf = size * 0.30f;
    for (int j = 0; j < hs; ++j)
    {
        for (int i = 0; i < hs; ++i)
        {
            const Vec2 p{terrain.origin().x + i * hspacing, terrain.origin().y + j * hspacing};
            const float fz = floodplainZ(p.x);
            float z = fz;
            // Valley margins: terraces rising to the uplands.
            const float dAxis = std::fabs(p.y - TerrainGenerator::valleyAxisY(p.x, size, axisPhase));
            const float terraceT = smoothstep(floodHalf, floodHalf + size * 0.16f, dAxis);
            const float terraceNoise = 0.75f + 0.25f * noise.fbm(p.x / 900.0f, p.y / 900.0f, 4);
            z += relief * terraceT * terraceNoise;
            z += terraceT * 4.0f * noise.ridged(p.x / 400.0f, p.y / 400.0f, 4);
            // Floodplain micro-relief.
            z += 0.6f * noise.fbm(p.x / 280.0f + 7.1f, p.y / 280.0f, 4);

            const Nearest nr = nearestOn(river, riverField, p);
            const float dR = nr.dist;
            // Natural levees.
            z += leveeHeight * std::exp(-std::max(0.0f, dR - width * 0.5f) / leveeLength) * (1.0f - terraceT);
            // Backswamps: low, distal floodplain.
            const float swampN = noise.fbm(p.x / 700.0f + 3.0f, p.y / 700.0f - 9.0f, 3);
            z -= 0.9f * smoothstep(450.0f, 1100.0f, dR) * smoothstep(0.0f, 0.35f, swampN) * (1.0f - terraceT);

            // Bluffs (eroding outer banks).
            for (const auto& b : bluffs)
            {
                const Vec2 rel = p - b.first;
                const float along = dot(rel, b.second);
                const float lat = std::fabs(cross(b.second, rel));
                if (along > -2.0f && along < 90.0f && lat < 120.0f)
                {
                    const float bump = 3.5f * smoothstep(-2.0f, 6.0f, along) * (1.0f - smoothstep(60.0f, 90.0f, along)) *
                                       (1.0f - smoothstep(70.0f, 120.0f, lat));
                    z += bump;
                }
            }
            // Crevasse splays: low sandy lobes.
            for (const Vec2& c : layout.crevasseSplays)
            {
                const float d = distance(p, c);
                if (d < 260.0f)
                {
                    z += 0.45f * (1.0f - smoothstep(0.0f, 260.0f, d));
                }
            }

            // Main channel with asymmetric cross-section (thalweg towards the outer bank) and point bars.
            if (nr.index >= 0 && dR < width * 1.6f)
            {
                const float k = river.kappa[static_cast<size_t>(nr.index)];
                const float half = width * 0.5f;
                const float shift = -0.28f * width * clampf(k * 250.0f, -1.0f, 1.0f); // towards outer bank
                const float n = (nr.lateral - shift) / half;
                const bool innerSide = (k > 0.0f && nr.lateral > 0.0f) || (k < 0.0f && nr.lateral < 0.0f);
                if (dR < half)
                {
                    const float profile = innerSide ? std::max(0.0f, 1.0f - std::fabs(n)) : std::max(0.0f, 1.0f - n * n * n * n);
                    const float bed = fz - depth * std::max(profile, 0.08f);
                    z = std::min(z, bed + 0.05f);
                }
                else if (innerSide && std::fabs(k) > 0.0012f)
                {
                    // Point bar: sandy slope from the water's edge up to the floodplain.
                    const float t = (dR - half) / width;
                    z = std::min(z, fz - 1.6f * (1.0f - saturate(t)));
                }
            }
            // Oxbow lakes.
            if (!oxbows.p.empty())
            {
                const Nearest no = nearestOn(oxbows, oxbowField, p);
                if (no.index >= 0)
                {
                    const LakeDesc& lake = layout.lakes[static_cast<size_t>(oxbowLakeIndex[static_cast<size_t>(no.index)])];
                    if (no.dist < lake.halfWidth * 1.6f)
                    {
                        const float t = no.dist / lake.halfWidth;
                        const float bed = fz - 0.6f - lake.maxDepth * std::max(0.0f, 1.0f - t * t);
                        if (t < 1.0f)
                        {
                            z = std::min(z, bed);
                        }
                        else
                        {
                            z = std::min(z, lerpf(fz - 0.6f, z, saturate(t - 1.0f) / 0.6f));
                        }
                    }
                }
            }
            // Tributary creek.
            {
                const Nearest nc = nearestOn(creek, creekField, p);
                if (nc.index >= 0 && nc.dist < creekWidth * 1.5f)
                {
                    const float t = nc.dist / (creekWidth * 0.5f);
                    if (t < 1.0f)
                    {
                        z = std::min(z, z - creekDepth * (1.0f - t * t));
                    }
                }
            }
            heights.at(i, j) = z;
        }
    }

    // Lake spill heights (absolute) = surrounding floodplain level near the lake centre.
    for (LakeDesc& lake : layout.lakes)
    {
        lake.spillHeight = floodplainZ(lake.center.x) - 0.6f;
    }

    // --- River description for the water system ---
    RiverDesc rd;
    rd.name = "Rivière principale";
    rd.meanDischargeM3s = 60.0f;
    for (size_t i = 0; i < river.p.size(); ++i)
    {
        RiverPoint rp;
        rp.position = river.p[i];
        rp.tangent = river.t[i];
        rp.width = width;
        rp.depth = depth;
        rp.curvature = river.kappa[i];
        rp.floodplainZ = floodplainZ(river.p[i].x);
        rp.distanceAlong = river.s[i];
        rd.points.push_back(rp);
    }
    layout.rivers.push_back(rd);
    RiverDesc cd;
    cd.name = "Ruisseau affluent";
    cd.meanDischargeM3s = 1.5f;
    cd.perennial = false;
    for (size_t i = 0; i < creek.p.size(); ++i)
    {
        RiverPoint rp;
        rp.position = creek.p[i];
        rp.tangent = creek.t[i];
        rp.width = creekWidth;
        rp.depth = creekDepth;
        rp.curvature = creek.kappa[i];
        rp.floodplainZ = terrain.heightAt(creek.p[i]) + creekDepth;
        rp.distanceAlong = creek.s[i];
        cd.points.push_back(rp);
    }
    // Make the creek's reference level monotonic downstream.
    for (size_t i = 1; i < cd.points.size(); ++i)
    {
        cd.points[i].floodplainZ = std::min(cd.points[i].floodplainZ, cd.points[i - 1].floodplainZ);
    }
    layout.rivers.push_back(cd);

    // --- Habitats, substrates, wetness ---
    Grid2D<u8>& habitats = terrain.habitats();
    Grid2D<u8>& substrates = terrain.substrates();
    Grid2D<float>& wet = terrain.baseWetness();
    const float stageNominal = depth * 0.6f;
    for (int y = 0; y < habitats.height(); ++y)
    {
        for (int x = 0; x < habitats.width(); ++x)
        {
            const Vec2 p = habitats.cellCenter(x, y);
            const float h = terrain.heightAt(p);
            const float slope = terrain.slopeDegAt(p);
            const Nearest nr = nearestOn(river, riverField, p);
            const float k = nr.index >= 0 ? river.kappa[static_cast<size_t>(nr.index)] : 0.0f;
            const float riverSurface = floodplainZ(nr.index >= 0 ? river.p[static_cast<size_t>(nr.index)].x : p.x) - depth + stageNominal;
            const float har = h - riverSurface;
            const bool innerSide = (k > 0.0f && nr.lateral > 0.0f) || (k < 0.0f && nr.lateral < 0.0f);
            const float dR = nr.dist;
            float dO = 1e9f;
            float lakeHalf = 25.0f;
            if (!oxbows.p.empty())
            {
                const Nearest no = nearestOn(oxbows, oxbowField, p);
                if (no.index >= 0)
                {
                    dO = no.dist;
                    lakeHalf = layout.lakes[static_cast<size_t>(oxbowLakeIndex[static_cast<size_t>(no.index)])].halfWidth;
                }
            }
            const Nearest nc = nearestOn(creek, creekField, p);
            bool nearBluff = false;
            for (const auto& b : bluffs)
            {
                if (distanceSq(p, b.first + b.second * 20.0f) < 70.0f * 70.0f)
                {
                    nearBluff = true;
                    break;
                }
            }
            const float mosaic = noise.fbm(p.x / 420.0f + 13.0f, p.y / 420.0f + 5.0f, 4) + 0.35f * std::exp(-dR / 650.0f);
            const float n2 = noiseB.sample(p.x / 60.0f, p.y / 60.0f);

            Habitat hab = Habitat::FloodplainOpen;
            Substrate sub = Substrate::Silt;
            float w = clampf(std::exp(-std::max(0.0f, har) / 2.5f), 0.05f, 1.0f);

            if (dR < width * 0.47f)
            {
                hab = Habitat::Channel;
                sub = n2 > 0.3f ? Substrate::Gravel : Substrate::Sand;
                w = 1.0f;
            }
            else if (dO < lakeHalf * 0.9f)
            {
                hab = Habitat::OxbowLake;
                sub = Substrate::Mud;
                w = 1.0f;
            }
            else if (nc.index >= 0 && nc.dist < creekWidth * 0.5f)
            {
                hab = Habitat::Channel;
                sub = Substrate::Sand;
                w = 1.0f;
            }
            else if (nearBluff || slope > 32.0f)
            {
                hab = Habitat::CutbankCliff;
                sub = slope > 32.0f ? Substrate::Rock : Substrate::Clay;
                w = 0.3f;
            }
            else if (innerSide && dR < width * 1.5f && std::fabs(k) > 0.0012f)
            {
                hab = Habitat::Sandbar;
                sub = Substrate::Sand;
                w = std::max(w, 0.7f);
            }
            else if (dR < width * 0.5f + 25.0f || (nc.index >= 0 && nc.dist < creekWidth * 0.5f + 8.0f))
            {
                hab = Habitat::Riverbank;
                sub = n2 > 0.0f ? Substrate::Mud : Substrate::Silt;
                w = std::max(w, 0.85f);
            }
            else if (dO < lakeHalf + 45.0f)
            {
                hab = Habitat::Backswamp;
                sub = Substrate::Mud;
                w = std::max(w, 0.9f);
            }
            else if (har > 7.0f)
            {
                hab = Habitat::UplandConiferForest;
                sub = Substrate::ForestSoil;
            }
            else if (dR < 240.0f && har > 0.6f)
            {
                hab = Habitat::LeveeWoodland;
                sub = Substrate::Silt;
            }
            else if (har < 1.4f && dR > 400.0f)
            {
                hab = Habitat::Backswamp;
                sub = n2 > 0.2f ? Substrate::Peat : Substrate::Clay;
                w = std::max(w, 0.85f);
            }
            else if (mosaic > 0.05f)
            {
                hab = Habitat::RiparianForest;
                sub = Substrate::ForestSoil;
            }
            else
            {
                hab = Habitat::FloodplainOpen;
                sub = n2 > 0.4f ? Substrate::Clay : Substrate::Silt;
            }
            for (const Vec2& c : layout.crevasseSplays)
            {
                if (distanceSq(p, c) < 200.0f * 200.0f && hab != Habitat::Channel)
                {
                    sub = Substrate::Sand;
                }
            }
            habitats.at(x, y) = static_cast<u8>(hab);
            substrates.at(x, y) = static_cast<u8>(sub);
            wet.at(x, y) = w;
            terrain.moisture().at(x, y) = w;
        }
    }
    layout.axisAngle = 0.0f;
    return layout;
}

WorldLayout generateDuneField(const EnvironmentSim& env, u64 seed, Terrain& terrain)
{
    WorldLayout layout;
    layout.generator = "dune_field";
    Rng rng(seed, 202);
    const GradientNoise2D noise(hashCombine(seed, 11));
    const GradientNoise2D noiseB(hashCombine(seed, 12));
    const float size = terrain.size();
    // Wind blows FROM prevailingWindDeg (meteorological, clockwise from north) -> towards.
    const float fromRad = env.prevailingWindDeg * kDegToRad;
    const Vec2 windFrom{std::sin(fromRad), std::cos(fromRad)};
    const Vec2 downwind = -windFrom;
    const Vec2 across = downwind.perp();
    const float wavelength = 320.0f;  // game assumption (dune spacing)
    const float amplitude = 14.0f;    // game assumption (dune height)
    const float relief = std::max(5.0f, env.reliefM);

    // Ephemeral ponds in interdune lows.
    std::vector<std::pair<Vec2, float>> ponds;
    for (int attempt = 0; attempt < 400 && ponds.size() < 7; ++attempt)
    {
        const Vec2 c{rng.range(400.0f, size - 400.0f), rng.range(400.0f, size - 400.0f)};
        const float mask = smoothstep(-0.05f, 0.35f, noise.fbm(c.x / 1600.0f, c.y / 1600.0f, 4));
        if (mask > 0.15f)
        {
            continue;
        }
        bool far = true;
        for (const auto& p : ponds)
        {
            if (distanceSq(p.first, c) < 900.0f * 900.0f)
            {
                far = false;
                break;
            }
        }
        if (far)
        {
            ponds.push_back({c, rng.range(35.0f, 90.0f)});
        }
    }

    Grid2D<float>& heights = terrain.heights();
    const int hs = heights.width();
    const float sp = terrain.spacing();
    for (int j = 0; j < hs; ++j)
    {
        for (int i = 0; i < hs; ++i)
        {
            const Vec2 p{terrain.origin().x + i * sp, terrain.origin().y + j * sp};
            float z = 30.0f + relief * 0.5f * noise.fbm(p.x / 2500.0f, p.y / 2500.0f, 3);
            const float mask = smoothstep(-0.05f, 0.35f, noise.fbm(p.x / 1600.0f, p.y / 1600.0f, 4));
            const float u = dot(p, downwind);
            const float v = dot(p, across);
            const float ph = u / wavelength + 0.35f * noiseB.fbm(v / 900.0f, u / 1400.0f, 3);
            const float f = ph - std::floor(ph);
            // Asymmetric profile: long gentle stoss slope, short steep lee face near angle of repose.
            const float shape = f < 0.78f ? smoothstep(0.0f, 1.0f, f / 0.78f) : 1.0f - smoothstep(0.0f, 1.0f, (f - 0.78f) / 0.22f);
            const float crestVar = 0.7f + 0.3f * noiseB.sample(v / 400.0f, 1.3f);
            z += amplitude * mask * shape * crestVar;
            for (const auto& pond : ponds)
            {
                const float d = distance(p, pond.first);
                if (d < pond.second * 1.5f)
                {
                    z -= 1.6f * (1.0f - smoothstep(0.0f, pond.second * 1.5f, d));
                }
            }
            heights.at(i, j) = z;
        }
    }

    for (size_t k = 0; k < ponds.size(); ++k)
    {
        LakeDesc lake;
        lake.name = "Mare temporaire " + std::to_string(k + 1);
        lake.center = ponds[k].first;
        lake.centerline = {ponds[k].first};
        lake.halfWidth = ponds[k].second;
        lake.maxDepth = 1.2f;
        lake.ephemeral = true;
        lake.spillHeight = terrain.heightAt(ponds[k].first) + 1.1f;
        layout.lakes.push_back(lake);
    }

    Grid2D<u8>& habitats = terrain.habitats();
    Grid2D<u8>& substrates = terrain.substrates();
    Grid2D<float>& wet = terrain.baseWetness();
    for (int y = 0; y < habitats.height(); ++y)
    {
        for (int x = 0; x < habitats.width(); ++x)
        {
            const Vec2 p = habitats.cellCenter(x, y);
            const float mask = smoothstep(-0.05f, 0.35f, noise.fbm(p.x / 1600.0f, p.y / 1600.0f, 4));
            Habitat hab = mask > 0.35f ? Habitat::DuneField : Habitat::InterduneFlat;
            Substrate sub = mask > 0.35f ? Substrate::Sand : Substrate::Silt;
            float w = 0.1f;
            for (const auto& pond : ponds)
            {
                const float d = distance(p, pond.first);
                if (d < pond.second)
                {
                    hab = Habitat::EphemeralPond;
                    sub = Substrate::Mud;
                    w = 0.9f;
                }
                else if (d < pond.second * 2.2f && hab != Habitat::EphemeralPond)
                {
                    hab = Habitat::XericScrub;
                    sub = Substrate::Clay;
                    w = std::max(w, 0.45f);
                }
            }
            if (hab == Habitat::InterduneFlat && noiseB.fbm(p.x / 500.0f, p.y / 500.0f, 3) > 0.15f)
            {
                hab = Habitat::XericScrub;
                w = std::max(w, 0.25f);
            }
            habitats.at(x, y) = static_cast<u8>(hab);
            substrates.at(x, y) = static_cast<u8>(sub);
            wet.at(x, y) = w;
            terrain.moisture().at(x, y) = w;
        }
    }
    // Deflation hollows in interdune flats expose older beds.
    for (int attempt = 0; attempt < 300 && layout.exposureSites.size() < 6; ++attempt)
    {
        const Vec2 c{rng.range(300.0f, size - 300.0f), rng.range(300.0f, size - 300.0f)};
        if (terrain.habitatAt(c) == Habitat::InterduneFlat)
        {
            bool far = true;
            for (const Vec2& e : layout.exposureSites)
            {
                if (distanceSq(e, c) < 1000.0f * 1000.0f)
                {
                    far = false;
                }
            }
            if (far)
            {
                layout.exposureSites.push_back(c);
            }
        }
    }
    layout.axisAngle = downwind.heading();
    return layout;
}
} // namespace terraingenimpl

WorldLayout TerrainGenerator::generate(const EnvironmentSim& env, u64 seed, Terrain& terrain, int heightSamples, int materialCells)
{
    terrain.create(heightSamples, env.mapSizeM, Vec2{0.0f, 0.0f}, materialCells);
    if (env.terrainGenerator == "dune_field")
    {
        return terraingenimpl::generateDuneField(env, seed, terrain);
    }
    if (env.terrainGenerator != "meandering_floodplain")
    {
        NOCTIS_LOG_WARN("Unknown terrain generator '%s', using meandering_floodplain", env.terrainGenerator.c_str());
    }
    return terraingenimpl::generateFloodplain(env, seed, terrain);
}
} // namespace noctis
