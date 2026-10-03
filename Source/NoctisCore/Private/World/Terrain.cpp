#include "Noctis/World/Terrain.h"

#include <cmath>

namespace noctis
{
namespace terrainimpl
{
// Dry and saturated end-members per substrate. Bekker values are approximations of published
// terramechanics tables (Bekker; Wong, Theory of Ground Vehicles) — see Data/Science/sim_constants.json
// "terramechanics" for the basis and verification status. Traction/rolling values are game assumptions.
struct SoilPair
{
    SoilMechanics dry;
    SoilMechanics wet;
};

const SoilPair kSoils[kSubstrateCount] = {
    // Rock: effectively rigid.
    {{1.0f, 1000.0f, 100000.0f, 500.0f, 40.0f, 0.75f, 0.015f, 0.0f, 0.0f}, {1.0f, 1000.0f, 100000.0f, 400.0f, 38.0f, 0.55f, 0.015f, 0.0f, 0.0f}},
    // Gravel
    {{1.1f, 2.0f, 3000.0f, 0.5f, 38.0f, 0.65f, 0.04f, 0.05f, 0.03f}, {1.0f, 2.0f, 2500.0f, 0.5f, 36.0f, 0.55f, 0.05f, 0.1f, 0.06f}},
    // Sand (dry sand / saturated sand)
    {{1.1f, 0.99f, 1528.0f, 1.0f, 28.0f, 0.5f, 0.12f, 0.2f, 0.2f}, {0.9f, 5.0f, 1200.0f, 2.0f, 30.0f, 0.55f, 0.08f, 0.6f, 0.25f}},
    // Silt (sandy loam end-members)
    {{0.7f, 5.27f, 1515.0f, 1.7f, 29.0f, 0.6f, 0.06f, 0.5f, 0.12f}, {0.3f, 2.79f, 141.0f, 1.4f, 25.0f, 0.4f, 0.14f, 0.85f, 0.35f}},
    // Mud (very soft, saturated fines)
    {{0.5f, 13.2f, 692.0f, 4.1f, 13.0f, 0.45f, 0.12f, 0.8f, 0.15f}, {0.3f, 2.0f, 200.0f, 2.0f, 5.0f, 0.18f, 0.3f, 0.9f, 0.6f}},
    // Clay (lean/heavy clay end-members)
    {{0.13f, 12.7f, 1556.0f, 69.0f, 34.0f, 0.65f, 0.04f, 0.6f, 0.05f}, {0.11f, 1.84f, 103.0f, 20.7f, 6.0f, 0.2f, 0.2f, 0.95f, 0.4f}},
    // Forest soil (organic loam, root-bound)
    {{0.6f, 8.0f, 1200.0f, 6.0f, 30.0f, 0.6f, 0.07f, 0.3f, 0.08f}, {0.4f, 4.0f, 400.0f, 4.0f, 22.0f, 0.4f, 0.12f, 0.5f, 0.15f}},
    // Peat (low bearing, spongy)
    {{0.5f, 2.0f, 300.0f, 3.0f, 20.0f, 0.45f, 0.15f, 0.4f, 0.25f}, {0.4f, 0.8f, 60.0f, 1.5f, 10.0f, 0.25f, 0.3f, 0.5f, 0.7f}},
};

SoilMechanics blendSoil(const SoilMechanics& a, const SoilMechanics& b, float t)
{
    SoilMechanics r;
    r.n = lerpf(a.n, b.n, t);
    // Moduli blend geometrically: they span orders of magnitude.
    r.kc = std::exp(lerpf(std::log(a.kc), std::log(b.kc), t));
    r.kphi = std::exp(lerpf(std::log(a.kphi), std::log(b.kphi), t));
    r.cohesionKPa = lerpf(a.cohesionKPa, b.cohesionKPa, t);
    r.frictionAngleDeg = lerpf(a.frictionAngleDeg, b.frictionAngleDeg, t);
    r.tractionMu = lerpf(a.tractionMu, b.tractionMu, t);
    r.rollingResistance = lerpf(a.rollingResistance, b.rollingResistance, t);
    r.trackRetention = lerpf(a.trackRetention, b.trackRetention, t);
    r.maxSinkageM = lerpf(a.maxSinkageM, b.maxSinkageM, t);
    return r;
}
} // namespace terrainimpl

const char* substrateKey(Substrate s)
{
    switch (s)
    {
    case Substrate::Rock: return "rock";
    case Substrate::Gravel: return "gravel";
    case Substrate::Sand: return "sand";
    case Substrate::Silt: return "silt";
    case Substrate::Mud: return "mud";
    case Substrate::Clay: return "clay";
    case Substrate::ForestSoil: return "forest_soil";
    case Substrate::Peat: return "peat";
    case Substrate::Count: break;
    }
    return "unknown";
}

const char* substrateLabelFr(Substrate s)
{
    switch (s)
    {
    case Substrate::Rock: return "Roche";
    case Substrate::Gravel: return "Graviers";
    case Substrate::Sand: return "Sable";
    case Substrate::Silt: return "Limon";
    case Substrate::Mud: return "Boue";
    case Substrate::Clay: return "Argile";
    case Substrate::ForestSoil: return "Sol forestier";
    case Substrate::Peat: return "Tourbe";
    case Substrate::Count: break;
    }
    return "?";
}

SoilMechanics soilMechanics(Substrate s, float moisture01)
{
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSubstrateCount)
    {
        return SoilMechanics{};
    }
    // Mechanical weakening is non-linear: soils keep their strength until fairly wet.
    const float t = smoothstep(0.25f, 0.95f, saturate(moisture01));
    return terrainimpl::blendSoil(terrainimpl::kSoils[i].dry, terrainimpl::kSoils[i].wet, t);
}

