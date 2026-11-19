// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/ScreenReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace lr::mcdu {

namespace {
constexpr char kMagic[8] = {'M', 'C', 'D', 'U', 'G', 'L', 'Y', '1'};
constexpr uint32_t kMaxSamples = 20000;
constexpr int kPix = kGlyphW * kGlyphH;
constexpr int kHalfW = 7, kHalfH = 11;          // a cell: 15 x 23 pixels around its centre
constexpr int kShortlist = 8;                   // samples looked at closely, with the picture moved by a pixel each way
constexpr float kGeoWeight = 0.06f, kUnsure = 0.55f;

// Where the screen sits in the pop-out at the size it opens with (pixels): its dark blue ground, inside a black border.
constexpr float kPanelX = 20.f, kPanelY = 4.f, kPanelW = 401.f, kPanelH = 358.f;

// The screen's rectangle in a picture of any size: the columns and rows that are mostly not black.
bool findPanel(const uint8_t* img, int w, int h, int stride, float& x0, float& y0, float& pw, float& ph) {
    auto lit = [&](int x, int y) { const uint8_t* p = img + y * stride + x * 4; return std::max({p[0], p[1], p[2]}) >= 4; };
    const int stepY = std::max(1, h / 128), stepX = std::max(1, w / 128);
    int left = -1, right = -1, top = -1, bottom = -1;
    for (int x = 0; x < w; ++x) {
        int n = 0, t = 0;
        for (int y = 0; y < h; y += stepY) ++t, n += lit(x, y);
        if (n * 2 > t) { if (left < 0) left = x; right = x; }
    }
    for (int y = 0; y < h; ++y) {
        int n = 0, t = 0;
        for (int x = 0; x < w; x += stepX) ++t, n += lit(x, y);
        if (n * 2 > t) { if (top < 0) top = y; bottom = y; }
    }
    if (left < 0 || top < 0) return false;
    pw = static_cast<float>(right - left + 1), ph = static_cast<float>(bottom - top + 1);
    const float aspect = (pw / ph) / (kPanelW / kPanelH);
    if (pw < 48 || ph < 40 || aspect < 0.9f || aspect > 1.1f) return false;  // not the screen as it should look: fall back
    x0 = static_cast<float>(left), y0 = static_cast<float>(top);
    return true;
}

bool punctuation(char32_t ch) { return ch == U'.' || ch == U'-' || ch == U'/' || ch == U':' || ch == U'\'' || ch == U'"' || ch == U','; }
}  // namespace

struct ScreenReader::Prepared {
    std::vector<float> pix, geo, mass;
    std::vector<char32_t> ch;
    std::vector<uint8_t> small;
    size_t size() const { return ch.size(); }
};

// ---- the sample file ----

bool GlyphBank::load(const std::string& path, std::string& err) {
    samples_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "no file " + path; return false; }
    char magic[8];
    uint32_t count = 0;
    uint16_t w = 0, h = 0;
    f.read(magic, 8);
    f.read(reinterpret_cast<char*>(&count), 4);
    f.read(reinterpret_cast<char*>(&w), 2);
    f.read(reinterpret_cast<char*>(&h), 2);
    if (!f || std::memcmp(magic, kMagic, 8) != 0 || w != kGlyphW || h != kGlyphH || count == 0 || count > kMaxSamples) { err = "not a sample file: " + path; return false; }
    samples_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        GlyphSample s;
        uint32_t ch = 0;
        uint8_t small = 0, pad[3];
        f.read(reinterpret_cast<char*>(&ch), 4);
        f.read(reinterpret_cast<char*>(&small), 1);
        f.read(reinterpret_cast<char*>(pad), 3);
        f.read(reinterpret_cast<char*>(s.geo.data()), sizeof(float) * 3);
        f.read(reinterpret_cast<char*>(s.pix.data()), kPix);
        if (!f || ch > 0x10ffff || !std::isfinite(s.geo[0]) || !std::isfinite(s.geo[1]) || !std::isfinite(s.geo[2])) { samples_.clear(); err = "broken sample file: " + path; return false; }
        s.ch = static_cast<char32_t>(ch);
        s.small = small != 0;
        samples_.push_back(s);
    }
    return true;
}

