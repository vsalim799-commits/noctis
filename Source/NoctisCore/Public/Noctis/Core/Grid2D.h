// Regular 2D grid over the world's horizontal plane (row-major, origin at min corner).
#pragma once

#include "Noctis/Core/Math.h"

#include <vector>

namespace noctis
{
template <typename T>
class Grid2D
{
public:
    Grid2D() = default;
    Grid2D(int width, int height, float cellSize, Vec2 origin, const T& fill = T())
    {
        resize(width, height, cellSize, origin, fill);
    }

    void resize(int width, int height, float cellSize, Vec2 origin, const T& fill = T())
    {
        width_ = width;
        height_ = height;
        cellSize_ = cellSize;
        origin_ = origin;
        data_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), fill);
    }

    int width() const { return width_; }
    int height() const { return height_; }
    float cellSize() const { return cellSize_; }
    Vec2 origin() const { return origin_; }
    size_t cellCount() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    Rect2 bounds() const { return {origin_, origin_ + Vec2{width_ * cellSize_, height_ * cellSize_}}; }

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    int clampX(int x) const { return x < 0 ? 0 : (x >= width_ ? width_ - 1 : x); }
    int clampY(int y) const { return y < 0 ? 0 : (y >= height_ ? height_ - 1 : y); }
    size_t index(int x, int y) const { return static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x); }

    T& at(int x, int y) { return data_[index(x, y)]; }
    const T& at(int x, int y) const { return data_[index(x, y)]; }
    T& atClamped(int x, int y) { return data_[index(clampX(x), clampY(y))]; }
    const T& atClamped(int x, int y) const { return data_[index(clampX(x), clampY(y))]; }
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    // World position -> integer cell (floor), and cell centre -> world.
    void worldToCell(const Vec2& p, int& x, int& y) const
    {
        x = static_cast<int>(std::floor((p.x - origin_.x) / cellSize_));
        y = static_cast<int>(std::floor((p.y - origin_.y) / cellSize_));
    }
    Vec2 cellCenter(int x, int y) const
    {
        return {origin_.x + (static_cast<float>(x) + 0.5f) * cellSize_, origin_.y + (static_cast<float>(y) + 0.5f) * cellSize_};
    }
    const T& sampleNearest(const Vec2& p) const
    {
        int x = 0;
        int y = 0;
        worldToCell(p, x, y);
        return atClamped(x, y);
    }
    T& sampleNearestMutable(const Vec2& p)
    {
        int x = 0;
        int y = 0;
        worldToCell(p, x, y);
        return atClamped(x, y);
    }

    std::vector<T>& raw() { return data_; }
    const std::vector<T>& raw() const { return data_; }

private:
    int width_ = 0;
    int height_ = 0;
    float cellSize_ = 1.0f;
    Vec2 origin_;
    std::vector<T> data_;
};

// Bilinear sampling for float grids whose samples sit on cell centres.
inline float sampleBilinear(const Grid2D<float>& g, const Vec2& p)
{
    if (g.empty())
    {
        return 0.0f;
    }
    const float fx = (p.x - g.origin().x) / g.cellSize() - 0.5f;
    const float fy = (p.y - g.origin().y) / g.cellSize() - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float a = g.atClamped(x0, y0);
    const float b = g.atClamped(x0 + 1, y0);
    const float c = g.atClamped(x0, y0 + 1);
    const float d = g.atClamped(x0 + 1, y0 + 1);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}
} // namespace noctis
