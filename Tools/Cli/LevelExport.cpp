// export-level — algorithmic layout of the playable level (Hell Creek vertical slice).
//
// The level is not hand-placed: every point of interest is selected by an explicit algorithm over the
// world that 'run' generates (same seed, same SimConfig) and over a short warm-up of the live simulation:
//
//   basecamp        WorldSimulation's own choice (gentle ground ~250 m from water, central; no flood criterion
//                   in the core -> flood alert rule in the level)
//   exposure_site   PaleontologySystem sites on eroding outer banks (older strata in cutbanks / bluffs), with a
//                   bench access point at the foot of the face
//   oxbow_lake      abandoned meanders from the terrain generator
//   water_access    simulation drinking sites whose stance is at the water edge (<= 0.5 m above it, bank <= 20°),
//                   ranked by activity, drinking events, herbivore presence, habitat and point bars
//   ford            candidate crossing points at inflections (|curvature| <= 0.0015 /m), ranked by banks, bed,
//                   trail wear and predicted corridors; fordability against the simulated minimum discharge,
//                   packraft crossings of the main river
//   overlook        greedy maximum coverage of an activity-weighted viewshed (terrain horizon,
//                   dense canopy as an opaque layer, foliage extinction near the target), >= 150 m from
//                   exposures and set back from bank edges
//   blind           concealed, downwind hide 70–240 m from the busiest targets with a clear line of sight,
//                   set back from bank edges and below the skyline seen from the target
//   nesting_area    SPECULATIVE habitat-model zones per species (flood-safe, substrate, slope, water)
//   carcass_site    carcasses present at export time (dynamic state)
//   camera_trap / audio_recorder / mic_array   sensor spots on corridors / acoustic coverage / open quiet ground
//
// plus predicted game corridors (least-cost paths hotspot -> water and between foraging hotspots), the
// on-foot route network, rover accessibility from the basecamp and gameplay masks. Outputs:
//   <json>                       Data/Levels/<level>.json   (schema noctis.level/1)
//   <out>/overview.png           annotated overview map (Render.h base map + overlays + legend)
//   <out>/constraints.png        rover practicability, flood risk and radio shadow
//   <out>/mask_*.png             256x256 gameplay masks (row 0 = north edge, like export-terrain)
#include "LevelExport.h"

#include "Render.h"

#include "Noctis/Core/Json.h"
#include "Noctis/Core/Log.h"
#include "Noctis/Research/Hypothesis.h"
#include "Noctis/Sim/WorldSimulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifndef NOCTIS_SOURCE_DIR
#define NOCTIS_SOURCE_DIR "."
#endif

namespace noctistools
{
namespace levelimpl
{
using namespace noctis;

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kCellM = 32.0f;            // analysis grid resolution (m)
constexpr float kFootSpeedMs = 1.25f;      // researcher walking speed on open level ground (game assumption)
constexpr float kRoverSpeedMs = 4.0f;      // rover cross-country cruising speed (game assumption)
constexpr float kWadeLimitM = 0.7f;        // deepest water the researcher may wade (game safety rule)
constexpr float kResearcherMassKg = 95.0f; // researcher + field pack (game assumption)
constexpr float kRadioMastM = 10.0f;       // basecamp antenna mast height (game assumption)
constexpr float kOverlookRadiusM = 1500.0f;
constexpr float kHerbivoreMassKg = 3000.0f; // representative large herbivore for corridor costs (game assumption)
constexpr float kEdgeMarginM = 300.0f;      // POIs keep this distance from the map edge (= study-area warning distance)
constexpr float kWadeBankSlopeMaxDeg = 30.0f;  // steepest bank the researcher may climb into / out of the water (game safety rule)
constexpr float kRoverBankSlopeMaxDeg = 25.0f; // same slope cap as the rover practicability mask
constexpr float kFordKappaMax = 0.0015f;       // crossings only at inflections between bends (|curvature| below this, 1/m)
// Baseflow floor of WaterSystem::update (Source/NoctisCore/Private/World/Water.cpp): with no rain the inflow tends to
// meanQ * base, so discharge never drops below base x mean. Keep these two values in sync with Water.cpp.
constexpr float kPerennialBaseflow = 0.45f;
constexpr float kEphemeralBaseflow = 0.05f;
constexpr float kFloodAlertFraction = 0.8f;    // flood alert (skip interrupted, camp x360 forbidden) above 0.8 x bankfull stage
constexpr float kOverlookExposureMinM = 150.0f; // an overlook is a separate place from the exposure it may overlook
constexpr float kEdgeSetbackM = 18.0f;          // overlooks and blinds stay back from steep bank edges
constexpr float kEdgeSteepDeg = 25.0f;          // slope that marks a bank edge for the setback test
constexpr float kDrinkMaxDropM = 0.5f;          // a drinking site stands at most this high above the water surface
constexpr float kDrinkMaxBankSlopeDeg = 20.0f;  // steepest bank (10 m transect to the water) an animal walks down to drink
constexpr float kNestSuitabilityMin = 0.4f;     // below this the habitat model proposes no nesting zone for the species
constexpr float kWearPassage = 0.05f;           // trail wear above this = at least one recent passage (wear saturates fast)

struct Options
{
    std::string data = std::string(NOCTIS_SOURCE_DIR) + "/Data";
    std::string env = "hell_creek";
    std::string profile = "standard";
    std::string out = "Output/level";
    std::string json; // empty: <data>/Levels/<level id>.json
    u64 seed = 7;
    int threads = 0;
    float scale = 1.0f;
    double warmupHours = 6.0;
};

Options parseOptions(int argc, char** argv)
{
    Options o;
    for (int i = 2; i + 1 < argc; ++i)
    {
        const std::string k = argv[i];
        const std::string v = argv[i + 1];
        bool used = true;
        if (k == "--data")
        {
            o.data = v;
        }
        else if (k == "--env")
        {
            o.env = v;
        }
        else if (k == "--profile")
        {
            o.profile = v;
        }
        else if (k == "--out")
        {
            o.out = v;
        }
        else if (k == "--json")
        {
            o.json = v;
        }
        else if (k == "--seed")
        {
            o.seed = std::strtoull(v.c_str(), nullptr, 10);
        }
        else if (k == "--threads")
        {
            o.threads = std::atoi(v.c_str());
        }
        else if (k == "--scale")
        {
            o.scale = std::strtof(v.c_str(), nullptr);
        }
        else if (k == "--warmup-hours" || k == "--hours")
        {
            o.warmupHours = std::max(0.0, std::strtod(v.c_str(), nullptr));
        }
        else
        {
            used = false;
        }
        if (used)
        {
            ++i;
        }
    }
    return o;
}

// ----------------------------------------------------------------------------- analysis grid

struct Field
{
    int n = 0;
    float cell = kCellM;
    std::vector<float> v;

    void init(int cells, float value)
    {
        n = cells;
        v.assign(static_cast<size_t>(n) * static_cast<size_t>(n), value);
    }
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(n) + static_cast<size_t>(x); }
    float& at(int x, int y) { return v[idx(x, y)]; }
    float at(int x, int y) const { return v[idx(x, y)]; }
    Vec2 center(int x, int y) const { return {(static_cast<float>(x) + 0.5f) * cell, (static_cast<float>(y) + 0.5f) * cell}; }
    Vec2 centerOf(size_t i) const
    {
        const size_t sn = static_cast<size_t>(n);
        return center(static_cast<int>(i % sn), static_cast<int>(i / sn));
    }
    bool cellOf(const Vec2& p, int& x, int& y) const
    {
        x = static_cast<int>(std::floor(p.x / cell));
        y = static_cast<int>(std::floor(p.y / cell));
        return x >= 0 && y >= 0 && x < n && y < n;
    }
    size_t indexOf(const Vec2& p) const
    {
        int x = 0;
        int y = 0;
        cellOf(p, x, y);
        return idx(std::clamp(x, 0, n - 1), std::clamp(y, 0, n - 1));
    }
    float sample(const Vec2& p) const
    {
        int x = 0;
        int y = 0;
        return cellOf(p, x, y) ? at(x, y) : 0.0f;
    }
    float maxValue() const
    {
        float m = 0.0f;
        for (const float f : v)
        {
            if (std::isfinite(f))
            {
                m = std::max(m, f);
            }
        }
        return m;
    }
    void normalize()
    {
        const float m = maxValue();
        if (m <= 0.0f)
        {
            return;
        }
        for (float& f : v)
        {
            f /= m;
        }
    }
    // Separable box blur (edge-normalised).
    void blur(int radius)
    {
        if (radius <= 0 || n == 0)
        {
            return;
        }
        std::vector<float> tmp(v.size());
        for (int y = 0; y < n; ++y)
        {
            for (int x = 0; x < n; ++x)
            {
                float s = 0.0f;
                int c = 0;
                for (int k = -radius; k <= radius; ++k)
                {
                    const int xx = x + k;
                    if (xx >= 0 && xx < n)
                    {
                        s += at(xx, y);
                        ++c;
                    }
                }
                tmp[idx(x, y)] = s / static_cast<float>(c);
            }
        }
        for (int y = 0; y < n; ++y)
        {
            for (int x = 0; x < n; ++x)
            {
                float s = 0.0f;
                int c = 0;
                for (int k = -radius; k <= radius; ++k)
                {
                    const int yy = y + k;
                    if (yy >= 0 && yy < n)
                    {
                        s += tmp[idx(x, yy)];
                        ++c;
                    }
                }
                at(x, y) = s / static_cast<float>(c);
            }
        }
    }
    void add(const Vec2& p, float amount)
    {
        int x = 0;
        int y = 0;
        if (cellOf(p, x, y))
        {
            at(x, y) += amount;
        }
    }
};

// ----------------------------------------------------------------------------- geometry helpers

float polylineLength(const std::vector<Vec2>& p)
{
    float l = 0.0f;
    for (size_t i = 1; i < p.size(); ++i)
    {
        l += distance(p[i - 1], p[i]);
    }
    return l;
}

float distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b)
{
    const Vec2 ab = b - a;
    const float l2 = ab.lengthSq();
    const float t = l2 > 0.0f ? saturate(dot(p - a, ab) / l2) : 0.0f;
    return distance(p, a + ab * t);
}

float distanceToPolyline(const Vec2& p, const std::vector<Vec2>& line)
{
    if (line.empty())
    {
        return kInf;
    }
    if (line.size() == 1)
    {
        return distance(p, line[0]);
    }
    float best = kInf;
    for (size_t i = 1; i < line.size(); ++i)
    {
        best = std::min(best, distanceToSegment(p, line[i - 1], line[i]));
    }
    return best;
}

// Douglas–Peucker simplification.
std::vector<Vec2> simplify(const std::vector<Vec2>& in, float tol)
{
    if (in.size() < 3)
    {
        return in;
    }
    std::vector<u8> keep(in.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> stack{{0, in.size() - 1}};
    while (!stack.empty())
    {
        const auto seg = stack.back();
        stack.pop_back();
        float worst = -1.0f;
        size_t worstIdx = seg.first;
        for (size_t i = seg.first + 1; i < seg.second; ++i)
        {
            const float d = distanceToSegment(in[i], in[seg.first], in[seg.second]);
            if (d > worst)
            {
                worst = d;
                worstIdx = i;
            }
        }
        if (worst > tol)
        {
            keep[worstIdx] = 1;
            stack.push_back({seg.first, worstIdx});
            stack.push_back({worstIdx, seg.second});
        }
    }
    std::vector<Vec2> out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (keep[i])
        {
            out.push_back(in[i]);
        }
    }
    return out;
}

// Chaikin corner cutting (keeps both end points): turns grid-aligned least-cost paths into natural curves.
std::vector<Vec2> chaikin(const std::vector<Vec2>& in, int iterations)
{
    std::vector<Vec2> cur = in;
    for (int it = 0; it < iterations && cur.size() > 2; ++it)
    {
        std::vector<Vec2> next{cur.front()};
        for (size_t i = 0; i + 1 < cur.size(); ++i)
        {
            next.push_back(lerp(cur[i], cur[i + 1], 0.25f));
            next.push_back(lerp(cur[i], cur[i + 1], 0.75f));
        }
        next.push_back(cur.back());
        cur = next;
    }
    return cur;
}

// Samples a polyline every 'step' metres (inclusive of both ends).
std::vector<Vec2> resample(const std::vector<Vec2>& line, float step)
{
    std::vector<Vec2> out;
    if (line.empty())
    {
        return out;
    }
    out.push_back(line.front());
    for (size_t i = 1; i < line.size(); ++i)
    {
        const float l = distance(line[i - 1], line[i]);
        const int k = std::max(1, static_cast<int>(std::ceil(l / step)));
        for (int j = 1; j <= k; ++j)
        {
            out.push_back(lerp(line[i - 1], line[i], static_cast<float>(j) / static_cast<float>(k)));
        }
    }
    return out;
}

// ----------------------------------------------------------------------------- least-cost paths

struct PathField
{
    std::vector<float> dist;
    std::vector<int> parent;
};

// Dijkstra over an 8-connected grid. cost = traversal cost per metre (kInf = impassable).
void dijkstra(const Field& cost, const std::vector<size_t>& sources, PathField& out, float maxDist = kInf)
{
    const size_t count = cost.v.size();
    out.dist.assign(count, kInf);
    out.parent.assign(count, -1);
    using Item = std::pair<float, size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (const size_t s : sources)
    {
        if (s < count && std::isfinite(cost.v[s]))
        {
            out.dist[s] = 0.0f;
            pq.push({0.0f, s});
        }
    }
    static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const size_t sn = static_cast<size_t>(cost.n);
    while (!pq.empty())
    {
        const Item it = pq.top();
        pq.pop();
        if (it.first > out.dist[it.second])
        {
            continue;
        }
        if (it.first > maxDist)
        {
            break;
        }
        const int x = static_cast<int>(it.second % sn);
        const int y = static_cast<int>(it.second / sn);
        for (int k = 0; k < 8; ++k)
        {
            const int nx = x + dx[k];
            const int ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= cost.n || ny >= cost.n)
            {
                continue;
            }
            const size_t ni = cost.idx(nx, ny);
            const float c = cost.v[ni];
            if (!std::isfinite(c))
            {
                continue;
            }
            const float len = (k < 4 ? 1.0f : 1.41421356f) * cost.cell;
            const float nd = it.first + 0.5f * (cost.v[it.second] + c) * len;
            if (nd < out.dist[ni])
            {
                out.dist[ni] = nd;
                out.parent[ni] = static_cast<int>(it.second);
                pq.push({nd, ni});
            }
        }
    }
}

