// Top-down map and spectrogram rendering of the live simulation (tools / debug only).
#pragma once

#include "Font5x7.h"
#include "Image.h"

#include "Noctis/Research/Acoustics.h"
#include "Noctis/Sim/WorldSimulation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace noctistools
{
inline Rgb habitatColor(noctis::Habitat h)
{
    using H = noctis::Habitat;
    switch (h)
    {
    case H::Channel: return {52, 92, 120};
    case H::Riverbank: return {120, 104, 78};
    case H::Sandbar: return {196, 178, 136};
    case H::OxbowLake: return {44, 78, 98};
    case H::Backswamp: return {74, 92, 58};
    case H::RiparianForest: return {42, 84, 44};
    case H::FloodplainOpen: return {128, 150, 72};
    case H::LeveeWoodland: return {66, 108, 54};
    case H::UplandConiferForest: return {38, 70, 56};
    case H::CutbankCliff: return {168, 130, 96};
    case H::DuneField: return {214, 182, 130};
    case H::InterduneFlat: return {190, 168, 130};
    case H::EphemeralPond: return {70, 104, 120};
    case H::XericScrub: return {150, 146, 98};
    case H::Count: break;
    }
    return {128, 128, 128};
}

inline Rgb speciesColor(const noctis::SpeciesDefinition& d, int ordinal)
{
    const bool predator = d.sim.predation.style != noctis::HuntStyle::None && d.sim.diet.type != noctis::DietType::Herbivore;
    static const Rgb predators[] = {{235, 64, 52}, {255, 140, 40}, {255, 90, 160}, {200, 40, 90}, {255, 200, 60}, {240, 110, 110}};
    static const Rgb others[] = {{90, 200, 255}, {255, 255, 255}, {170, 120, 255}, {120, 255, 200}, {255, 230, 140}, {80, 140, 255}, {200, 255, 120}, {255, 180, 230}};
    return predator ? predators[ordinal % 6] : others[ordinal % 8];
}

struct MapOptions
{
    int size = 1024;
    bool creatures = true;
    bool tracks = true;
    bool trails = true;
    bool legend = true;
    bool explored = false; // dim cells the player has not explored
    noctis::Rect2 window{}; // world sub-rectangle (empty = whole map)
    std::string title;
};

inline Image renderMap(noctis::WorldSimulation& w, const MapOptions& opt)
{
    using namespace noctis;
    const Terrain& t = w.terrain();
    Rect2 win = opt.window;
    if (win.size().x <= 0.0f)
    {
        win = t.bounds();
    }
    const int W = opt.size;
    const int H = opt.size;
    Image img(W, H);
    const float sx = win.size().x / static_cast<float>(W);
    const float sy = win.size().y / static_cast<float>(H);
    const Vec3 light = Vec3{-0.55f, 0.55f, 0.63f}.normalized();
    auto toPx = [&](const Vec2& p, float& px, float& py) {
        px = (p.x - win.min.x) / sx;
        py = static_cast<float>(H) - (p.y - win.min.y) / sy;
    };
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const Vec2 p{win.min.x + (static_cast<float>(x) + 0.5f) * sx, win.min.y + (static_cast<float>(H - 1 - y) + 0.5f) * sy};
            const float e = std::max(t.spacing(), sx);
            const float hx = (t.heightAt({p.x + e, p.y}) - t.heightAt({p.x - e, p.y})) / (2.0f * e) * 4.0f;
            const float hy = (t.heightAt({p.x, p.y + e}) - t.heightAt({p.x, p.y - e})) / (2.0f * e) * 4.0f;
            const Vec3 n = Vec3{-hx, -hy, 1.0f}.normalized();
            const float shade = 0.45f + 0.75f * std::max(0.0f, dot(n, light));
            Rgb c = habitatColor(t.habitatAt(p));
            const float canopy = w.vegetation().canopyCover(p);
            c = mix(c, Rgb{28, 58, 34}, canopy * 0.45f);
            if (opt.trails)
            {
                const float tr = w.vegetation().trail(p);
                if (tr > 0.15f)
                {
                    c = mix(c, Rgb{150, 128, 96}, saturate((tr - 0.15f) * 1.5f));
                }
            }
            if (w.vegetation().burning(p))
            {
                c = Rgb{255, 110, 30};
            }
            c = scale(c, shade);
            const WaterQuery wq = w.water().query(p, t);
            if (wq.isWater)
            {
                const Rgb shallow{70, 120, 140};
                const Rgb deep{18, 44, 70};
                c = mix(c, mix(shallow, deep, saturate(wq.depth / 3.0f)), 0.85f);
            }
            if (opt.explored && !w.research().map().explored(p))
            {
                c = scale(c, 0.35f);
            }
            img.at(x, y) = c;
        }
    }
    const float pxPerM = static_cast<float>(W) / win.size().x;
    if (opt.tracks)
    {
        w.tracks().forEach([&](const Footprint& f) {
            if (!win.contains(f.position) || f.depthM < 0.01f)
            {
                return;
            }
            float px = 0.0f;
            float py = 0.0f;
            toPx(f.position, px, py);
            img.blend(static_cast<int>(px), static_cast<int>(py), Rgb{40, 30, 20}, 0.5f * f.sharpness);
        });
    }
    // Carcasses.
    for (const Carcass& c : w.carcasses().all())
    {
        if (!c.active || !win.contains(c.position))
        {
            continue;
        }
        float px = 0.0f;
        float py = 0.0f;
        toPx(c.position, px, py);
        const float r = std::max(3.0f, 4.0f * pxPerM * 4.0f);
        img.line(px - r, py - r, px + r, py + r, Rgb{120, 10, 10});
        img.line(px - r, py + r, px + r, py - r, Rgb{120, 10, 10});
    }
    // Nests.
    for (const Nest& n : w.nests().all())
    {
        if (!n.active || !win.contains(n.position))
        {
            continue;
        }
        float px = 0.0f;
        float py = 0.0f;
        toPx(n.position, px, py);
        img.ring(px, py, 4.0f, Rgb{255, 240, 200});
    }
    std::map<int, int> ordinals;
    std::map<int, int> counts;
    if (opt.creatures)
    {
        int ord = 0;
        for (const int si : w.speciesInWorld())
        {
            if (w.db().speciesAt(si).sim.role == SimRole::Agent)
            {
                ordinals[si] = ord++;
            }
        }
        for (const Group& g : w.creatures().groups())
        {
            if (g.members.size() < 2 || !win.contains(g.centroid))
            {
                continue;
            }
            float px = 0.0f;
            float py = 0.0f;
            toPx(g.centroid, px, py);
            const Rgb gc = speciesColor(w.db().speciesAt(g.speciesIndex), ordinals[g.speciesIndex]);
            img.ring(px, py, std::max(6.0f, (g.spread + 10.0f) * pxPerM), gc, 0.6f);
        }
        for (const Creature& c : w.creatures().creatures())
        {
            if (!c.alive || !win.contains(c.pos2()))
            {
                continue;
            }
            ++counts[c.speciesIndex];
            float px = 0.0f;
            float py = 0.0f;
            toPx(c.pos2(), px, py);
            const Rgb col = speciesColor(w.db().speciesAt(c.speciesIndex), ordinals[c.speciesIndex]);
            const float r = std::max(1.6f, std::min(9.0f, 1.2f + std::log10(std::max(1.0f, c.massKg)) * 1.3f) * std::max(1.0f, pxPerM * 2.0f));
            img.disc(px, py, r + 1.0f, Rgb{0, 0, 0}, 0.7f);
            img.disc(px, py, r, col);
            // Heading tick.
            const Vec2 hd = Vec2::fromHeading(c.loco.heading);
            img.line(px, py, px + hd.x * (r + 4.0f), py - hd.y * (r + 4.0f), col);
        }
    }
    // Researcher, vehicle, drone.
    {
        float px = 0.0f;
        float py = 0.0f;
        toPx(w.research().vehicle().state().position.xy(), px, py);
        img.rect(static_cast<int>(px) - 4, static_cast<int>(py) - 4, static_cast<int>(px) + 4, static_cast<int>(py) + 4, Rgb{255, 255, 255});
        img.rect(static_cast<int>(px) - 2, static_cast<int>(py) - 2, static_cast<int>(px) + 2, static_cast<int>(py) + 2, Rgb{20, 20, 20});
        if (w.research().drone().state().mode != DroneMode::Docked)
        {
            toPx(w.research().drone().state().position.xy(), px, py);
            img.ring(px, py, 6.0f, Rgb{255, 255, 255});
            img.line(px - 6, py, px + 6, py, Rgb{255, 255, 255});
            img.line(px, py - 6, px, py + 6, Rgb{255, 255, 255});
        }
    }
    if (opt.legend)
    {
        int y = 10;
        if (!opt.title.empty())
        {
            drawText(img, 10, y, opt.title, Rgb{255, 255, 255}, 2);
            y += 22;
        }
        drawText(img, 10, y, w.clock().formatted() + "  " + weatherRegimeLabelFr(w.weather().state().regime), Rgb{230, 230, 230}, 2);
        y += 22;
        for (const auto& o : ordinals)
        {
            if (counts[o.first] == 0)
            {
                continue;
            }
            const SpeciesDefinition& d = w.db().speciesAt(o.first);
            img.disc(16.0f, static_cast<float>(y) + 6.0f, 5.0f, speciesColor(d, o.second));
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  %d", d.scientificName.c_str(), counts[o.first]);
            drawText(img, 28, y, buf, Rgb{255, 255, 255}, 2);
            y += 18;
        }
        // Scale bar (1 km or 100 m).
        const float barM = win.size().x > 3000.0f ? 1000.0f : (win.size().x > 600.0f ? 100.0f : 10.0f);
        const int barPx = static_cast<int>(barM * pxPerM);
        img.rect(10, H - 20, 10 + barPx, H - 16, Rgb{255, 255, 255});
        drawText(img, 14 + barPx, H - 26, barM >= 1000.0f ? "1 KM" : (barM >= 100.0f ? "100 M" : "10 M"), Rgb{255, 255, 255}, 2);
    }
    return img;
}