bool GlyphBank::save(const std::string& path, std::string& err) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot write " + path; return false; }
    const uint32_t count = static_cast<uint32_t>(samples_.size());
    const uint16_t w = kGlyphW, h = kGlyphH;
    f.write(kMagic, 8);
    f.write(reinterpret_cast<const char*>(&count), 4);
    f.write(reinterpret_cast<const char*>(&w), 2);
    f.write(reinterpret_cast<const char*>(&h), 2);
    for (const auto& s : samples_) {
        const uint32_t ch = static_cast<uint32_t>(s.ch);
        const uint8_t small = s.small ? 1 : 0, pad[3] = {0, 0, 0};
        f.write(reinterpret_cast<const char*>(&ch), 4);
        f.write(reinterpret_cast<const char*>(&small), 1);
        f.write(reinterpret_cast<const char*>(pad), 3);
        f.write(reinterpret_cast<const char*>(s.geo.data()), sizeof(float) * 3);
        f.write(reinterpret_cast<const char*>(s.pix.data()), kPix);
    }
    if (!f) { err = "cannot write " + path; return false; }
    return true;
}

std::string defaultGlyphBankPath() {
    std::string base;
    if (const char* d = std::getenv("XDG_DATA_HOME"); d && *d) base = d;
    else if (const char* home = std::getenv("HOME"); home && *home) base = std::string(home) + "/.local/share";
    else return {};
    return base + "/msfs-linux-companion/mcdu-glyphs.bin";
}

Color classifyColor(float r, float g, float b) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    if (mx <= 0.f) return Color::White;
    const float sat = (mx - mn) / mx;
    if (sat < 0.25f) return mx < 130.f ? Color::Grey : Color::White;
    float hue;
    const float d = mx - mn;
    if (mx == r) hue = std::fmod((g - b) / d, 6.f) * 60.f;
    else if (mx == g) hue = ((b - r) / d + 2.f) * 60.f;
    else hue = ((r - g) / d + 4.f) * 60.f;
    if (hue < 0.f) hue += 360.f;
    if (hue < 20.f || hue >= 340.f) return Color::Red;
    if (hue < 50.f) return Color::Amber;
    if (hue < 70.f) return Color::Yellow;
    if (hue < 165.f) return Color::Green;
    if (hue < 250.f) return Color::Cyan;
    return Color::Magenta;
}

// ---- reading ----

ScreenReader::ScreenReader(const GlyphBank& bank) : cache_(static_cast<size_t>(kRows * kCols)) {
    auto p = std::make_shared<Prepared>();
    const size_t n = bank.samples().size();
    p->pix.resize(n * kPix);
    p->geo.resize(n * 3);
    p->mass.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const GlyphSample& s = bank.samples()[i];
        float m = 0.f;
        for (int k = 0; k < kPix; ++k) m += (p->pix[i * kPix + k] = s.pix[static_cast<size_t>(k)] / 255.f);
        p->mass[i] = m;
        for (int k = 0; k < 3; ++k) p->geo[i * 3 + k] = s.geo[static_cast<size_t>(k)];
        p->ch.push_back(s.ch);
        p->small.push_back(s.small ? 1 : 0);
    }
    bank_ = std::move(p);
}

void ScreenReader::reset() {
    for (auto& c : cache_) c.valid = false;
}