// Source-to-target cell sequence (empty when unreachable).
std::vector<size_t> tracePath(const PathField& pf, size_t target)
{
    std::vector<size_t> path;
    if (target >= pf.dist.size() || !std::isfinite(pf.dist[target]))
    {
        return path;
    }
    size_t c = target;
    for (size_t guard = 0; guard < pf.dist.size(); ++guard)
    {
        path.push_back(c);
        const int p = pf.parent[c];
        if (p < 0)
        {
            break;
        }
        c = static_cast<size_t>(p);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// ----------------------------------------------------------------------------- river lookup

class RiverIndex
{
public:
    void build(const WorldLayout& layout, float sizeM, float bucketM)
    {
        layout_ = &layout;
        bucket_ = bucketM;
        nb_ = std::max(1, static_cast<int>(std::ceil(sizeM / bucketM)));
        cells_.assign(static_cast<size_t>(nb_) * static_cast<size_t>(nb_), {});
        for (size_t r = 0; r < layout.rivers.size(); ++r)
        {
            const RiverDesc& rd = layout.rivers[r];
            for (size_t i = 0; i < rd.points.size(); ++i)
            {
                const int bx = static_cast<int>(std::floor(rd.points[i].position.x / bucket_));
                const int by = static_cast<int>(std::floor(rd.points[i].position.y / bucket_));
                if (bx < 0 || by < 0 || bx >= nb_ || by >= nb_)
                {
                    continue;
                }
                cells_[static_cast<size_t>(by) * static_cast<size_t>(nb_) + static_cast<size_t>(bx)].push_back({static_cast<int>(r), static_cast<int>(i)});
            }
        }
    }
    // Nearest centreline point of river 'river' (-1 = any river).
    bool nearest(const Vec2& p, int river, int& outRiver, int& outPoint, float& outDist) const
    {
        if (!layout_)
        {
            return false;
        }
        const int bx = std::clamp(static_cast<int>(std::floor(p.x / bucket_)), 0, nb_ - 1);
        const int by = std::clamp(static_cast<int>(std::floor(p.y / bucket_)), 0, nb_ - 1);
        float best = kInf;
        for (int r = 0; r < nb_; ++r)
        {
            for (int y = by - r; y <= by + r; ++y)
            {
                for (int x = bx - r; x <= bx + r; ++x)
                {
                    if (x < 0 || y < 0 || x >= nb_ || y >= nb_ || (std::abs(x - bx) != r && std::abs(y - by) != r))
                    {
                        continue;
                    }
                    for (const auto& e : cells_[static_cast<size_t>(y) * static_cast<size_t>(nb_) + static_cast<size_t>(x)])
                    {
                        if (river >= 0 && e.first != river)
                        {
                            continue;
                        }
                        const float d = distance(p, layout_->rivers[static_cast<size_t>(e.first)].points[static_cast<size_t>(e.second)].position);
                        if (d < best)
                        {
                            best = d;
                            outRiver = e.first;
                            outPoint = e.second;
                        }
                    }
                }
            }
            if (std::isfinite(best) && best <= static_cast<float>(r) * bucket_)
            {
                break;
            }
        }
        outDist = best;
        return std::isfinite(best);
    }

private:
    const WorldLayout* layout_ = nullptr;
    float bucket_ = 128.0f;
    int nb_ = 0;
    std::vector<std::vector<std::pair<int, int>>> cells_;
};

// ----------------------------------------------------------------------------- level entities

struct Poi
{
    std::string id;
    std::string type;
    std::string nameFr;
    std::string nameEn;
    Vec2 pos;
    float z = 0.0f;
    float radiusM = 25.0f;
    float heading = 0.0f; // core convention: radians, 0 = east, counter-clockwise
    bool hasHeading = false;
    Confidence confidence = Confidence::Inferred;
    std::string confidenceSubject; // what the confidence level qualifies
    std::string placement;         // generated_terrain | simulation_state | model_prediction | game_design
    std::string rationaleFr;
    std::vector<std::string> sources;
    JsonValue metrics = JsonValue::makeObject();
    bool visibleAtStart = false;
    bool dynamicState = false;
    bool designerOnly = false;
    std::string profileToggle;
    std::vector<std::string> links;
    std::vector<std::string> sightlines; // POIs visible from this one (overlooks / blinds)
    float score = 0.0f;
    size_t footCell = 0;
    bool footOk = false;
    Vec2 access;          // separate access point (exposure bench), when hasAccess
    bool hasAccess = false;
};

struct Trail
{
    std::string id;
    std::string kind; // drinking_route | forage_link
    std::vector<Vec2> points;
    std::string fromLabel;
    std::string toLabel;
    float lengthM = 0.0f;
    float wearMean = 0.0f;
    float wearMax = 0.0f;
    float presence = 0.0f;
    float maxWaterM = 0.0f;
    float maxSlopeDeg = 0.0f;
    bool crossesRiver = false;
    float costPerM = 0.0f;
    std::vector<std::string> usableBy;    // herbivore species whose depth / slope limits allow the whole corridor
    std::vector<std::string> notUsableBy; // herbivore species stopped by deep water (non-swimmers) or slope
};

struct Edge
{
    size_t a = 0;
    size_t b = 0;
    float costM = 0.0f;
    float lengthM = 0.0f;
    float maxSlopeDeg = 0.0f;
    float maxWaterM = 0.0f;
    int packraftCells = 0; // > 0: the route crosses the main river by packraft
    std::vector<Vec2> path;
};

struct Viewshed
{
    std::vector<std::pair<u32, float>> cells; // analysis cell, visibility 0..1
    float areaKm2 = 0.0f;
    float value = 0.0f;
};

struct Candidate
{
    Vec2 pos;
    float score = 0.0f;
    int ref = -1;
};

double rnd(double v, int decimals)
{
    const double f = std::pow(10.0, decimals);
    return std::round(v * f) / f;
}

JsonValue vec3Json(const Vec2& p, float z)
{
    JsonValue a = JsonValue::makeArray();
    a.push(rnd(p.x, 1));
    a.push(rnd(p.y, 1));
    a.push(rnd(z, 2));
    return a;
}

// Unreal location (cm): X = east, Y = -north (south), Z = up — same mapping as FNoctisSpace::ToUE.
JsonValue ueJson(const Vec2& p, float z)
{
    JsonValue a = JsonValue::makeArray();
    a.push(std::round(p.x * 100.0));
    a.push(std::round(-p.y * 100.0));
    a.push(std::round(z * 100.0));
    return a;
}

JsonValue strings(const std::vector<std::string>& v)
{
    JsonValue a = JsonValue::makeArray();
    for (const std::string& s : v)
    {
        a.push(s);
    }
    return a;
}

JsonValue polylineJson(const std::vector<Vec2>& pts)
{
    JsonValue a = JsonValue::makeArray();
    for (const Vec2& p : pts)
    {
        JsonValue q = JsonValue::makeArray();
        q.push(rnd(p.x, 1));
        q.push(rnd(p.y, 1));
        a.push(q);
    }
    return a;
}

JsonValue names(const std::string& fr, const std::string& en)
{
    JsonValue o = JsonValue::makeObject();
    o.set("fr", fr);
    o.set("en", en);
    return o;
}

bool isPredator(const SpeciesSim& s)
{
    return s.predation.style != HuntStyle::None && s.diet.type != DietType::Herbivore;
}

float substrateFirmness(Substrate s)
{
    switch (s)
    {
    case Substrate::Rock: return 1.0f;
    case Substrate::Gravel: return 1.0f;
    case Substrate::Sand: return 0.75f;
    case Substrate::Silt: return 0.4f;
    case Substrate::ForestSoil: return 0.5f;
    case Substrate::Clay: return 0.2f;
    case Substrate::Mud: return 0.1f;
    case Substrate::Peat: return 0.0f;
    case Substrate::Count: break;
    }
    return 0.3f;
}

// Nest-site substrate preference per nest type (game assumption by analogy with extant archosaurs).
float nestSubstrate(NestType t, Substrate s)
{
    switch (t)
    {
    case NestType::Mound: // vegetation-mound nests (crocodylians, megapodes): soil and litter available
        return s == Substrate::ForestSoil ? 1.0f : s == Substrate::Silt ? 0.8f : s == Substrate::Sand ? 0.6f : s == Substrate::Clay ? 0.4f : 0.15f;
    case NestType::Buried: // eggs buried in loose sediment
        return s == Substrate::Sand ? 1.0f : s == Substrate::Silt ? 0.8f : s == Substrate::ForestSoil ? 0.5f : 0.15f;
    case NestType::OpenScrape:
        return s == Substrate::Sand ? 1.0f : s == Substrate::Silt ? 0.8f : s == Substrate::Gravel ? 0.5f : s == Substrate::ForestSoil ? 0.5f : 0.2f;
    case NestType::Brooded:
        return (s == Substrate::Sand || s == Substrate::Silt || s == Substrate::ForestSoil) ? 0.9f : s == Substrate::Clay ? 0.5f : 0.2f;
    case NestType::LiveBirth:
    case NestType::None: break;
    }
    return 0.0f;
}

const char* nestTypeKeyLocal(NestType t)
{
    switch (t)
    {
    case NestType::Mound: return "mound";
    case NestType::OpenScrape: return "open_scrape";
    case NestType::Brooded: return "brooded";
    case NestType::Buried: return "buried";
    case NestType::LiveBirth: return "live_birth";
    case NestType::None: return "none";
    }
    return "none";
}

const char* nestTypeLabelFrLocal(NestType t)
{
    switch (t)
    {
    case NestType::Mound: return "nid-monticule (végétation/sol)";
    case NestType::OpenScrape: return "cuvette ouverte";
    case NestType::Brooded: return "nid couvé";
    case NestType::Buried: return "œufs enfouis";
    case NestType::LiveBirth: return "viviparité";
    case NestType::None: return "aucun";
    }
    return "aucun";
}

// ----------------------------------------------------------------------------- builder

class LevelBuilder
{
public:
    LevelBuilder(WorldSimulation& world, const Options& opt) : w_(world), opt_(opt)
    {
        size_ = w_.terrain().size();
        n_ = static_cast<int>(std::ceil(size_ / kCellM));
        levelId_ = opt_.env == "hell_creek" ? std::string("hell_creek_valley") : opt_.env + "_level";
    }

    int run()
    {
        const auto t0 = std::chrono::steady_clock::now();
        loadEquipment();
        rivers_.build(w_.layout(), size_, 128.0f);
        initFields();
        warmup();
        lap_ = std::chrono::steady_clock::now();
        buildLayers();
        buildCosts();
        lap("couches et coûts");
        addBasecamp();
        addFloodRefuge();
        addExposureSites();
        addOxbows();
        addBackswamps();
        rankWaterAccess();
        buildCorridors();
        lap("corridors");
        addFords();
        buildPackraftNetwork();
        addWaterAccess();
        addOverlooks();
        lap("points de vue");
        addBlinds();
        addNestingAreas();
        addCarcasses();
        addSensors();
        computeRadio();
        lap("affûts, nids, capteurs, radio");
        buildRoutes();
        computeSightlines();
        lap("réseau d'itinéraires");
        std::filesystem::create_directories(opt_.out);
        writeMasks();
        writeOverview();
        writeConstraints();
        const std::string jsonPath = opt_.json.empty() ? opt_.data + "/Levels/" + levelId_ + ".json" : opt_.json;
        std::filesystem::create_directories(std::filesystem::path(jsonPath).parent_path());
        writeJson(jsonPath);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printSummary(jsonPath, secs);
        return 0;
    }

private:
    void lap(const char* what)
    {
        const auto now = std::chrono::steady_clock::now();
        std::printf("  [%s : %.1f s]\n", what, std::chrono::duration<double>(now - lap_).count());
        lap_ = now;
    }

    // ------------------------------------------------------------------ setup
    void loadEquipment()
    {
        std::string text;
        if (!w_.fileSystem().readText(opt_.data + "/Equipment/equipment.json", text))
        {
            NOCTIS_LOG_WARN("equipment.json introuvable : valeurs par défaut du véhicule");
            return;
        }
        const JsonParseResult r = parseJson(text);
        if (!r.ok)
        {
            return;
        }
        const JsonValue& v = r.value.get("vehicle");
        fordingDepthM_ = v.getFloat("fording_depth_m", fordingDepthM_);
        vehicleMassKg_ = v.getFloat("mass_kg", vehicleMassKg_);
        wheels_ = v.getInt("wheels", wheels_);
        wheelWidthM_ = v.getFloat("wheel_width_m", wheelWidthM_);
        wheelRadiusM_ = v.getFloat("wheel_radius_m", wheelRadiusM_);
        noiseElectricDb_ = v.getFloat("noise_electric_db_1m", noiseElectricDb_);
        noiseTyreRefDb_ = v.getFloat("noise_tyre_db_1m_ref", noiseTyreRefDb_);
        noiseTyreRefSpeedMs_ = v.getFloat("noise_tyre_ref_speed_ms", noiseTyreRefSpeedMs_);
        noiseTyreSpeedExpDb_ = v.getFloat("noise_tyre_speed_exponent_db", noiseTyreSpeedExpDb_);
        noiseGeneratorDb_ = v.getFloat("noise_generator_db_1m", noiseGeneratorDb_);
        batteryKWh_ = v.getFloat("battery_kwh", batteryKWh_);
        droneRadioRangeM_ = r.value.get("drone").getFloat("radio_range_m", droneRadioRangeM_);
        for (const JsonValue& it : r.value.get("items").items())
        {
            if (it.getString("id") == "packraft")
            {
                const JsonValue& s = it.get("specs");
                packraftFound_ = true;
                packraftMaxCurrentMs_ = s.getFloat("max_crossing_current_ms", packraftMaxCurrentMs_);
                packraftBankSlopeDeg_ = s.getFloat("bank_slope_max_deg", packraftBankSlopeDeg_);
                packraftPaddleMs_ = s.getFloat("paddle_speed_ms", packraftPaddleMs_);
                packraftSetupMin_ = s.getFloat("setup_min", packraftSetupMin_);
                packraftMassKg_ = it.getFloat("mass_kg", packraftMassKg_);
            }
        }
        if (!packraftFound_)
        {
            NOCTIS_LOG_WARN("equipment.json : pas d'article 'packraft' — valeurs par défaut pour la traversée de la rivière");
        }
    }

    void initFields()
    {
        for (Field* f : {&height_, &slope_, &water_, &wetFrac_, &canopy_, &cover_, &trail_, &har_, &riverDist_, &creekDist_, &creekHar_, &noise_,
                         &prominence_, &blockH_, &backdropH_, &suitability_, &presence_, &herbPresence_, &predPresence_, &drinkEv_, &callEv_, &activity_, &flood_,
                         &footCost_, &roverCost_, &roverPract_, &animalCost_, &radio_, &nestSuit_, &corridorUse_})
        {
            f->init(n_, 0.0f);
        }
        habitat_.assign(height_.v.size(), 0);
        substrate_.assign(height_.v.size(), 0);
        scratch_.assign(height_.v.size(), -1.0f);
    }

    // ------------------------------------------------------------------ warm-up of the live simulation
    void sampleCreatures()
    {
        for (const Creature& c : w_.creatures().creatures())
        {
            if (!c.alive)
            {
                continue;
            }
            const SpeciesSim& s = w_.db().speciesAt(c.speciesIndex).sim;
            presence_.add(c.pos2(), 1.0f);
            if (isPredator(s))
            {
                predPresence_.add(c.pos2(), 1.0f);
            }
            else
            {
                herbPresence_.add(c.pos2(), 1.0f);
            }
        }
        ++samples_;
    }

    void onEvent(const SimEvent& e)
    {
        ++eventCounts_[eventTypeName(e.type)];
        const Vec2 p = e.position.xy();
        switch (e.type)
        {
        case EventType::Drink: drinkEv_.add(p, 1.0f); break;
        case EventType::Vocalization: callEv_.add(p, 1.0f); break;
        case EventType::HuntStalk:
        case EventType::HuntChase:
        case EventType::HuntKill: predPresence_.add(p, 3.0f); break;
        case EventType::EggsLaid:
        case EventType::NestBuilt:
        {
            const Creature* c = w_.creatures().find(e.subject);
            if (c)
            {
                nestEvents_.push_back({p, static_cast<int>(c->speciesIndex)});
            }
            break;
        }
        default: break;
        }
    }

    void warmup()
    {
        const int handle = w_.events().subscribe([this](const SimEvent& e) { onEvent(e); });
        sampleCreatures();
        const double total = opt_.warmupHours * 3600.0;
        double done = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        while (done < total)
        {
            const double chunk = std::min(600.0, total - done);
            w_.advance(chunk, 1.0);
            done += chunk;
            sampleCreatures();
        }
        w_.events().unsubscribe(handle);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("Préchauffage : %.1f h simulées en %.0f s (%d relevés de présence) -> %s\n", opt_.warmupHours, secs, samples_, w_.clock().formatted().c_str());
    }

    // ------------------------------------------------------------------ layers
    void buildLayers()
    {
        const Terrain& t = w_.terrain();
        WaterSystem& water = w_.water();
        const VegetationSystem& veg = w_.vegetation();
        const WorldLayout& layout = w_.layout();
        // Herbivore habitat suitability: habitat weights averaged over agent herbivores, weighted by abundance.
        std::vector<std::pair<int, float>> herbivores;
        float herbTotal = 0.0f;
        for (const int si : w_.speciesInWorld())
        {
            const SpeciesDefinition& d = w_.db().speciesAt(si);
            if (d.sim.role == SimRole::Agent && !isPredator(d.sim) && d.sim.population.initialCount > 0.0f)
            {
                herbivores.push_back({si, d.sim.population.initialCount});
                herbTotal += d.sim.population.initialCount;
            }
        }
        for (int y = 0; y < n_; ++y)
        {
            for (int x = 0; x < n_; ++x)
            {
                const size_t i = height_.idx(x, y);
                const Vec2 p = height_.center(x, y);
                height_.v[i] = t.heightAt(p);
                float smax = 0.0f;
                float wmax = 0.0f;
                int wet = 0;
                for (int sy = -1; sy <= 1; ++sy)
                {
                    for (int sx = -1; sx <= 1; ++sx)
                    {
                        const Vec2 q = t.clampToBounds(p + Vec2{static_cast<float>(sx) * 10.0f, static_cast<float>(sy) * 10.0f}, 1.0f);
                        smax = std::max(smax, t.slopeDegAt(q));
                        const WaterQuery wq = water.query(q, t);
                        if (wq.isWater)
                        {
                            wmax = std::max(wmax, wq.depth);
                            ++wet;
                        }
                    }
                }
                slope_.v[i] = smax;
                water_.v[i] = wmax;
                wetFrac_.v[i] = static_cast<float>(wet) / 9.0f;
                canopy_.v[i] = veg.canopyCover(p);
                cover_.v[i] = veg.cover(p);
                float tr = 0.0f;
                for (int sy = -1; sy <= 1; sy += 2)
                {
                    for (int sx = -1; sx <= 1; sx += 2)
                    {
                        tr = std::max(tr, veg.trail(p + Vec2{static_cast<float>(sx) * 8.0f, static_cast<float>(sy) * 8.0f}));
                    }
                }
                trail_.v[i] = tr;
                habitat_[i] = static_cast<u8>(t.habitatAt(p));
                substrate_[i] = static_cast<u8>(t.substrateAt(p));
                int ri = -1;
                int pi = -1;
                float d = 0.0f;
                if (!layout.rivers.empty() && rivers_.nearest(p, 0, ri, pi, d))
                {
                    har_.v[i] = height_.v[i] - layout.rivers[0].points[static_cast<size_t>(pi)].floodplainZ;
                    riverDist_.v[i] = d;
                }
                else
                {
                    har_.v[i] = 99.0f;
                    riverDist_.v[i] = 1e5f;
                }
                if (layout.rivers.size() > 1 && rivers_.nearest(p, 1, ri, pi, d))
                {
                    creekDist_.v[i] = d;
                    creekHar_.v[i] = height_.v[i] - layout.rivers[1].points[static_cast<size_t>(pi)].floodplainZ;
                }
                else
                {
                    creekDist_.v[i] = 1e5f;
                    creekHar_.v[i] = 99.0f;
                }
                noise_.v[i] = water.noiseLevelDb(p);
                // Canopy top: biomass-weighted height of tree functional types; opaque only where canopy is dense.
                const int ci = veg.cellIndexAt(p);
                float hsum = 0.0f;
                float dsum = 0.0f;
                if (ci >= 0)
                {
                    for (const PlantType pt : {PlantType::AngioTree, PlantType::Conifer, PlantType::Ginkgo, PlantType::Palm})
                    {
                        const PlantTypeSpec& sp = veg.spec(pt);
                        if (!sp.present || sp.maxBiomassKgM2 <= 0.0f)
                        {
                            continue;
                        }
                        const float dens = saturate(veg.biomass(ci, pt) / sp.maxBiomassKgM2);
                        hsum += dens * sp.heightM;
                        dsum += dens;
                    }
                }
                const float treeTop = dsum > 0.0f ? hsum / dsum : 0.0f;
                blockH_.v[i] = treeTop * 0.8f * smoothstep(0.45f, 0.8f, canopy_.v[i]);
                backdropH_.v[i] = treeTop * smoothstep(0.2f, 0.5f, canopy_.v[i]); // visual backdrop: even open woodland breaks a silhouette
                float suit = 0.0f;
                for (const auto& hb : herbivores)
                {
                    suit += w_.db().speciesAt(hb.first).sim.habitat[habitat_[i]] * hb.second;
                }
                suitability_.v[i] = herbTotal > 0.0f ? suit / herbTotal : 0.0f;
            }
        }
        // Local prominence: height above the mean of a ~290 m neighbourhood.
        Field mean = height_;
        mean.blur(4);
        for (size_t i = 0; i < height_.v.size(); ++i)
        {
            prominence_.v[i] = height_.v[i] - mean.v[i];
        }
        // Activity index 0..1 (where animals were and are expected): all agents seen during warm-up,
        // herbivore habitat suitability, trail wear and drinking events.
        Field pres = presence_;
        pres.blur(2);
        pres.normalize();
        Field drink = drinkEv_;
        drink.blur(2);
        drink.normalize();
        Field suit = suitability_;
        suit.normalize();
        Field tr = trail_;
        tr.normalize();
        for (size_t i = 0; i < activity_.v.size(); ++i)
        {
            activity_.v[i] = (0.4f * pres.v[i] + 0.3f * suit.v[i] + 0.15f * tr.v[i] + 0.15f * drink.v[i]) * (1.0f - 0.6f * wetFrac_.v[i]);
        }
        activity_.normalize();
        herbPresBlur_ = herbPresence_;
        herbPresBlur_.blur(2);
        herbPresBlur_.normalize();
        predPresBlur_ = predPresence_;
        predPresBlur_.blur(3);
        predPresBlur_.normalize();
        drinkBlur_ = drink;
        suitNorm_ = suit;
        // Flood risk from height above the bankfull reference of the main river (and creek, backswamps).
        for (size_t i = 0; i < flood_.v.size(); ++i)
        {
            float risk = har_.v[i] < 0.0f ? 1.0f : 1.0f - smoothstep(0.0f, 2.5f, har_.v[i]);
            const Habitat h = static_cast<Habitat>(habitat_[i]);
            if (h == Habitat::Backswamp || h == Habitat::OxbowLake)
            {
                risk = std::max(risk, 0.7f);
            }
            if (creekDist_.v[i] < 30.0f && creekHar_.v[i] < 1.0f)
            {
                risk = std::max(risk, 0.6f);
            }
            flood_.v[i] = risk;
        }
    }

    // ------------------------------------------------------------------ movement costs
    void buildCosts()
    {
        const Terrain& t = w_.terrain();
        const float footPressure = kResearcherMassKg * 9.81f / (0.11f * 0.28f) / 1000.0f;
        const float wheelLoad = vehicleMassKg_ * 9.81f / static_cast<float>(std::max(1, wheels_));
        const float contactLen = std::max(0.15f, wheelRadiusM_ * 0.45f);
        const float wheelPressure = wheelLoad / (wheelWidthM_ * contactLen) / 1000.0f;
        const float maxWheelSink = 0.35f * wheelRadiusM_ * 0.7f; // 70 % of the stuck threshold of VehicleSystem
        const float herbFoot = std::sqrt(kHerbivoreMassKg / 3000.0f) * 0.35f;
        const float herbPressure = kHerbivoreMassKg * 9.81f / 4.0f / (herbFoot * herbFoot) / 1000.0f;
        for (int y = 0; y < n_; ++y)
        {
            for (int x = 0; x < n_; ++x)
            {
                const size_t i = height_.idx(x, y);
                const Vec2 p = height_.center(x, y);
                const float slope = slope_.v[i];
                const float depth = water_.v[i];
                const Habitat hab = static_cast<Habitat>(habitat_[i]);
                // --- researcher on foot
                {
                    float c = 1.0f + (slope / 14.0f) * (slope / 14.0f);
                    if (depth > kWadeLimitM || slope > 40.0f)
                    {
                        c = kInf;
                    }
                    else
                    {
                        c += 6.0f * depth + 0.8f * cover_.v[i];
                        c += 4.0f * saturate(t.sinkage(p, footPressure, 0.11f) / 0.1f);
                    }
                    footCost_.v[i] = c;
                }
                // --- rover (Bekker sinkage and traction exactly as VehicleSystem::wheelGround)
                {
                    const SoilMechanics s = soilMechanics(t.substrateAt(p), t.moistureAt(p));
                    const float sink = t.sinkage(p, wheelPressure, wheelWidthM_);
                    const float mu = s.tractionMu * (1.0f - 0.5f * saturate(sink / wheelRadiusM_));
                    const float rolling = s.rollingResistance + 0.6f * sink / (2.0f * wheelRadiusM_);
                    const float maxSlope = std::min(25.0f, std::atan(std::max(0.0f, mu - rolling)) * kRadToDeg);
                    const bool ok = depth <= fordingDepthM_ && slope <= maxSlope && canopy_.v[i] <= 0.55f && sink <= maxWheelSink && hab != Habitat::CutbankCliff;
                    roverPract_.v[i] = ok ? 1.0f : 0.0f;
                    roverCost_.v[i] = ok ? 1.0f + slope / 12.0f + 1.5f * canopy_.v[i] + 2.0f * sink / std::max(0.01f, maxWheelSink) + (depth > 0.05f ? 2.0f + 3.0f * depth : 0.0f)
                                         : kInf;
                }
                // --- large herbivore (predicted corridors)
                {
                    float c = 1.0f + (slope / 15.0f) * (slope / 15.0f);
                    if (slope > 35.0f)
                    {
                        c = kInf;
                    }
                    else
                    {
                        if (depth > 0.3f)
                        {
                            c += 2.5f * std::min(depth, 4.0f);
                        }
                        c += 1.2f * cover_.v[i] + 3.0f * saturate(t.sinkage(p, herbPressure, herbFoot) / 0.3f);
                        c *= 1.0f - 0.5f * saturate(trail_.v[i] * 2.0f);
                    }
                    animalCost_.v[i] = c;
                }
            }
        }
        // Reference searches from the basecamp: the camp bank (on foot) and the rover network.
        size_t bc = 0;
        if (snapPassable(footCost_, w_.basecamp(), 300.0f, bc))
        {
            dijkstra(footCost_, {bc}, footFromBase_);
        }
        else
        {
            footFromBase_.dist.assign(footCost_.v.size(), kInf);
            footFromBase_.parent.assign(footCost_.v.size(), -1);
        }
        if (snapPassable(roverCost_, w_.basecamp(), 300.0f, bc))
        {
            dijkstra(roverCost_, {bc}, roverFromBase_);
        }
        else
        {
            roverFromBase_.dist.assign(roverCost_.v.size(), kInf);
            roverFromBase_.parent.assign(roverCost_.v.size(), -1);
        }
    }

    // True when p is on the bank of the main river that the basecamp is on (reachable on foot at current stage).
    bool campBank(const Vec2& p) const
    {
        size_t c = 0;
        return snapPassable(footCost_, p, 120.0f, c) && std::isfinite(footFromBase_.dist[c]);
    }

    static const char* floodClass(float har)
    {
        // WaterSystem caps the stage at 1.6 x bankfull depth: 2.4 m above the bankfull level for a 4 m channel.
        return har < 0.5f ? "frequent" : har < 1.5f ? "occasional" : har < 2.4f ? "extreme_only" : "never";
    }

    // Height of p above the bankfull (floodplain) reference of the nearest main-river point. Computed at the point
    // itself, not from its 32 m analysis cell (a cell can mix the channel and the bluff top).
    float harAt(const Vec2& p) const
    {
        int ri = -1;
        int pi = -1;
        float d = 0.0f;
        if (w_.layout().rivers.empty() || !rivers_.nearest(p, 0, ri, pi, d))
        {
            return 99.0f;
        }
        return terrainZ(p) - w_.layout().rivers[0].points[static_cast<size_t>(pi)].floodplainZ;
    }

    // Flood risk 0..1 of the point itself (same formula as the flood_ mask, evaluated at the point).
    float floodRiskAt(const Vec2& p) const
    {
        const float har = harAt(p);
        float risk = har < 0.0f ? 1.0f : 1.0f - smoothstep(0.0f, 2.5f, har);
        const Habitat h = w_.terrain().habitatAt(p);
        if (h == Habitat::Backswamp || h == Habitat::OxbowLake)
        {
            risk = std::max(risk, 0.7f);
        }
        int ri = -1;
        int pi = -1;
        float d = 0.0f;
        if (w_.layout().rivers.size() > 1 && rivers_.nearest(p, 1, ri, pi, d) && d < 30.0f &&
            terrainZ(p) - w_.layout().rivers[1].points[static_cast<size_t>(pi)].floodplainZ < 1.0f)
        {
            risk = std::max(risk, 0.6f);
        }
        return risk;
    }

    // Hydraulic geometry of WaterSystem: stage = 0.6 * bankfull * (Q/Qmean)^0.4, mean velocity = Q / (0.66 * width * stage).
    float bankfullOf(int ri) const { return w_.water().bankfullDepth(ri); }
    float baseflowOf(int ri) const { return w_.layout().rivers[static_cast<size_t>(ri)].perennial ? kPerennialBaseflow : kEphemeralBaseflow; }
    float stageAtFraction(int ri, float f) const { return 0.6f * bankfullOf(ri) * std::pow(std::max(1e-4f, f), 0.4f); }
    float fractionAtStage(int ri, float s) const { return s <= 0.0f ? 0.0f : std::pow(s / std::max(0.01f, 0.6f * bankfullOf(ri)), 2.5f); }
    float currentAtFraction(int ri, float f) const
    {
        const RiverDesc& rd = w_.layout().rivers[static_cast<size_t>(ri)];
        const float width = rd.points.empty() ? 10.0f : rd.points.front().width;
        const float area = std::max(0.5f, width * std::max(0.1f, stageAtFraction(ri, f)) * 0.66f);
        return clampf(f * rd.meanDischargeM3s / area, 0.0f, 3.5f);
    }
    // Largest discharge fraction at which the mean current stays at or below vmax (current grows as Q^0.6).
    float fractionAtCurrent(int ri, float vmax) const
    {
        const RiverDesc& rd = w_.layout().rivers[static_cast<size_t>(ri)];
        const float width = rd.points.empty() ? 10.0f : rd.points.front().width;
        const float k = 0.66f * width * 0.6f * bankfullOf(ri) / std::max(0.01f, rd.meanDischargeM3s); // v = f^0.6 / k
        return std::pow(std::max(0.0f, vmax * k), 1.0f / 0.6f);
    }

    // No steep edge (slope above steepDeg), no cutbank-cliff ground and no sudden drop (> 2 m) within 'setback' metres.
    bool edgeSetbackOk(const Vec2& p, float setback, float steepDeg = kEdgeSteepDeg) const
    {
        const Terrain& t = w_.terrain();
        const float z0 = t.heightAt(p);
        if (t.slopeDegAt(p) > steepDeg || t.habitatAt(p) == Habitat::CutbankCliff)
        {
            return false;
        }
        for (const float r : {4.0f, 8.0f, 12.0f, 16.0f, setback})
        {
            if (r > setback)
            {
                continue;
            }
            for (int a = 0; a < 16; ++a)
            {
                const Vec2 q = t.clampToBounds(p + Vec2::fromHeading(kTwoPi * static_cast<float>(a) / 16.0f) * r, 1.0f);
                if (t.slopeDegAt(q) > steepDeg || t.heightAt(q) < z0 - 2.0f || t.habitatAt(q) == Habitat::CutbankCliff)
                {
                    return false;
                }
            }
        }
        return true;
    }

    // Reachable from the base camp on foot, crossing the main river by packraft at the selected crossing points.
    bool reachableWithPackraft(const Vec2& p) const
    {
        size_t c = 0;
        return !footPackFromBase_.dist.empty() && snapPassable(footPackCost_, p, 120.0f, c) && std::isfinite(footPackFromBase_.dist[c]);
    }

    // A hide must not stand out against the sky as seen from its target: the background behind it along the sight line
    // (terrain plus woodland, out to 3 km or the map edge) rises above the line from the target's eye through the top of
    // the hide, or the hide itself stands under woodland. A hide on a crest or a cliff edge above the target fails.
    bool belowSkyline(const Vec2& hide, float hideTopZ, const Vec2& target, float targetEyeZ) const
    {
        const Terrain& t = w_.terrain();
        const float dist = distance(hide, target);
        if (dist < 1.0f || backdropH_.sample(hide) > 3.0f)
        {
            return true;
        }
        const Vec2 dir = (hide - target).normalized();
        const float hideTan = (hideTopZ - targetEyeZ) / dist;
        for (float s = 8.0f; s <= 3000.0f; s += (s < 400.0f ? 8.0f : 24.0f))
        {
            const Vec2 q = hide + dir * s;
            if (!t.bounds().contains(q))
            {
                break;
            }
            if ((t.heightAt(q) + backdropH_.sample(q) - targetEyeZ) / (dist + s) >= hideTan)
            {
                return true;
            }
        }
        return false;
    }

    // Shallowest water left at the main-river crossing points when the river is at its simulated minimum.
    float minCrossingDepthAtBaseflow() const
    {
        const float drop = w_.water().riverStage(0) - stageAtFraction(0, kPerennialBaseflow);
        float m = kInf;
        for (const Poi& p : pois_)
        {
            if (p.type == "ford" && p.metrics.get("river").asString("") == "main")
            {
                m = std::min(m, p.metrics.getFloat("max_depth_m", 0.0f) - drop);
            }
        }
        return std::isfinite(m) ? std::max(0.0f, m) : 0.0f;
    }

    // ------------------------------------------------------------------ helpers
    float terrainZ(const Vec2& p) const { return w_.terrain().heightAt(p); }

    bool insideMargin(const Vec2& p, float m) const { return p.x > m && p.y > m && p.x < size_ - m && p.y < size_ - m; }

    // Nearest cell with a finite cost within radius (spiral search on the analysis grid).
    bool snapPassable(const Field& cost, const Vec2& p, float radius, size_t& out) const
    {
        int cx = 0;
        int cy = 0;
        cost.cellOf(p, cx, cy);
        const int rmax = static_cast<int>(std::ceil(radius / cost.cell));
        float best = kInf;
        for (int dy = -rmax; dy <= rmax; ++dy)
        {
            for (int dx = -rmax; dx <= rmax; ++dx)
            {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || y < 0 || x >= cost.n || y >= cost.n || !std::isfinite(cost.at(x, y)))
                {
                    continue;
                }
                const float d = distance(cost.center(x, y), p);
                if (d <= radius && d < best)
                {
                    best = d;
                    out = cost.idx(x, y);
                }
            }
        }
        return std::isfinite(best);
    }

    std::string nextId(const std::string& prefix)
    {
        const int k = ++idCounters_[prefix];
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s%02d", prefix.c_str(), k);
        return buf;
    }

    Poi& addPoi(const std::string& prefix, const std::string& type, const Vec2& p)
    {
        Poi poi;
        poi.id = nextId(prefix);
        poi.type = type;
        poi.pos = p;
        poi.z = terrainZ(p);
        pois_.push_back(poi);
        return pois_.back();
    }

    bool farFromPois(const Vec2& p, const std::string& type, float minDist) const
    {
        for (const Poi& q : pois_)
        {
            if ((type.empty() || q.type == type) && distance(q.pos, p) < minDist)
            {
                return false;
            }
        }
        return true;
    }

    const char* habitatFr(size_t cell) const { return habitatLabelFr(static_cast<Habitat>(habitat_[cell])); }

    // ------------------------------------------------------------------ viewshed
    // Radial horizon sweep over the heightfield: a target point (ground + targetH) is visible when its
    // elevation angle exceeds every blocking angle before it. Dense canopy acts as an opaque layer
    // (blockH_); foliage extinction is integrated over the last 64 m of the sight line.
    Viewshed viewshed(const Vec2& eye, float eyeH, float radius, float targetH, int azimuths, float stepM, bool vegetation, float skipBlockM)
    {
        const Terrain& t = w_.terrain();
        const VegetationSystem& veg = w_.vegetation();
        const float eyeZ = t.heightAt(eye) + eyeH;
        std::vector<u32> touched;
        std::vector<float> g;
        std::vector<Vec2> pts;
        for (int a = 0; a < azimuths; ++a)
        {
            const Vec2 dir = Vec2::fromHeading(kTwoPi * static_cast<float>(a) / static_cast<float>(azimuths));
            float maxBlock = -kInf;
            g.clear();
            pts.clear();
            for (float r = stepM; r <= radius; r += stepM)
            {
                const Vec2 p = eye + dir * r;
                if (!t.contains(p))
                {
                    break;
                }
                const float gz = t.heightAt(p);
                g.push_back(gz);
                pts.push_back(p);
                const float tgt = (gz + targetH - eyeZ) / r;
                if (tgt >= maxBlock)
                {
                    float trans = 1.0f;
                    if (vegetation)
                    {
                        float tau = 0.0f;
                        const int k0 = static_cast<int>(g.size()) - 1;
                        for (int k = 0; k <= 8 && k0 - k >= 0; ++k)
                        {
                            const float rk = r - static_cast<float>(k) * stepM;
                            if (rk < skipBlockM)
                            {
                                break;
                            }
                            const float lineZ = eyeZ + (gz + targetH - eyeZ) * (rk / r);
                            const float hAbove = k == 0 ? targetH * 0.5f : lineZ - g[static_cast<size_t>(k0 - k)];
                            if (hAbove > 0.0f)
                            {
                                tau += veg.opticalDensity(pts[static_cast<size_t>(k0 - k)], hAbove) * stepM;
                            }
                        }
                        trans = std::exp(-tau);
                    }
                    int cx = 0;
                    int cy = 0;
                    if (activity_.cellOf(p, cx, cy))
                    {
                        const size_t ci = activity_.idx(cx, cy);
                        if (scratch_[ci] < 0.0f)
                        {
                            touched.push_back(static_cast<u32>(ci));
                            scratch_[ci] = trans;
                        }
                        else
                        {
                            scratch_[ci] = std::max(scratch_[ci], trans);
                        }
                    }
                }
                const float bh = (vegetation && r > skipBlockM) ? blockH_.sample(p) : 0.0f;
                maxBlock = std::max(maxBlock, (gz + bh - eyeZ) / r);
            }
        }
        Viewshed vs;
        vs.cells.reserve(touched.size());
        const float cellKm2 = kCellM * kCellM / 1e6f;
        for (const u32 ci : touched)
        {
            const float vis = scratch_[ci];
            vs.cells.push_back({ci, vis});
            if (vis > 0.3f)
            {
                vs.areaKm2 += cellKm2;
            }
            vs.value += vis * activity_.v[ci];
            scratch_[ci] = -1.0f;
        }
        return vs;
    }

    // ------------------------------------------------------------------ POIs: basecamp, exposures, oxbows
    void addBasecamp()
    {
        const Vec2 bc = w_.basecamp();
        Poi& p = addPoi("BC", "basecamp", bc);
        p.nameFr = "Camp de base";
        p.nameEn = "Base camp";
        p.radiusM = 40.0f;
        p.confidence = Confidence::Unknown;
        p.confidenceSubject = "élément de jeu (installation des chercheurs) — sans objet paléontologique";
        p.placement = "simulation_state";
        p.visibleAtStart = true;
        const size_t c = height_.indexOf(bc);
        DrinkSite ds;
        float dWater = -1.0f;
        if (w_.water().findDrinkSite(bc, 2000.0f, ds))
        {
            dWater = distance(ds.position, bc);
        }
        const float har = harAt(bc);
        const float bankfull = bankfullOf(0);
        const float floodStage = bankfull + std::max(0.0f, har); // the camp ground goes under water above this stage
        char buf[1100];
        std::snprintf(buf, sizeof(buf),
                      "Choisi par WorldSimulation : sol hors d'eau au moment du choix et peu pentu (%.1f°) en %s, à %.0f m de l'eau la plus proche "
                      "(cible 250 m : assez près pour la logistique, assez loin pour ne pas occuper un point d'abreuvement), près du centre de la "
                      "vallée. Le choix du cœur n'a pas de critère de crue : le camp est sur la plaine d'inondation basse, %.1f m au-dessus du "
                      "niveau de plein bord (classe de crue « %s ») ; il est inondé dès que le niveau de la rivière dépasse %.1f m (%.2f x plein "
                      "bord). Règle de jeu associée (rules.flood_alert) : alerte de crue au-dessus de %.1f m, qui interrompt tout saut de temps et "
                      "interdit le saut x360 au camp ; repli sur FR01. Antenne radio de %.0f m, recharge du véhicule, laboratoire de terrain.",
                      w_.terrain().slopeDegAt(bc), habitatFr(c), dWater, har, floodClass(har), floodStage, floodStage / bankfull,
                      kFloodAlertFraction * bankfull, kRadioMastM);
        p.rationaleFr = buf;
        p.metrics.set("distance_to_water_m", rnd(dWater, 0));
        p.metrics.set("habitat", habitatKey(static_cast<Habitat>(habitat_[c])));
        p.metrics.set("height_above_bankfull_m", rnd(har, 2));
        p.metrics.set("flood_stage_m", rnd(floodStage, 2));
        p.metrics.set("flood_discharge_fraction", rnd(fractionAtStage(0, floodStage), 2));
        p.metrics.set("radio_mast_m", kRadioMastM);
        basecampIdx_ = pois_.size() - 1;
    }

    // Nearest rover-reachable high ground that the simulated river can never flood (stage <= 1.6 x bankfull).
    void addFloodRefuge()
    {
        float best = kInf;
        size_t bestCell = 0;
        for (size_t i = 0; i < roverPract_.v.size(); ++i)
        {
            if (roverPract_.v[i] < 0.5f || har_.v[i] < 2.6f || slope_.v[i] > 8.0f || !std::isfinite(roverFromBase_.dist[i]) || riverDist_.v[i] < 250.0f ||
                static_cast<Habitat>(habitat_[i]) == Habitat::CutbankCliff ||
                !std::isfinite(footFromBase_.dist[i]) || (creekDist_.v[i] < 200.0f && creekHar_.v[i] < 2.6f))
            {
                continue;
            }
            if (roverFromBase_.dist[i] < best)
            {
                best = roverFromBase_.dist[i];
                bestCell = i;
            }
        }
        if (!std::isfinite(best))
        {
            return;
        }
        const Vec2 pos = height_.centerOf(bestCell);
        Poi& p = addPoi("FR", "flood_refuge", pos);
        p.nameFr = "Repli hors crue";
        p.nameEn = "Flood refuge";
        p.radiusM = 40.0f;
        p.confidence = Confidence::Unknown;
        p.confidenceSubject = "règle de sécurité du jeu — sans objet paléontologique";
        p.placement = "game_design";
        const float bcHar = harAt(w_.basecamp());
        char buf[700];
        std::snprintf(buf, sizeof(buf),
                      "Le camp de base est sur la plaine d'inondation (%.1f m au-dessus du niveau de plein bord, crue « %s ») : point de repli le plus "
                      "proche en véhicule (%.0f m de trajet effectif, ~%.0f min) sur un sol que le niveau maximal de la rivière simulée (1,6 x la "
                      "profondeur de plein bord) n'atteint jamais : %.1f m au-dessus du plein bord, pente %.0f°, %s. Le camp y replie le véhicule, "
                      "les échantillons et la radio lors des alertes de crue ; pendant une alerte, c'est le seul lieu où le saut x360 reste permis.",
                      bcHar, floodClass(bcHar), best, best / kRoverSpeedMs / 60.0f, har_.v[bestCell], slope_.v[bestCell], habitatFr(bestCell));
        p.rationaleFr = buf;
        p.metrics.set("height_above_bankfull_m", rnd(har_.v[bestCell], 2));
        p.metrics.set("drive_cost_from_basecamp_m", rnd(best, 0));
        p.links.push_back(pois_[basecampIdx_].id);
    }

    // Backswamps: low, wet distal floodplain (still water and swamp vegetation) — the calm-water habitat that
    // replaces oxbow lakes when the generator produced none for this seed.
    void addBackswamps()
    {
        std::vector<int> label(habitat_.size(), -1);
        std::vector<std::pair<int, size_t>> comps; // (cells, seed cell)
        for (size_t s0 = 0; s0 < habitat_.size(); ++s0)
        {
            if (label[s0] >= 0 || static_cast<Habitat>(habitat_[s0]) != Habitat::Backswamp)
            {
                continue;
            }
            const int id = static_cast<int>(comps.size());
            std::vector<size_t> stack{s0};
            label[s0] = id;
            int count = 0;
            const size_t sn = static_cast<size_t>(n_);
            while (!stack.empty())
            {
                const size_t c = stack.back();
                stack.pop_back();
                ++count;
                const int x = static_cast<int>(c % sn);
                const int y = static_cast<int>(c / sn);
                const int nx[4] = {x + 1, x - 1, x, x};
                const int ny[4] = {y, y, y + 1, y - 1};
                for (int k = 0; k < 4; ++k)
                {
                    if (nx[k] < 0 || ny[k] < 0 || nx[k] >= n_ || ny[k] >= n_)
                    {
                        continue;
                    }
                    const size_t j = height_.idx(nx[k], ny[k]);
                    if (label[j] < 0 && static_cast<Habitat>(habitat_[j]) == Habitat::Backswamp)
                    {
                        label[j] = id;
                        stack.push_back(j);
                    }
                }
            }
            comps.push_back({count, s0});
        }
        std::vector<int> order(comps.size());
        for (size_t k = 0; k < comps.size(); ++k)
        {
            order[k] = static_cast<int>(k);
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) { return comps[static_cast<size_t>(a)].first > comps[static_cast<size_t>(b)].first; });
        int made = 0;
        int perBank[2] = {0, 0};
        for (const int id : order)
        {
            const int cells = comps[static_cast<size_t>(id)].first;
            if (cells < 20)
            {
                break;
            }
            // Wettest point of the patch (lowest height above the bankfull reference) at least kEdgeMarginM from the map edge.
            size_t best = label.size();
            for (size_t i = 0; i < label.size(); ++i)
            {
                if (label[i] == id && insideMargin(height_.centerOf(i), kEdgeMarginM) && (best == label.size() || har_.v[i] < har_.v[best]))
                {
                    best = i;
                }
            }
            if (best == label.size())
            {
                continue;
            }
            const Vec2 pos = height_.centerOf(best);
            const int bank = campBank(pos) ? 0 : 1;
            if (perBank[bank] >= 1 || !farFromPois(pos, "backswamp", 1000.0f))
            {
                continue;
            }
            ++perBank[bank];
            Poi& p = addPoi("BS", "backswamp", pos);
            const int k = idCounters_["BS"];
            p.nameFr = "Marais arrière " + std::to_string(k);
            p.nameEn = "Backswamp " + std::to_string(k);
            p.radiusM = std::min(400.0f, std::sqrt(static_cast<float>(cells)) * kCellM * 0.5f);
            p.confidence = Confidence::StronglySupported;
            p.confidenceSubject = "marais et étangs de plaine d'inondation (dépôts d'étangs, lignites et argiles organiques documentés)";
            p.placement = "generated_terrain";
            p.sources = {"fastovsky2016_hellcreek_primer", "johnson2002_hellcreek_megaflora"};
            char buf[700];
            std::snprintf(buf, sizeof(buf),
                          "Plus grande zone de marais arrière %s (%.2f km²) : plaine distale basse (point le plus humide à %.1f m sous/au-dessus du niveau "
                          "de plein bord), sols %s, inondée en premier lors des débordements. Eaux calmes, conifères de milieux humides et plantes "
                          "aquatiques dans la reconstitution ; habitat des taxons semi-aquatiques. Aucun bras mort n'a été généré pour cette graine : "
                          "ce marais tient lieu de milieu d'eau calme de la tranche verticale.",
                          bank == 0 ? "de la rive du camp" : "de la rive opposée", static_cast<float>(cells) * kCellM * kCellM / 1e6f, har_.v[best],
                          substrateLabelFr(static_cast<Substrate>(substrate_[best])));
            p.rationaleFr = buf;
            p.metrics.set("area_km2", rnd(static_cast<float>(cells) * kCellM * kCellM / 1e6f, 3));
            p.metrics.set("height_above_bankfull_m", rnd(har_.v[best], 2));
            p.metrics.set("water_noise_db", rnd(noise_.v[best], 1));
            if (++made >= 2)
            {
                break;
            }
        }
    }

    void addExposureSites()
    {
        const WorldLayout& layout = w_.layout();
        const Terrain& t = w_.terrain();
        for (const FossilSite& site : w_.research().paleontology().sites())
        {
            Poi& p = addPoi("EX", "exposure_site", site.position);
            p.nameFr = site.nameFr;
            p.nameEn = "Exposure " + site.nameFr.substr(site.nameFr.size() - 1);
            p.radiusM = 45.0f;
            p.confidence = Confidence::StronglySupported;
            p.placement = "generated_terrain";
            p.sources = {"fastovsky2016_hellcreek_primer", "langbein1966_meanders", "behrensmeyer1978_bone_weathering"};
            p.confidenceSubject = "principe : berge concave érodée exposant des dépôts fluviatiles plus anciens (géomorphologie des rivières à "
                                  "méandres) ; la hauteur du bluff est une hypothèse de jeu du générateur (metrics.bluff_height_basis)";
            int ri = -1;
            int pi = -1;
            float d = 0.0f;
            float relief = 0.0f;
            float top = t.heightAt(site.position);
            float surface = top;
            Vec2 face{1.0f, 0.0f};
            if (!layout.rivers.empty() && rivers_.nearest(site.position, 0, ri, pi, d))
            {
                const RiverPoint& rp = layout.rivers[static_cast<size_t>(ri)].points[static_cast<size_t>(pi)];
                surface = rp.floodplainZ - rp.depth + w_.water().riverStage(ri);
                for (int a = 0; a < 16; ++a)
                {
                    for (const float r : {15.0f, 30.0f, 45.0f})
                    {
                        top = std::max(top, t.heightAt(site.position + Vec2::fromHeading(kTwoPi * static_cast<float>(a) / 16.0f) * r));
                    }
                }
                relief = top - surface;
                face = (rp.position - site.position).normalized();
            }
            // Access point: walking from the site towards the channel, the first dry ground at least 2.5 m below the
            // bluff top is the bench at the foot of the face (levee level), above the channel bank that drops into the
            // water. It is reached laterally along the levee, from the flanks of the bluff.
            Vec2 access = site.position;
            bool hasBench = false;
            for (float s = 1.0f; s <= 70.0f; s += 1.0f)
            {
                const Vec2 q = site.position + face * s;
                if (w_.water().query(q, t).isWater)
                {
                    break;
                }
                if (t.heightAt(q) < top - 2.5f)
                {
                    access = q;
                    hasBench = true;
                    break;
                }
            }
            const float benchZ = t.heightAt(access);
            const float faceH = hasBench ? top - benchZ : relief;
            const float bankH = hasBench ? benchZ - surface : 0.0f;
            p.heading = face.heading();
            p.hasHeading = true;
            char buf[1500];
            std::snprintf(buf, sizeof(buf),
                          "Berge concave d'un méandre (bluff) : l'érosion latérale du chenal entaille la plaine et expose une coupe d'environ %.1f m "
                          "au-dessus de l'eau : talus du bluff (%.1f m, du sommet à la banquette) au-dessus de la berge du chenal (%.1f m, de la "
                          "banquette à l'eau ; son pied est sous l'eau au niveau actuel). Dans la vallée maastrichtienne, ces couches sont des "
                          "alluvions plus anciennes de la même séquence fluviatile (sables de chenal, argiles de plaine d'inondation) : leur âge "
                          "relatif est sûr (plus anciennes que la surface), leur âge absolu n'est pas contraint (Information insuffisante — "
                          "recherche nécessaire). Hauteur du bluff : surélévation fixe de 3,5 m du générateur aux boucles serrées (hypothèse de "
                          "jeu) ; dans une plaine côtière en aggradation (pente 0,0004), une coupe plus haute que plein bord + levée (~5,3 m "
                          "au-dessus du lit) supposerait une terrasse plus ancienne. Le contenu fossile est généré (SPÉCULATIF) et tiré de la faune "
                          "simulée. Accès : %s ; le talus est une paroi bloquante en jeu (PCG_Noctis_Exposures).",
                          relief, faceH, bankH,
                          hasBench ? "banquette au pied du talus (metrics.access_point), rejointe latéralement par la levée depuis les flancs du bluff"
                                   : "aucune banquette sèche au pied du talus au niveau actuel : coupe observée depuis le sommet ou depuis l'eau "
                                     "(packraft)");
            p.rationaleFr = buf;
            p.metrics.set("section_height_m", rnd(relief, 1));
            p.metrics.set("bluff_face_height_m", rnd(faceH, 1));
            p.metrics.set("channel_bank_height_m", rnd(bankH, 1));
            p.metrics.set("bluff_height_basis", "game_assumption: surélévation fixe de 3,5 m de TerrainGenerator.cpp aux boucles serrées (|courbure| > 0,004 /m)");
            if (hasBench)
            {
                p.metrics.set("access_point", vec3Json(access, benchZ));
                p.metrics.set("access_point_ue_cm", ueJson(access, benchZ));
                p.metrics.set("access_kind", "bench_at_face_foot");
                p.metrics.set("access_point_height_above_water_m", rnd(benchZ - surface, 2));
                p.access = access;
                p.hasAccess = true;
            }
            else
            {
                p.metrics.set("access_kind", "top_or_water_only");
            }
            p.metrics.set("specimens_hidden", static_cast<int>(site.specimens.size()));
            p.metrics.set("fossil_content_confidence", confidenceKey(Confidence::Speculative));
            p.metrics.set("relative_age", "plus ancien que la surface actuelle (âge absolu : information insuffisante)");
            p.metrics.set("paleontology_site_id", site.id);
            p.metrics.set("face_heading_deg", rnd(face.heading() * kRadToDeg, 0));
        }
    }

    void addOxbows()
    {
        const WorldLayout& layout = w_.layout();
        std::vector<std::string> aquatic;
        for (const int si : w_.speciesInWorld())
        {
            const SpeciesDefinition& d = w_.db().speciesAt(si);
            if (d.sim.habitat[static_cast<int>(Habitat::OxbowLake)] >= 0.7f)
            {
                aquatic.push_back(d.id);
            }
        }
        for (size_t li = 0; li < layout.lakes.size(); ++li)
        {
            const LakeDesc& lake = layout.lakes[li];
            Poi& p = addPoi("OX", lake.ephemeral ? "pond" : "oxbow_lake", lake.center);
            p.nameFr = lake.name;
            p.nameEn = lake.ephemeral ? "Ephemeral pond " + std::to_string(li + 1) : "Oxbow lake " + std::to_string(li + 1);
            const float len = polylineLength(lake.centerline);
            p.radiusM = std::max(lake.halfWidth * 2.0f, len * 0.35f);
            p.confidence = Confidence::StronglySupported;
            p.confidenceSubject = "bras morts et étangs de plaine d'inondation (faciès documentés dans la formation)";
            p.placement = "generated_terrain";
            p.sources = {"fastovsky2016_hellcreek_primer", "johnson2002_hellcreek_megaflora", "langbein1966_meanders"};
            const WaterQuery wq = w_.water().query(lake.center, w_.terrain());
            p.z = wq.isWater ? wq.surfaceZ : p.z;
            char buf[700];
            std::snprintf(buf, sizeof(buf),
                          "Méandre abandonné du générateur (arc de %.0f m, demi-largeur %.0f m, profondeur actuelle %.1f m sur %.1f m max). Eau "
                          "calme : forte affinité d'habitat dans la simulation pour %zu taxons aquatiques ou semi-aquatiques ; rives boueuses qui "
                          "conservent bien les empreintes. Site d'observation et d'enregistrement (peu de bruit d'eau courante).",
                          len, lake.halfWidth, w_.water().lakeDepth(static_cast<int>(li)), lake.maxDepth, aquatic.size());
            p.rationaleFr = buf;
            p.metrics.set("arc_length_m", rnd(len, 0));
            p.metrics.set("depth_m", rnd(w_.water().lakeDepth(static_cast<int>(li)), 2));
            p.metrics.set("max_depth_m", rnd(lake.maxDepth, 2));
            p.metrics.set("habitat_affinity_species", strings(aquatic));
            p.metrics.set("water_noise_db", rnd(noise_.sample(lake.center), 1));
            p.metrics.set("centerline", polylineJson(simplify(lake.centerline, 6.0f)));
            p.metrics.set("lake_index", static_cast<int>(li));
        }
    }

    // ------------------------------------------------------------------ water access and corridors
    struct WaterInfo
    {
        Vec2 stand;           // last dry ground before the water, where an animal stands to drink
        float dropM = 0.0f;   // stand height above the water surface
        float bankDeg = 0.0f; // steepest 2 m segment of the 10 m transect ending at the water edge
        float surfaceZ = 0.0f;
        bool pointBar = false;
        float drinks = 0.0f;  // Drink events observed within ~100 m during the warm-up
        float activity = 0.0f;
        float drinkTerm = 0.0f;
        float herb = 0.0f;
        float suit = 0.0f;
    };

    // Usable drinking stance near a simulation drink site. WaterSystem's own test (Water.cpp, drinkSites_) only checks
    // the slope of the dry cell, so a site can sit on a cliff top above the water; here the stance must be the last dry
    // ground before the water, at most kDrinkMaxDropM above it, with a bank no steeper than kDrinkMaxBankSlopeDeg.
    bool drinkStance(const Vec2& site, WaterInfo& wi) const
    {
        const Terrain& t = w_.terrain();
        const WaterSystem& water = w_.water();
        auto nearest = [&](const Vec2& from, bool wantWet, Vec2& out) {
            for (float r = 0.0f; r <= 30.0f; r += 1.0f)
            {
                const int az = r < 0.5f ? 1 : 24;
                for (int a = 0; a < az; ++a)
                {
                    const Vec2 q = t.clampToBounds(from + Vec2::fromHeading(kTwoPi * static_cast<float>(a) / static_cast<float>(az)) * r, 1.0f);
                    if (water.query(q, t).isWater == wantWet)
                    {
                        out = q;
                        return true;
                    }
                }
            }
            return false;
        };
        Vec2 wet;
        Vec2 dry;
        if (!nearest(site, true, wet) || !nearest(wet, false, dry))
        {
            return false;
        }
        const Vec2 dir = (wet - dry).normalized();
        Vec2 stand = dry;
        Vec2 edge = wet;
        for (float s = 0.0f; s <= distance(dry, wet) + 0.01f; s += 0.5f)
        {
            const Vec2 q = dry + dir * s;
            if (water.query(q, t).isWater)
            {
                edge = q;
                break;
            }
            stand = q;
        }
        wi.stand = stand;
        wi.surfaceZ = water.query(edge, t).surfaceZ;
        wi.dropM = t.heightAt(stand) - wi.surfaceZ;
        wi.bankDeg = std::atan(std::max(0.0f, wi.dropM) / std::max(0.5f, distance(stand, edge))) * kRadToDeg;
        for (int k = 0; k < 5; ++k)
        {
            const Vec2 a = stand - dir * (2.0f * static_cast<float>(k));
            const Vec2 b = stand - dir * (2.0f * static_cast<float>(k + 1));
            wi.bankDeg = std::max(wi.bankDeg, std::atan(std::fabs(t.heightAt(b) - t.heightAt(a)) / 2.0f) * kRadToDeg);
        }
        wi.pointBar = t.habitatAt(stand) == Habitat::Sandbar;
        return true;
    }

    void rankWaterAccess()
    {
        std::vector<Candidate> c;
        std::vector<WaterInfo> info;
        const std::vector<DrinkSite>& sites = w_.water().drinkSites();
        for (size_t k = 0; k < sites.size(); ++k)
        {
            const DrinkSite& s = sites[k];
            if (s.slopeDeg > 18.0f || !insideMargin(s.position, kEdgeMarginM))
            {
                continue;
            }
            WaterInfo wi;
            if (!drinkStance(s.position, wi) || wi.dropM > kDrinkMaxDropM || wi.bankDeg > kDrinkMaxBankSlopeDeg)
            {
                ++drinkRejected_;
                continue;
            }
            int cx = 0;
            int cy = 0;
            drinkEv_.cellOf(wi.stand, cx, cy);
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (cx + dx >= 0 && cy + dy >= 0 && cx + dx < n_ && cy + dy < n_)
                    {
                        wi.drinks += drinkEv_.at(cx + dx, cy + dy);
                    }
                }
            }
            wi.activity = activity_.sample(wi.stand);
            wi.drinkTerm = drinkBlur_.sample(wi.stand);
            wi.herb = 0.5f * herbPresBlur_.sample(wi.stand);
            wi.suit = 0.3f * suitNorm_.sample(wi.stand);
            // Point bars: gentle sandy slope down to the water, the classic drinking and crossing approach.
            const float sc = wi.activity + wi.drinkTerm + wi.herb + wi.suit + (wi.pointBar ? 0.3f : 0.0f);
            c.push_back({wi.stand, sc, static_cast<int>(k)});
            info.push_back(wi);
        }
        std::vector<size_t> order(c.size());
        for (size_t k = 0; k < order.size(); ++k)
        {
            order[k] = k;
        }
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return c[a].score > c[b].score; });
        for (const size_t k : order)
        {
            const Candidate& cand = c[k];
            bool far = true;
            for (const Candidate& q : waterAccess_)
            {
                far = far && distance(q.pos, cand.pos) > 450.0f;
            }
            if (far)
            {
                waterAccess_.push_back(cand);
                waterInfo_.push_back(info[k]);
            }
            if (waterAccess_.size() >= 14)
            {
                break;
            }
        }
    }

    void buildCorridors()
    {
        // Foraging hotspots: local maxima of herbivore suitability and observed herbivore presence.
        std::vector<Candidate> cand;
        for (int y = 3; y < n_ - 3; ++y)
        {
            for (int x = 3; x < n_ - 3; ++x)
            {
                const size_t i = height_.idx(x, y);
                const Vec2 p = height_.center(x, y);
                const Habitat h = static_cast<Habitat>(habitat_[i]);
                if (water_.v[i] > 0.0f || riverDist_.v[i] < 150.0f || h == Habitat::Channel || h == Habitat::OxbowLake || h == Habitat::CutbankCliff ||
                    !insideMargin(p, 200.0f) || !std::isfinite(animalCost_.v[i]))
                {
                    continue;
                }
                const float s = 0.55f * suitNorm_.v[i] + 0.45f * herbPresBlur_.v[i];
                bool isMax = true;
                for (int dy = -3; dy <= 3 && isMax; ++dy)
                {
                    for (int dx = -3; dx <= 3; ++dx)
                    {
                        const size_t j = height_.idx(x + dx, y + dy);
                        if (0.55f * suitNorm_.v[j] + 0.45f * herbPresBlur_.v[j] > s)
                        {
                            isMax = false;
                            break;
                        }
                    }
                }
                if (isMax)
                {
                    cand.push_back({p, s, static_cast<int>(i)});
                }
            }
        }
        std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
        for (const Candidate& c : cand)
        {
            bool far = true;
            for (const Candidate& q : hotspots_)
            {
                far = far && distance(q.pos, c.pos) > 700.0f;
            }
            if (far)
            {
                hotspots_.push_back(c);
            }
            if (hotspots_.size() >= 10)
            {
                break;
            }
        }
        // Multi-source search from every water access point -> drinking routes.
        std::vector<size_t> sources;
        for (const Candidate& wa : waterAccess_)
        {
            size_t cell = 0;
            if (snapPassable(animalCost_, wa.pos, 96.0f, cell))
            {
                sources.push_back(cell);
            }
        }
        PathField toWater;
        dijkstra(animalCost_, sources, toWater);
        for (size_t h = 0; h < hotspots_.size(); ++h)
        {
            const std::vector<size_t> cells = tracePath(toWater, static_cast<size_t>(hotspots_[h].ref));
            if (cells.size() < 3)
            {
                continue;
            }
            Trail tr;
            tr.kind = "drinking_route";
            for (auto it = cells.rbegin(); it != cells.rend(); ++it)
            {
                tr.points.push_back(height_.centerOf(*it));
            }
            tr.fromLabel = "hotspot_" + std::to_string(h + 1);
            tr.toLabel = "eau";
            tr.costPerM = toWater.dist[static_cast<size_t>(hotspots_[h].ref)];
            addTrail(tr);
        }
        // Minimum spanning tree between hotspots -> movement links between foraging areas.
        const size_t nh = hotspots_.size();
        std::vector<PathField> fromHotspot(nh);
        for (size_t h = 0; h < nh; ++h)
        {
            dijkstra(animalCost_, {static_cast<size_t>(hotspots_[h].ref)}, fromHotspot[h]);
        }
        std::vector<u8> inTree(nh, 0);
        if (nh > 0)
        {
            inTree[0] = 1;
        }
        for (size_t step = 1; step < nh; ++step)
        {
            float best = kInf;
            size_t ba = 0;
            size_t bb = 0;
            for (size_t a = 0; a < nh; ++a)
            {
                if (!inTree[a])
                {
                    continue;
                }
                for (size_t b = 0; b < nh; ++b)
                {
                    if (inTree[b])
                    {
                        continue;
                    }
                    const float d = fromHotspot[a].dist[static_cast<size_t>(hotspots_[b].ref)];
                    if (d < best)
                    {
                        best = d;
                        ba = a;
                        bb = b;
                    }
                }
            }
            if (!std::isfinite(best))
            {
                break;
            }
            inTree[bb] = 1;
            const std::vector<size_t> cells = tracePath(fromHotspot[ba], static_cast<size_t>(hotspots_[bb].ref));
            Trail tr;
            tr.kind = "forage_link";
            for (const size_t c : cells)
            {
                tr.points.push_back(height_.centerOf(c));
            }
            tr.fromLabel = "hotspot_" + std::to_string(ba + 1);
            tr.toLabel = "hotspot_" + std::to_string(bb + 1);
            tr.costPerM = best;
            addTrail(tr);
        }
    }

    void addTrail(Trail tr)
    {
        if (tr.points.size() < 2)
        {
            return;
        }
        const std::vector<Vec2> fine = resample(tr.points, 8.0f);
        std::set<size_t> cells;
        float wearSum = 0.0f;
        float presSum = 0.0f;
        for (const Vec2& p : fine)
        {
            const float wv = w_.vegetation().trail(p);
            wearSum += wv;
            tr.wearMax = std::max(tr.wearMax, wv);
            const size_t c = height_.indexOf(p);
            presSum += herbPresBlur_.v[c];
            tr.maxWaterM = std::max(tr.maxWaterM, water_.v[c]);
            tr.maxSlopeDeg = std::max(tr.maxSlopeDeg, slope_.v[c]);
            if (static_cast<Habitat>(habitat_[c]) == Habitat::Channel && riverDist_.v[c] < 30.0f)
            {
                tr.crossesRiver = true;
            }
            cells.insert(c);
        }
        tr.wearMean = wearSum / static_cast<float>(fine.size());
        tr.presence = presSum / static_cast<float>(fine.size());
        // The cost model uses one generic 3000 kg herbivore; tag the corridor with the agent herbivores whose own
        // limits allow it (Locomotion.cpp: a non-swimmer stops where the water exceeds ~0.95 x its hip height).
        for (const int si : w_.speciesInWorld())
        {
            const SpeciesDefinition& d = w_.db().speciesAt(si);
            const SpeciesSim& s = d.sim;
            if (s.role != SimRole::Agent || isPredator(s) || s.locomotion.fly || s.population.initialCount <= 0.0f || s.adultMassKg < 100.0f)
            {
                continue;
            }
            const bool swims = s.locomotion.swim && s.locomotion.swimSpeedMs > 0.0f;
            const bool ok = (swims || tr.maxWaterM <= 0.95f * s.hipHeightM) && tr.maxSlopeDeg <= s.locomotion.maxSlopeDeg + 5.0f;
            (ok ? tr.usableBy : tr.notUsableBy).push_back(d.id);
        }
        for (const size_t c : cells)
        {
            corridorUse_.v[c] += 1.0f;
        }
        tr.points = simplify(chaikin(simplify(tr.points, 20.0f), 2), 4.0f);
        tr.lengthM = polylineLength(tr.points);
        tr.costPerM = tr.lengthM > 0.0f ? tr.costPerM / tr.lengthM : 0.0f;
        tr.id = nextId("TR");
        trails_.push_back(tr);
    }

    int trailHits(const Vec2& p, float radius) const
    {
        int hits = 0;
        for (const Trail& t : trails_)
        {
            hits += distanceToPolyline(p, t.points) < radius ? 1 : 0;
        }
        return hits;
    }

    // ------------------------------------------------------------------ fords / crossing points
    // The generated channel has no riffle / pool contrast (its depth is nearly uniform along the river), so these are
    // candidate crossing points ranked by banks, bed, trails and corridors at inflections between bends, not shallow
    // fords. Fordability is evaluated against the lowest stage the water model can reach (baseflow floor).
    struct CrossingSeg
    {
        size_t poi = 0;
        Vec2 a;
        Vec2 b;
        Vec2 normal;
        bool packraftNow = false;
    };

    void addFords()
    {
        const WorldLayout& layout = w_.layout();
        const Terrain& t = w_.terrain();
        const VegetationSystem& veg = w_.vegetation();
        struct FordCand
        {
            int river = 0;
            size_t point = 0;
            Vec2 pos;
            Vec2 mid;
            Vec2 normal;
            float score = 0.0f;
            float maxDepth = 0.0f;
            float wetWidth = 0.0f;
            float bankSlopeA = 0.0f;
            float bankSlopeB = 0.0f;
            float bankSlope = 0.0f;
            float firm = 0.0f;
            float wear = 0.0f;
            int hits = 0;
            float kappa = 0.0f;
            float along = 0.0f;
            bool gate = false;
            Substrate bed = Substrate::Sand;
            Vec2 bankA;
            Vec2 bankB;
        };
        for (int ri = 0; ri < static_cast<int>(std::min<size_t>(2, layout.rivers.size())); ++ri)
        {
            const RiverDesc& rd = layout.rivers[static_cast<size_t>(ri)];
            std::vector<FordCand> cands;
            for (size_t i = 10; i + 10 < rd.points.size(); i += 5)
            {
                const RiverPoint& rp = rd.points[i];
                if (!insideMargin(rp.position, kEdgeMarginM))
                {
                    continue;
                }
                ++sampledSections_[ri];
                if (std::fabs(rp.curvature) > kFordKappaMax)
                {
                    ++kappaRejected_[ri]; // apex of a bend: pool against the outer bank, never a crossing
                    continue;
                }
                FordCand fc;
                fc.river = ri;
                fc.point = i;
                fc.pos = rp.position;
                fc.normal = rp.tangent.perp();
                fc.kappa = rp.curvature;
                fc.along = rp.distanceAlong;
                // Wet / dry profile across the channel at 1 m steps; keep the wetted run nearest to the centreline
                // (the centreline sample itself can be dry, e.g. on the creek where the reference level drops).
                const int ns = static_cast<int>(std::floor(rp.width * 0.5f + 30.0f));
                const int count = 2 * ns + 1;
                std::vector<u8> wetS(static_cast<size_t>(count), 0);
                std::vector<float> depthS(static_cast<size_t>(count), 0.0f);
                for (int k = 0; k < count; ++k)
                {
                    const WaterQuery wq = w_.water().query(rp.position + fc.normal * static_cast<float>(k - ns), t);
                    wetS[static_cast<size_t>(k)] = wq.isWater ? 1 : 0;
                    depthS[static_cast<size_t>(k)] = wq.isWater ? wq.depth : 0.0f;
                }
                int runA = -1;
                int runB = -1;
                int bestOff = count;
                for (int k = 0; k < count;)
                {
                    if (!wetS[static_cast<size_t>(k)])
                    {
                        ++k;
                        continue;
                    }
                    const int a = k;
                    while (k < count && wetS[static_cast<size_t>(k)])
                    {
                        ++k;
                    }
                    const int b = k - 1;
                    if (a == 0 || b == count - 1)
                    {
                        continue; // not bounded by dry banks within the scan
                    }
                    const int off = (a <= ns && b >= ns) ? 0 : std::min(std::abs(a - ns), std::abs(b - ns));
                    if (off < bestOff)
                    {
                        bestOff = off;
                        runA = a;
                        runB = b;
                    }
                }
                if (runA < 0)
                {
                    ++drySections_[ri]; // "lit à sec" at the current stage: reported separately, never ranked as a ford
                    continue;
                }
                fc.bankA = rp.position + fc.normal * static_cast<float>(runA - 1 - ns);
                fc.bankB = rp.position + fc.normal * static_cast<float>(runB + 1 - ns);
                fc.mid = (fc.bankA + fc.bankB) * 0.5f;
                fc.wetWidth = static_cast<float>(runB - runA + 1);
                Vec2 deepest = fc.mid;
                for (int k = runA; k <= runB; ++k)
                {
                    if (depthS[static_cast<size_t>(k)] > fc.maxDepth)
                    {
                        fc.maxDepth = depthS[static_cast<size_t>(k)];
                        deepest = rp.position + fc.normal * static_cast<float>(k - ns);
                    }
                }
                for (int side = 0; side < 2; ++side)
                {
                    const float sgn = side == 0 ? -1.0f : 1.0f;
                    const Vec2 bank = side == 0 ? fc.bankA : fc.bankB;
                    float& s = side == 0 ? fc.bankSlopeA : fc.bankSlopeB;
                    for (const float k : {0.0f, 4.0f, 8.0f, 12.0f, 16.0f})
                    {
                        const Vec2 q = bank + fc.normal * (sgn * k);
                        s = std::max(s, t.slopeDegAt(q));
                        fc.wear = std::max(fc.wear, veg.trail(q));
                    }
                }
                fc.bankSlope = std::max(fc.bankSlopeA, fc.bankSlopeB);
                fc.bed = t.substrateAt(deepest);
                fc.firm = substrateFirmness(fc.bed);
                fc.hits = trailHits(fc.mid, 80.0f);
                const float pres = herbPresBlur_.sample(fc.mid + fc.normal * 40.0f) + herbPresBlur_.sample(fc.mid - fc.normal * 40.0f);
                // Gate: banks the player can use (packraft launch on the river, wading on the creek).
                fc.gate = fc.bankSlope <= (ri == 0 ? packraftBankSlopeDeg_ : kWadeBankSlopeMaxDeg);
                fc.score = -1.5f * fc.maxDepth - 0.04f * fc.bankSlope + 0.6f * fc.firm + 0.6f * (fc.wear >= kWearPassage ? 1.0f : 0.0f) +
                           0.8f * std::min(1.0f, static_cast<float>(fc.hits)) + 0.5f * (1.0f - std::fabs(fc.kappa) / kFordKappaMax) + 0.4f * pres -
                           0.005f * fc.wetWidth + (fc.gate ? 2.0f : 0.0f);
                cands.push_back(fc);
            }
            std::sort(cands.begin(), cands.end(), [](const FordCand& a, const FordCand& b) { return a.score > b.score; });
            const size_t want = ri == 0 ? 4 : 2;
            const float spacing = ri == 0 ? 700.0f : 450.0f;
            std::vector<FordCand> chosen;
            for (const FordCand& fc : cands)
            {
                bool ok = true;
                for (const FordCand& q : chosen)
                {
                    ok = ok && std::fabs(q.along - fc.along) > spacing && distance(q.pos, fc.pos) > 500.0f;
                }
                if (ok)
                {
                    chosen.push_back(fc);
                }
                if (chosen.size() >= want)
                {
                    break;
                }
            }
            const float stage = w_.water().riverStage(ri);
            const float bankfull = bankfullOf(ri);
            const float base = baseflowOf(ri);
            const float stageMin = stageAtFraction(ri, base);
            const float qNow = w_.water().riverDischarge(ri) / std::max(0.01f, rd.meanDischargeM3s);
            const float currentNow = currentAtFraction(ri, qNow);
            const float stagePack = std::min(stageAtFraction(ri, fractionAtCurrent(ri, packraftMaxCurrentMs_)), kFloodAlertFraction * bankfull);
            for (const FordCand& fc : chosen)
            {
                const WaterQuery mq = w_.water().query(fc.mid, t);
                Poi& p = addPoi("FD", "ford", fc.mid);
                const int k = idCounters_["FD"];
                p.nameFr = ri == 0 ? "Point de franchissement candidat " + std::to_string(k) + " (rivière)"
                                   : "Gué candidat " + std::to_string(k) + " (ruisseau)";
                p.nameEn = ri == 0 ? "Candidate river crossing " + std::to_string(k) : "Candidate creek ford " + std::to_string(k);
                p.radiusM = std::max(30.0f, fc.wetWidth * 0.6f);
                p.heading = fc.normal.heading();
                p.hasHeading = true;
                p.confidence = Confidence::Inferred;
                p.confidenceSubject = "lieu de traversée préférentiel prédit par le modèle (berges, fond, pistes, corridors) à une inflexion entre deux "
                                      "boucles ; le chenal généré n'a ni seuils ni mouilles (profondeur quasi uniforme) : point de franchissement "
                                      "candidat, pas un haut-fond";
                p.placement = "model_prediction";
                p.sources = {"leopold1953_hydraulic_geometry", "langbein1966_meanders"};
                p.z = mq.isWater ? std::max(mq.surfaceZ, terrainZ(fc.mid)) : terrainZ(fc.mid);
                const float stageFoot = stage - (fc.maxDepth - kWadeLimitM);
                const float stageRover = stage - (fc.maxDepth - fordingDepthM_);
                const bool bankFoot = fc.bankSlope <= kWadeBankSlopeMaxDeg;
                const bool bankRover = fc.bankSlope <= kRoverBankSlopeMaxDeg;
                const bool footNow = bankFoot && fc.maxDepth <= kWadeLimitM;
                const bool roverNow = bankRover && fc.maxDepth <= fordingDepthM_;
                const bool footEver = bankFoot && stageFoot >= stageMin;
                const bool roverEver = bankRover && stageRover >= stageMin;
                auto state = [&](bool bankOk, float limitDeg, float depthLimit, float stageMax) {
                    char s[260];
                    if (!bankOk)
                    {
                        std::snprintf(s, sizeof(s), "impraticable (berges à %.0f° > %.0f°)", fc.bankSlope, limitDeg);
                    }
                    else if (fc.maxDepth <= depthLimit)
                    {
                        std::snprintf(s, sizeof(s), "praticable maintenant");
                    }
                    else if (stageMax >= stageMin)
                    {
                        std::snprintf(s, sizeof(s), "seulement en basses eaux (niveau ≤ %.2f m, débit ≤ %.0f %% du débit moyen)", stageMax,
                                      100.0f * fractionAtStage(ri, stageMax));
                    }
                    else
                    {
                        std::snprintf(s, sizeof(s), "impraticable (débit minimal simulé %.0f %% : le niveau ne descend jamais sous %.2f m)", 100.0f * base,
                                      stageMin);
                    }
                    return std::string(s);
                };
                const std::string footTxt = state(bankFoot, kWadeBankSlopeMaxDeg, kWadeLimitM, stageFoot);
                const std::string roverTxt = state(bankRover, kRoverBankSlopeMaxDeg, fordingDepthM_, stageRover);
                const bool packUsable = ri == 0 && fc.bankSlope <= packraftBankSlopeDeg_;
                const bool packNow = packUsable && stage <= stagePack;
                char pack[400] = "";
                if (ri == 0)
                {
                    std::snprintf(pack, sizeof(pack),
                                  " Packraft (equipment.json, id packraft) : %s ; interdit au-dessus de %.2f m (courant moyen > %.1f m/s ou alerte de "
                                  "crue).",
                                  !packUsable ? "berges trop raides pour mettre à l'eau"
                                              : (packNow ? "traversée possible maintenant" : "traversée interdite au niveau actuel"),
                                  stagePack, packraftMaxCurrentMs_);
                }
                char buf[1800];
                std::snprintf(buf, sizeof(buf),
                              "Section transversale la mieux notée sur %s, à une inflexion entre deux boucles (courbure %.4f /m ; les sections de "
                              "|courbure| > %.4f /m sont exclues) : chenal mouillé de %.0f m, profondeur max %.2f m au niveau actuel (%.2f m), berges à "
                              "%.0f° et %.0f°, fond %s, %s, %d corridor(s) prédit(s) à moins de 80 m. Le chenal généré n'a pas de contraste seuil/mouille "
                              "(profondeur presque uniforme) : franchissement candidat, pas un haut-fond. Débit minimal simulé %.0f %% du débit moyen "
                              "(niveau jamais sous %.2f m). À pied (≤ %.1f m d'eau, berges ≤ %.0f°) : %s. Véhicule (gué %.1f m, berges ≤ %.0f°) : %s.%s",
                              ri == 0 ? "la rivière principale" : "le ruisseau affluent", fc.kappa, kFordKappaMax, fc.wetWidth, fc.maxDepth, stage,
                              fc.bankSlopeA, fc.bankSlopeB, substrateLabelFr(fc.bed),
                              fc.wear >= kWearPassage ? "passage d'animaux observé sur les berges" : "aucun passage observé sur les berges", fc.hits,
                              100.0f * base, stageMin, kWadeLimitM, kWadeBankSlopeMaxDeg, footTxt.c_str(), fordingDepthM_, kRoverBankSlopeMaxDeg,
                              roverTxt.c_str(), pack);
                p.rationaleFr = buf;
                p.metrics.set("river", ri == 0 ? "main" : "creek");
                p.metrics.set("crossing_kind", "candidate_crossing");
                p.metrics.set("river_point", static_cast<int>(fc.point));
                p.metrics.set("max_depth_m", rnd(fc.maxDepth, 2));
                p.metrics.set("wet_width_m", rnd(fc.wetWidth, 0));
                p.metrics.set("stage_now_m", rnd(stage, 2));
                p.metrics.set("bankfull_depth_m", rnd(bankfull, 2));
                p.metrics.set("stage_min_simulated_m", rnd(stageMin, 2));
                p.metrics.set("baseflow_fraction_min", rnd(base, 2));
                p.metrics.set("bank_slope_a_deg", rnd(fc.bankSlopeA, 1));
                p.metrics.set("bank_slope_b_deg", rnd(fc.bankSlopeB, 1));
                p.metrics.set("bank_slope_max_deg", rnd(fc.bankSlope, 1));
                p.metrics.set("bed_substrate", substrateKey(fc.bed));
                p.metrics.set("trail_wear", rnd(fc.wear, 3));
                p.metrics.set("corridor_hits", fc.hits);
                p.metrics.set("curvature_per_m", rnd(fc.kappa, 5));
                p.metrics.set("current_now_ms", rnd(currentNow, 2));
                p.metrics.set("fordable_on_foot_now", footNow);
                p.metrics.set("fordable_rover_now", roverNow);
                p.metrics.set("fordable_on_foot_ever", footEver);
                p.metrics.set("fordable_rover_ever", roverEver);
                p.metrics.set("never_fordable", !footEver && !roverEver);
                p.metrics.set("on_foot", footTxt);
                p.metrics.set("rover", roverTxt);
                if (footEver)
                {
                    p.metrics.set("stage_max_on_foot_m", rnd(std::max(0.0f, stageFoot), 2));
                    p.metrics.set("discharge_fraction_on_foot", rnd(fractionAtStage(ri, stageFoot), 3));
                }
                if (roverEver)
                {
                    p.metrics.set("stage_max_rover_m", rnd(std::max(0.0f, stageRover), 2));
                    p.metrics.set("discharge_fraction_rover", rnd(fractionAtStage(ri, stageRover), 3));
                }
                if (ri == 0)
                {
                    p.metrics.set("packraft_usable", packUsable);
                    p.metrics.set("packraft_ok_now", packNow);
                    p.metrics.set("packraft_stage_max_m", rnd(stagePack, 2));
                }
                p.metrics.set("bank_a", polylineJson({fc.bankA}));
                p.metrics.set("bank_b", polylineJson({fc.bankB}));
                p.score = fc.score;
                if (ri == 0)
                {
                    crossingSegs_.push_back({pois_.size() - 1, fc.bankA, fc.bankB, fc.normal, packNow});
                }
            }
        }
    }

    // Foot network plus packraft crossings of the main river at the selected crossing points (only those usable at the
    // current stage). The rover never crosses: fording depth 1.0 m against a river that never drops below ~1.7 m.
    void buildPackraftNetwork()
    {
        footPackCost_ = footCost_;
        packraftCell_.assign(footCost_.v.size(), 0);
        // Cost per metre relative to walking on flat ground: paddling speed, plus inflating / packing the boat
        // (setup time) spread over a typical two-cell crossing. Game assumption.
        const float paddleCost = kFootSpeedMs / std::max(0.1f, packraftPaddleMs_) + packraftSetupMin_ * 60.0f * kFootSpeedMs / (2.0f * kCellM);
        for (const CrossingSeg& cs : crossingSegs_)
        {
            if (!cs.packraftNow)
            {
                continue;
            }
            const Vec2 a = cs.a - cs.normal * 40.0f;
            const Vec2 b = cs.b + cs.normal * 40.0f;
            const float len = distance(a, b);
            for (float s = 0.0f; s <= len; s += 4.0f)
            {
                const size_t c = footPackCost_.indexOf(a + (b - a) * (s / std::max(1.0f, len)));
                if (!std::isfinite(footPackCost_.v[c]))
                {
                    footPackCost_.v[c] = paddleCost;
                    packraftCell_[c] = 1;
                }
            }
            packraftCrossings_.push_back(pois_[cs.poi].id);
        }
        size_t bc = 0;
        if (snapPassable(footPackCost_, w_.basecamp(), 300.0f, bc))
        {
            dijkstra(footPackCost_, {bc}, footPackFromBase_);
        }
        else
        {
            footPackFromBase_.dist.assign(footPackCost_.v.size(), kInf);
            footPackFromBase_.parent.assign(footPackCost_.v.size(), -1);
        }
        footLabel_ = labelComponents(footCost_);
        footPackLabel_ = labelComponents(footPackCost_);
    }

    // 8-connected components of the passable cells (-1 = impassable).
    std::vector<int> labelComponents(const Field& cost) const
    {
        std::vector<int> label(cost.v.size(), -1);
        int next = 0;
        const size_t sn = static_cast<size_t>(cost.n);
        for (size_t s0 = 0; s0 < cost.v.size(); ++s0)
        {
            if (label[s0] >= 0 || !std::isfinite(cost.v[s0]))
            {
                continue;
            }
            std::vector<size_t> stack{s0};
            label[s0] = next;
            while (!stack.empty())
            {
                const size_t c = stack.back();
                stack.pop_back();
                const int x = static_cast<int>(c % sn);
                const int y = static_cast<int>(c / sn);
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= cost.n || ny >= cost.n)
                        {
                            continue;
                        }
                        const size_t j = cost.idx(nx, ny);
                        if (label[j] < 0 && std::isfinite(cost.v[j]))
                        {
                            label[j] = next;
                            stack.push_back(j);
                        }
                    }
                }
            }
            ++next;
        }
        return label;
    }

    void addWaterAccess()
    {
        int added = 0;
        int perBank[2] = {0, 0};
        const auto it = eventCounts_.find("Drink");
        const int totalDrinks = it == eventCounts_.end() ? 0 : it->second;
        for (size_t k = 0; k < waterAccess_.size(); ++k)
        {
            const Candidate& wa = waterAccess_[k];
            const WaterInfo& wi = waterInfo_[k];
            const int bank = campBank(wa.pos) ? 0 : 1;
            if (perBank[bank] >= 2 || !farFromPois(wa.pos, "ford", 150.0f) || !farFromPois(wa.pos, "water_access", 600.0f))
            {
                continue;
            }
            ++perBank[bank];
            Poi& p = addPoi("WA", "water_access", wi.stand);
            p.nameFr = "Abreuvoir " + std::to_string(added + 1);
            p.nameEn = "Watering point " + std::to_string(added + 1);
            p.radiusM = 35.0f;
            p.confidence = Confidence::Inferred;
            p.confidenceSubject = "les grands herbivores doivent boire régulièrement (besoin physiologique) ; le point exact est un résultat de la simulation";
            p.placement = "simulation_state";
            const DrinkSite& ds = w_.water().drinkSites()[static_cast<size_t>(wa.ref)];
            char buf[1200];
            std::snprintf(buf, sizeof(buf),
                          "Point d'abreuvement à la lisière de l'eau%s : sol à %.2f m au-dessus de l'eau, berge à %.0f° au plus sur les 10 derniers "
                          "mètres (sites de la simulation rejetés au-delà de %.1f m ou de %.0f° : %d). Classement (score %.2f) = activité animale "
                          "%.2f + événements « boire » %.2f + présence d'herbivores %.2f + habitat favorable %.2f%s. Événements « boire » observés à "
                          "moins de ~100 m pendant le préchauffage : %.0f (sur %d dans toute la carte)%s. Cible pour affûts, pièges photographiques "
                          "et relevés d'empreintes.",
                          wi.pointBar ? " sur une barre de méandre" : "", wi.dropM, wi.bankDeg, kDrinkMaxDropM, kDrinkMaxBankSlopeDeg, drinkRejected_, wa.score,
                          wi.activity, wi.drinkTerm, wi.herb, wi.suit, wi.pointBar ? " + barre de méandre 0,30" : "", wi.drinks, totalDrinks,
                          wi.drinks < 0.5f ? " : le classement repose donc sur la présence et l'habitat, pas sur des abreuvements observés" : "");
            p.rationaleFr = buf;
            p.metrics.set("drink_body", ds.bodyId >= 1000 ? "lake" : (ds.bodyId == 0 ? "main_river" : "creek"));
            p.metrics.set("drink_events_warmup", rnd(wi.drinks, 0));
            p.metrics.set("drink_events_warmup_total", totalDrinks);
            p.metrics.set("height_above_water_m", rnd(wi.dropM, 2));
            p.metrics.set("bank_slope_10m_deg", rnd(wi.bankDeg, 1));
            p.metrics.set("point_bar", wi.pointBar);
            p.metrics.set("simulation_drink_site", polylineJson({ds.position}));
            JsonValue sc = JsonValue::makeObject();
            sc.set("activity", rnd(wi.activity, 3));
            sc.set("drink_events", rnd(wi.drinkTerm, 3));
            sc.set("herbivore_presence", rnd(wi.herb, 3));
            sc.set("habitat_suitability", rnd(wi.suit, 3));
            sc.set("point_bar", wi.pointBar ? 0.3 : 0.0);
            p.metrics.set("score_terms", sc);
            p.metrics.set("score", rnd(wa.score, 3));
            p.score = wa.score;
            if (++added >= 4)
            {
                break;
            }
        }
    }

    // ------------------------------------------------------------------ overlooks and blinds
    void addOverlooks()
    {
        std::vector<Candidate> cand;
        for (int y = 3; y < n_ - 3; ++y)
        {
            for (int x = 3; x < n_ - 3; ++x)
            {
                const size_t i = height_.idx(x, y);
                const Vec2 p = height_.center(x, y);
                if (water_.v[i] > 0.0f || har_.v[i] < 0.5f || slope_.v[i] > 25.0f || canopy_.v[i] > 0.55f || !insideMargin(p, kEdgeMarginM) ||
                    !farFromPois(p, "exposure_site", kOverlookExposureMinM))
                {
                    continue;
                }
                const float s = prominence_.v[i] + 0.15f * std::min(har_.v[i], 20.0f);
                bool isMax = true;
                for (int dy = -3; dy <= 3 && isMax; ++dy)
                {
                    for (int dx = -3; dx <= 3; ++dx)
                    {
                        const size_t j = height_.idx(x + dx, y + dy);
                        if (canopy_.v[j] <= 0.55f && water_.v[j] <= 0.0f && prominence_.v[j] + 0.15f * std::min(har_.v[j], 20.0f) > s)
                        {
                            isMax = false;
                            break;
                        }
                    }
                }
                if (isMax && prominence_.v[i] > 0.3f)
                {
                    // Refine to the highest 10-m sub-sample of the cell that keeps the setback from steep bank edges
                    // (no crest-edge overlooks) and the distance to the exposures.
                    Vec2 best = p;
                    float bz = -kInf;
                    for (int sy = -1; sy <= 1; ++sy)
                    {
                        for (int sx = -1; sx <= 1; ++sx)
                        {
                            const Vec2 q = p + Vec2{static_cast<float>(sx) * 10.0f, static_cast<float>(sy) * 10.0f};
                            if (terrainZ(q) > bz && w_.water().depthAt(q, w_.terrain()) <= 0.0f && edgeSetbackOk(q, kEdgeSetbackM) &&
                                farFromPois(q, "exposure_site", kOverlookExposureMinM))
                            {
                                bz = terrainZ(q);
                                best = q;
                            }
                        }
                    }
                    if (std::isfinite(bz))
                    {
                        cand.push_back({best, s, static_cast<int>(i)});
                    }
                }
            }
        }
        std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
        std::vector<Candidate> pool;
        for (const Candidate& c : cand)
        {
            bool far = true;
            for (const Candidate& q : pool)
            {
                far = far && distance(q.pos, c.pos) > 160.0f;
            }
            if (far)
            {
                pool.push_back(c);
            }
            if (pool.size() >= 120)
            {
                break;
            }
        }
        std::vector<Viewshed> sheds(pool.size());
        for (size_t k = 0; k < pool.size(); ++k)
        {
            sheds[k] = viewshed(pool[k].pos, 1.7f, kOverlookRadiusM, 2.0f, 360, 8.0f, true, 12.0f);
        }
        std::vector<float> covered(activity_.v.size(), 0.0f);
        std::vector<u8> used(pool.size(), 0);
        float firstGain = 0.0f;
        for (int pick = 0; pick < 5; ++pick)
        {
            float bestGain = 0.0f;
            size_t best = pool.size();
            for (size_t k = 0; k < pool.size(); ++k)
            {
                if (used[k] || !farFromPois(pool[k].pos, "overlook", 900.0f))
                {
                    continue;
                }
                float gain = 0.0f;
                for (const auto& c : sheds[k].cells)
                {
                    gain += std::max(0.0f, c.second - covered[c.first]) * activity_.v[c.first];
                }
                if (gain > bestGain)
                {
                    bestGain = gain;
                    best = k;
                }
            }
            if (best >= pool.size() || (pick > 0 && bestGain < 0.05f * firstGain))
            {
                break;
            }
            if (pick == 0)
            {
                firstGain = bestGain;
            }
            used[best] = 1;
            for (const auto& c : sheds[best].cells)
            {
                covered[c.first] = std::max(covered[c.first], c.second);
            }
            const Vec2 pos = pool[best].pos;
            const size_t cell = height_.indexOf(pos);
            Poi& p = addPoi("OV", "overlook", pos);
            const int k = idCounters_["OV"];
            p.nameFr = "Point de vue " + std::to_string(k);
            p.nameEn = "Overlook " + std::to_string(k);
            p.radiusM = 20.0f;
            p.confidence = Confidence::Unknown;
            p.confidenceSubject = "emplacement d'observation (choix méthodologique du joueur) — sans objet paléontologique";
            p.placement = "model_prediction";
            char buf[800];
            std::snprintf(buf, sizeof(buf),
                          "Sélection gloutonne par couverture maximale : parmi %zu hauteurs candidates (proéminence locale, hors forêt dense, hors "
                          "zone inondable, à %.0f m au moins de tout affleurement et en retrait de %.0f m de toute arête de berge), ce point ajoute "
                          "le plus d'« activité animale visible » non encore couverte (gain %.1f). Champ de vision calculé à 1,7 m d'œil sur %.0f m "
                          "(horizon du relief, canopée dense opaque, feuillage atténuant près de la cible) : %.2f km² visibles. Proéminence %.1f m, %s.",
                          pool.size(), kOverlookExposureMinM, kEdgeSetbackM, bestGain, kOverlookRadiusM, sheds[best].areaKm2, prominence_.v[cell],
                          habitatFr(cell));
            p.rationaleFr = buf;
            p.metrics.set("eye_height_m", 1.7);
            p.metrics.set("viewshed_radius_m", kOverlookRadiusM);
            p.metrics.set("visible_area_km2", rnd(sheds[best].areaKm2, 3));
            p.metrics.set("activity_value", rnd(sheds[best].value, 2));
            p.metrics.set("marginal_gain", rnd(bestGain, 2));
            p.metrics.set("prominence_m", rnd(prominence_.v[cell], 2));
            p.metrics.set("height_above_river_m", rnd(harAt(pos), 2));
            float nearestEx = kInf;
            for (const Poi& q : pois_)
            {
                if (q.type == "exposure_site")
                {
                    nearestEx = std::min(nearestEx, distance(q.pos, pos));
                }
            }
            if (std::isfinite(nearestEx))
            {
                p.metrics.set("nearest_exposure_m", rnd(nearestEx, 0));
            }
            p.metrics.set("edge_setback_min_m", kEdgeSetbackM);
            p.score = bestGain;
            overlookSheds_.push_back({pois_.size() - 1, sheds[best]});
        }
    }

    void addBlinds()
    {
        struct Target
        {
            size_t poi = 0;
            float score = 0.0f;
        };
        std::vector<Target> targets;
        for (size_t i = 0; i < pois_.size(); ++i)
        {
            const Poi& p = pois_[i];
            if (p.type == "ford" || p.type == "water_access" || p.type == "oxbow_lake" || p.type == "backswamp")
            {
                float s = 0.0f;
                for (int a = 0; a < 8; ++a)
                {
                    s += activity_.sample(p.pos + Vec2::fromHeading(kTwoPi * static_cast<float>(a) / 8.0f) * 100.0f);
                }
                targets.push_back({i, s + drinkBlur_.sample(p.pos)});
            }
        }
        std::sort(targets.begin(), targets.end(), [](const Target& a, const Target& b) { return a.score > b.score; });
        const float fromRad = w_.environment().sim.prevailingWindDeg * kDegToRad;
        const Vec2 downwind = -Vec2{std::sin(fromRad), std::cos(fromRad)};
        const Terrain& t = w_.terrain();
        const VegetationSystem& veg = w_.vegetation();
        int made = 0;
        std::vector<Vec2> used;
        for (int pass = 0; pass < 2 && made < 4; ++pass)
        {
        for (const Target& tg : targets)
        {
            const Poi target = pois_[tg.poi];
            bool far = true;
            for (const Vec2& u : used)
            {
                far = far && distance(u, target.pos) > 500.0f;
            }
            if (!far)
            {
                continue;
            }
            const WaterQuery twq = w_.water().query(target.pos, t);
            const float tZ = (twq.isWater ? twq.surfaceZ : t.heightAt(target.pos)) + 1.5f;
            float bestScore = 0.05f;
            Vec2 best;
            float bestTrans = 0.0f;
            float bestCover = 0.0f;
            float bestDown = 0.0f;
            bool bestNatural = false;
            bool bestRaised = false;
            float bestEye = 1.2f;
            for (const float r : {70.0f, 100.0f, 130.0f, 160.0f, 200.0f, 240.0f})
            {
                for (int a = 0; a < 32; ++a)
                {
                    const Vec2 dir = Vec2::fromHeading(kTwoPi * static_cast<float>(a) / 32.0f);
                    const Vec2 b = target.pos + dir * r;
                    if (!insideMargin(b, kEdgeMarginM) || w_.water().depthAt(b, t) > 0.0f || t.slopeDegAt(b) > 15.0f || harAt(b) < 0.3f)
                    {
                        ++blindReject_["terrain"];
                        continue;
                    }
                    if ((pass == 0 && !campBank(b)) || (pass == 1 && !reachableWithPackraft(b)))
                    {
                        ++blindReject_["access"];
                        continue;
                    }
                    if (!edgeSetbackOk(b, 15.0f, 35.0f))
                    {
                        ++blindReject_["edge_setback"];
                        continue;
                    }
                    const float cov = veg.cover(b);
                    const Vec2 toT = (target.pos - b).normalized();
                    // Ground hide (seated, eye 1.2 m) when the view clears the understorey; otherwise a raised hide
                    // (portable platform, eye 3.5 m, above the fern / shrub layer and below the canopy).
                    bool raised = false;
                    float eye = 1.2f;
                    float trans = 0.0f;
                    bool los = false;
                    for (const float e : {1.2f, 3.5f})
                    {
                        if (!t.lineOfSight(Vec3{b, t.heightAt(b) + e}, Vec3{target.pos, tZ}, 4.0f))
                        {
                            continue;
                        }
                        los = true;
                        const float tr = veg.transmittance(b + toT * 8.0f, target.pos, e);
                        if (tr >= 0.3f)
                        {
                            eye = e;
                            trans = tr;
                            raised = e > 2.0f;
                            break;
                        }
                    }
                    if (!los)
                    {
                        ++blindReject_["line_of_sight"];
                        continue;
                    }
                    if (trans < 0.3f)
                    {
                        ++blindReject_["foliage"];
                        continue;
                    }
                    if (!belowSkyline(b, t.heightAt(b) + eye + (raised ? 1.0f : 0.4f), target.pos, tZ))
                    {
                        ++blindReject_["skyline"];
                        continue;
                    }
                    const bool natural = !raised && cov >= 0.15f; // otherwise a portable hide (tent) replaces natural cover
                    const float down = 0.5f + 0.5f * dot(dir, downwind);
                    const float s = trans * (natural ? 0.4f + 0.6f * cov : 0.3f) * (raised ? 0.8f : 1.0f) * (0.3f + 0.7f * down) *
                                    (1.0f - 0.25f * std::fabs(r - 120.0f) / 80.0f);
                    if (s > bestScore)
                    {
                        bestScore = s;
                        best = b;
                        bestTrans = trans;
                        bestCover = cov;
                        bestDown = down;
                        bestNatural = natural;
                        bestRaised = raised;
                        bestEye = eye;
                    }
                }
            }
            if (bestScore <= 0.05f)
            {
                continue;
            }
            used.push_back(target.pos);
            Poi& p = addPoi("BL", "blind", best);
            const int k = idCounters_["BL"];
            p.nameFr = "Affût " + std::to_string(k);
            p.nameEn = "Blind " + std::to_string(k);
            p.radiusM = 6.0f;
            p.heading = (target.pos - best).heading();
            p.hasHeading = true;
            p.confidence = Confidence::Unknown;
            p.confidenceSubject = "installation d'observation (choix méthodologique) — sans objet paléontologique";
            p.placement = "model_prediction";
            p.links.push_back(target.id);
            char buf[1200];
            std::snprintf(buf, sizeof(buf),
                          "%s pour %s (%s), à %.0f m : %s (couvert végétal %.2f), ligne de visée dégagée par le relief à %.1f m d'œil, "
                          "transmission du feuillage %.2f, sous le vent dominant (alignement %.2f) pour limiter la détection olfactive par les "
                          "animaux observés. En retrait de 15 m au moins de toute arête de berge ou de bluff et sous la ligne d'horizon vue depuis "
                          "la cible (relief ou boisement derrière l'affût : pas de silhouette sur le ciel). Distance choisie dans 70–240 m : assez "
                          "près pour la photo-identification au téléobjectif, assez loin pour ne pas provoquer de fuite (la distance réelle de "
                          "tolérance est une question de recherche du jeu).%s",
                          bestRaised ? "Affût surélevé" : "Affût au sol", target.id.c_str(), target.nameFr.c_str(), distance(best, target.pos),
                          bestRaised ? "plate-forme portable au-dessus des fougères et arbustes (à 1,2 m d'œil, le sous-bois masque la cible)"
                                     : (bestNatural ? "dissimulé dans la végétation" : "tente-affût portable (couvert naturel insuffisant)"),
                          bestCover, bestEye, bestTrans, bestDown, campBank(best) ? "" : " Sur la rive opposée : accès en packraft.");
            p.rationaleFr = buf;
            p.metrics.set("target", target.id);
            p.metrics.set("distance_to_target_m", rnd(distance(best, target.pos), 0));
            p.metrics.set("cover", rnd(bestCover, 2));
            p.metrics.set("hide_type", bestRaised ? "raised_platform" : (bestNatural ? "natural_cover" : "portable_hide"));
            p.metrics.set("foliage_transmittance", rnd(bestTrans, 2));
            p.metrics.set("downwind_alignment", rnd(bestDown, 2));
            p.metrics.set("eye_height_m", rnd(bestEye, 1));
            p.metrics.set("below_skyline", true);
            p.metrics.set("edge_setback_min_m", 15);
            p.score = bestScore;
            if (++made >= 4 || (pass == 0 && made >= 2))
            {
                break;
            }
        }
        }
        std::printf("Affûts : %d placés ; positions candidates rejetées :", made);
        for (const auto& r : blindReject_)
        {
            std::printf(" %s %d", r.first.c_str(), r.second);
        }
        std::printf("\n");
    }

    // ------------------------------------------------------------------ speculative nesting areas
    void addNestingAreas()
    {
        const ReconstructionProfile* prof = w_.profile();
        std::vector<std::pair<int, float>> species;
        for (const int si : w_.speciesInWorld())
        {
            const SpeciesDefinition& d = w_.db().speciesAt(si);
            const NestType nt = d.sim.reproduction.nest;
            if (d.sim.role != SimRole::Agent || nt == NestType::LiveBirth || nt == NestType::None || d.sim.population.initialCount <= 0.0f)
            {
                continue;
            }
            species.push_back({si, d.sim.population.initialCount});
        }
        std::sort(species.begin(), species.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (species.size() > 8)
        {
            species.resize(8);
        }
        Field dWater;
        dWater.init(n_, 1e5f);
        {
            std::vector<size_t> src;
            for (size_t i = 0; i < water_.v.size(); ++i)
            {
                if (wetFrac_.v[i] > 0.3f)
                {
                    src.push_back(i);
                }
            }
            Field unit;
            unit.init(n_, 1.0f);
            PathField pf;
            dijkstra(unit, src, pf, 3000.0f);
            for (size_t i = 0; i < dWater.v.size(); ++i)
            {
                dWater.v[i] = std::isfinite(pf.dist[i]) ? pf.dist[i] : 1e5f;
            }
        }
        std::vector<std::string> enabledIn;
        std::vector<std::string> disabledIn;
        for (const ReconstructionProfile& rp : w_.db().profiles())
        {
            (rp.enabled("nesting_grounds") ? enabledIn : disabledIn).push_back(rp.id);
        }
        for (const auto& sp : species)
        {
            const SpeciesDefinition& d = w_.db().speciesAt(sp.first);
            const NestType nt = d.sim.reproduction.nest;
            const bool aquaticNester = d.sim.habitat[static_cast<int>(Habitat::Riverbank)] >= 0.8f &&
                                       (d.sim.habitat[static_cast<int>(Habitat::Channel)] >= 0.6f || d.sim.habitat[static_cast<int>(Habitat::OxbowLake)] >= 0.6f);
            float best = 0.0f;
            size_t bestCell = 0;
            for (size_t i = 0; i < height_.v.size(); ++i)
            {
                const Vec2 p = height_.centerOf(i);
                if (water_.v[i] > 0.0f || !insideMargin(p, kEdgeMarginM))
                {
                    continue;
                }
                const float hw = d.sim.habitat[habitat_[i]];
                const float floodSafe = smoothstep(0.3f, 1.5f, har_.v[i]);
                const float sub = nestSubstrate(nt, static_cast<Substrate>(substrate_[i]));
                const float slopeF = 1.0f - smoothstep(4.0f, 12.0f, slope_.v[i]);
                const float dw = dWater.v[i];
                const float waterF = aquaticNester ? (1.0f - smoothstep(60.0f, 160.0f, dw)) * smoothstep(5.0f, 15.0f, dw)
                                                   : smoothstep(40.0f, 120.0f, dw) * (1.0f - smoothstep(900.0f, 1700.0f, dw));
                float s = hw * floodSafe * sub * slopeF * waterF * (1.0f - 0.4f * predPresBlur_.v[i]);
                for (const auto& ne : nestEvents_)
                {
                    if (ne.second == sp.first && distance(ne.first, p) < 100.0f)
                    {
                        s += 0.5f;
                    }
                }
                nestSuit_.v[i] = std::max(nestSuit_.v[i], s);
                if (s > best && farFromPois(p, "nesting_area", 400.0f))
                {
                    best = s;
                    bestCell = i;
                }
            }
            if (best < kNestSuitabilityMin)
            {
                nestNotPlaced_.push_back({d.id, best}); // no zone forced per species: the model finds no favourable ground
                continue;
            }
            const Vec2 pos = height_.centerOf(bestCell);
            Poi& p = addPoi("NA", "nesting_area", pos);
            p.nameFr = "Aire de nidification probable — " + d.commonNameFr;
            p.nameEn = "Probable nesting area — " + d.commonNameEn;
            p.radiusM = d.sim.social.groupSizeMean > 4.0f ? 220.0f : 120.0f;
            p.confidence = Confidence::Speculative;
            p.confidenceSubject = "localisation prédite par un modèle d'habitat ; aucun site de nidification n'est connu dans la Hell Creek";
            p.placement = "model_prediction";
            p.profileToggle = "nesting_grounds";
            const Claim* repro = d.claim("reproduction");
            // Evidence for the nest TYPE used by the simulation comes from sim_basis["reproduction.nest_type"], not from
            // the general reproduction claim: a game assumption is SPECULATIVE, a derived value is at best INFERRED and
            // never stronger than the species' reproduction claim. Sources = the references that basis cites (analogies).
            const std::string basis = d.basisFor("reproduction.nest_type");
            const bool assumed = d.isAssumption("reproduction.nest_type");
            Confidence nestEv = assumed ? Confidence::Speculative : Confidence::Inferred;
            if (repro && static_cast<int>(repro->confidence) > static_cast<int>(nestEv))
            {
                nestEv = repro->confidence;
            }
            {
                std::string tok;
                for (size_t ci = 0; ci <= basis.size(); ++ci)
                {
                    const char ch = ci < basis.size() ? basis[ci] : ' ';
                    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')
                    {
                        tok.push_back(ch);
                        continue;
                    }
                    if (tok.size() > 6 && w_.db().findSource(tok) && std::find(p.sources.begin(), p.sources.end(), tok) == p.sources.end())
                    {
                        p.sources.push_back(tok);
                    }
                    tok.clear();
                }
            }
            int nestsSeen = 0;
            for (const auto& ne : nestEvents_)
            {
                nestsSeen += (ne.second == sp.first && distance(ne.first, pos) < 300.0f) ? 1 : 0;
            }
            char buf[1800];
            std::snprintf(buf, sizeof(buf),
                          "HYPOTHÈSE (SPÉCULATIF). Zone la plus favorable du modèle pour %s (aptitude %.2f, seuil %.2f) — type de nid de la "
                          "simulation : %s, preuve de ce type : %s (%s%s). Critères : habitat préféré (%s, poids %.2f), sol hors d'atteinte des crues "
                          "de plein bord (%.1f m au-dessus), substrat %s, pente %.0f°, %.0f m de l'eau, faible activité de prédateurs. %d ponte(s) "
                          "simulée(s) à moins de 300 m pendant le préchauffage. Affichée seulement si le profil active « nesting_grounds » ; elle "
                          "oriente la recherche, elle ne prouve rien.",
                          d.scientificName.c_str(), best, kNestSuitabilityMin, nestTypeLabelFrLocal(nt), confidenceLabelFr(nestEv), basis.c_str(),
                          p.sources.empty() ? " ; aucune source citée pour ce type de nid dans la fiche espèce (sim_basis) : Information insuffisante — recherche nécessaire." : "",
                          habitatFr(bestCell), d.sim.habitat[habitat_[bestCell]], har_.v[bestCell], substrateLabelFr(static_cast<Substrate>(substrate_[bestCell])),
                          slope_.v[bestCell], dWater.v[bestCell], nestsSeen);
            p.rationaleFr = buf;
            p.metrics.set("species", d.id);
            p.metrics.set("nest_type", nestTypeKeyLocal(nt));
            p.metrics.set("nest_type_evidence", confidenceKey(nestEv));
            p.metrics.set("nest_type_basis", basis);
            if (repro)
            {
                JsonValue rc = JsonValue::makeObject();
                rc.set("confidence", confidenceKey(repro->confidence));
                rc.set("sources", strings(repro->sources));
                rc.set("note_fr", "fiche espèce, reproduction en général (oviparité, soins) : ne documente pas le type ni l'emplacement du nid");
                p.metrics.set("species_reproduction_claim", rc);
            }
            p.metrics.set("suitability", rnd(best, 3));
            JsonValue doy = JsonValue::makeArray();
            doy.push(rnd(d.sim.reproduction.breedingStartDoy, 0));
            doy.push(rnd(d.sim.reproduction.breedingEndDoy, 0));
            p.metrics.set("breeding_doy", doy);
            p.metrics.set("nests_simulated_within_300m", nestsSeen);
            p.metrics.set("enabled_in_profiles", strings(enabledIn));
            p.metrics.set("disabled_in_profiles", strings(disabledIn));
            p.metrics.set("enabled_in_current_profile", prof != nullptr && prof->enabled("nesting_grounds"));
        }
        nestSuit_.normalize();
    }

    void addCarcasses()
    {
        const double now = w_.clock().seconds();
        for (const Carcass& c : w_.carcasses().all())
        {
            if (!c.active || !insideMargin(c.position, kEdgeMarginM))
            {
                continue;
            }
            Poi& p = addPoi("CA", "carcass_site", c.position);
            const int k = idCounters_["CA"];
            p.nameFr = "Carcasse " + std::to_string(k);
            p.nameEn = "Carcass " + std::to_string(k);
            p.radiusM = 15.0f;
            p.confidence = Confidence::Established;
            p.confidenceSubject = "les carcasses attirent les charognards et se décomposent par stades (taphonomie actualiste)";
            p.placement = "simulation_state";
            p.dynamicState = true;
            p.sources = {"behrensmeyer1978_bone_weathering"};
            const float days = static_cast<float>((now - c.timeOfDeath) / 86400.0);
            char buf[600];
            std::snprintf(buf, sizeof(buf),
                          "État de la simulation à l'export (graine %llu) : carcasse au stade « %s », morte depuis %.0f jours environ. Point chaud "
                          "dynamique : charognards, insectes nécrophages, traces de morsures. Elle évoluera (décomposition, dispersion, "
                          "enfouissement par les crues) : le niveau ne doit pas la figer.",
                          static_cast<unsigned long long>(opt_.seed), decayStageLabelFr(c.stage), days);
            p.rationaleFr = buf;
            p.metrics.set("truth_species_designer_only", c.speciesIndex >= 0 ? w_.db().speciesAt(c.speciesIndex).id : std::string("unknown"));
            p.metrics.set("days_since_death", rnd(days, 1));
            p.metrics.set("mass_at_death_kg", rnd(c.massAtDeathKg, 0));
            p.metrics.set("scatter", rnd(c.scatter, 2));
        }
    }

    // ------------------------------------------------------------------ sensors
    void addSensors()
    {
        // Camera traps (PIR, ~20 m range): on predicted corridors (at least one), preferably near a crossing point or a
        // watering point, facing the trail. Trail wear saturates within minutes of loitering (VegetationSystem::trample),
        // so it only counts as "passage observed" (>= kWearPassage); the magnitude comes from herbivore presence sampled
        // every 10 min of the warm-up, which is normalised by the warm-up duration (Field::normalize of the counts).
        {
            struct TrapCand
            {
                Vec2 pos;
                float score = 0.0f;
                size_t cell = 0;
                int target = -1; // nearest ford / water_access within 200 m
                float targetDist = 0.0f;
            };
            std::vector<TrapCand> cand;
            for (size_t i = 0; i < corridorUse_.v.size(); ++i)
            {
                const Vec2 p = height_.centerOf(i);
                if (corridorUse_.v[i] < 1.0f || water_.v[i] > 0.0f || !insideMargin(p, kEdgeMarginM))
                {
                    continue;
                }
                TrapCand tc;
                tc.pos = p;
                tc.cell = i;
                float bestD = 200.0f;
                for (size_t q = 0; q < pois_.size(); ++q)
                {
                    const Poi& o = pois_[q];
                    const float dd = distance(o.pos, p);
                    if ((o.type == "ford" || o.type == "water_access") && dd < bestD)
                    {
                        bestD = dd;
                        tc.target = static_cast<int>(q);
                    }
                }
                tc.targetDist = bestD;
                if (tc.target >= 0)
                {
                    // The trap goes on the side the researcher reaches without crossing: camp bank for a main-river
                    // crossing point, the target's own bank for a watering point.
                    const Poi& o = pois_[static_cast<size_t>(tc.target)];
                    const bool mainCrossing = o.type == "ford" && o.metrics.get("river").asString("") == "main";
                    if ((mainCrossing && !campBank(p)) || (!mainCrossing && campBank(p) != campBank(o.pos)))
                    {
                        continue;
                    }
                }
                if (!campBank(p) && !reachableWithPackraft(p))
                {
                    continue;
                }
                tc.score = 0.4f * std::min(corridorUse_.v[i], 3.0f) + 1.0f * herbPresBlur_.v[i] + (tc.target >= 0 ? 0.8f : 0.0f) +
                           (trail_.v[i] >= kWearPassage ? 0.3f : 0.0f);
                cand.push_back(tc);
            }
            std::sort(cand.begin(), cand.end(), [](const TrapCand& a, const TrapCand& b) { return a.score > b.score; });
            int made = 0;
            int perBank[2] = {0, 0};
            for (const TrapCand& c : cand)
            {
                if (!farFromPois(c.pos, "camera_trap", 500.0f) || !farFromPois(c.pos, "blind", 60.0f))
                {
                    continue;
                }
                const int bank = campBank(c.pos) ? 0 : 1;
                if (perBank[bank] >= 2)
                {
                    continue;
                }
                ++perBank[bank];
                // Local trail direction from the nearest corridor polyline.
                Vec2 dir{1.0f, 0.0f};
                float bestD = kInf;
                for (const Trail& tr : trails_)
                {
                    for (size_t s = 1; s < tr.points.size(); ++s)
                    {
                        const float d = distanceToSegment(c.pos, tr.points[s - 1], tr.points[s]);
                        if (d < bestD)
                        {
                            bestD = d;
                            dir = (tr.points[s] - tr.points[s - 1]).normalized();
                        }
                    }
                }
                const Vec2 side = dir.perp();
                const Vec2 pos = c.pos + side * 6.0f;
                const std::string targetId = c.target >= 0 ? pois_[static_cast<size_t>(c.target)].id : std::string();
                const std::string targetName = c.target >= 0 ? pois_[static_cast<size_t>(c.target)].nameFr : std::string();
                Poi& p = addPoi("CT", "camera_trap", pos);
                const int k = idCounters_["CT"];
                p.nameFr = "Emplacement de piège photo " + std::to_string(k);
                p.nameEn = "Camera trap spot " + std::to_string(k);
                p.radiusM = 20.0f;
                p.heading = (-side).heading();
                p.hasHeading = true;
                p.confidence = Confidence::Unknown;
                p.confidenceSubject = "emplacement d'instrument (choix méthodologique) — sans objet paléontologique";
                p.placement = "model_prediction";
                char where[300];
                if (c.target >= 0)
                {
                    std::snprintf(where, sizeof(where), "à %.0f m de %s (%s), du côté atteint sans traverser", c.targetDist, targetId.c_str(),
                                  targetName.c_str());
                    p.links.push_back(targetId);
                }
                else
                {
                    std::snprintf(where, sizeof(where), "en zone de passage, à plus de 200 m de tout gué ou abreuvoir");
                }
                const float wear = trail_.v[c.cell];
                char buf[900];
                std::snprintf(buf, sizeof(buf),
                              "Sur %.0f corridor(s) prédit(s) superposé(s), %s ; présence d'herbivores pendant le préchauffage %.2f (relevés toutes les "
                              "10 min, normalisés) ; %s. Piège à détection infrarouge passive (portée ~20 m, champ 50°) posé à 6 m du passage et "
                              "orienté perpendiculairement à la piste.%s",
                              corridorUse_.v[c.cell], where, herbPresBlur_.v[c.cell],
                              wear >= kWearPassage ? "passage observé (l'usure de végétation sature après quelques passages ou un stationnement : "
                                                     "indice de passage, pas de piste établie)"
                                                   : "aucun passage observé pendant le préchauffage",
                              bank == 1 ? " Rive opposée : accès en packraft." : "");
                p.rationaleFr = buf;
                p.metrics.set("corridor_overlap", rnd(corridorUse_.v[c.cell], 0));
                p.metrics.set("trail_wear", rnd(wear, 3));
                p.metrics.set("passage_observed", wear >= kWearPassage);
                p.metrics.set("herbivore_presence", rnd(herbPresBlur_.v[c.cell], 3));
                p.metrics.set("near_water", c.target >= 0);
                if (c.target >= 0)
                {
                    p.metrics.set("target", targetId);
                    p.metrics.set("distance_to_target_m", rnd(c.targetDist, 0));
                }
                p.metrics.set("equipment", "camera_trap");
                p.score = c.score;
                if (++made >= 4)
                {
                    break;
                }
            }
        }
        // Autonomous audio recorders: activity within ~700 m, penalised by running-water noise (masking).
        {
            const float callMax = std::max(1.0f, callEv_.maxValue());
            std::vector<Candidate> cand;
            for (int y = 2; y < n_ - 2; y += 4)
            {
                for (int x = 2; x < n_ - 2; x += 4)
                {
                    const size_t i = height_.idx(x, y);
                    const Vec2 p = height_.center(x, y);
                    if (water_.v[i] > 0.0f || flood_.v[i] > 0.8f || !insideMargin(p, 300.0f))
                    {
                        continue;
                    }
                    float s = 0.0f;
                    for (int dy = -22; dy <= 22; dy += 2)
                    {
                        for (int dx = -22; dx <= 22; dx += 2)
                        {
                            const int xx = x + dx;
                            const int yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= n_ || yy >= n_)
                            {
                                continue;
                            }
                            const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy)) * kCellM;
                            if (d < 700.0f)
                            {
                                s += activity_.at(xx, yy) * (1.0f - d / 700.0f) + 0.5f * callEv_.at(xx, yy) / callMax;
                            }
                        }
                    }
                    s *= 1.0f - 0.5f * smoothstep(25.0f, 40.0f, noise_.v[i]);
                    s *= 1.0f + 0.05f * std::clamp(prominence_.v[i], 0.0f, 4.0f);
                    cand.push_back({p, s, static_cast<int>(i)});
                }
            }
            std::sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
            int made = 0;
            for (const Candidate& c : cand)
            {
                if (!farFromPois(c.pos, "audio_recorder", 1600.0f))
                {
                    continue;
                }
                Poi& p = addPoi("AR", "audio_recorder", c.pos);
                const int k = idCounters_["AR"];
                p.nameFr = "Station d'écoute " + std::to_string(k);
                p.nameEn = "Listening station " + std::to_string(k);
                p.radiusM = 10.0f;
                p.confidence = Confidence::Unknown;
                p.confidenceSubject = "emplacement d'instrument — les propriétés des cris enregistrés restent des HYPOTHÈSES";
                p.placement = "model_prediction";
                const size_t ci = static_cast<size_t>(c.ref);
                char buf[600];
                std::snprintf(buf, sizeof(buf),
                              "Enregistreur autonome (60 s toutes les 10 min) : maximise l'activité animale et les vocalisations simulées dans un rayon "
                              "de 700 m, pénalisé par le bruit d'eau courante qui masque les sons faibles (%.0f dB ici) ; légère prime à la hauteur. "
                              "Espacement ≥ 1,6 km pour un échantillonnage indépendant. Les cris captés sont des hypothèses de reconstitution.",
                              noise_.v[ci]);
                p.rationaleFr = buf;
                p.metrics.set("water_noise_db", rnd(noise_.v[ci], 1));
                p.metrics.set("score", rnd(c.score, 2));
                p.metrics.set("equipment", "audio_recorder");
                p.score = c.score;
                if (++made >= 3)
                {
                    break;
                }
            }
        }
        // Microphone array (localisation): flat, open, quiet ground near activity.
        {
            float best = 0.0f;
            Vec2 bestPos;
            for (int y = 4; y < n_ - 4; y += 2)
            {
                for (int x = 4; x < n_ - 4; x += 2)
                {
                    const size_t i = height_.idx(x, y);
                    const Vec2 p = height_.center(x, y);
                    bool ok = insideMargin(p, 300.0f) && noise_.v[i] < 30.0f && flood_.v[i] < 0.6f;
                    for (int dy = -2; dy <= 2 && ok; ++dy)
                    {
                        for (int dx = -2; dx <= 2; ++dx)
                        {
                            const size_t j = height_.idx(x + dx, y + dy);
                            if (slope_.v[j] > 6.0f || canopy_.v[j] > 0.3f || water_.v[j] > 0.0f)
                            {
                                ok = false;
                                break;
                            }
                        }
                    }
                    if (!ok)
                    {
                        continue;
                    }
                    float s = 0.0f;
                    for (int dy = -30; dy <= 30; dy += 3)
                    {
                        for (int dx = -30; dx <= 30; dx += 3)
                        {
                            const int xx = x + dx;
                            const int yy = y + dy;
                            if (xx >= 0 && yy >= 0 && xx < n_ && yy < n_ && dx * dx + dy * dy <= 900)
                            {
                                s += activity_.at(xx, yy);
                            }
                        }
                    }
                    if (s > best && farFromPois(p, "", 150.0f))
                    {
                        best = s;
                        bestPos = p;
                    }
                }
            }
            if (best > 0.0f)
            {
                Poi& p = addPoi("MA", "mic_array", bestPos);
                p.nameFr = "Site de réseau de microphones";
                p.nameEn = "Microphone array site";
                p.radiusM = 60.0f;
                p.confidence = Confidence::Unknown;
                p.confidenceSubject = "emplacement d'instrument (localisation acoustique par différences de temps d'arrivée)";
                p.placement = "model_prediction";
                p.sources = {"iso1993_9613_1"};
                const size_t ci = height_.indexOf(bestPos);
                char buf[500];
                std::snprintf(buf, sizeof(buf),
                              "Terrain plat (< 6°) et ouvert (canopée < 0,3) sur 160 m, calme (bruit d'eau %.0f dB), hors zone inondable, au centre "
                              "de la plus forte activité dans un rayon d'environ 1 km : conditions pour déployer 4 micros synchronisés sur un cercle "
                              "de 60 m et localiser les sources par triangulation.",
                              noise_.v[ci]);
                p.rationaleFr = buf;
                p.metrics.set("array_radius_m", 60);
                p.metrics.set("microphones", 4);
                p.metrics.set("water_noise_db", rnd(noise_.v[ci], 1));
                p.metrics.set("equipment", "omni_mic");
            }
        }
    }

    // ------------------------------------------------------------------ radio coverage
    void computeRadio()
    {
        const Viewshed vs = viewshed(w_.basecamp(), kRadioMastM, size_ * 1.45f, 1.8f, 720, 16.0f, false, 0.0f);
        for (const auto& c : vs.cells)
        {
            radio_.v[c.first] = 1.0f;
        }
        // Close cells the sweep stepped over (sub-cell gaps near the mast) are covered too.
        for (size_t i = 0; i < radio_.v.size(); ++i)
        {
            if (distance(radio_.centerOf(i), w_.basecamp()) < 64.0f)
            {
                radio_.v[i] = 1.0f;
            }
        }
        // One-cell dilation to fill azimuth gaps far from the mast.
        Field d = radio_;
        for (int y = 1; y < n_ - 1; ++y)
        {
            for (int x = 1; x < n_ - 1; ++x)
            {
                int cov = 0;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        cov += radio_.at(x + dx, y + dy) > 0.5f ? 1 : 0;
                    }
                }
                if (cov >= 5)
                {
                    d.at(x, y) = 1.0f;
                }
            }
        }
        radio_ = d;
    }

    // ------------------------------------------------------------------ route network
    void buildRoutes()
    {
        const size_t np = pois_.size();
        // Routes use the foot network plus the packraft crossings of the main river; reachability on foot alone is
        // reported separately (bank, reachable_on_foot_from_basecamp, foot_component).
        for (Poi& p : pois_)
        {
            p.footOk = snapPassable(footPackCost_, p.pos, 250.0f, p.footCell);
        }
        std::vector<PathField> pf(np);
        for (size_t i = 0; i < np; ++i)
        {
            if (pois_[i].footOk)
            {
                dijkstra(footPackCost_, {pois_[i].footCell}, pf[i]);
            }
        }
        auto cost = [&](size_t a, size_t b) {
            return (pois_[a].footOk && pois_[b].footOk) ? pf[a].dist[pois_[b].footCell] : kInf;
        };
        std::set<std::pair<size_t, size_t>> edgeSet;
        for (size_t a = 0; a < np; ++a)
        {
            std::vector<std::pair<float, size_t>> near;
            for (size_t b = 0; b < np; ++b)
            {
                if (b != a && std::isfinite(cost(a, b)))
                {
                    near.push_back({cost(a, b), b});
                }
            }
            std::sort(near.begin(), near.end());
            for (size_t k = 0; k < std::min<size_t>(3, near.size()); ++k)
            {
                edgeSet.insert({std::min(a, near[k].second), std::max(a, near[k].second)});
            }
        }
        // Connectivity: Prim over each connected component.
        std::vector<int> comp(np, -1);
        int ncomp = 0;
        for (size_t s = 0; s < np; ++s)
        {
            if (comp[s] >= 0)
            {
                continue;
            }
            comp[s] = ncomp;
            std::vector<size_t> members{s};
            for (size_t b = 0; b < np; ++b)
            {
                if (b != s && comp[b] < 0 && std::isfinite(cost(s, b)))
                {
                    comp[b] = ncomp;
                    members.push_back(b);
                }
            }
            std::vector<u8> in(np, 0);
            in[s] = 1;
            for (size_t step = 1; step < members.size(); ++step)
            {
                float best = kInf;
                size_t ba = 0;
                size_t bb = 0;
                for (const size_t a : members)
                {
                    if (!in[a])
                    {
                        continue;
                    }
                    for (const size_t b : members)
                    {
                        if (!in[b] && cost(a, b) < best)
                        {
                            best = cost(a, b);
                            ba = a;
                            bb = b;
                        }
                    }
                }
                if (!std::isfinite(best))
                {
                    break;
                }
                in[bb] = 1;
                edgeSet.insert({std::min(ba, bb), std::max(ba, bb)});
            }
            ++ncomp;
        }
        componentsPack_ = ncomp;
        // Foot-only components (the main river splits the valley): compact numbering of the grid labels.
        std::map<int, int> footComp;
        std::vector<int> poiFootComp(np, -1);
        std::vector<size_t> poiFootCell(np, 0);
        std::vector<u8> poiFootOk(np, 0);
        for (size_t i = 0; i < np; ++i)
        {
            size_t c = 0;
            if (snapPassable(footCost_, pois_[i].pos, 250.0f, c))
            {
                poiFootOk[i] = 1;
                poiFootCell[i] = c;
                const int lab = footLabel_.empty() ? 0 : footLabel_[c];
                if (footComp.find(lab) == footComp.end())
                {
                    const int next = static_cast<int>(footComp.size());
                    footComp[lab] = next;
                }
                poiFootComp[i] = footComp[lab];
            }
        }
        components_ = static_cast<int>(footComp.size());
        for (size_t i = 0; i < np; ++i)
        {
            const bool footReach = poiFootOk[i] && std::isfinite(footFromBase_.dist[poiFootCell[i]]);
            const bool reach = comp[i] == comp[basecampIdx_] && pois_[i].footOk;
            reachableFromBase_ += reach ? 1 : 0;
            pois_[i].metrics.set("foot_component", poiFootComp[i]);
            pois_[i].metrics.set("reachable_on_foot_from_basecamp", footReach);
            pois_[i].metrics.set("reachable_from_basecamp", reach);
            pois_[i].metrics.set("needs_packraft", reach && !footReach);
            pois_[i].metrics.set("bank", !poiFootOk[i] ? "none" : (footReach ? "camp_bank" : "far_bank"));
            if (footReach)
            {
                pois_[i].metrics.set("walk_time_from_basecamp_min", rnd(footFromBase_.dist[poiFootCell[i]] / kFootSpeedMs / 60.0f, 1));
            }
            else if (reach && std::isfinite(footPackFromBase_.dist[pois_[i].footCell]))
            {
                pois_[i].metrics.set("travel_time_from_basecamp_min", rnd(footPackFromBase_.dist[pois_[i].footCell] / kFootSpeedMs / 60.0f, 1));
            }
        }
        for (const auto& e : edgeSet)
        {
            Edge ed;
            ed.a = e.first;
            ed.b = e.second;
            ed.costM = cost(e.first, e.second);
            const std::vector<size_t> cells = tracePath(pf[e.first], pois_[e.second].footCell);
            std::vector<Vec2> pts{pois_[e.first].pos};
            for (const size_t c : cells)
            {
                pts.push_back(footPackCost_.centerOf(c));
                if (packraftCell_[c])
                {
                    ++ed.packraftCells;
                    continue;
                }
                ed.maxSlopeDeg = std::max(ed.maxSlopeDeg, slope_.v[c]);
                ed.maxWaterM = std::max(ed.maxWaterM, water_.v[c]);
            }
            pts.push_back(pois_[e.second].pos);
            ed.lengthM = polylineLength(pts);
            ed.path = simplify(pts, 16.0f);
            pois_[e.first].links.push_back(pois_[e.second].id);
            pois_[e.second].links.push_back(pois_[e.first].id);
            edges_.push_back(ed);
        }
        // Rover access from the basecamp: drop-off on practicable terrain, then the POI's own route (on foot, with a
        // packraft crossing when the POI is on the far bank). The rover itself never crosses the main river.
        for (size_t i = 0; i < np; ++i)
        {
            Poi& p = pois_[i];
            float bestScore = kInf;
            size_t bestCell = 0;
            if (p.footOk)
            {
                int cx = 0;
                int cy = 0;
                roverCost_.cellOf(p.pos, cx, cy);
                for (int dy = -16; dy <= 16; ++dy)
                {
                    for (int dx = -16; dx <= 16; ++dx)
                    {
                        const int x = cx + dx;
                        const int y = cy + dy;
                        if (x < 0 || y < 0 || x >= n_ || y >= n_)
                        {
                            continue;
                        }
                        const size_t c = roverCost_.idx(x, y);
                        const float walk = pf[i].dist[c];
                        if (!std::isfinite(walk) || walk > 900.0f || !std::isfinite(roverFromBase_.dist[c]))
                        {
                            continue;
                        }
                        const float sc = roverFromBase_.dist[c] + 4.0f * walk;
                        if (sc < bestScore)
                        {
                            bestScore = sc;
                            bestCell = c;
                        }
                    }
                }
            }
            JsonValue ra = JsonValue::makeObject();
            ra.set("reachable", std::isfinite(bestScore));
            if (std::isfinite(bestScore))
            {
                const Vec2 drop = roverCost_.centerOf(bestCell);
                bool viaPack = false;
                for (const size_t c : tracePath(pf[i], bestCell))
                {
                    viaPack = viaPack || packraftCell_[c] != 0;
                }
                ra.set("drop_off", polylineJson({drop}));
                ra.set("walk_cost_m", rnd(pf[i].dist[bestCell], 0));
                ra.set("walk_time_min", rnd(pf[i].dist[bestCell] / kFootSpeedMs / 60.0f, 1));
                ra.set("drive_cost_m", rnd(roverFromBase_.dist[bestCell], 0));
                ra.set("drive_time_min", rnd(roverFromBase_.dist[bestCell] / kRoverSpeedMs / 60.0f, 1));
                ra.set("via_packraft", viaPack);
                ++roverReachable_;
            }
            p.metrics.set("rover_access", ra);
            p.metrics.set("radio_coverage", radio_.sample(p.pos) > 0.5f);
            // Flood class from the POI's own ground height above the local bankfull level (not its 32 m cell).
            const float har = harAt(p.pos);
            p.metrics.set("height_above_bankfull_m", rnd(har, 2));
            p.metrics.set("flood_risk", rnd(floodRiskAt(p.pos), 2));
            p.metrics.set("flood_class", floodClass(har));
            if (p.hasAccess)
            {
                // The bench is narrower than the 32 m analysis cell: reachability is that of the nearest walkable cell.
                size_t ac = 0;
                p.metrics.set("access_point_reachable_on_foot", snapPassable(footCost_, p.access, 64.0f, ac) && std::isfinite(footFromBase_.dist[ac]));
                p.metrics.set("access_point_reachable_with_packraft", reachableWithPackraft(p.access));
            }
            p.metrics.set("distance_to_map_edge_m", rnd(std::min(std::min(p.pos.x, p.pos.y), std::min(size_ - p.pos.x, size_ - p.pos.y)), 0));
        }
    }

    void computeSightlines()
    {
        for (const auto& os : overlookSheds_)
        {
            std::map<u32, float> vis(os.second.cells.begin(), os.second.cells.end());
            Poi& ov = pois_[os.first];
            for (const Poi& q : pois_)
            {
                if (q.id == ov.id || q.designerOnly)
                {
                    continue;
                }
                const auto it = vis.find(static_cast<u32>(height_.indexOf(q.pos)));
                if (it != vis.end() && it->second > 0.3f)
                {
                    ov.sightlines.push_back(q.id);
                }
            }
            JsonValue tv = JsonValue::makeArray();
            for (const Trail& t : trails_)
            {
                int seen = 0;
                const std::vector<Vec2> pts = resample(t.points, 32.0f);
                for (const Vec2& p : pts)
                {
                    const auto it = vis.find(static_cast<u32>(height_.indexOf(p)));
                    seen += (it != vis.end() && it->second > 0.3f) ? 1 : 0;
                }
                if (seen >= 4) // at least ~130 m of the corridor in view
                {
                    tv.push(t.id);
                }
            }
            ov.metrics.set("trails_in_view", tv);
        }
    }

    // ------------------------------------------------------------------ outputs: masks
    void writeMask(const std::string& name, const std::function<float(size_t)>& value)
    {
        Image img(n_, n_);
        for (int y = 0; y < n_; ++y)
        {
            for (int x = 0; x < n_; ++x)
            {
                // Row 0 = northern edge (same orientation as export-terrain).
                const u8 v = static_cast<u8>(std::lround(saturate(value(height_.idx(x, n_ - 1 - y))) * 255.0f));
                img.at(x, y) = Rgb{v, v, v};
            }
        }
        writePng(opt_.out + "/mask_" + name + ".png", img);
        maskFiles_.push_back(name);
    }

    void writeMasks()
    {
        writeMask("rover_practicable", [&](size_t i) {
            return roverPract_.v[i] > 0.5f ? (std::isfinite(roverFromBase_.dist[i]) ? 1.0f : 0.5f) : 0.0f;
        });
        writeMask("flood_risk", [&](size_t i) { return flood_.v[i]; });
        writeMask("radio_coverage", [&](size_t i) { return radio_.v[i]; });
        writeMask("activity", [&](size_t i) { return activity_.v[i]; });
        writeMask("nesting_suitability_speculative", [&](size_t i) { return nestSuit_.v[i]; });
        writeMask("corridor_use", [&](size_t i) { return std::min(1.0f, corridorUse_.v[i] / 3.0f); });
        writeMask("foot_passable", [&](size_t i) { return std::isfinite(footCost_.v[i]) ? 1.0f : 0.0f; });
    }

    // ------------------------------------------------------------------ outputs: annotated maps
    struct Canvas
    {
        Image img;
        float mPerPx = 4.0f;
        int h = 0;
        void toPx(const Vec2& p, float& px, float& py) const
        {
            px = p.x / mPerPx;
            py = static_cast<float>(h) - p.y / mPerPx;
        }
        void thick(float x0, float y0, float x1, float y1, Rgb c, float a, int w)
        {
            for (int o = -(w / 2); o <= w / 2; ++o)
            {
                img.line(x0 + static_cast<float>(o), y0, x1 + static_cast<float>(o), y1, c, a);
                img.line(x0, y0 + static_cast<float>(o), x1, y1 + static_cast<float>(o), c, a);
            }
        }
        void polyline(const std::vector<Vec2>& pts, Rgb c, float a, int w, float dashPx)
        {
            float acc = 0.0f;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                float x0 = 0.0f;
                float y0 = 0.0f;
                float x1 = 0.0f;
                float y1 = 0.0f;
                toPx(pts[i - 1], x0, y0);
                toPx(pts[i], x1, y1);
                const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
                const int steps = std::max(1, static_cast<int>(len / 2.0f));
                for (int s = 0; s < steps; ++s)
                {
                    const float t0 = static_cast<float>(s) / static_cast<float>(steps);
                    const float t1 = static_cast<float>(s + 1) / static_cast<float>(steps);
                    acc += len / static_cast<float>(steps);
                    if (dashPx > 0.0f && std::fmod(acc, dashPx * 2.0f) > dashPx)
                    {
                        continue;
                    }
                    thick(x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0, x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1, c, a, w);
                }
            }
        }
    };

    static Rgb poiColor(const std::string& type)
    {
        if (type == "basecamp") return {255, 255, 255};
        if (type == "overlook") return {255, 220, 60};
        if (type == "blind") return {120, 230, 110};
        if (type == "ford") return {80, 230, 255};
        if (type == "oxbow_lake" || type == "pond") return {90, 150, 255};
        if (type == "backswamp") return {60, 200, 170};
        if (type == "flood_refuge") return {190, 190, 190};
        if (type == "exposure_site") return {230, 140, 60};
        if (type == "nesting_area") return {255, 130, 200};
        if (type == "carcass_site") return {200, 30, 30};
        if (type == "camera_trap") return {230, 80, 230};
        if (type == "audio_recorder") return {170, 120, 255};
        if (type == "mic_array") return {200, 170, 255};
        if (type == "water_access") return {150, 210, 255};
        return {200, 200, 200};
    }

    static void drawIcon(Image& img, const std::string& type, float px, float py, float s)
    {
        const Rgb c = poiColor(type);
        const Rgb k{10, 10, 10};
        if (type == "basecamp")
        {
            img.rect(static_cast<int>(px - s - 2), static_cast<int>(py - s - 2), static_cast<int>(px + s + 2), static_cast<int>(py + s + 2), k);
            img.rect(static_cast<int>(px - s), static_cast<int>(py - s), static_cast<int>(px + s), static_cast<int>(py + s), c);
        }
        else if (type == "overlook")
        {
            for (int o = 0; o < 3; ++o)
            {
                const float r = s + 2.0f - static_cast<float>(o);
                const Rgb col = o == 0 ? k : c;
                img.line(px, py - r, px - r, py + r * 0.8f, col);
                img.line(px - r, py + r * 0.8f, px + r, py + r * 0.8f, col);
                img.line(px + r, py + r * 0.8f, px, py - r, col);
            }
            img.disc(px, py + 1.0f, s * 0.45f, c);
        }
        else if (type == "blind")
        {
            for (int o = 0; o < 3; ++o)
            {
                const float r = s + 1.0f - static_cast<float>(o);
                const Rgb col = o == 0 ? k : c;
                img.line(px, py - r, px + r, py, col);
                img.line(px + r, py, px, py + r, col);
                img.line(px, py + r, px - r, py, col);
                img.line(px - r, py, px, py - r, col);
            }
        }
        else if (type == "carcass_site")
        {
            for (int o = -1; o <= 1; ++o)
            {
                img.line(px - s + static_cast<float>(o), py - s, px + s + static_cast<float>(o), py + s, c);
                img.line(px - s + static_cast<float>(o), py + s, px + s + static_cast<float>(o), py - s, c);
            }
        }
        else if (type == "exposure_site" || type == "camera_trap")
        {
            const float r = type == "camera_trap" ? s * 0.6f : s;
            img.rect(static_cast<int>(px - r - 1), static_cast<int>(py - r - 1), static_cast<int>(px + r + 1), static_cast<int>(py + r + 1), k);
            img.rect(static_cast<int>(px - r), static_cast<int>(py - r), static_cast<int>(px + r), static_cast<int>(py + r), c);
        }
        else if (type == "flood_refuge")
        {
            img.rect(static_cast<int>(px - s - 1), static_cast<int>(py - s - 1), static_cast<int>(px + s + 1), static_cast<int>(py + s + 1), k);
            img.rect(static_cast<int>(px - s + 1), static_cast<int>(py - s + 1), static_cast<int>(px + s - 1), static_cast<int>(py + s - 1), c);
            img.rect(static_cast<int>(px - s + 3), static_cast<int>(py - s + 3), static_cast<int>(px + s - 3), static_cast<int>(py + s - 3), k);
        }
        else if (type == "oxbow_lake" || type == "pond" || type == "mic_array" || type == "backswamp")
        {
            img.ring(px, py, s, k);
            img.ring(px, py, s - 1.5f, c);
            img.ring(px, py, s - 3.0f, c);
        }
        else if (type == "ford")
        {
            img.disc(px, py, s + 1.5f, k);
            img.disc(px, py, s, c);
            img.line(px - s, py - 2.0f, px + s, py - 2.0f, k);
            img.line(px - s, py + 2.0f, px + s, py + 2.0f, k);
        }
        else
        {
            img.disc(px, py, s * 0.8f + 1.5f, k);
            img.disc(px, py, s * 0.8f, c);
        }
    }

    void writeOverview()
    {
        MapOptions mo;
        mo.size = 2048;
        mo.creatures = false;
        mo.tracks = false;
        mo.trails = true;
        mo.legend = false;
        const Image base = renderMap(w_, mo);
        const int panel = 640;
        Canvas cv;
        cv.img = Image(mo.size + panel, mo.size, Rgb{18, 20, 24});
        cv.h = mo.size;
        cv.mPerPx = size_ / static_cast<float>(mo.size);
        for (int y = 0; y < mo.size; ++y)
        {
            for (int x = 0; x < mo.size; ++x)
            {
                cv.img.at(x, y) = base.px[static_cast<size_t>(y) * static_cast<size_t>(base.w) + static_cast<size_t>(x)];
            }
        }
        const float pxPerCell = kCellM / cv.mPerPx;
        // Overlook viewsheds (light tint).
        for (const auto& os : overlookSheds_)
        {
            for (const auto& c : os.second.cells)
            {
                if (c.second < 0.3f)
                {
                    continue;
                }
                const Vec2 p = height_.centerOf(c.first);
                float px = 0.0f;
                float py = 0.0f;
                cv.toPx(p, px, py);
                cv.img.rect(static_cast<int>(px - pxPerCell * 0.5f), static_cast<int>(py - pxPerCell * 0.5f), static_cast<int>(px + pxPerCell * 0.5f) - 1,
                            static_cast<int>(py + pxPerCell * 0.5f) - 1, Rgb{255, 235, 110}, 0.38f * c.second);
            }
        }
        // Nesting zones (speculative, dashed rings).
        for (const Poi& p : pois_)
        {
            if (p.type != "nesting_area")
            {
                continue;
            }
            float px = 0.0f;
            float py = 0.0f;
            cv.toPx(p.pos, px, py);
            const float r = p.radiusM / cv.mPerPx;
            for (int a = 0; a < 48; a += 2)
            {
                const float a0 = kTwoPi * static_cast<float>(a) / 48.0f;
                const float a1 = kTwoPi * static_cast<float>(a + 1) / 48.0f;
                cv.thick(px + std::cos(a0) * r, py + std::sin(a0) * r, px + std::cos(a1) * r, py + std::sin(a1) * r, poiColor(p.type), 0.9f, 2);
            }
        }
        // Route network (on foot).
        for (const Edge& e : edges_)
        {
            cv.polyline(e.path, Rgb{235, 235, 235}, 0.55f, 1, 3.0f);
        }
        // Predicted corridors.
        for (const Trail& t : trails_)
        {
            cv.polyline(t.points, t.kind == "drinking_route" ? Rgb{255, 150, 40} : Rgb{255, 200, 120}, 0.9f, 3, t.wearMean > 0.05f ? 0.0f : 8.0f);
        }
        // Blind lines of sight.
        for (const Poi& p : pois_)
        {
            if (p.type != "blind" || p.links.empty())
            {
                continue;
            }
            for (const Poi& q : pois_)
            {
                if (q.id == p.links.front())
                {
                    float x0 = 0.0f;
                    float y0 = 0.0f;
                    float x1 = 0.0f;
                    float y1 = 0.0f;
                    cv.toPx(p.pos, x0, y0);
                    cv.toPx(q.pos, x1, y1);
                    cv.img.line(x0, y0, x1, y1, Rgb{120, 230, 110}, 0.9f);
                }
            }
        }
        // POI icons, then labels placed greedily around their icon so that no label covers another label or an icon;
        // a leader line joins a displaced label to its icon.
        struct Box
        {
            int x0, y0, x1, y1;
            bool hits(const Box& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
        };
        std::vector<Box> boxes;
        for (const Poi& p : pois_)
        {
            float px = 0.0f;
            float py = 0.0f;
            cv.toPx(p.pos, px, py);
            drawIcon(cv.img, p.type, px, py, p.type == "basecamp" ? 9.0f : 7.0f);
            boxes.push_back({static_cast<int>(px) - 10, static_cast<int>(py) - 10, static_cast<int>(px) + 10, static_cast<int>(py) + 10});
        }
        for (const Poi& p : pois_)
        {
            float px = 0.0f;
            float py = 0.0f;
            cv.toPx(p.pos, px, py);
            const int w = static_cast<int>(p.id.size()) * 12;
            const int h = 14;
            const int ix = static_cast<int>(px);
            const int iy = static_cast<int>(py);
            Box chosen{ix + 11, iy - 7, ix + 11 + w, iy - 7 + h};
            bool placed = false;
            bool displaced = false;
            for (int ring = 1; ring <= 4 && !placed; ++ring)
            {
                const int r = 11 + (ring - 1) * 16;
                const int cand[8][2] = {{r, -7},          {-r - w, -7},     {-w / 2, -r - h}, {-w / 2, r},
                                        {r, -r - h + 4}, {r, r - 4},       {-r - w, -r - h + 4}, {-r - w, r - 4}};
                for (const auto& c : cand)
                {
                    const Box b{ix + c[0], iy + c[1], ix + c[0] + w, iy + c[1] + h};
                    if (b.x0 < 0 || b.y0 < 0 || b.x1 >= mo.size || b.y1 >= mo.size)
                    {
                        continue;
                    }
                    bool free = true;
                    for (const Box& o : boxes)
                    {
                        free = free && !b.hits(o);
                    }
                    if (free)
                    {
                        chosen = b;
                        placed = true;
                        displaced = ring > 1;
                        break;
                    }
                }
            }
            if (displaced)
            {
                const float lx = static_cast<float>(std::clamp(ix, chosen.x0, chosen.x1));
                const float ly = static_cast<float>(std::clamp(iy, chosen.y0, chosen.y1));
                cv.img.line(px, py, lx, ly, poiColor(p.type), 0.8f);
            }
            drawText(cv.img, chosen.x0, chosen.y0, p.id, poiColor(p.type), 2);
            boxes.push_back(chosen);
        }
        // Title, scale bar, north arrow.
        drawText(cv.img, 14, 12, "NOCTIS - " + w_.environment().nameFr + " - NIVEAU " + levelId_, Rgb{255, 255, 255}, 3);
        char sub[200];
        std::snprintf(sub, sizeof(sub), "GRAINE %llu  PROFIL %s  DEPART JOUR %d %02d:00  PRECHAUFFAGE %.0f H  EXPORT %s",
                      static_cast<unsigned long long>(opt_.seed), opt_.profile.c_str(), static_cast<int>(w_.config().startDayOfYear) + 1,
                      static_cast<int>(w_.config().startHour), opt_.warmupHours, w_.clock().formatted().c_str());
        drawText(cv.img, 14, 42, sub, Rgb{230, 230, 230}, 2);
        const int bar = static_cast<int>(1000.0f / cv.mPerPx);
        cv.img.rect(14, mo.size - 30, 14 + bar, mo.size - 24, Rgb{255, 255, 255});
        drawText(cv.img, 20 + bar, mo.size - 36, "1 KM", Rgb{255, 255, 255}, 2);
        cv.thick(static_cast<float>(mo.size - 40), 110.0f, static_cast<float>(mo.size - 40), 70.0f, Rgb{255, 255, 255}, 1.0f, 3);
        cv.img.line(static_cast<float>(mo.size - 50), 82.0f, static_cast<float>(mo.size - 40), 68.0f, Rgb{255, 255, 255});
        cv.img.line(static_cast<float>(mo.size - 30), 82.0f, static_cast<float>(mo.size - 40), 68.0f, Rgb{255, 255, 255});
        drawText(cv.img, mo.size - 46, 118, "N", Rgb{255, 255, 255}, 2);
        // Side panel: legend and POI list.
        const int x0 = mo.size + 16;
        int y = 16;
        drawText(cv.img, x0, y, "LEGENDE", Rgb{255, 255, 255}, 3);
        y += 34;
        const std::vector<std::pair<std::string, std::string>> legend = {
            {"basecamp", "CAMP DE BASE"},
            {"overlook", "POINT DE VUE (ZONE JAUNE = VISIBLE)"},
            {"blind", "AFFUT (LIGNE = VISEE)"},
            {"ford", "FRANCHISSEMENT CANDIDAT (PACKRAFT / GUE)"},
            {"water_access", "ABREUVOIR"},
            {"oxbow_lake", "BRAS MORT"},
            {"backswamp", "MARAIS ARRIERE"},
            {"flood_refuge", "REPLI HORS CRUE"},
            {"exposure_site", "AFFLEUREMENT (STRATES PLUS ANCIENNES)"},
            {"nesting_area", "NIDIFICATION - SPECULATIF"},
            {"carcass_site", "CARCASSE (ETAT DYNAMIQUE)"},
            {"camera_trap", "PIEGE PHOTO"},
            {"audio_recorder", "STATION D'ECOUTE"},
            {"mic_array", "RESEAU DE MICROS"},
        };
        for (const auto& l : legend)
        {
            drawIcon(cv.img, l.first, static_cast<float>(x0 + 10), static_cast<float>(y + 7), 7.0f);
            drawText(cv.img, x0 + 28, y, l.second, Rgb{230, 230, 230}, 2);
            y += 24;
        }
        cv.thick(static_cast<float>(x0), static_cast<float>(y + 7), static_cast<float>(x0 + 20), static_cast<float>(y + 7), Rgb{255, 150, 40}, 1.0f, 3);
        drawText(cv.img, x0 + 28, y, "CORRIDOR PREDIT (PLEIN = USURE OBSERVEE)", Rgb{230, 230, 230}, 2);
        y += 24;
        cv.img.line(static_cast<float>(x0), static_cast<float>(y + 7), static_cast<float>(x0 + 20), static_cast<float>(y + 7), Rgb{235, 235, 235}, 0.8f);
        drawText(cv.img, x0 + 28, y, "ITINERAIRE A PIED", Rgb{230, 230, 230}, 2);
        y += 36;
        drawText(cv.img, x0, y, "POINTS D'INTERET", Rgb{255, 255, 255}, 3);
        y += 32;
        for (const Poi& p : pois_)
        {
            if (y > mo.size - 24)
            {
                drawText(cv.img, x0, y, "...", Rgb{200, 200, 200}, 2);
                break;
            }
            std::string label = p.id + " " + p.nameFr;
            if (label.size() > 46)
            {
                label = label.substr(0, 46);
            }
            drawText(cv.img, x0, y, label, poiColor(p.type), 2);
            y += 19;
        }
        writePng(opt_.out + "/overview.png", cv.img);
    }

    void writeConstraints()
    {
        MapOptions mo;
        mo.size = 1024;
        mo.creatures = false;
        mo.tracks = false;
        mo.trails = false;
        mo.legend = false;
        Image img = renderMap(w_, mo);
        const float mPerPx = size_ / static_cast<float>(mo.size);
        for (int y = 0; y < mo.size; ++y)
        {
            for (int x = 0; x < mo.size; ++x)
            {
                const Vec2 p{(static_cast<float>(x) + 0.5f) * mPerPx, (static_cast<float>(mo.size - 1 - y) + 0.5f) * mPerPx};
                const size_t c = height_.indexOf(p);
                Rgb col = scale(img.at(x, y), 0.6f);
                if (roverPract_.v[c] < 0.5f)
                {
                    col = mix(col, Rgb{200, 40, 40}, 0.35f);
                }
                else if (!std::isfinite(roverFromBase_.dist[c]))
                {
                    col = mix(col, Rgb{220, 160, 40}, 0.35f);
                }
                col = mix(col, Rgb{40, 120, 255}, 0.5f * flood_.v[c]);
                if (radio_.v[c] < 0.5f && ((x + y) % 8) < 2)
                {
                    col = Rgb{15, 15, 15};
                }
                img.at(x, y) = col;
            }
        }
        for (const Poi& p : pois_)
        {
            if (p.type == "basecamp" || p.type == "ford")
            {
                drawIcon(img, p.type, p.pos.x / mPerPx, static_cast<float>(mo.size) - p.pos.y / mPerPx, 6.0f);
                drawText(img, static_cast<int>(p.pos.x / mPerPx) + 9, static_cast<int>(static_cast<float>(mo.size) - p.pos.y / mPerPx) - 6, p.id, poiColor(p.type), 1);
            }
        }
        drawText(img, 10, 10, "CONTRAINTES - " + levelId_, Rgb{255, 255, 255}, 2);
        drawText(img, 10, 32, "ROUGE : VEHICULE IMPRATICABLE   ORANGE : PRATICABLE MAIS ISOLE DU CAMP", Rgb{255, 220, 220}, 1);
        drawText(img, 10, 44, "BLEU : RISQUE DE CRUE   HACHURES : OMBRE RADIO (ANTENNE DU CAMP)", Rgb{220, 220, 255}, 1);
        writePng(opt_.out + "/constraints.png", img);
    }

    // ------------------------------------------------------------------ outputs: JSON
    JsonValue poiJson(const Poi& p) const
    {
        JsonValue o = JsonValue::makeObject();
        o.set("id", p.id);
        o.set("type", p.type);
        o.set("name", names(p.nameFr, p.nameEn));
        o.set("position_m", vec3Json(p.pos, p.z));
        o.set("ue_location_cm", ueJson(p.pos, p.z));
        o.set("radius_m", rnd(p.radiusM, 1));
        if (p.hasHeading)
        {
            o.set("heading_deg", rnd(p.heading * kRadToDeg, 1));
            o.set("ue_yaw_deg", rnd(-p.heading * kRadToDeg, 1));
        }
        o.set("confidence", confidenceKey(p.confidence));
        o.set("confidence_label_fr", confidenceLabelFr(p.confidence));
        o.set("confidence_subject_fr", p.confidenceSubject);
        o.set("placement", p.placement);
        o.set("rationale_fr", p.rationaleFr);
        o.set("sources", strings(p.sources));
        o.set("visible_at_start", p.visibleAtStart);
        o.set("dynamic_state", p.dynamicState);
        o.set("designer_only", p.designerOnly);
        if (!p.profileToggle.empty())
        {
            o.set("profile_toggle", p.profileToggle);
        }
        std::vector<std::string> links;
        for (const std::string& l : p.links)
        {
            if (std::find(links.begin(), links.end(), l) == links.end())
            {
                links.push_back(l);
            }
        }
        o.set("links", strings(links));
        if (!p.sightlines.empty())
        {
            o.set("sightlines", strings(p.sightlines));
        }
        o.set("metrics", p.metrics);
        return o;
    }

    JsonValue framesJson() const
    {
        const Terrain& t = w_.terrain();
        const float lo = t.minHeight();
        const float hi = t.maxHeight();
        JsonValue f = JsonValue::makeObject();
        f.set("units", "m");
        f.set("origin", "coin sud-ouest de la carte");
        f.set("axes", "x = est, y = nord, z = altitude absolue (m) — repère du cœur NoctisCore");
        f.set("size_m", size_);
        f.set("height_samples", t.samplesPerSide());
        f.set("spacing_m", rnd(t.spacing(), 5));
        f.set("min_height_m", rnd(lo, 3));
        f.set("max_height_m", rnd(hi, 3));
        f.set("analysis_cell_m", kCellM);
        JsonValue ue = JsonValue::makeObject();
        ue.set("mapping", "X_cm = x*100 + offset.X ; Y_cm = -y*100 + offset.Y ; Z_cm = z*100 + offset.Z (FNoctisSpace::ToUE, UNoctisSettings::WorldOffsetCm = 0 par défaut)");
        ue.set("landscape_location_cm", ueJson(Vec2{0.0f, size_}, lo + (hi - lo) * 0.5f));
        JsonValue sc = JsonValue::makeArray();
        sc.push(rnd(t.spacing() * 100.0, 4));
        sc.push(rnd(t.spacing() * 100.0, 4));
        sc.push(rnd((hi - lo) * 100.0 / 512.0, 5));
        ue.set("landscape_scale", sc);
        ue.set("landscape_note",
               "Heightmap 16 bits de export-terrain : ligne 0 = bord nord = Y Unreal le plus négatif. Z_monde = Location.Z + Scale.Z * (v - 32768) / 128 "
               "(cm) ; avec Scale.Z = (max-min)*100/512 la plage 0..65535 couvre exactement [min, max] m. Quads = samples - 1 = 2016 = 16 composants x "
               "126 quads (2x2 sections de 63).");
        ue.set("yaw_note", "yaw_UE (deg) = -heading_cœur (deg) ; heading 0 = est, sens trigonométrique");
        f.set("unreal", ue);
        return f;
    }

    JsonValue timeJson() const
    {
        const CalendarSpec& cal = w_.environment().sim.calendar;
        JsonValue t = JsonValue::makeObject();
        t.set("sim_day_length_h", cal.dayLengthHours);
        t.set("sim_days_per_year", cal.daysPerYear);
        t.set("calendar_basis_fr",
              "Hypothèse de jeu (Data/Science/sim_constants.json, calendrier) : 24 h et 365 jours. Le jour était un peu plus court au Crétacé "
              "supérieur ; la valeur pour la Hell Creek n'est pas établie dans le projet (Information insuffisante — recherche nécessaire ; piste : "
              "dewinter2020_rudist_daily, non vérifiée).");
        t.set("play_time_scale", 1.0);
        t.set("timelapse_max_scale", 8.0);
        t.set("rest_skip_scale", 360.0);
        t.set("time_scale_justification_fr",
              "Le temps de jeu reste 1:1 pendant l'observation : vitesses, taux d'appels par minute, distances de fuite et budgets d'activité sont "
              "mesurés par le joueur et testés statistiquement ; les comprimer fausserait ses données. L'accéléré « time-lapse » est plafonné à x8 "
              "(FullDetailMaxTimeScale : simulation complète des animaux), signalé à l'écran et exclu des mesures de comportement. Les longues attentes "
              "(nuit, pluie) se font au camp ou dans le véhicule avec un saut à x360 en simulation grossière — sauf pendant une alerte de crue "
              "(rules.flood_alert) : l'alerte interrompt tout saut et le x360 n'est plus permis qu'au repli hors crue.");
        t.set("day_convention_fr",
              "Jours affichés (horloge du jeu, cartes, documents) numérotés à partir de 1 : « jour 141 ». start_day_of_year est l'indice en base 0 "
              "de SimConfig (140 = jour 141).");
        t.set("start_day_of_year", w_.config().startDayOfYear);
        t.set("start_day_display", static_cast<int>(w_.config().startDayOfYear) + 1);
        t.set("start_hour", w_.config().startHour);
        t.set("start_justification_fr",
              "Jour 141 (indice 140) : dans la saison humide supposée (indices 100–200) et dans la fenêtre de reproduction simulée de plusieurs "
              "espèces ; 05:00 : aube, pic d'activité attendu des animaux diurnes et crépusculaires et des chœurs d'aube (hypothèses). Ce biais de "
              "départ rend la première heure riche sans scénario.");
        JsonValue ex = JsonValue::makeObject();
        ex.set("default_days", 3);
        ex.set("min_days", 1);
        ex.set("max_days", 7);
        ex.set("vertical_slice_days", 1);
        char buf[700];
        std::snprintf(buf, sizeof(buf),
                      "3 jours par défaut : au moins deux aubes et deux crépuscules pour répliquer les sessions (le système d'hypothèses signale la "
                      "pseudo-réplication), une nuit d'écoute complète, et une logistique crédible (batterie du véhicule %.0f kWh + générateur, "
                      "batteries de drone et de capteurs à recharger au camp). 7 jours maximum : au-delà, la météo et les crues dominent et la session "
                      "de jeu devient trop longue sans sauvegarde de mi-parcours.",
                      batteryKWh_);
        ex.set("justification_fr", buf);
        t.set("expedition", ex);
        return t;
    }

    JsonValue rulesJson() const
    {
        JsonValue r = JsonValue::makeObject();
        r.set("player_role_fr", "Chercheuse ou chercheur de terrain, physiquement présent au Maastrichtien, seul sur le terrain, en liaison radio avec le "
                                "camp de base. Jamais chasseur : aucune arme, aucune capture, aucun appât.");
        r.set("permadeath", false);
        JsonValue inj = JsonValue::makeObject();
        inj.set("model_fr", "ResearcherSystem::applyAttack : blessure graduée (santé 0..1). En dessous de 0,35 : déplacement ralenti, instruments "
                            "inutilisables, appel radio d'évacuation. À 0 : évacuation automatique vers le camp, fin de l'expédition en cours "
                            "(« Aborted »), données déjà sauvegardées conservées, temps de convalescence au camp.");
        inj.set("evacuation_fr", "Évacuation par le véhicule (conduite autonome jusqu'au point praticable le plus proche, voir rover_access de chaque "
                                 "POI). Hors couverture radio, le joueur doit d'abord rejoindre une zone couverte (masque radio_coverage) ou activer la "
                                 "balise du véhicule.");
        r.set("injury", inj);
        JsonValue veh = JsonValue::makeObject();
        veh.set("refuge", true);
        veh.set("fording_depth_m", fordingDepthM_);
        veh.set("noise_electric_db_1m", noiseElectricDb_);
        veh.set("noise_tyre_db_1m_ref", noiseTyreRefDb_);
        veh.set("noise_tyre_ref_speed_ms", noiseTyreRefSpeedMs_);
        veh.set("noise_tyre_speed_exponent_db", noiseTyreSpeedExpDb_);
        veh.set("noise_generator_db_1m", noiseGeneratorDb_);
        veh.set("practicability_fr",
                "Praticable si : eau <= profondeur de gué, pente <= min(25°, atan(mu_traction - résistance au roulement)), enfoncement Bekker < 70 % "
                "du seuil d'enlisement de VehicleSystem, canopée <= 0,55 (troncs), hors berges abruptes. Masque mask_rover_practicable.png "
                "(255 = praticable et relié au camp, 128 = praticable isolé, 0 = impraticable).");
        veh.set("disturbance_fr", "Le véhicule est une source sonore simulée (VehicleSystem::noiseAt1m) : moteur électrique ~52 dB à 1 m, "
                                  "générateur ~80 dB à 1 m (il porte donc beaucoup plus loin et perturbe la faune, événement VehicleNoticed), "
                                  "plus le bruit de roulement pneus / terrain 85 + 35 log10(v / 14 m/s) dB à 1 m, qui domine dès ~5 m/s "
                                  "(hypothèses de conception).");
        r.set("vehicle", veh);
        JsonValue onfoot = JsonValue::makeObject();
        onfoot.set("walk_speed_ms", rnd(kFootSpeedMs, 2));
        onfoot.set("max_wading_depth_m", rnd(kWadeLimitM, 2));
        onfoot.set("max_wading_bank_slope_deg", kWadeBankSlopeMaxDeg);
        char note[700];
        std::snprintf(note, sizeof(note),
                      "Le joueur ne nage pas. La rivière principale n'est jamais guéable : le modèle d'eau garde au moins %.0f %% du débit moyen "
                      "(niveau ≥ %.2f m), soit encore ~%.1f m d'eau aux points de franchissement. On la traverse en packraft (rules.packraft). "
                      "Le ruisseau, non pérenne, se passe à gué ou à sec. Coûts de marche : pente, eau, couvert, enfoncement (mask_foot_passable.png).",
                      100.0f * kPerennialBaseflow, stageAtFraction(0, kPerennialBaseflow), minCrossingDepthAtBaseflow());
        onfoot.set("note_fr", note);
        r.set("on_foot", onfoot);
        JsonValue pr = JsonValue::makeObject();
        pr.set("equipment_id", "packraft");
        pr.set("in_equipment_json", packraftFound_);
        pr.set("mass_kg", rnd(packraftMassKg_, 2));
        pr.set("max_crossing_current_ms", rnd(packraftMaxCurrentMs_, 2));
        pr.set("bank_slope_max_deg", packraftBankSlopeDeg_);
        pr.set("paddle_speed_ms", rnd(packraftPaddleMs_, 2));
        pr.set("setup_min", packraftSetupMin_);
        pr.set("max_stage_m", rnd(std::min(stageAtFraction(0, fractionAtCurrent(0, packraftMaxCurrentMs_)), kFloodAlertFraction * bankfullOf(0)), 2));
        pr.set("rule_fr", "Traversée de la rivière principale uniquement aux points de franchissement candidats dont les berges permettent la mise à "
                          "l'eau (packraft_usable), courant moyen ≤ max_crossing_current_ms et hors alerte de crue ; gilet obligatoire, bateau "
                          "porté dans le sac ensuite. Le véhicule reste sur la rive du camp : la rive opposée n'a pas de refuge proche. Règle de "
                          "sécurité moderne, sans objet paléontologique.");
        r.set("packraft", pr);
        JsonValue fa = JsonValue::makeObject();
        const float campHar = harAt(w_.basecamp());
        fa.set("alert_stage_fraction_of_bankfull", rnd(kFloodAlertFraction, 2));
        fa.set("alert_stage_m", rnd(kFloodAlertFraction * bankfullOf(0), 2));
        fa.set("alert_discharge_fraction", rnd(fractionAtStage(0, kFloodAlertFraction * bankfullOf(0)), 2));
        fa.set("camp_flood_stage_m", rnd(bankfullOf(0) + std::max(0.0f, campHar), 2));
        fa.set("camp_flood_discharge_fraction", rnd(fractionAtStage(0, bankfullOf(0) + std::max(0.0f, campHar)), 2));
        fa.set("refuge_poi", "FR01");
        fa.set("rule_fr", "Quand le niveau de la rivière dépasse alert_stage_m, la radio du camp lance l'alerte de crue (jauge sur la carte, "
                          "sous-titre, vibration optionnelle) : tout saut de temps en cours s'arrête, le saut x360 est interdit au camp et dans le véhicule garé "
                          "en plaine, il reste permis au repli FR01 ; traversées en packraft interdites. Le camp est inondé au-dessus de "
                          "camp_flood_stage_m. Avec le réservoir linéaire de 3 jours du modèle d'eau, un orage fort peut faire passer le niveau du "
                          "seuil d'alerte à l'inondation du camp en quelques heures de jeu : quelques secondes en x360, d'où l'interruption.");
        r.set("flood_alert", fa);
        JsonValue ta = JsonValue::makeObject();
        ta.set("ue_walkable_floor_angle_deg", 44.765);
        ta.set("landscape_quad_m", rnd(w_.terrain().spacing(), 3));
        ta.set("rule_fr", "Au pas du paysage (4,06 m), les berges verticales deviennent des rampes de 30–51° : les talus des bluffs et les berges "
                          "concaves sont des parois bloquantes (maillages à collision BlockAll de PCG_Noctis_Exposures), pas des pentes. Accès à "
                          "l'eau à pied uniquement aux gués du ruisseau, aux barres de méandre et aux abreuvoirs (berge ≤ 20° sur 10 m) ; mise à "
                          "l'eau du packraft aux points de franchissement.");
        r.set("terrain_access", ta);
        JsonValue lightning = JsonValue::makeObject();
        lightning.set("rule_fr", "Orage : les points de vue dégagés deviennent dangereux ; se réfugier dans le véhicule (carrosserie métallique) ou "
                                 "redescendre en contrebas. Règle de sécurité moderne, sans objet paléontologique.");
        r.set("storm", lightning);
        JsonValue drone = JsonValue::makeObject();
        drone.set("radio_range_m", droneRadioRangeM_);
        drone.set("note_fr", "Le drone décolle du véhicule ou du camp ; sa portée radio limite les survols (bruit des pales simulé, perturbation "
                             "mesurable par le joueur).");
        r.set("drone", drone);
        return r;
    }

    JsonValue eventsJson() const
    {
        auto ids = [&](std::initializer_list<const char*> types) {
            std::vector<std::string> v;
            for (const Poi& p : pois_)
            {
                for (const char* t : types)
                {
                    if (p.type == t)
                    {
                        v.push_back(p.id);
                    }
                }
            }
            return strings(v);
        };
        JsonValue a = JsonValue::makeArray();
        auto ev = [&](const char* id, const char* nameFr, const char* nameEn, const char* drivers, JsonValue where, const char* cuesFr, const char* a11yFr,
                      const char* responseFr, Confidence conf, const char* confFr, std::vector<std::string> src) {
            JsonValue e = JsonValue::makeObject();
            e.set("id", id);
            e.set("name", names(nameFr, nameEn));
            e.set("drivers", drivers);
            e.set("likely_poi_ids", where);
            e.set("cues_fr", cuesFr);
            e.set("accessibility_fr", a11yFr);
            e.set("player_response_fr", responseFr);
            e.set("confidence", confidenceKey(conf));
            e.set("confidence_subject_fr", confFr);
            e.set("sources", strings(src));
            e.set("scripted", false);
            a.push(e);
        };
        ev("herd_crossing", "Traversée de troupeau", "Herd crossing",
           "CreatureSystem (groupes, Drink, GroupFormationChange) + WaterSystem (profondeur aux gués)", ids({"ford", "water_access"}),
           "Bruits de pas et d'eau, vocalisations de contact (hypothèses), nuage de poussière ou éclaboussures, nouvelles empreintes sur les berges.",
           "Indicateur de direction des sons à l'écran, sous-titres « [éclaboussures — groupe d'herbivores, nord-est] », spectrogramme en direct "
           "étiqueté « hypothèse » pour les cris.",
           "Observer depuis un affût ou un point de vue, filmer la traversée, mesurer ensuite la piste d'empreintes laissée au gué.",
           Confidence::Inferred, "comportement de groupe et besoins en eau inférés ; la traversée elle-même est émergente",
           {"horner2011_hellcreek_census"});
        ev("predator_approach", "Approche d'un prédateur", "Predator approach",
           "CreatureSystem (HuntSearch/HuntStalk/HuntChase, ResearcherNoticed), profils (alarm_calls)", ids({"water_access", "ford", "carcass_site"}),
           "Herbivores qui lèvent la tête et se regroupent (VigilanceRaised), cris d'alarme si le profil les active (SPÉCULATIF), silence relatif, "
           "pas lourds.",
           "Vibration manette optionnelle, pictogramme de vigilance sur les animaux observés, sous-titres « [cri d'alarme — hypothèse] ».",
           "Se mettre à couvert, rejoindre le véhicule ; le jeu ne déclenche jamais d'attaque scriptée.", Confidence::Speculative,
           "les cris d'alarme et la réaction des proies aux prédateurs sont des hypothèses par analogie", {});
        ev("storm", "Orage", "Storm", "WeatherSystem (probabilité d'orage par jour de l'environnement), handleLightning (Lightning, tonnerre)",
           ids({"overlook"}), "Assombrissement, vent, tonnerre (délai éclair-tonnerre = distance), pluie qui masque les sons faibles.",
           "Icône météo et compteur éclair-tonnerre, sous-titres « [tonnerre à ~3 km] », alerte visuelle de foudre.",
           "Quitter les hauteurs, mettre les instruments à l'abri, se réfugier dans le véhicule.", Confidence::Inferred,
           "climat humide et orages saisonniers inférés ; fréquence = hypothèse de jeu", {"arens2014_hellcreek_basal_florule"});
        ev("flood", "Crue", "Flood", "WaterSystem (débit, niveau > plein bord => flooding), précipitations cumulées", ids({"ford", "oxbow_lake", "backswamp", "flood_refuge"}),
           "Montée et accélération de l'eau, bruit du courant, débris, gués devenus profonds, plaine qui se couvre d'eau en commençant par les "
           "marais arrière.",
           "Jauge de niveau de la rivière sur la carte du joueur, sous-titres « [courant fort] », surbrillance des zones déjà inondées.",
           "Rejoindre les terrasses, ne pas traverser ; après la crue : nouvelles carcasses enfouies, empreintes effacées, dépôts de crevasse.",
           Confidence::StronglySupported, "les dépôts de crevasse et de débordement enregistrent des crues répétées ; fréquence non quantifiée",
           {"fastovsky2016_hellcreek_primer"});
        ev("fire", "Incendie (rare)", "Wildfire (rare)", "VegetationSystem::updateFire (allumage par la foudre si sécheresse), vent", ids({}),
           "Odeur et fumée, lueur, crépitement ; fuite des animaux.", "Panache visible sur la carte, sous-titres « [crépitements — feu] ».",
           "S'éloigner contre le vent, rejoindre l'eau ou le véhicule ; plus tard, étudier la recolonisation.", Confidence::Inferred,
           "les feux faisaient probablement partie des perturbations ; aucune fréquence vérifiée pour la Hell Creek", {"bamforth2014_frenchman_fire"});
        ev("morning_fog", "Brouillard matinal", "Morning fog", "WeatherSystem (probabilité de brouillard matinal)", ids({"oxbow_lake", "backswamp", "audio_recorder"}),
           "Visibilité réduite au fond de vallée, sons portant loin, rosée.", "Indicateur de visibilité ; les sons restent visualisés.",
           "Privilégier l'écoute et l'enregistrement ; les photos d'identification attendent la levée du brouillard.", Confidence::Speculative,
           "brouillards par analogie avec les plaines alluviales actuelles", {});
        ev("carcass", "Nouvelle carcasse", "New carcass", "CarcassSystem (CarcassCreated, décomposition, charognards)", ids({"carcass_site"}),
           "Rassemblement de charognards, insectes, odeur.", "Marqueur de découverte uniquement après observation directe par le joueur.",
           "Examiner la carcasse (stade, morsures, temps depuis la mort) à distance de sécurité.", Confidence::Established,
           "décomposition et charognage (taphonomie actualiste)", {"behrensmeyer1978_bone_weathering"});
        ev("nesting_activity", "Activité de nidification", "Nesting activity", "NestSystem (EggsLaid, NestBuilt, Hatch) — selon profil",
           ids({"nesting_area"}), "Allées et venues répétées d'adultes au même endroit, remaniement du sol ou de la végétation.",
           "Marqueur « hypothèse » sur la carte si le profil l'autorise.", "Observer de loin sans déranger ; ne jamais manipuler les œufs.",
           Confidence::Speculative, "sites et comportements de nidification inconnus pour ces espèces dans la Hell Creek",
           {"horner1979_maiasaura_nest", "wosik2017_edmontosaurus_nestling"});
        return a;
    }

    JsonValue missionsJson() const
    {
        // First POI of a type, preferring the basecamp bank (reachable on foot at the start).
        auto firstOf = [&](const char* type) {
            std::string any;
            for (const Poi& p : pois_)
            {
                if (p.type == type)
                {
                    if (p.metrics.get("bank").asString("") == "camp_bank")
                    {
                        return p.id;
                    }
                    if (any.empty())
                    {
                        any = p.id;
                    }
                }
            }
            return any;
        };
        auto allOf = [&](const char* type) {
            std::vector<std::string> v;
            for (const Poi& p : pois_)
            {
                if (p.type == type)
                {
                    v.push_back(p.id);
                }
            }
            return v;
        };
        JsonValue a = JsonValue::makeArray();
        auto mission = [&](const char* id, const char* titleFr, const char* titleEn, const char* questionFr, std::vector<std::string> pois, int days,
                           std::vector<std::string> loadout, JsonValue targets, JsonValue hyps, const char* notesFr) {
            JsonValue m = JsonValue::makeObject();
            m.set("id", id);
            m.set("title", names(titleFr, titleEn));
            m.set("question_fr", questionFr);
            pois.erase(std::remove(pois.begin(), pois.end(), std::string()), pois.end());
            m.set("recommended_pois", strings(pois));
            m.set("planned_days", days);
            m.set("loadout", strings(loadout));
            m.set("data_targets", targets);
            m.set("hypothesis_templates", hyps);
            m.set("notes_fr", notesFr);
            a.push(m);
        };
        auto target = [](const char* kind, const char* species, double required) {
            JsonValue t = JsonValue::makeObject();
            t.set("kind", kind);
            t.set("species", species);
            t.set("required", required);
            return t;
        };
        auto hyp = [](const char* kind, const char* species, Measure response, const char* condition, double value, const char* predicted,
                      const char* statementFr) {
            JsonValue h = JsonValue::makeObject();
            h.set("kind", kind);
            h.set("species", species);
            h.set("response", measureKey(response));
            h.set("condition", condition);
            h.set("value", value);
            h.set("predicted", predicted);
            h.set("statement_fr", statementFr);
            return h;
        };
        auto arr = [](std::initializer_list<JsonValue> v) {
            JsonValue x = JsonValue::makeArray();
            for (const JsonValue& e : v)
            {
                x.push(e);
            }
            return x;
        };
        std::vector<std::string> first{firstOf("basecamp"), firstOf("overlook"), firstOf("water_access"), firstOf("ford")};
        mission("M01_premier_releve", "Premier relevé de la vallée", "First valley survey",
                "Quelles espèces fréquentent les abords du camp, où boivent-elles et à quelle heure ?", first, 1,
                {"binoculars_10x42", "camera_body_ff", "lens_100_400", "gps_unit", "omni_mic"},
                arr({target("observations", "", 10), target("photos", "", 5), target("recordings", "", 1)}), JsonValue::makeArray(),
                "Expédition d'accueil (2 premières heures) : aucune hypothèse imposée ; le carnet propose des pistes à partir des propres données du joueur.");
        mission("M02_distance_de_vigilance", "Distance de vigilance des hadrosaures", "Hadrosaur vigilance distance",
                "La part d'Edmontosaurus vigilants augmente-t-elle quand l'observateur est à moins de 150 m ?", allOf("blind"), 2,
                {"binoculars_10x42", "video_camera", "lens_600"},
                arr({target("session_hours", "edmontosaurus_annectens", 3), target("observations", "edmontosaurus_annectens", 20)}),
                arr({hyp("comparison", "edmontosaurus_annectens", Measure::VigilantFraction, "observer_within", 150.0, "increase",
                         "Les Edmontosaurus sont plus souvent vigilants quand je suis à moins de 150 m.")}),
                "Comparer des sessions depuis les affûts (loin) et en approche (près) ; biais signalés : distance d'observation, heure, pseudo-réplication.");
        mission("M03_effet_du_drone", "Le drone dérange-t-il ?", "Does the drone disturb?",
                "La vigilance des groupes change-t-elle quand le drone est en vol ?", {firstOf("overlook"), firstOf("water_access")}, 2,
                {"drone_quad", "drone_battery", "binoculars_10x42"},
                arr({target("session_hours", "", 2)}),
                arr({hyp("comparison", "triceratops_horridus", Measure::VigilantFraction, "drone_airborne", 0.0, "increase",
                         "Les Triceratops sont plus vigilants quand le drone vole (bruit des pales ~160 Hz).")}),
                "Question méthodologique : le joueur mesure l'effet de ses propres instruments.");
        mission("M04_appels_et_regroupement", "Appels et regroupement", "Calls and grouping",
                "Après un type d'appel donné, l'espacement entre individus diminue-t-il dans la minute ?", allOf("audio_recorder"), 3,
                {"audio_recorder", "parabolic_mic", "omni_mic", "video_camera"},
                arr({target("recordings", "", 6), target("acoustic_detections", "", 30)}),
                arr({hyp("event_response", "edmontosaurus_annectens", Measure::GroupSpacing, "call_cluster", 60.0, "decrease",
                         "Après le type d'appel n° k (mon classement), le groupe se resserre dans les 60 s.")}),
                "Les cris sont des HYPOTHÈSES de reconstitution (lecture « bouche fermée » par défaut) : le jeu ne dit jamais ce qu'un appel « signifie ».");
        mission("M05_nuit", "Activité nocturne", "Night activity", "Le taux d'appels change-t-il la nuit ?", {firstOf("mic_array"), firstOf("oxbow_lake"), firstOf("backswamp")}, 2,
                {"audio_recorder", "omni_mic"}, arr({target("recordings", "", 8)}),
                arr({hyp("comparison", "", Measure::CallRatePerMin, "night", 0.0, "different", "Le taux d'appels diffère entre le jour et la nuit.")}),
                "Écoute depuis le véhicule (moteur coupé) ou le camp ; vision nocturne limitée.");
        mission("M06_pistes_et_vitesses", "Pistes et vitesses", "Trackways and speeds",
                "La formule d'Alexander estime-t-elle correctement la vitesse d'animaux que j'ai filmés ?", allOf("ford"), 2,
                {"video_camera", "gps_unit", "camera_body_ff"}, arr({target("trackways", "", 3), target("observations", "", 6)}),
                JsonValue::makeArray(), "Calibration : vitesse filmée contre vitesse estimée par la longueur de foulée (méthode des paléontologues).");
        mission("M07_affleurements", "Affleurements", "Exposures", "Que contiennent les couches plus anciennes exposées dans les berges ?",
                allOf("exposure_site"), 3, {"excavation_kit", "camera_body_ff", "gps_unit"}, arr({target("specimens", "", 3)}), JsonValue::makeArray(),
                "Fouille, identification au laboratoire mobile, météorisation ; le contenu est SPÉCULATIF et l'âge absolu non contraint.");
        mission("M08_carcasse", "Lire une carcasse", "Reading a carcass", "Depuis quand cet animal est-il mort et qui l'a consommé ?",
                allOf("carcass_site"), 1, {"camera_body_ff", "excavation_kit"}, arr({target("observations", "", 3)}), JsonValue::makeArray(),
                "Examen : stade de décomposition, degrés-jours, morsures ; prudence, prédateurs et charognards attirés.");
        mission("M09_nidification", "Indices de nidification (spéculatif)", "Nesting clues (speculative)",
                "Les adultes reviennent-ils régulièrement au même endroit pendant la saison de reproduction ?", allOf("nesting_area"), 3,
                {"camera_trap", "binoculars_10x42", "lens_600"}, arr({target("observations", "", 10), target("photos", "", 6)}), JsonValue::makeArray(),
                "Disponible seulement si le profil active « nesting_grounds » ; le jeu présente les zones comme des hypothèses à tester.");
        return a;
    }

    JsonValue partitionJson() const
    {
        JsonValue p = JsonValue::makeObject();
        p.set("engine", "Unreal Engine 5.4+ (World Partition, PCG activé)");
        p.set("map", "/Game/Maps/HellCreek");
        JsonValue land = JsonValue::makeObject();
        land.set("quads_per_section", 63);
        land.set("sections_per_component", "2x2");
        land.set("components", "16x16");
        land.set("component_size_m", rnd(126.0 * w_.terrain().spacing(), 1));
        land.set("streaming_proxy_components", 1);
        p.set("landscape", land);
        JsonValue grid = JsonValue::makeObject();
        grid.set("name", "MainGrid");
        grid.set("cell_size_cm", 25600);
        grid.set("loading_range_cm", 102400);
        grid.set("note_fr", "Cellules de 256 m chargées jusqu'à ~1 km : couvre le rayon d'acteurs créatures (450 m) et les affûts ; au-delà, HLOD du "
                            "paysage et de la végétation pour les vues des points de vue (1,5 km).");
        p.set("runtime_grid", grid);
        JsonValue hlod = JsonValue::makeArray();
        hlod.push("HLOD_Landscape (toujours visible, terrain lointain)");
        hlod.push("HLOD_Foliage_Instanced (forêts et levées, 1–4 km)");
        p.set("hlod_layers", hlod);
        JsonValue dl = JsonValue::makeArray();
        dl.push("DL_POI_Markers (éditeur : marqueurs de tous les POI sauf les aires de nidification)");
        dl.push("DL_Nesting_Speculative (éditeur : marqueurs des aires de nidification SPÉCULATIVES, masquables par les concepteurs)");
        p.set("data_layers", dl);
        p.set("runtime_data_layers", JsonValue::makeArray());
        p.set("data_layers_note_fr", "Aucune couche runtime : les marqueurs de POI sont réservés à l'éditeur. En jeu, l'affichage des aires de "
                                     "nidification suit profile_toggle (nesting_grounds) lu dans ce JSON par le code de jeu ; build_level.py "
                                     "assigne chaque marqueur à sa couche éditeur.");
        JsonValue pcg = JsonValue::makeObject();
        pcg.set("partition_grid_cm", 25600);
        pcg.set("volume_size_m", size_);
        pcg.set("volumes_note_fr", "Un volume PCG par graphe couvrant tout le paysage, génération partitionnée (Is Partitioned) sur la grille PCG "
                                   "de 256 m : le découpage vient de la partition, pas de volumes multiples.");
        pcg.set("graphs", strings({"PCG_Noctis_Foliage (masques foliage_*.png d'export-terrain)", "PCG_Noctis_Trails (corridors du JSON)",
                                    "PCG_Noctis_Exposures (parois et strates à collision BlockAll le long de weight_cutbank_cliff, alignées sur "
                                    "l'arête de berge ; banquette marchable au pied, metrics.access_point)",
                                    "PCG_Noctis_RiverDebris (barres sableuses, laisses de crue)"}));
        p.set("pcg", pcg);
        return p;
    }

    JsonValue accessibilityJson() const
    {
        JsonValue a = JsonValue::makeObject();
        a.set("acoustic_cues_fr", "Tout indice sonore a un équivalent visuel : spectrogramme en direct (fenêtre de l'enregistreur), indicateur de "
                                  "direction, sous-titres décrivant le son. Les vocalisations sont toujours étiquetées « hypothèse ».");
        a.set("caption_label_fr", "hypothèse");
        a.set("caption_label_en", "hypothesis");
        a.set("colour_fr", "Icônes de carte distinguées par la forme et pas seulement la couleur (triangle, losange, carré, anneau, croix).");
        a.set("onboarding_fr", "Indices diégétiques dans le carnet de terrain uniquement ; pas de marqueur de quête ; la carte se construit à partir "
                               "des propres données du joueur (FieldMap).");
        a.set("ui_languages", strings({"fr", "en"}));
        return a;
    }

    void writeJson(const std::string& path) const
    {
        JsonValue root = JsonValue::makeObject();
        root.set("schema", "noctis.level/1");
        root.set("id", levelId_);
        root.set("name", names("Vallée de Hell Creek — tranche verticale", "Hell Creek valley — vertical slice"));
        root.set("environment", w_.environment().id);
        root.set("reconstruction_version", w_.environment().reconstructionVersion);
        root.set("generated_by", "noctis_sim export-level (Tools/Cli/LevelExport.cpp)");
        root.set("note_fr", "Fichier généré : ne pas éditer à la main. Chaque POI est choisi par un algorithme sur le monde de 'run' (même graine et "
                            "même configuration). 'confidence' qualifie l'existence ou la nature de l'élément au Maastrichtien "
                            "(confidence_subject_fr) ; 'placement' dit d'où vient sa position. La carte du joueur reste vide au départ : seuls les "
                            "POI visible_at_start sont connus.");
        JsonValue gen = JsonValue::makeObject();
        gen.set("seed", static_cast<double>(opt_.seed));
        gen.set("profile", opt_.profile);
        gen.set("population_scale", opt_.scale);
        gen.set("warmup_hours", opt_.warmupHours);
        gen.set("terrain_generator", w_.layout().generator);
        gen.set("export_time", w_.clock().formatted());
        JsonValue ec = JsonValue::makeObject();
        for (const auto& e : eventCounts_)
        {
            ec.set(e.first, e.second);
        }
        gen.set("warmup_event_counts", ec);
        {
            char cmd[300];
            std::snprintf(cmd, sizeof(cmd), "noctis_sim export-level --env %s --seed %llu --profile %s --scale %g --warmup-hours %g", opt_.env.c_str(),
                          static_cast<unsigned long long>(opt_.seed), opt_.profile.c_str(), static_cast<double>(opt_.scale), opt_.warmupHours);
            gen.set("command", std::string(cmd));
        }
        root.set("generation", gen);
        root.set("frame", framesJson());
        JsonValue playable = JsonValue::makeObject();
        playable.set("bounds_m", polylineJson({Vec2{0.0f, 0.0f}, Vec2{size_, size_}}));
        playable.set("poi_edge_margin_m", kEdgeMarginM);
        {
            JsonValue edges = JsonValue::makeObject();
            edges.set("north_south_fr", "Terrasses et plateaux boisés de la marge de vallée : limite naturelle du relief, renforcée par la limite "
                                        "d'étude ci-dessous.");
            edges.set("east_west_fr", "Le bord coupe la plaine et la rivière sans obstacle naturel : c'est la « limite de la zone d'étude ». La "
                                      "radio du camp prévient à 300 m du bord (« vous sortez de la zone d'étude »), le carnet trace la limite sur la "
                                      "carte du joueur ; à 100 m, la coordinatrice demande de faire demi-tour ; au bord, un volume bloquant invisible "
                                      "(seul mur invisible du niveau). Aucun POI à moins de 300 m d'un bord.");
            edges.set("warning_distance_m", 300);
            edges.set("turn_back_distance_m", 100);
            playable.set("edges", edges);
        }
        playable.set("note_fr", "Zone jouable = tout le terrain généré par 'run'. Nord et sud : terrasses de la marge de vallée ; est et ouest : "
                                "limite de la zone d'étude signalée par la radio et le carnet (voir edges).");
        root.set("playable_area", playable);
        root.set("time", timeJson());
        {
            JsonValue rc = JsonValue::makeObject();
            std::vector<std::string> mainFords;
            std::vector<std::string> creekFords;
            bool footNow = false;
            bool footEver = false;
            bool roverEver = false;
            for (const Poi& p : pois_)
            {
                if (p.type != "ford")
                {
                    continue;
                }
                if (p.metrics.get("river").asString("") == "main")
                {
                    mainFords.push_back(p.id);
                    footNow = footNow || p.metrics.get("fordable_on_foot_now").asBool(false);
                    footEver = footEver || p.metrics.get("fordable_on_foot_ever").asBool(false);
                    roverEver = roverEver || p.metrics.get("fordable_rover_ever").asBool(false);
                }
                else
                {
                    creekFords.push_back(p.id);
                }
            }
            JsonValue mr = JsonValue::makeObject();
            mr.set("crossing_points", strings(mainFords));
            mr.set("perennial", true);
            mr.set("baseflow_fraction_min", rnd(kPerennialBaseflow, 2));
            mr.set("stage_min_simulated_m", rnd(stageAtFraction(0, kPerennialBaseflow), 2));
            mr.set("stage_now_m", rnd(w_.water().riverStage(0), 2));
            mr.set("bankfull_depth_m", rnd(bankfullOf(0), 2));
            mr.set("min_crossing_depth_at_baseflow_m", rnd(minCrossingDepthAtBaseflow(), 2));
            mr.set("fordable_on_foot_now", footNow);
            mr.set("fordable_on_foot_ever", footEver);
            mr.set("fordable_rover_ever", roverEver);
            mr.set("never_fordable", !footEver && !roverEver);
            mr.set("packraft_crossings_now", strings(packraftCrossings_));
            mr.set("sections_sampled", sampledSections_[0]);
            mr.set("sections_rejected_curvature", kappaRejected_[0]);
            rc.set("main_river", mr);
            JsonValue cr = JsonValue::makeObject();
            if (w_.layout().rivers.size() > 1)
            {
                cr.set("fords", strings(creekFords));
                cr.set("perennial", w_.layout().rivers[1].perennial);
                cr.set("baseflow_fraction_min", rnd(baseflowOf(1), 2));
                cr.set("stage_min_simulated_m", rnd(stageAtFraction(1, baseflowOf(1)), 2));
                cr.set("stage_now_m", rnd(w_.water().riverStage(1), 2));
                cr.set("sections_sampled", sampledSections_[1]);
                cr.set("sections_rejected_curvature", kappaRejected_[1]);
                cr.set("sections_dry_bed", drySections_[1]);
                cr.set("dry_bed_note_fr", "« lit à sec » : section sans eau au niveau actuel (le niveau de référence du ruisseau descend par "
                                          "endroits sous le lit creusé) ; franchissable à pied, jamais classée comme gué.");
            }
            rc.set("creek", cr);
            rc.set("foot_components", components_);
            rc.set("components_with_packraft", componentsPack_);
            char design[1400];
            std::snprintf(design, sizeof(design),
                          "La rivière principale traverse toute la carte et sépare la rive du camp (camp_bank) de la rive opposée (far_bank). Elle "
                          "n'est jamais guéable : le modèle d'eau garde au moins %.0f %% du débit moyen même sans pluie (niveau ≥ %.2f m), soit encore "
                          "~%.1f m d'eau aux points de franchissement, au-delà des limites à pied (%.1f m) et en véhicule (gué %.1f m). Décision : "
                          "un packraft dans l'équipement du joueur (Data/Equipment/equipment.json, id packraft), utilisable aux points de "
                          "franchissement candidats dont les berges permettent la mise à l'eau, sous %.1f m/s de courant et hors alerte de crue. Le "
                          "véhicule reste sur la rive du camp : la rive opposée n'a pas de refuge proche. Le ruisseau affluent (non pérenne, débit "
                          "minimal %.0f %%) se passe à gué ou à sec ; c'est lui, pas la rivière, dont la praticabilité change avec la saison.",
                          100.0f * kPerennialBaseflow, stageAtFraction(0, kPerennialBaseflow), minCrossingDepthAtBaseflow(), kWadeLimitM, fordingDepthM_,
                          packraftMaxCurrentMs_, 100.0f * kEphemeralBaseflow);
            rc.set("design_fr", std::string(design));
            rc.set("equipment", "packraft");
            root.set("river_crossing", rc);
        }
        root.set("rules", rulesJson());
        JsonValue masks = JsonValue::makeObject();
        masks.set("directory", opt_.out);
        masks.set("resolution_px", n_);
        masks.set("cell_m", kCellM);
        masks.set("orientation", "ligne 0 = bord nord (comme export-terrain)");
        masks.set("files", strings([&] {
                      std::vector<std::string> f;
                      for (const std::string& m : maskFiles_)
                      {
                          f.push_back("mask_" + m + ".png");
                      }
                      f.push_back("overview.png");
                      f.push_back("constraints.png");
                      return f;
                  }()));
        root.set("masks", masks);
        JsonValue pa = JsonValue::makeArray();
        for (const Poi& p : pois_)
        {
            pa.push(poiJson(p));
        }
        root.set("pois", pa);
        JsonValue ta = JsonValue::makeArray();
        for (const Trail& t : trails_)
        {
            JsonValue o = JsonValue::makeObject();
            o.set("id", t.id);
            o.set("kind", t.kind);
            o.set("from", t.fromLabel);
            o.set("to", t.toLabel);
            o.set("length_m", rnd(t.lengthM, 0));
            o.set("observed_wear_mean", rnd(t.wearMean, 3));
            o.set("observed_wear_max", rnd(t.wearMax, 3));
            o.set("herbivore_presence", rnd(t.presence, 3));
            o.set("max_water_depth_m", rnd(t.maxWaterM, 2));
            o.set("crosses_main_river", t.crossesRiver);
            o.set("max_cell_slope_deg", rnd(t.maxSlopeDeg, 1));
            o.set("usable_by", strings(t.usableBy));
            o.set("not_usable_by", strings(t.notUsableBy));
            o.set("model_cost_per_m", rnd(t.costPerM, 3));
            o.set("evidence", t.wearMean > 0.05f ? "observed_wear" : "predicted_only");
            o.set("confidence", confidenceKey(Confidence::Inferred));
            o.set("confidence_subject_fr", "les grands herbivores actuels réutilisent des pistes vers l'eau et entre zones d'alimentation ; le tracé est "
                                           "un chemin de moindre coût du modèle (pente, eau, couvert, enfoncement, usure)");
            o.set("placement", "model_prediction");
            JsonValue pts = JsonValue::makeArray();
            for (const Vec2& q : t.points)
            {
                pts.push(vec3Json(q, terrainZ(q)));
            }
            o.set("points", pts);
            ta.push(o);
        }
        root.set("trails", ta);
        JsonValue ca = JsonValue::makeArray();
        for (const Edge& e : edges_)
        {
            JsonValue o = JsonValue::makeObject();
            o.set("from", pois_[e.a].id);
            o.set("to", pois_[e.b].id);
            o.set("mode", e.packraftCells > 0 ? "foot_packraft" : "foot");
            o.set("length_m", rnd(e.lengthM, 0));
            o.set("time_min", rnd(e.costM / kFootSpeedMs / 60.0f, 1));
            o.set("max_slope_deg", rnd(e.maxSlopeDeg, 1));
            o.set("max_water_depth_m", rnd(e.maxWaterM, 2));
            if (e.packraftCells > 0)
            {
                o.set("note_fr", "traversée de la rivière principale en packraft (max_water_depth_m = parties à pied)");
            }
            o.set("path", polylineJson(e.path));
            ca.push(o);
        }
        root.set("connections", ca);
        JsonValue hs = JsonValue::makeArray();
        for (const Candidate& h : hotspots_)
        {
            JsonValue o = JsonValue::makeObject();
            o.set("position_m", vec3Json(h.pos, terrainZ(h.pos)));
            o.set("score", rnd(h.score, 3));
            hs.push(o);
        }
        JsonValue designer = JsonValue::makeObject();
        designer.set("note_fr", "Données de conception, jamais montrées au joueur.");
        designer.set("forage_hotspots", hs);
        JsonValue ph = JsonValue::makeArray();
        {
            Field pp = predPresBlur_;
            std::vector<Candidate> c;
            for (size_t i = 0; i < pp.v.size(); ++i)
            {
                c.push_back({pp.centerOf(i), pp.v[i], static_cast<int>(i)});
            }
            std::sort(c.begin(), c.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
            std::vector<Vec2> chosen;
            for (const Candidate& k : c)
            {
                bool far = true;
                for (const Vec2& q : chosen)
                {
                    far = far && distance(q, k.pos) > 800.0f;
                }
                if (far && k.score > 0.2f)
                {
                    chosen.push_back(k.pos);
                    JsonValue o = JsonValue::makeObject();
                    o.set("position_m", vec3Json(k.pos, terrainZ(k.pos)));
                    o.set("relative_activity", rnd(k.score, 2));
                    ph.push(o);
                }
                if (chosen.size() >= 4)
                {
                    break;
                }
            }
        }
        designer.set("predator_activity_warmup", ph);
        root.set("designer", designer);
        root.set("events", eventsJson());
        root.set("missions", missionsJson());
        root.set("world_partition", partitionJson());
        root.set("accessibility", accessibilityJson());
        JsonValue sum = JsonValue::makeObject();
        std::map<std::string, int> counts;
        for (const Poi& p : pois_)
        {
            ++counts[p.type];
        }
        JsonValue cs = JsonValue::makeObject();
        for (const auto& c : counts)
        {
            cs.set(c.first, c.second);
        }
        sum.set("poi_counts", cs);
        sum.set("trails", static_cast<int>(trails_.size()));
        sum.set("connections", static_cast<int>(edges_.size()));
        sum.set("foot_components", components_);
        sum.set("components_with_packraft", componentsPack_);
        sum.set("reachable_from_basecamp_pois", reachableFromBase_);
        sum.set("rover_reachable_pois", roverReachable_);
        sum.set("drink_sites_rejected_bank", drinkRejected_);
        {
            JsonValue nn = JsonValue::makeArray();
            for (const auto& e : nestNotPlaced_)
            {
                JsonValue o = JsonValue::makeObject();
                o.set("species", e.first);
                o.set("best_suitability", rnd(e.second, 3));
                nn.push(o);
            }
            sum.set("nesting_species_without_zone", nn);
            sum.set("nesting_suitability_min", rnd(kNestSuitabilityMin, 2));
        }
        float pract = 0.0f;
        float covered = 0.0f;
        for (size_t i = 0; i < roverPract_.v.size(); ++i)
        {
            pract += (roverPract_.v[i] > 0.5f && std::isfinite(roverFromBase_.dist[i])) ? 1.0f : 0.0f;
            covered += radio_.v[i];
        }
        sum.set("rover_practicable_connected_fraction", rnd(pract / static_cast<float>(roverPract_.v.size()), 3));
        sum.set("radio_coverage_fraction", rnd(covered / static_cast<float>(radio_.v.size()), 3));
        root.set("summary", sum);
        std::ofstream f(path, std::ios::binary);
        f << root.dump(true) << "\n";
    }

    void printSummary(const std::string& jsonPath, double secs) const
    {
        std::printf("\n=== Niveau %s (graine %llu, profil %s) ===\n", levelId_.c_str(), static_cast<unsigned long long>(opt_.seed), opt_.profile.c_str());
        for (const Poi& p : pois_)
        {
            std::printf("  %-5s %-15s (%6.0f, %6.0f, %5.1f)  %-18s %-17s %s\n", p.id.c_str(), p.type.c_str(), p.pos.x, p.pos.y, p.z,
                        confidenceKey(p.confidence), p.placement.c_str(), p.nameFr.c_str());
        }
        std::printf("Corridors prédits : %zu ; liaisons : %zu ; composantes à pied : %d (avec packraft : %d) ; POI atteignables depuis le camp : %d/%zu ; "
                    "POI accessibles en véhicule : %d/%zu\n",
                    trails_.size(), edges_.size(), components_, componentsPack_, reachableFromBase_, pois_.size(), roverReachable_, pois_.size());
        std::printf("JSON : %s\nCartes et masques : %s/overview.png, constraints.png, mask_*.png\nDurée totale : %.0f s\n", jsonPath.c_str(), opt_.out.c_str(),
                    secs);
    }

    // ------------------------------------------------------------------ state
    WorldSimulation& w_;
    Options opt_;
    std::string levelId_;
    float size_ = 8192.0f;
    int n_ = 256;
    RiverIndex rivers_;
    Field height_, slope_, water_, wetFrac_, canopy_, cover_, trail_, har_, riverDist_, creekDist_, creekHar_, noise_, prominence_, blockH_, backdropH_;
    Field suitability_, presence_, herbPresence_, predPresence_, drinkEv_, callEv_, activity_, flood_;
    Field footCost_, roverCost_, roverPract_, animalCost_, radio_, nestSuit_, corridorUse_;
    Field herbPresBlur_, predPresBlur_, drinkBlur_, suitNorm_;
    std::vector<u8> habitat_;
    std::vector<u8> substrate_;
    std::vector<float> scratch_;
    std::vector<Poi> pois_;
    std::vector<Trail> trails_;
    std::vector<Edge> edges_;
    std::vector<Candidate> hotspots_;
    std::vector<Candidate> waterAccess_;
    std::vector<std::pair<size_t, Viewshed>> overlookSheds_;
    std::vector<std::pair<Vec2, int>> nestEvents_;
    std::map<std::string, int> eventCounts_;
    std::map<std::string, int> idCounters_;
    std::vector<std::string> maskFiles_;
    PathField roverFromBase_;
    PathField footFromBase_;
    std::chrono::steady_clock::time_point lap_;
    size_t basecampIdx_ = 0;
    int samples_ = 0;
    int components_ = 0;
    int roverReachable_ = 0;
    float fordingDepthM_ = 1.0f;
    float vehicleMassKg_ = 7200.0f;
    int wheels_ = 6;
    float wheelWidthM_ = 0.42f;
    float wheelRadiusM_ = 0.6f;
    float noiseElectricDb_ = 52.0f;
    float noiseTyreRefDb_ = 85.0f;
    float noiseTyreRefSpeedMs_ = 14.0f;
    float noiseTyreSpeedExpDb_ = 35.0f;
    float noiseGeneratorDb_ = 80.0f;
    float batteryKWh_ = 200.0f;
    float droneRadioRangeM_ = 6000.0f;
    bool packraftFound_ = false;
    float packraftMaxCurrentMs_ = 1.2f;
    float packraftBankSlopeDeg_ = 30.0f;
    float packraftPaddleMs_ = 0.8f;
    float packraftSetupMin_ = 10.0f;
    float packraftMassKg_ = 3.5f;
    // Packraft network (foot costs + main-river crossings at the selected crossing points).
    Field footPackCost_;
    std::vector<u8> packraftCell_;
    PathField footPackFromBase_;
    std::vector<int> footLabel_;
    std::vector<int> footPackLabel_;
    int componentsPack_ = 0;
    int reachableFromBase_ = 0;
    std::vector<std::string> packraftCrossings_;
    int drySections_[2] = {0, 0};
    int sampledSections_[2] = {0, 0};
    std::vector<CrossingSeg> crossingSegs_;
    int kappaRejected_[2] = {0, 0};
    std::vector<std::pair<std::string, float>> nestNotPlaced_;
    std::vector<WaterInfo> waterInfo_; // parallel to waterAccess_
    int drinkRejected_ = 0;
    std::map<std::string, int> blindReject_;
};
} // namespace levelimpl

int cmdExportLevel(int argc, char** argv)
{
    using namespace noctis;
    const levelimpl::Options o = levelimpl::parseOptions(argc, argv);
    WorldSimulation w;
    SimConfig cfg; // same configuration as 'run'
    cfg.dataRoot = o.data;
    cfg.environmentId = o.env;
    cfg.profileId = o.profile;
    cfg.seed = o.seed;
    cfg.threads = o.threads;
    cfg.populationScale = o.scale;
    const auto t0 = std::chrono::steady_clock::now();
    if (!w.initialize(cfg))
    {
        std::printf("Échec de l'initialisation du monde (%s, graine %llu)\n", o.env.c_str(), static_cast<unsigned long long>(o.seed));
        return 1;
    }
    std::printf("Monde initialisé en %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    levelimpl::LevelBuilder builder(w, o);
    return builder.run();
}
} // namespace noctistools