inline Image renderSpectrogram(const noctis::Recording& r, const noctis::RecordingAnalysis& a, const std::string& title, int width = 1400, int height = 520,
                               float minHz = 10.0f, float maxHz = 4000.0f)
{
    using namespace noctis;
    Image img(width, height, Rgb{12, 12, 18});
    const Spectrogram& s = a.spectrogram;
    if (s.frames == 0)
    {
        return img;
    }
    const int left = 70;
    const int right = 20;
    const int top = 40;
    const int bottom = 40;
    const int pw = width - left - right;
    const int ph = height - top - bottom;
    std::vector<float> all = s.db;
    std::nth_element(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(all.size() / 2), all.end());
    const float lo = all[all.size() / 2] - 5.0f;
    float hi = lo + 10.0f;
    for (const float v : s.db)
    {
        hi = std::max(hi, v);
    }
    const float lmin = std::log10(minHz);
    const float lmax = std::log10(maxHz);
    for (int x = 0; x < pw; ++x)
    {
        const int f = std::min(s.frames - 1, x * s.frames / pw);
        for (int y = 0; y < ph; ++y)
        {
            const float lf = lmax - (lmax - lmin) * static_cast<float>(y) / static_cast<float>(ph);
            const float hz = std::pow(10.0f, lf);
            const int b = std::min(s.bins - 1, std::max(0, static_cast<int>(hz / s.binHz)));
            const float v = (s.at(f, b) - lo) / std::max(1.0f, hi - lo);
            img.at(left + x, top + y) = viridis(std::pow(saturate(v), 0.8f));
        }
    }
    // Detected events.
    for (const AcousticEvent& e : a.events)
    {
        const int x0 = left + static_cast<int>(e.startS / r.durationS * static_cast<float>(pw));
        const int x1 = left + static_cast<int>(e.endS / r.durationS * static_cast<float>(pw));
        const float lf = std::log10(std::max(minHz, e.f0Hz));
        const int yf = top + static_cast<int>((lmax - lf) / (lmax - lmin) * static_cast<float>(ph));
        img.line(static_cast<float>(x0), static_cast<float>(top + 2), static_cast<float>(x1), static_cast<float>(top + 2), Rgb{255, 80, 80});
        img.line(static_cast<float>(x0), static_cast<float>(yf), static_cast<float>(x1), static_cast<float>(yf), Rgb{255, 255, 255}, 0.7f);
    }
    for (const float hz : {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f})
    {
        const int y = top + static_cast<int>((lmax - std::log10(hz)) / (lmax - lmin) * static_cast<float>(ph));
        img.line(static_cast<float>(left - 6), static_cast<float>(y), static_cast<float>(left), static_cast<float>(y), Rgb{200, 200, 200});
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f", hz);
        drawText(img, 4, y - 6, buf, Rgb{200, 200, 200}, 2, false);
    }
    for (int sec = 0; sec <= static_cast<int>(r.durationS); sec += std::max(1, static_cast<int>(r.durationS / 10.0f)))
    {
        const int x = left + static_cast<int>(static_cast<float>(sec) / r.durationS * static_cast<float>(pw));
        img.line(static_cast<float>(x), static_cast<float>(top + ph), static_cast<float>(x), static_cast<float>(top + ph + 6), Rgb{200, 200, 200});
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%dS", sec);
        drawText(img, x - 8, top + ph + 10, buf, Rgb{200, 200, 200}, 2, false);
    }
    drawText(img, left, 10, title, Rgb{255, 255, 255}, 2, false);
    drawText(img, 4, top - 14, "HZ", Rgb{200, 200, 200}, 2, false);
    return img;
}
} // namespace noctistools