ScreenReader::CellResult ScreenReader::readCell(const uint8_t* img, int stride, int r, int c) const {
    CellResult out;
    const int cx = static_cast<int>(std::lround(kX0 + c * kPitchX)), cy = static_cast<int>(std::lround(kY0 + r * kPitchY));
    const int x0 = std::max(cx - kHalfW, 0), x1 = std::min(cx + kHalfW, kRefW - 1), y0 = std::max(cy - kHalfH, 0), y1 = std::min(cy + kHalfH, kRefH - 1);
    if (x1 < x0 || y1 < y0) return out;
    const int pw = x1 - x0 + 1, ph = y1 - y0 + 1;
    float v[kGlyphH * 15];
    float m = 0.f;
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) {
            const uint8_t* px = img + (y0 + y) * stride + (x0 + x) * 4;
            const float val = std::max({px[0], px[1], px[2]});
            v[y * pw + x] = val;
            m = std::max(m, val);
        }
    if (m < 60.f) return out;
    int on = 0, minX = pw, maxX = -1, minY = ph, maxY = -1;
    float sr = 0, sg = 0, sb = 0;
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
            if (v[y * pw + x] > 0.55f * m) {
                ++on;
                minX = std::min(minX, x), maxX = std::max(maxX, x), minY = std::min(minY, y), maxY = std::max(maxY, y);
                const uint8_t* px = img + (y0 + y) * stride + (x0 + x) * 4;
                sb += px[0], sg += px[1], sr += px[2];
            }
    if (on < 4) return out;
    out.blank = false;
    out.color = classifyColor(sr / on, sg / on, sb / on);
    // the character, centred on its box, as the samples are
    const int bh = maxY - minY + 1, bw = maxX - minX + 1;
    float cv[kPix] = {};
    const int oy = (kGlyphH - bh) / 2, ox = (kGlyphW - bw) / 2;
    for (int y = 0; y < bh && oy + y < kGlyphH; ++y)
        for (int x = 0; x < bw && ox + x < kGlyphW; ++x) {
            const float val = v[(minY + y) * pw + minX + x];
            cv[(oy + y) * kGlyphW + ox + x] = val > 0.25f * m ? std::min(val / m, 1.f) : 0.f;
        }
    const float geo[3] = {static_cast<float>(bh), static_cast<float>(bw), static_cast<float>(minY + y0 - cy)};
    const Prepared& b = *bank_;
    const size_t n = b.size();
    if (n == 0) { out.unsure = true, out.ch = U'?'; return out; }
    auto distance = [&](size_t i, const float* a) {
        const float* s = &b.pix[i * kPix];
        float diff = 0.f, uni = 0.f;
        for (int k = 0; k < kPix; ++k) diff += std::fabs(s[k] - a[k]), uni += std::max(s[k], a[k]);
        const float* g = &b.geo[i * 3];
        return diff / std::max(uni, 1e-3f) + kGeoWeight * (std::fabs(g[0] - geo[0]) + std::fabs(g[1] - geo[1]) + std::fabs(g[2] - geo[2]));
    };
    // first pass on all samples, then the closest few again with the picture shifted by one pixel
    std::pair<float, size_t> best[kShortlist];
    int nb = 0;
    for (size_t i = 0; i < n; ++i) {
        const float d = distance(i, cv);
        if (nb < kShortlist) best[nb++] = {d, i};
        else {
            auto worst = std::max_element(best, best + nb);
            if (d < worst->first) *worst = {d, i};
        }
    }
    float shifted[kPix];
    float bestD = 1e9f;
    size_t bestI = 0;
    for (int k = 0; k < nb; ++k) {
        float d = best[k].first;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dy && !dx) continue;
                for (int y = 0; y < kGlyphH; ++y)
                    for (int x = 0; x < kGlyphW; ++x) {
                        const int sy = y - dy, sx = x - dx;
                        shifted[y * kGlyphW + x] = sy >= 0 && sy < kGlyphH && sx >= 0 && sx < kGlyphW ? cv[sy * kGlyphW + sx] : 0.f;
                    }
                d = std::min(d, distance(best[k].second, shifted));
            }
        if (d < bestD) bestD = d, bestI = best[k].second;
    }
    out.ch = b.ch[bestI];
    out.small = b.small[bestI] != 0;
    if (bestD > kUnsure) out.unsure = true, out.ch = U'?';
    return out;
}

