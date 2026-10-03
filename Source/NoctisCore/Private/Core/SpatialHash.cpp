#include "Noctis/Core/SpatialHash.h"

namespace noctis
{
void SpatialHash::configure(const Rect2& bounds, float cellSize)
{
    bounds_ = bounds;
    cellSize_ = cellSize > 1.0f ? cellSize : 1.0f;
    const Vec2 size = bounds.size();
    cellsX_ = static_cast<int>(std::ceil(size.x / cellSize_)) + 1;
    cellsY_ = static_cast<int>(std::ceil(size.y / cellSize_)) + 1;
    clear();
}

void SpatialHash::clear()
{
    entries_.clear();
    pendingCell_.clear();
    dirty_ = true;
}

void SpatialHash::insert(u32 handle, const Vec2& position)
{
    entries_.push_back({handle, position});
    int cx = static_cast<int>((position.x - bounds_.min.x) / cellSize_);
    int cy = static_cast<int>((position.y - bounds_.min.y) / cellSize_);
    cx = cx < 0 ? 0 : (cx >= cellsX_ ? cellsX_ - 1 : cx);
    cy = cy < 0 ? 0 : (cy >= cellsY_ ? cellsY_ - 1 : cy);
    pendingCell_.push_back(cellIndex(cx, cy));
    dirty_ = true;
}

void SpatialHash::build() const
{
    if (!dirty_)
    {
        return;
    }
    const size_t cellCount = static_cast<size_t>(cellsX_) * static_cast<size_t>(cellsY_);
    builtStart_.assign(cellCount + 1u, 0u);
    for (const int c : pendingCell_)
    {
        ++builtStart_[static_cast<size_t>(c) + 1u];
    }
    for (size_t i = 1; i <= cellCount; ++i)
    {
        builtStart_[i] += builtStart_[i - 1];
    }
    builtEntries_.assign(entries_.size(), 0u);
    std::vector<u32> cursor(builtStart_.begin(), builtStart_.end() - 1);
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const size_t c = static_cast<size_t>(pendingCell_[i]);
        builtEntries_[cursor[c]++] = static_cast<u32>(i);
    }
    dirty_ = false;
}

void SpatialHash::query(const Vec2& center, float radius, std::vector<u32>& out) const
{
    if (entries_.empty())
    {
        return;
    }
    build();
    const float r2 = radius * radius;
    const int x0 = std::max(0, static_cast<int>((center.x - radius - bounds_.min.x) / cellSize_));
    const int y0 = std::max(0, static_cast<int>((center.y - radius - bounds_.min.y) / cellSize_));
    const int x1 = std::min(cellsX_ - 1, static_cast<int>((center.x + radius - bounds_.min.x) / cellSize_));
    const int y1 = std::min(cellsY_ - 1, static_cast<int>((center.y + radius - bounds_.min.y) / cellSize_));
    for (int cy = y0; cy <= y1; ++cy)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            const size_t c = static_cast<size_t>(cellIndex(cx, cy));
            for (u32 k = builtStart_[c]; k < builtStart_[c + 1]; ++k)
            {
                const Entry& e = entries_[builtEntries_[k]];
                if (distanceSq(e.position, center) <= r2)
                {
                    out.push_back(e.handle);
                }
            }
        }
    }
}

void SpatialHash::queryBox(const Rect2& box, std::vector<u32>& out) const
{
    if (entries_.empty())
    {
        return;
    }
    build();
    const int x0 = std::max(0, static_cast<int>((box.min.x - bounds_.min.x) / cellSize_));
    const int y0 = std::max(0, static_cast<int>((box.min.y - bounds_.min.y) / cellSize_));
    const int x1 = std::min(cellsX_ - 1, static_cast<int>((box.max.x - bounds_.min.x) / cellSize_));
    const int y1 = std::min(cellsY_ - 1, static_cast<int>((box.max.y - bounds_.min.y) / cellSize_));
    for (int cy = y0; cy <= y1; ++cy)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            const size_t c = static_cast<size_t>(cellIndex(cx, cy));
            for (u32 k = builtStart_[c]; k < builtStart_[c + 1]; ++k)
            {
                const Entry& e = entries_[builtEntries_[k]];
                if (box.contains(e.position))
                {
                    out.push_back(e.handle);
                }
            }
        }
    }
}
} // namespace noctis
