// Minimal RGB image + PNG encoder (stored deflate, no external dependency) for tools.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace noctistools
{
struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
};

inline Rgb mix(Rgb a, Rgb b, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return {static_cast<uint8_t>(a.r + (b.r - a.r) * t), static_cast<uint8_t>(a.g + (b.g - a.g) * t), static_cast<uint8_t>(a.b + (b.b - a.b) * t)};
}

inline Rgb scale(Rgb a, float s)
{
    auto c = [&](uint8_t v) {
        const float x = v * s;
        return static_cast<uint8_t>(x < 0.0f ? 0.0f : (x > 255.0f ? 255.0f : x));
    };
    return {c(a.r), c(a.g), c(a.b)};
}

struct Image
{
    int w = 0;
    int h = 0;
    std::vector<Rgb> px;
    Image() = default;
    Image(int width, int height, Rgb fill = {}) : w(width), h(height), px(static_cast<size_t>(width) * static_cast<size_t>(height), fill) {}
    Rgb& at(int x, int y) { return px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
    void set(int x, int y, Rgb c)
    {
        if (x >= 0 && y >= 0 && x < w && y < h)
        {
            at(x, y) = c;
        }
    }
    void blend(int x, int y, Rgb c, float a)
    {
        if (x >= 0 && y >= 0 && x < w && y < h)
        {
            at(x, y) = mix(at(x, y), c, a);
        }
    }
    void disc(float cx, float cy, float r, Rgb c, float alpha = 1.0f)
    {
        for (int y = static_cast<int>(cy - r - 1); y <= static_cast<int>(cy + r + 1); ++y)
        {
            for (int x = static_cast<int>(cx - r - 1); x <= static_cast<int>(cx + r + 1); ++x)
            {
                const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                const float cov = r + 0.5f - d;
                if (cov > 0.0f)
                {
                    blend(x, y, c, alpha * (cov > 1.0f ? 1.0f : cov));
                }
            }
        }
    }
    void ring(float cx, float cy, float r, Rgb c, float alpha = 1.0f)
    {
        for (int y = static_cast<int>(cy - r - 2); y <= static_cast<int>(cy + r + 2); ++y)
        {
            for (int x = static_cast<int>(cx - r - 2); x <= static_cast<int>(cx + r + 2); ++x)
            {
                const float d = std::fabs(std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) - r);
                if (d < 1.0f)
                {
                    blend(x, y, c, alpha * (1.0f - d));
                }
            }
        }
    }
    void line(float x0, float y0, float x1, float y1, Rgb c, float alpha = 1.0f)
    {
        const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        const int n = static_cast<int>(len * 2.0f) + 1;
        for (int i = 0; i <= n; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            blend(static_cast<int>(x0 + (x1 - x0) * t), static_cast<int>(y0 + (y1 - y0) * t), c, alpha);
        }
    }
    void rect(int x0, int y0, int x1, int y1, Rgb c, float alpha = 1.0f)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                blend(x, y, c, alpha);
            }
        }
    }
};

namespace pngimpl
{
inline uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0xFFFFFFFFu)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < n; ++i)
    {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

inline void put32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put32(out, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
} // namespace pngimpl

// Encodes an 8-bit RGB or 16-bit grayscale PNG with uncompressed deflate blocks.
inline bool writePngRaw(const std::string& path, int w, int h, const std::vector<uint8_t>& rows, int colorType, int bitDepth)
{
    using namespace pngimpl;
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(w));
    put32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(static_cast<uint8_t>(bitDepth));
    ihdr.push_back(static_cast<uint8_t>(colorType));
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1;
    uint32_t b = 0;
    size_t pos = 0;
    while (pos < rows.size())
    {
        const size_t n = std::min<size_t>(65535, rows.size() - pos);
        const bool last = pos + n >= rows.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        for (size_t i = 0; i < n; ++i)
        {
            const uint8_t v = rows[pos + i];
            z.push_back(v);
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
        }
        pos += n;
    }
    put32(z, (b << 16) | a);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

inline bool writePng(const std::string& path, const Image& img)
{
    std::vector<uint8_t> rows;
    rows.reserve(static_cast<size_t>(img.h) * (static_cast<size_t>(img.w) * 3u + 1u));
    for (int y = 0; y < img.h; ++y)
    {
        rows.push_back(0);
        for (int x = 0; x < img.w; ++x)
        {
            const Rgb c = img.px[static_cast<size_t>(y) * static_cast<size_t>(img.w) + static_cast<size_t>(x)];
            rows.push_back(c.r);
            rows.push_back(c.g);
            rows.push_back(c.b);
        }
    }
    return writePngRaw(path, img.w, img.h, rows, 2, 8);
}

inline bool writePng16Gray(const std::string& path, int w, int h, const std::vector<uint16_t>& v)
{
    std::vector<uint8_t> rows;
    rows.reserve(static_cast<size_t>(h) * (static_cast<size_t>(w) * 2u + 1u));
    for (int y = 0; y < h; ++y)
    {
        rows.push_back(0);
        for (int x = 0; x < w; ++x)
        {
            const uint16_t s = v[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            rows.push_back(static_cast<uint8_t>(s >> 8));
            rows.push_back(static_cast<uint8_t>(s & 0xFF));
        }
    }
    return writePngRaw(path, w, h, rows, 0, 16);
}

// Perceptual colormap (approximation of viridis) for spectrograms.
inline Rgb viridis(float t)
{
    static const std::array<Rgb, 6> k = {Rgb{68, 1, 84}, Rgb{65, 68, 135}, Rgb{42, 120, 142}, Rgb{34, 168, 132}, Rgb{122, 209, 81}, Rgb{253, 231, 37}};
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float f = t * 5.0f;
    const int i = static_cast<int>(f);
    if (i >= 5)
    {
        return k[5];
    }
    return mix(k[static_cast<size_t>(i)], k[static_cast<size_t>(i + 1)], f - static_cast<float>(i));
}
} // namespace noctistools
