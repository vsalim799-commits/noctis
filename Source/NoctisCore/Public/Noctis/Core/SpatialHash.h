// Uniform-grid spatial index for neighbour queries. Rebuilt once per simulation step.
#pragma once

#include "Noctis/Core/Math.h"

#include <vector>

namespace noctis
{
class NOCTIS_API SpatialHash
{
public:
    void configure(const Rect2& bounds, float cellSize);
    void clear();
    void insert(u32 handle, const Vec2& position);
    // Must be called after the last insert and before concurrent queries (queries are then read-only).
    void finalize() const { build(); }
    // Appends handles within radius (exact distance test against stored positions).
    void query(const Vec2& center, float radius, std::vector<u32>& out) const;
    // Appends handles within the axis-aligned box (no distance test).
    void queryBox(const Rect2& box, std::vector<u32>& out) const;
    size_t count() const { return entries_.size(); }

private:
    struct Entry
    {
        u32 handle;
        Vec2 position;
    };
    int cellIndex(int cx, int cy) const { return cy * cellsX_ + cx; }

    Rect2 bounds_{};
    float cellSize_ = 64.0f;
    int cellsX_ = 0;
    int cellsY_ = 0;
    std::vector<Entry> entries_;
    std::vector<int> pendingCell_;
    // Compressed (CSR) cell layout, rebuilt lazily on first query after inserts.
    void build() const;
    mutable bool dirty_ = true;
    mutable std::vector<u32> builtStart_;
    mutable std::vector<u32> builtEntries_;
};
} // namespace noctis
