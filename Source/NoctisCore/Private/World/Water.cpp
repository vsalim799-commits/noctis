#include "Noctis/World/Water.h"

#include "PolylineField.h"

#include <cmath>

namespace noctis
{
namespace waterimpl
{
constexpr float kDryLevel = -1.0e6f;
constexpr float kReservoirSeconds = 3.0f * 86400.0f; // linear-reservoir constant (game assumption)
} // namespace waterimpl

void WaterSystem::build(const WorldLayout& layout, const Terrain& terrain)
{
    using namespace polylinefield;
    layout_ = layout;
    rivers_.clear();
    lakes_.clear();
    const Grid2D<float>& spec = terrain.moisture();
    cellKind_.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), static_cast<u8>(CellKind::Dry));
    cellRef_.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), -1);
    cellRiver_.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), 0);
    surface_.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), waterimpl::kDryLevel);
    riverDist_.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), 1e9f);

    for (size_t r = 0; r < layout.rivers.size(); ++r)
    {
        const RiverDesc& rd = layout.rivers[r];
        RiverState st;
        st.meanQ = rd.meanDischargeM3s;
        st.width = rd.points.empty() ? 10.0f : rd.points.front().width;
        st.bankfullDepth = rd.points.empty() ? 1.0f : rd.points.front().depth;
        st.q = rd.perennial ? st.meanQ : st.meanQ * 0.3f;
        st.storage = st.q * waterimpl::kReservoirSeconds;
        st.stage = st.bankfullDepth * 0.6f * std::pow(std::max(0.01f, st.q / st.meanQ), 0.4f);
        rivers_.push_back(st);

        Polyline line;
        for (const RiverPoint& p : rd.points)
        {
            line.p.push_back(p.position);
        }
        line.finalize();
        DistField field;
        buildDistField(line, spec, field);
        for (int y = 0; y < spec.height(); ++y)
        {
            for (int x = 0; x < spec.width(); ++x)
            {
                const Vec2 c = spec.cellCenter(x, y);
                const Nearest n = nearestOn(line, field, c);
                if (n.index < 0)
                {
                    continue;
                }
                if (r == 0)
                {
                    riverDist_.at(x, y) = n.dist;
                }
                const RiverPoint& rp = rd.points[static_cast<size_t>(n.index)];
                const u8 kind = cellKind_.at(x, y);
                if (n.dist < rp.width * 0.5f + 2.0f)
                {
                    cellKind_.at(x, y) = static_cast<u8>(CellKind::River);
                    cellRef_.at(x, y) = n.index;
                    cellRiver_.at(x, y) = static_cast<u8>(r);
                }
                else if (r == 0 && kind == static_cast<u8>(CellKind::Dry) && n.dist < 1400.0f &&
                         terrain.heightAt(c) < rp.floodplainZ + 1.2f)
                {
                    cellKind_.at(x, y) = static_cast<u8>(CellKind::Floodable);
                    cellRef_.at(x, y) = n.index;
                    cellRiver_.at(x, y) = 0;
                }
            }
        }
    }

    for (size_t l = 0; l < layout.lakes.size(); ++l)
    {
        const LakeDesc& lake = layout.lakes[l];
        LakeState ls;
        ls.maxDepth = lake.maxDepth;
        ls.ephemeral = lake.ephemeral;
        ls.depth = lake.maxDepth * (lake.ephemeral ? 0.5f : 0.75f);
        lakes_.push_back(ls);
        Rect2 box{lake.center, lake.center};
        for (const Vec2& q : lake.centerline)
        {
            box.min = Vec2{std::min(box.min.x, q.x), std::min(box.min.y, q.y)};
            box.max = Vec2{std::max(box.max.x, q.x), std::max(box.max.y, q.y)};
        }
        const float pad = lake.halfWidth * 1.2f;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        spec.worldToCell(box.min - Vec2{pad, pad}, x0, y0);
        spec.worldToCell(box.max + Vec2{pad, pad}, x1, y1);
        for (int y = std::max(0, y0); y <= std::min(spec.height() - 1, y1); ++y)
        {
            for (int x = std::max(0, x0); x <= std::min(spec.width() - 1, x1); ++x)
            {
                const Vec2 c = spec.cellCenter(x, y);
                float d = 1e9f;
                for (const Vec2& q : lake.centerline)
                {
                    d = std::min(d, distance(c, q));
                }
                if (d < lake.halfWidth)
                {
                    cellKind_.at(x, y) = static_cast<u8>(CellKind::Lake);
                    cellRef_.at(x, y) = static_cast<int>(l);
                }
            }
        }
    }

    // Drinking sites: gentle dry cells bordering water.
    drinkSites_.clear();
    for (int y = 1; y < spec.height() - 1; ++y)
    {
        for (int x = 1; x < spec.width() - 1; ++x)
        {
            const CellKind k = static_cast<CellKind>(cellKind_.at(x, y));
            if (k == CellKind::River || k == CellKind::Lake || ((x * 3 + y * 7) % 4) != 0)
            {
                continue;
            }
            int bestBody = -1;
            Vec2 waterDir;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const CellKind nk = static_cast<CellKind>(cellKind_.at(x + dx, y + dy));
                    if (nk == CellKind::River)
                    {
                        bestBody = cellRiver_.at(x + dx, y + dy);
                        waterDir = Vec2{static_cast<float>(dx), static_cast<float>(dy)};
                    }
                    else if (nk == CellKind::Lake)
                    {
                        bestBody = 1000 + cellRef_.at(x + dx, y + dy);
                        waterDir = Vec2{static_cast<float>(dx), static_cast<float>(dy)};
                    }
                }
            }
            if (bestBody < 0)
            {
                continue;
            }
            const Vec2 c = spec.cellCenter(x, y);
            const float slope = terrain.slopeDegAt(c);
            if (slope > 20.0f)
            {
                continue;
            }
            drinkSites_.push_back({c + waterDir.normalized() * (spec.cellSize() * 0.5f), bestBody, slope});
        }
    }
    drinkIndex_.configure(terrain.bounds(), 128.0f);
    for (size_t i = 0; i < drinkSites_.size(); ++i)
    {
        drinkIndex_.insert(static_cast<u32>(i), drinkSites_[i].position);
    }
    drinkIndex_.finalize();
    rebuildSurface(terrain);
}

