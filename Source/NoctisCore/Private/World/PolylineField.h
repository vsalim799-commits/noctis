// Internal helpers shared by the terrain generator and the water system:
// dense polylines (river centrelines) and nearest-seed distance fields on a grid.
#pragma once

#include "Noctis/Core/Grid2D.h"

#include <limits>
#include <vector>

namespace noctis
{
namespace polylinefield
{
struct Polyline
{
    std::vector<Vec2> p;
    std::vector<Vec2> t;
    std::vector<float> kappa;
    std::vector<float> s;

    void finalize()
    {
        const size_t n = p.size();
        t.assign(n, Vec2{1.0f, 0.0f});
        kappa.assign(n, 0.0f);
        s.assign(n, 0.0f);
        for (size_t i = 1; i < n; ++i)
        {
            s[i] = s[i - 1] + distance(p[i], p[i - 1]);
        }
        for (size_t i = 0; i < n; ++i)
        {
            const size_t a = i == 0 ? 0 : i - 1;
            const size_t b = i + 1 >= n ? n - 1 : i + 1;
            t[i] = (p[b] - p[a]).normalized();
        }
        for (size_t i = 0; i < n; ++i)
        {
            const size_t a = i < 4 ? 0 : i - 4;
            const size_t b = i + 4 >= n ? n - 1 : i + 4;
            const float ds = s[b] - s[a];
            if (ds > kEpsilon)
            {
                kappa[i] = wrapAngle(t[b].heading() - t[a].heading()) / ds;
            }
        }
    }
};

struct DistField
{
    Grid2D<float> dist;
    Grid2D<int> seed;
};

inline void buildDistField(const Polyline& line, const Grid2D<float>& spec, DistField& out)
{
    const float inf = std::numeric_limits<float>::max();
    out.dist.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), inf);
    out.seed.resize(spec.width(), spec.height(), spec.cellSize(), spec.origin(), -1);
    for (size_t i = 0; i < line.p.size(); ++i)
    {
        int cx = 0;
        int cy = 0;
        out.dist.worldToCell(line.p[i], cx, cy);
        if (!out.dist.inBounds(cx, cy))
        {
            continue;
        }
        const float d = distance(out.dist.cellCenter(cx, cy), line.p[i]);
        if (d < out.dist.at(cx, cy))
        {
            out.dist.at(cx, cy) = d;
            out.seed.at(cx, cy) = static_cast<int>(i);
        }
    }
    const int w = spec.width();
    const int h = spec.height();
    auto relax = [&](int x, int y, int nx, int ny) {
        if (!out.seed.inBounds(nx, ny))
        {
            return;
        }
        const int s = out.seed.at(nx, ny);
        if (s < 0)
        {
            return;
        }
        const float d = distance(out.dist.cellCenter(x, y), line.p[static_cast<size_t>(s)]);
        if (d < out.dist.at(x, y))
        {
            out.dist.at(x, y) = d;
            out.seed.at(x, y) = s;
        }
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                relax(x, y, x - 1, y);
                relax(x, y, x - 1, y - 1);
                relax(x, y, x, y - 1);
                relax(x, y, x + 1, y - 1);
            }
        }
        for (int y = h - 1; y >= 0; --y)
        {
            for (int x = w - 1; x >= 0; --x)
            {
                relax(x, y, x + 1, y);
                relax(x, y, x + 1, y + 1);
                relax(x, y, x, y + 1);
                relax(x, y, x - 1, y + 1);
            }
        }
    }
}

struct Nearest
{
    float dist = std::numeric_limits<float>::max();
    int index = -1;
    float lateral = 0.0f; // signed, positive = left of flow
};

inline Nearest nearestOn(const Polyline& line, const DistField& field, const Vec2& p)
{
    Nearest r;
    if (line.p.empty())
    {
        return r;
    }
    const int hint = field.seed.sampleNearest(p);
    if (hint < 0)
    {
        return r;
    }
    const int n = static_cast<int>(line.p.size());
    const int a = std::max(0, hint - 6);
    const int b = std::min(n - 1, hint + 6);
    for (int i = a; i <= b; ++i)
    {
        const float d = distance(p, line.p[static_cast<size_t>(i)]);
        if (d < r.dist)
        {
            r.dist = d;
            r.index = i;
        }
    }
    const size_t k = static_cast<size_t>(r.index);
    r.lateral = cross(line.t[k], p - line.p[k]);
    return r;
}

} // namespace polylinefield
} // namespace noctis