ReadResult ScreenReader::read(const uint8_t* bgrx, int width, int height, int stride) {
    ReadResult res;
    if (!bgrx || width < 64 || height < 64 || width > 8192 || height > 8192 || stride < width * 4) return res;
    const uint8_t* img = bgrx;
    int st = stride;
    if (width != kRefW || height != kRefH) {
        // A resized pop-out: the simulator fits the screen into the window, keeping its shape, with a border of a few
        // pixels that does not grow with the window. The screen is found by its dark blue ground and brought back to the
        // size the grid was measured at.
        float px0, py0, pw, ph;
        if (!findPanel(bgrx, width, height, stride, px0, py0, pw, ph)) {
            const float fit = std::min((width - 8.f) / kPanelW, (height - 8.f) / kPanelH);
            pw = kPanelW * fit, ph = kPanelH * fit, px0 = (width - pw) / 2.f, py0 = (height - ph) / 2.f;
        }
        const float scale = pw / kPanelW, scaleY = ph / kPanelH;
        res.scale = scale;
        const float offX = px0 - kPanelX * scale, offY = py0 - kPanelY * scaleY;
        scaled_.resize(static_cast<size_t>(kRefW) * kRefH * 4);
        for (int y = 0; y < kRefH; ++y) {
            const float fy = std::clamp(offY + (y + 0.5f) * scaleY - 0.5f, 0.f, height - 1.f);
            const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, height - 1);
            const float wy = fy - y0;
            for (int x = 0; x < kRefW; ++x) {
                const float fx = std::clamp(offX + (x + 0.5f) * scale - 0.5f, 0.f, width - 1.f);
                const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, width - 1);
                const float wx = fx - x0;
                for (int k = 0; k < 3; ++k) {
                    const float a = bgrx[y0 * stride + x0 * 4 + k], b = bgrx[y0 * stride + x1 * 4 + k], c = bgrx[y1 * stride + x0 * 4 + k], d = bgrx[y1 * stride + x1 * 4 + k];
                    scaled_[(static_cast<size_t>(y) * kRefW + x) * 4 + k] = static_cast<uint8_t>(std::lround((a * (1 - wx) + b * wx) * (1 - wy) + (c * (1 - wx) + d * wx) * wy));
                }
                scaled_[(static_cast<size_t>(y) * kRefW + x) * 4 + 3] = 0;
            }
        }
        img = scaled_.data();
        st = kRefW * 4;
    }
    CellResult cells[kRows][kCols];
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) {
            // a cell that looks exactly as before is not read again
            const int cx = static_cast<int>(std::lround(kX0 + c * kPitchX)), cy = static_cast<int>(std::lround(kY0 + r * kPitchY));
            uint64_t h = 1469598103934665603ull;
            for (int y = std::max(cy - kHalfH, 0); y <= std::min(cy + kHalfH, kRefH - 1); ++y) {
                const uint8_t* row = img + y * st;
                for (int x = std::max(cx - kHalfW, 0); x <= std::min(cx + kHalfW, kRefW - 1); ++x)
                    for (int k = 0; k < 3; ++k) h = (h ^ row[x * 4 + k]) * 1099511628211ull;
            }
            Cache& cache = cache_[static_cast<size_t>(r * kCols + c)];
            if (!cache.valid || cache.hash != h) cache = {h, true, readCell(img, st, r, c)};
            cells[r][c] = cache.cell;
        }
    for (int r = 0; r < kRows; ++r) {
        // punctuation has no size of its own: it takes the size of the closest letter or digit
        for (int c = 0; c < kCols; ++c) {
            CellResult& cell = cells[r][c];
            if (cell.blank || !punctuation(cell.ch)) continue;
            for (int d = 1; d < kCols; ++d) {
                const int a = c - d, b = c + d;
                if (a >= 0 && !cells[r][a].blank && !punctuation(cells[r][a].ch)) { cell.small = cells[r][a].small; break; }
                if (b < kCols && !cells[r][b].blank && !punctuation(cells[r][b].ch)) { cell.small = cells[r][b].small; break; }
            }
        }
        // the unit has 24 columns; the aircraft's scratchpad can use a 25th, then the line moves left by one
        const int first = cells[r][kCols - 1].blank ? 0 : 1;
        for (int c = 0; c < kCols; ++c) {
            const CellResult& cell = cells[r][c];
            if (cell.blank) continue;
            ++res.glyphs;
            if (cell.unsure) ++res.unsure;
            const int col = c - first;
            if (col < 0 || col >= kCols - 1) continue;
            Cell& out = res.screen.at(r, col);
            out.ch = cell.ch;
            out.color = cell.color;
            out.small = cell.small;
        }
    }
    return res;
}

}  // namespace lr::mcdu
