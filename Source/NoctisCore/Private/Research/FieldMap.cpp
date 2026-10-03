#include "Noctis/Research/FieldMap.h"

#include <algorithm>
#include <cmath>

namespace noctis
{
const char* mapFeatureLabelFr(MapFeatureKind k)
{
    switch (k)
    {
    case MapFeatureKind::Water: return "Point d'eau";
    case MapFeatureKind::Nest: return "Nid";
    case MapFeatureKind::Carcass: return "Carcasse";
    case MapFeatureKind::FossilSite: return "Affleurement fossilifère";
    case MapFeatureKind::Trackway: return "Piste d'empreintes";
    case MapFeatureKind::Trail: return "Sentier animal";
    case MapFeatureKind::Sighting: return "Observation";
    case MapFeatureKind::Danger: return "Zone dangereuse";
    case MapFeatureKind::Event: return "Événement";
    case MapFeatureKind::Sensor: return "Capteur";
    case MapFeatureKind::Vehicle: return "Véhicule";
    case MapFeatureKind::Note: return "Note";
    }
    return "?";
}

void FieldMap::configure(const Rect2& bounds, float cellSizeM)
{
    bounds_ = bounds;
    cell_ = cellSizeM;
    w_ = std::max(1, static_cast<int>(std::ceil(bounds.size().x / cellSizeM)));
    h_ = std::max(1, static_cast<int>(std::ceil(bounds.size().y / cellSizeM)));
    explored_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0);
    effortS_.assign(explored_.size(), 0.0f);
}

int FieldMap::cellOf(const Vec2& p) const
{
    const int x = static_cast<int>((p.x - bounds_.min.x) / cell_);
    const int y = static_cast<int>((p.y - bounds_.min.y) / cell_);
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
    {
        return -1;
    }
    return y * w_ + x;
}

void FieldMap::reveal(const Vec3& eye, float radiusM, const Terrain& terrain, double time, float effortSeconds)
{
    (void)time;
    const int cx = static_cast<int>((eye.x - bounds_.min.x) / cell_);
    const int cy = static_cast<int>((eye.y - bounds_.min.y) / cell_);
    const int r = static_cast<int>(radiusM / cell_) + 1;
    for (int y = std::max(0, cy - r); y <= std::min(h_ - 1, cy + r); ++y)
    {
        for (int x = std::max(0, cx - r); x <= std::min(w_ - 1, cx + r); ++x)
        {
            const Vec2 c{bounds_.min.x + (static_cast<float>(x) + 0.5f) * cell_, bounds_.min.y + (static_cast<float>(y) + 0.5f) * cell_};
            const float d = distance(c, eye.xy());
            if (d > radiusM)
            {
                continue;
            }
            const size_t i = static_cast<size_t>(y * w_ + x);
            const Vec3 target{c, terrain.heightAt(c) + 1.0f};
            if (d < cell_ * 1.5f || terrain.lineOfSight(eye, target, std::max(8.0f, d / 25.0f)))
            {
                explored_[i] = 1;
                effortS_[i] += effortSeconds * (1.0f - d / radiusM);
            }
        }
    }
}

bool FieldMap::explored(const Vec2& p) const
{
    const int c = cellOf(p);
    return c >= 0 && explored_[static_cast<size_t>(c)] != 0;
}

float FieldMap::exploredFraction() const
{
    size_t n = 0;
    for (const u8 e : explored_)
    {
        n += e ? 1 : 0;
    }
    return explored_.empty() ? 0.0f : static_cast<float>(n) / static_cast<float>(explored_.size());
}

u32 FieldMap::addFeature(MapFeatureKind kind, const Vec2& pos, double time, const std::string& label, u32 ref, i16 species, float value,
                         float dedupeRadiusM)
{
    for (MapFeature& f : features_)
    {
        if (f.kind == kind && distanceSq(f.position, pos) < dedupeRadiusM * dedupeRadiusM && (ref == 0 || f.ref == ref))
        {
            f.time = time;
            f.value = value;
            return f.id;
        }
    }
    MapFeature f;
    f.id = nextId_++;
    f.kind = kind;
    f.position = pos;
    f.time = time;
    f.label = label;
    f.ref = ref;
    f.speciesIndex = species;
    f.value = value;
    features_.push_back(f);
    return f.id;
}

void FieldMap::addSighting(u32 catalogId, i16 species, const Vec2& pos, double time, u32 observationId)
{
    if (catalogId != 0)
    {
        sightings_[catalogId].push_back({pos, time, observationId});
    }
    addSpeciesSighting(species, pos);
}

void FieldMap::addSpeciesSighting(i16 species, const Vec2& pos)
{
    if (species < 0)
    {
        return;
    }
    std::vector<int>& counts = speciesCounts_[species];
    if (counts.empty())
    {
        counts.assign(explored_.size(), 0);
    }
    const int c = cellOf(pos);
    if (c >= 0)
    {
        ++counts[static_cast<size_t>(c)];
    }
}

TerritoryEstimate FieldMap::territory(u32 catalogId) const
{
    TerritoryEstimate t;
    auto it = sightings_.find(catalogId);
    if (it == sightings_.end() || it->second.size() < 3)
    {
        t.points = it == sightings_.end() ? 0 : static_cast<int>(it->second.size());
        return t;
    }
    std::vector<Vec2> pts;
    for (const Sighting& s : it->second)
    {
        pts.push_back(s.position);
        t.centroid += s.position;
    }
    t.points = static_cast<int>(pts.size());
    t.centroid = t.centroid / static_cast<float>(pts.size());
    // Monotone chain convex hull.
    std::sort(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    std::vector<Vec2> hull(pts.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        while (k >= 2 && cross(hull[k - 1] - hull[k - 2], pts[i] - hull[k - 2]) <= 0.0f)
        {
            --k;
        }
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lower = k + 1; i > 0; --i)
    {
        while (k >= lower && cross(hull[k - 1] - hull[k - 2], pts[i - 1] - hull[k - 2]) <= 0.0f)
        {
            --k;
        }
        hull[k++] = pts[i - 1];
    }
    hull.resize(k > 1 ? k - 1 : k);
    float area = 0.0f;
    for (size_t i = 0; i < hull.size(); ++i)
    {
        area += cross(hull[i], hull[(i + 1) % hull.size()]);
    }
    t.mcpAreaKm2 = std::fabs(area) * 0.5f / 1e6f;
    t.hull = hull;
    std::vector<float> d;
    for (const Sighting& s : it->second)
    {
        d.push_back(distance(s.position, t.centroid));
    }
    std::sort(d.begin(), d.end());
    t.radius95M = d[static_cast<size_t>(std::min<size_t>(d.size() - 1, static_cast<size_t>(0.95f * static_cast<float>(d.size()))))];
    t.valid = true;
    return t;
}

std::vector<float> FieldMap::encounterRate(i16 species) const
{
    std::vector<float> out(explored_.size(), 0.0f);
    auto it = speciesCounts_.find(species);
    if (it == speciesCounts_.end())
    {
        return out;
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (effortS_[i] > 60.0f)
        {
            out[i] = static_cast<float>(it->second[i]) / (effortS_[i] / 3600.0f);
        }
    }
    return out;
}
} // namespace noctis