void WaterSystem::update(float dtHours, float precipMmPerHour, float evapMmPerHour)
{
    const float dt = dtHours * 3600.0f;
    // Normalised runoff: long-run mean inflow equals the mean discharge when rain equals its mean rate.
    const float rainRatio = precipMmPerHour / std::max(1e-4f, meanRainRate_);
    flooding_ = false;
    for (size_t r = 0; r < rivers_.size(); ++r)
    {
        RiverState& st = rivers_[r];
        const bool perennial = layout_.rivers[r].perennial;
        const float base = perennial ? 0.45f : 0.05f;
        const float rainShare = perennial ? 0.55f : 0.95f;
        const float inflow = st.meanQ * (base + rainShare * rainRatio);
        st.storage = std::max(0.0f, st.storage + (inflow - st.q) * dt);
        st.q = st.storage / waterimpl::kReservoirSeconds;
        st.stage = st.bankfullDepth * 0.6f * std::pow(std::max(1e-4f, st.q / st.meanQ), 0.4f);
        st.stage = std::min(st.stage, st.bankfullDepth * 1.6f);
        if (r == 0 && st.stage > st.bankfullDepth)
        {
            flooding_ = true;
        }
    }
    for (LakeState& l : lakes_)
    {
        const float catchment = l.ephemeral ? 6.0f : 3.0f; // contributing area / lake area (game assumption)
        const float delta = (precipMmPerHour * catchment - evapMmPerHour) * dtHours / 1000.0f;
        float seep = l.ephemeral ? 0.004f * dtHours / 24.0f : 0.0005f * dtHours / 24.0f;
        if (!l.ephemeral && flooding_)
        {
            seep = -0.05f * dtHours; // floodwaters refill floodplain lakes
        }
        l.depth = clampf(l.depth + delta - seep, 0.0f, l.maxDepth);
    }
}

void WaterSystem::rebuildSurface(const Terrain& terrain)
{
    for (int y = 0; y < surface_.height(); ++y)
    {
        for (int x = 0; x < surface_.width(); ++x)
        {
            const CellKind k = static_cast<CellKind>(cellKind_.at(x, y));
            float s = waterimpl::kDryLevel;
            if (k == CellKind::River || k == CellKind::Floodable)
            {
                const size_t r = cellRiver_.at(x, y);
                const RiverState& st = rivers_[r];
                const RiverPoint& rp = layout_.rivers[r].points[static_cast<size_t>(cellRef_.at(x, y))];
                const float level = rp.floodplainZ - rp.depth + st.stage;
                if (k == CellKind::River || (flooding_ && terrain.heightAt(surface_.cellCenter(x, y)) < level))
                {
                    s = level;
                }
            }
            else if (k == CellKind::Lake)
            {
                const size_t l = static_cast<size_t>(cellRef_.at(x, y));
                const LakeDesc& lake = layout_.lakes[l];
                s = lake.spillHeight - lake.maxDepth + lakes_[l].depth;
            }
            surface_.at(x, y) = s;
        }
    }
}