void Terrain::create(int samplesPerSide, float sizeM, Vec2 origin, int materialCellsPerSide)
{
    samples_ = samplesPerSide;
    size_ = sizeM;
    origin_ = origin;
    spacing_ = sizeM / static_cast<float>(samplesPerSide - 1);
    height_.resize(samplesPerSide, samplesPerSide, spacing_, origin, 0.0f);
    const float matCell = sizeM / static_cast<float>(materialCellsPerSide);
    substrate_.resize(materialCellsPerSide, materialCellsPerSide, matCell, origin, static_cast<u8>(Substrate::Silt));
    habitat_.resize(materialCellsPerSide, materialCellsPerSide, matCell, origin, static_cast<u8>(Habitat::FloodplainOpen));
    moisture_.resize(materialCellsPerSide, materialCellsPerSide, matCell, origin, 0.5f);
    baseWetness_.resize(materialCellsPerSide, materialCellsPerSide, matCell, origin, 0.5f);
}

Vec2 Terrain::clampToBounds(const Vec2& p, float margin) const
{
    return {clampf(p.x, origin_.x + margin, origin_.x + size_ - margin), clampf(p.y, origin_.y + margin, origin_.y + size_ - margin)};
}

float Terrain::heightAt(const Vec2& p) const
{
    if (samples_ == 0)
    {
        return 0.0f;
    }
    const float fx = (p.x - origin_.x) / spacing_;
    const float fy = (p.y - origin_.y) / spacing_;
    const int i = static_cast<int>(std::floor(fx));
    const int j = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(i);
    const float ty = fy - static_cast<float>(j);
    const float a = sampleVertex(i, j);
    const float b = sampleVertex(i + 1, j);
    const float c = sampleVertex(i, j + 1);
    const float d = sampleVertex(i + 1, j + 1);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}

Vec3 Terrain::normalAt(const Vec2& p) const
{
    const float e = spacing_;
    const float hx0 = heightAt({p.x - e, p.y});
    const float hx1 = heightAt({p.x + e, p.y});
    const float hy0 = heightAt({p.x, p.y - e});
    const float hy1 = heightAt({p.x, p.y + e});
    return Vec3{(hx0 - hx1) / (2.0f * e), (hy0 - hy1) / (2.0f * e), 1.0f}.normalized();
}

float Terrain::slopeDegAt(const Vec2& p) const
{
    const Vec3 n = normalAt(p);
    return std::acos(clampf(n.z, -1.0f, 1.0f)) * kRadToDeg;
}

Substrate Terrain::substrateAt(const Vec2& p) const
{
    return static_cast<Substrate>(substrate_.sampleNearest(p));
}

Habitat Terrain::habitatAt(const Vec2& p) const
{
    return static_cast<Habitat>(habitat_.sampleNearest(p));
}

float Terrain::moistureAt(const Vec2& p) const
{
    return moisture_.sampleNearest(p);
}

TerrainQuery Terrain::query(const Vec2& p) const
{
    TerrainQuery q;
    q.height = heightAt(p);
    q.normal = normalAt(p);
    q.slopeDeg = std::acos(clampf(q.normal.z, -1.0f, 1.0f)) * kRadToDeg;
    q.substrate = substrateAt(p);
    q.habitat = habitatAt(p);
    q.moisture = moistureAt(p);
    return q;
}

float Terrain::sinkage(const Vec2& p, float pressureKPa, float contactWidthM) const
{
    const SoilMechanics s = soilMechanics(substrateAt(p), moistureAt(p));
    const float b = std::max(0.02f, contactWidthM);
    const float k = s.kc / b + s.kphi; // kN/m^(n+2) -> with p in kPa (kN/m^2), z in m
    if (k <= 0.0f || pressureKPa <= 0.0f)
    {
        return 0.0f;
    }
    const float z = std::pow(pressureKPa / k, 1.0f / std::max(0.05f, s.n));
    return clampf(z, 0.0f, s.maxSinkageM);
}

bool Terrain::lineOfSight(const Vec3& from, const Vec3& to, float stepM) const
{
    const float step = stepM > 0.0f ? stepM : spacing_;
    const Vec3 d = to - from;
    const float len = d.length();
    if (len < step)
    {
        return true;
    }
    const int n = static_cast<int>(len / step);
    for (int k = 1; k < n; ++k)
    {
        const float t = static_cast<float>(k) / static_cast<float>(n);
        const Vec3 p = from + d * t;
        if (heightAt(p.xy()) > p.z)
        {
            return false;
        }
    }
    return true;
}

void Terrain::updateMoisture(float dtHours, float rainMmPerHour, float potentialEvapMmPerHour)
{
    // Bucket model per cell: field capacity ~ 150 mm of plant-available water (game assumption);
    // wet habitats are buffered by groundwater (baseWetness acts as a floor).
    const float capacityMm = 150.0f;
    const float gain = rainMmPerHour * dtHours / capacityMm;
    const float loss = potentialEvapMmPerHour * dtHours / capacityMm;
    std::vector<float>& m = moisture_.raw();
    const std::vector<float>& w = baseWetness_.raw();
    for (size_t i = 0; i < m.size(); ++i)
    {
        const float floorWet = w[i] * 0.9f;
        float v = m[i] + gain - loss * (0.3f + 0.7f * m[i]);
        v = std::max(v, floorWet);
        m[i] = clampf(v, 0.0f, 1.0f);
    }
}

float Terrain::minHeight() const
{
    float v = 1e30f;
    for (const float h : height_.raw())
    {
        v = std::min(v, h);
    }
    return v;
}

float Terrain::maxHeight() const
{
    float v = -1e30f;
    for (const float h : height_.raw())
    {
        v = std::max(v, h);
    }
    return v;
}
} // namespace noctis