WaterQuery WaterSystem::query(const Vec2& p, const Terrain& terrain) const
{
    WaterQuery q;
    if (surface_.empty())
    {
        return q;
    }
    int cx = 0;
    int cy = 0;
    surface_.worldToCell(p, cx, cy);
    if (!surface_.inBounds(cx, cy))
    {
        return q;
    }
    const float s = surface_.at(cx, cy);
    if (s <= waterimpl::kDryLevel + 1.0f)
    {
        return q;
    }
    const float h = terrain.heightAt(p);
    q.surfaceZ = s;
    q.depth = std::max(0.0f, s - h);
    q.isWater = q.depth > 0.02f;
    const CellKind k = static_cast<CellKind>(cellKind_.at(cx, cy));
    if (k == CellKind::River || k == CellKind::Floodable)
    {
        const size_t r = cellRiver_.at(cx, cy);
        const RiverPoint& rp = layout_.rivers[r].points[static_cast<size_t>(cellRef_.at(cx, cy))];
        const RiverState& st = rivers_[r];
        q.bodyId = static_cast<int>(r);
        const float area = std::max(0.5f, st.width * std::max(0.1f, st.stage) * 0.66f);
        const float v = k == CellKind::River ? st.q / area : 0.05f;
        q.flow = rp.tangent * clampf(v, 0.0f, 3.5f);
        q.flowing = k == CellKind::River && v > 0.05f;
    }
    else if (k == CellKind::Lake)
    {
        q.bodyId = 1000 + cellRef_.at(cx, cy);
    }
    return q;
}

bool WaterSystem::findDrinkSite(const Vec2& from, float maxRadius, DrinkSite& out) const
{
    std::vector<u32> hits;
    float radius = std::min(maxRadius, 400.0f);
    while (true)
    {
        hits.clear();
        drinkIndex_.query(from, radius, hits);
        if (!hits.empty() || radius >= maxRadius)
        {
            break;
        }
        radius = std::min(maxRadius, radius * 2.5f);
    }
    float best = 1e30f;
    bool found = false;
    for (const u32 h : hits)
    {
        const DrinkSite& d = drinkSites_[h];
        // Dried-up ephemeral bodies do not count.
        if (d.bodyId >= 1000 && lakes_[static_cast<size_t>(d.bodyId - 1000)].depth < 0.05f)
        {
            continue;
        }
        if (d.bodyId >= 0 && d.bodyId < 1000 && rivers_[static_cast<size_t>(d.bodyId)].stage < 0.08f)
        {
            continue;
        }
        const float dd = distanceSq(d.position, from);
        if (dd < best)
        {
            best = dd;
            out = d;
            found = true;
        }
    }
    return found;
}

float WaterSystem::noiseLevelDb(const Vec2& p) const
{
    if (rivers_.empty() || riverDist_.empty())
    {
        return 0.0f;
    }
    const float d = std::max(5.0f, riverDist_.sampleNearest(p));
    const float flowFactor = std::max(0.05f, rivers_[0].q / rivers_[0].meanQ);
    // Line source: -3 dB per doubling of distance; ~48 dB at the bank for a low-gradient river (game assumption).
    const float level = 48.0f + 10.0f * std::log10(flowFactor) - 10.0f * std::log10(d / 5.0f);
    return std::max(0.0f, level);
}

void WaterSystem::addDisturbance(const Vec2& p, float strength, double time)
{
    if (disturbances_.size() > 4096)
    {
        disturbances_.erase(disturbances_.begin(), disturbances_.begin() + 1024);
    }
    disturbances_.push_back({p, strength, time});
}

void WaterSystem::pruneDisturbances(double now, double maxAgeS)
{
    size_t keep = 0;
    for (size_t i = 0; i < disturbances_.size(); ++i)
    {
        if (now - disturbances_[i].time <= maxAgeS)
        {
            disturbances_[keep++] = disturbances_[i];
        }
    }
    disturbances_.resize(keep);
}

float WaterSystem::riverDischarge(int river) const
{
    return river >= 0 && static_cast<size_t>(river) < rivers_.size() ? rivers_[static_cast<size_t>(river)].q : 0.0f;
}

float WaterSystem::riverStage(int river) const
{
    return river >= 0 && static_cast<size_t>(river) < rivers_.size() ? rivers_[static_cast<size_t>(river)].stage : 0.0f;
}

float WaterSystem::bankfullDepth(int river) const
{
    return river >= 0 && static_cast<size_t>(river) < rivers_.size() ? rivers_[static_cast<size_t>(river)].bankfullDepth : 0.0f;
}

std::vector<float> WaterSystem::saveState() const
{
    std::vector<float> s;
    for (const RiverState& r : rivers_)
    {
        s.push_back(r.storage);
        s.push_back(r.q);
        s.push_back(r.stage);
    }
    for (const LakeState& l : lakes_)
    {
        s.push_back(l.depth);
    }
    return s;
}

void WaterSystem::loadState(const std::vector<float>& state)
{
    size_t i = 0;
    for (RiverState& r : rivers_)
    {
        if (i + 3 > state.size())
        {
            return;
        }
        r.storage = state[i++];
        r.q = state[i++];
        r.stage = state[i++];
    }
    for (LakeState& l : lakes_)
    {
        if (i >= state.size())
        {
            return;
        }
        l.depth = state[i++];
    }
}
} // namespace noctis
