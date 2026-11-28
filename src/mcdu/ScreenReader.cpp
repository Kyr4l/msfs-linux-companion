// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kyr4l
#include "mcdu/ScreenReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
constexpr int kCoarse = 24;                     // samples compared pixel by pixel after the quick pass on summaries
constexpr int kPoolW = 5, kPoolH = 6, kPool = kPoolW * kPoolH;  // a character summarised in 4 x 4 pixel blocks
constexpr float kSizeSkip = 4.5f;               // a sample this many pixels taller or wider is not the same character

void pool(const float* pix, float* out) {
    std::fill(out, out + kPool, 0.f);
    for (int y = 0; y < kGlyphH; ++y)
        for (int x = 0; x < kGlyphW; ++x) out[std::min(y / 4, kPoolH - 1) * kPoolW + std::min(x / 4, kPoolW - 1)] += pix[y * kGlyphW + x];
}
constexpr float kGeoWeight = 0.06f, kUnsure = 0.55f;

// The screen's rectangle in a picture of any size: the columns and rows that are mostly not black.
bool findPanel(const ScreenLayout& L, const uint8_t* img, int w, int h, int stride, float& x0, float& y0, float& pw, float& ph) {
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
    const float aspect = (pw / ph) / (L.panelW / L.panelH);
    if (pw < 48 || ph < 40 || aspect < 0.9f || aspect > 1.1f) return false;  // not the screen as it should look: fall back
    x0 = static_cast<float>(left), y0 = static_cast<float>(top);
    return true;
}

std::array<float, 16> evenRows(float y0, float pitch, int n) {
    std::array<float, 16> out{};
    for (int r = 0; r < n; ++r) out[static_cast<size_t>(r)] = y0 + r * pitch;
    return out;
}
std::array<float, 16> wt21Rows() {  // the title stands apart; the other 14 rows are evenly spaced (measured at 525 x 425)
    std::array<float, 16> out = evenRows(62.5f - 25.73f, 25.73f, 15);
    out[0] = 20.f;
    return out;
}
}  // namespace

const ScreenLayout& airbusLayout() {
    // measured on the A330 and A320neo pop-outs at 442 x 366: the dark blue ground inside a black border
    static const ScreenLayout l{"Airbus", "WASMINSTRUMENT", "mcdu-glyphs.bin", 442, 366, 25, 14, 42.61f, 15.544f, evenRows(29.5f, 23.67f, 14),
                                20.f, 4.f, 401.f, 358.f, false, "POP OUT THE MCDU"};
    return l;
}

const ScreenLayout& wt21Layout() {
    // measured on the CJ4 of MSFS 2024 (2026-11-24): 24 x 15, scratchpad in brackets on row 13, messages under it
    static const ScreenLayout l{"CJ4", "WT21_FMC_", "mcdu-glyphs-wt21.bin", 525, 425, 24, 15, 71.9f, 16.58f, wt21Rows(),
                                59.f, 0.f, 407.f, 425.f, true, "POP OUT THE FMC"};
    return l;
}

namespace {
// Letters and digits under this height (in pixels at the reference size) are the small font: Airbus small 13-15 / large 18-19,
// CJ4 small 14-16 / large 17-19.
constexpr int kSmallBelow = 17;
bool punctuation(char32_t ch) { return ch == U'.' || ch == U'-' || ch == U'/' || ch == U':' || ch == U'\'' || ch == U'"' || ch == U','; }
}  // namespace

struct ScreenReader::Prepared {
    std::vector<float> pix, geo, mass, pooled;
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

std::string defaultGlyphBankPath(const char* file) {
    std::string base;
    if (const char* d = std::getenv("XDG_DATA_HOME"); d && *d) base = d;
    else if (const char* home = std::getenv("HOME"); home && *home) base = std::string(home) + "/.local/share";
    else return {};
    return base + "/msfs-linux-companion/" + file;
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

ScreenReader::ScreenReader(const GlyphBank& bank, const ScreenLayout& layout) : layout_(&layout), cache_(static_cast<size_t>(kMaxRows * kMaxCols)) {
    auto p = std::make_shared<Prepared>();
    const size_t n = bank.samples().size();
    p->pix.resize(n * kPix);
    p->geo.resize(n * 3);
    p->mass.resize(n);
    p->pooled.resize(n * kPool);
    for (size_t i = 0; i < n; ++i) {
        const GlyphSample& s = bank.samples()[i];
        float m = 0.f;
        for (int k = 0; k < kPix; ++k) m += (p->pix[i * kPix + k] = s.pix[static_cast<size_t>(k)] / 255.f);
        p->mass[i] = m;
        pool(&p->pix[i * kPix], &p->pooled[i * kPool]);
        for (int k = 0; k < 3; ++k) p->geo[i * 3 + k] = s.geo[static_cast<size_t>(k)];
        p->ch.push_back(s.ch);
        p->small.push_back(s.small ? 1 : 0);
    }
    bank_ = std::move(p);
}

void ScreenReader::reset() {
    for (auto& c : cache_) c.valid = false;
}

ScreenReader::CellResult ScreenReader::readCell(const uint8_t* img, int stride, int r, int cx, int cy, int yTop, int yBottom) const {
    CellResult out;
    const int refW = layout_->refW, refH = layout_->refH;
    const int x0 = std::max(cx - kHalfW, 0), x1 = std::min(cx + kHalfW, refW - 1);
    const int y0 = std::max({cy - kHalfH, yTop, 0}), y1 = std::min({cy + kHalfH, yBottom, refH - 1});
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
    // Reverse video (the CJ4 draws EXEC as dark letters in a white box, 2026-11-26): most of the cell is bright. The box is found as the
    // rows and columns that are mostly bright; inside it the picture is turned over (the letters become the bright strokes), outside it is
    // dropped, and the colour is the box's.
    bool inverse = false;
    float boxR = 0, boxG = 0, boxB = 0;
    {
        int bright = 0, fullRows = 0;  // a box has rows bright across almost the whole cell; a letter, even a bold one, never does
        for (int y = 0; y < ph; ++y) {
            int c = 0;
            for (int x = 0; x < pw; ++x) c += v[y * pw + x] > 0.55f * m;
            bright += c;
            fullRows += c * 100 >= pw * 85;
        }
        // and the gaps between letters, at both edges of the cell, are bright inside a box (two empty boxes side by side light only one)
        int edgeL = 0, edgeR = 0;
        for (int y = 0; y < ph; ++y) edgeL += v[y * pw] > 0.55f * m, edgeR += v[y * pw + pw - 1] > 0.55f * m;
        const bool brightEdge = std::min(edgeL, edgeR) * 10 >= ph * 6;
        if (bright > (pw * ph) * 45 / 100 && fullRows >= 2 && brightEdge) {
            int bx0 = pw, bx1 = -1, by0 = ph, by1 = -1;
            for (int y = 0; y < ph; ++y) {
                int c = 0;
                for (int x = 0; x < pw; ++x) c += v[y * pw + x] > 0.55f * m;
                if (c * 10 >= pw * 3) by0 = std::min(by0, y), by1 = std::max(by1, y);
            }
            for (int x = 0; x < pw; ++x) {
                int c = 0;
                for (int y = 0; y < ph; ++y) c += v[y * pw + x] > 0.55f * m;
                if (c * 10 >= ph * 3) bx0 = std::min(bx0, x), bx1 = std::max(bx1, x);
            }
            if (bx1 >= bx0 && by1 >= by0) {
                inverse = true;
                int n = 0;
                for (int y = 0; y < ph; ++y)
                    for (int x = 0; x < pw; ++x) {
                        float& val = v[y * pw + x];
                        if (val > 0.55f * m) {
                            const uint8_t* px = img + (y0 + y) * stride + (x0 + x) * 4;
                            boxB += px[0], boxG += px[1], boxR += px[2], ++n;
                        }
                        val = x >= bx0 && x <= bx1 && y >= by0 && y <= by1 ? m - val : 0.f;
                    }
                if (n) boxR /= n, boxG /= n, boxB /= n;
            }
        }
    }
    int on = 0, minX = pw, maxX = -1, minY = ph, maxY = -1;
    float sr = 0, sg = 0, sb = 0;
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
        {
            const float val = v[y * pw + x];
            if (val > 0.35f * m)  // the outline takes in thin, dim strokes too (the tail that tells Q from O)
                minX = std::min(minX, x), maxX = std::max(maxX, x), minY = std::min(minY, y), maxY = std::max(maxY, y);
            if (val > 0.55f * m) {
                ++on;
                const uint8_t* px = img + (y0 + y) * stride + (x0 + x) * 4;
                sb += px[0], sg += px[1], sr += px[2];
            }
        }
    if (on < 4) return out;
    out.blank = false;
    out.color = inverse ? classifyColor(boxR, boxG, boxB) : classifyColor(sr / on, sg / on, sb / on);
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
    if (dump_) {
        DumpedCell d;
        d.row = r;
        d.sample.geo = {geo[0], geo[1], geo[2]};
        for (int k = 0; k < kPix; ++k) d.sample.pix[static_cast<size_t>(k)] = static_cast<uint8_t>(std::lround(std::clamp(cv[k], 0.f, 1.f) * 255.f));
        d.sample.small = bh < kSmallBelow;
        dump_->push_back(d);
    }
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
    // 1. a quick pass on 4 x 4 block summaries, skipping samples of a clearly different size; 2. the closest few pixel by pixel;
    // 3. the closest of those again with the picture moved by a pixel each way. About 15 times less work than comparing every sample.
    auto keep = [](auto* list, int& count, int cap, float d, size_t i) {
        if (count < cap) { list[count++] = {d, i}; return; }
        auto worst = std::max_element(list, list + count);
        if (d < worst->first) *worst = {d, i};
    };
    float cp[kPool];
    pool(cv, cp);
    std::pair<float, size_t> coarse[kCoarse];
    int nc = 0;
    std::vector<std::pair<float, size_t>> all;
    if (exhaustive_) all.reserve(n);
    for (int pass = 0; pass < 2 && nc == 0; ++pass)  // nothing of about this size: every sample then
        for (size_t i = 0; i < n; ++i) {
            const float* g = &b.geo[i * 3];
            if (pass == 0 && (std::fabs(g[0] - geo[0]) > kSizeSkip || std::fabs(g[1] - geo[1]) > kSizeSkip)) continue;
            const float* sp = &b.pooled[i * kPool];
            float diff = 0.f, uni = 0.f;
            for (int k = 0; k < kPool; ++k) diff += std::fabs(sp[k] - cp[k]), uni += std::max(sp[k], cp[k]);
            keep(coarse, nc, kCoarse, diff / std::max(uni, 1e-3f) + kGeoWeight * (std::fabs(g[0] - geo[0]) + std::fabs(g[1] - geo[1]) + std::fabs(g[2] - geo[2])), i);
        }
    if (exhaustive_) for (size_t i = 0; i < n; ++i) all.push_back({0.f, i});
    std::pair<float, size_t> best[kShortlist];
    int nb = 0;
    if (exhaustive_) for (const auto& c : all) keep(best, nb, kShortlist, distance(c.second, cv), c.second);
    else for (int k = 0; k < nc; ++k) keep(best, nb, kShortlist, distance(coarse[k].second, cv), coarse[k].second);
    static thread_local float shifted[8][kPix];  // the picture moved by one pixel each way, made once per character
    int ns = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dy && !dx) continue;
            float* s = shifted[ns++];
            for (int y = 0; y < kGlyphH; ++y)
                for (int x = 0; x < kGlyphW; ++x) {
                    const int sy = y - dy, sx = x - dx;
                    s[y * kGlyphW + x] = sy >= 0 && sy < kGlyphH && sx >= 0 && sx < kGlyphW ? cv[sy * kGlyphW + sx] : 0.f;
                }
        }
    float bestD = 1e9f;
    size_t bestI = 0;
    for (int k = 0; k < nb; ++k) {
        float d = best[k].first;
        for (int s = 0; s < ns; ++s) d = std::min(d, distance(best[k].second, shifted[s]));
        if (d < bestD) bestD = d, bestI = best[k].second;
    }
    out.ch = b.ch[bestI];
    // A letter's or digit's size is its height, not the size of the sample it matched: the two fonts share shapes, and a small D
    // matched to a large one read "aDvISory" (2026-11-26). Other characters (< > / - ° boxes) have heights of their own.
    const bool alnum = (out.ch >= U'A' && out.ch <= U'Z') || (out.ch >= U'0' && out.ch <= U'9');
    // A letter turned over in a box (the CJ4's EXEC) measures shorter than it is drawn: the box holds the large font.
    out.small = inverse ? false : alnum ? bh < kSmallBelow : b.small[bestI] != 0;
    if (bestD > kUnsure) out.unsure = true, out.ch = U'?';
    return out;
}

ReadResult ScreenReader::read(const uint8_t* bgrx, int width, int height, int stride) {
    ReadResult res;
    const ScreenLayout& L = *layout_;
    const int kRefW = L.refW, kRefH = L.refH, kCols = L.cols, kRows = L.rows;
    const float kX0 = L.x0, kPitchX = L.pitchX;
    if (kRefW > kMaxRefW || kCols > kMaxCols || kRows > kMaxRows) return res;  // a layout larger than the buffers below
    if (!bgrx || width < 64 || height < 64 || width > 8192 || height > 8192 || stride < width * 4) return res;
    const uint8_t* img = bgrx;
    int st = stride;
    if (width != kRefW || height != kRefH) {
        // A resized pop-out: the simulator fits the screen into the window, keeping its shape, with a border of a few
        // pixels that does not grow with the window. The screen is found by its dark blue ground and brought back to the
        // size the grid was measured at.
        float px0, py0, pw, ph;
        if (!findPanel(L, bgrx, width, height, stride, px0, py0, pw, ph)) {
            const float fit = std::min((width - 8.f) / L.panelW, (height - 8.f) / L.panelH);
            pw = L.panelW * fit, ph = L.panelH * fit, px0 = (width - pw) / 2.f, py0 = (height - ph) / 2.f;
        }
        const float scale = pw / L.panelW, scaleY = ph / L.panelH;
        res.scale = scale;
        const float offX = px0 - L.panelX * scale, offY = py0 - L.panelY * scaleY;
        scaled_.resize(static_cast<size_t>(kRefW) * kRefH * 4);
        // Each pixel of the measured size takes the average of the area it covers: a bigger pop-out shrinks without losing thin
        // strokes (a point sample of it misses them and the small letters change: H reads as N). A smaller one is spread out.
        auto taps = [](float off, float step, int count, int limit) {
            std::vector<std::vector<std::pair<int, float>>> out(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                const float a = off + i * step, b = a + step;
                if (step <= 1.f) {  // spreading: the two nearest pixels
                    const float f = std::clamp(a + step / 2.f - 0.5f, 0.f, limit - 1.f);
                    const int p0 = static_cast<int>(f), p1 = std::min(p0 + 1, limit - 1);
                    out[static_cast<size_t>(i)] = {{p0, 1.f - (f - p0)}, {p1, f - p0}};
                    continue;
                }
                for (int p = std::max(0, static_cast<int>(std::floor(a))); p < std::min(limit, static_cast<int>(std::ceil(b))); ++p) {
                    const float w = std::min(b, p + 1.f) - std::max(a, static_cast<float>(p));
                    if (w > 0.f) out[static_cast<size_t>(i)].push_back({p, w / step});
                }
            }
            return out;
        };
        const float key[6] = {offX, offY, scale, scaleY, static_cast<float>(width), static_cast<float>(height)};
        if (!std::equal(key, key + 6, taps_.key) || taps_.x.empty()) {  // made again only when the pop-out changes size or the screen moves
            std::copy(key, key + 6, taps_.key);
            taps_.x = taps(offX, scale, kRefW, width);
            taps_.y = taps(offY, scaleY, kRefH, height);
        }
        const auto& tx = taps_.x;
        const auto& ty = taps_.y;
        for (int y = 0; y < kRefH; ++y)
            for (int x = 0; x < kRefW; ++x) {
                float acc[3] = {0.f, 0.f, 0.f};
                for (const auto& [sy, wy] : ty[static_cast<size_t>(y)])
                    for (const auto& [sx, wx] : tx[static_cast<size_t>(x)]) {
                        const uint8_t* p = bgrx + sy * stride + sx * 4;
                        for (int k = 0; k < 3; ++k) acc[k] += p[k] * wy * wx;
                    }
                uint8_t* q = &scaled_[(static_cast<size_t>(y) * kRefW + x) * 4];
                for (int k = 0; k < 3; ++k) q[k] = static_cast<uint8_t>(std::clamp(std::lround(acc[k]), 0l, 255l));
                q[3] = 0;
            }
        img = scaled_.data();
        st = kRefW * 4;
    }
    // Characters are found row by row from their outlines, not cut at fixed places: some text sits half a cell off the grid,
    // and a run of amber boxes is drawn as one frame whose edge reaches into the next cell.
    CellResult cells[kMaxRows][kMaxCols];
    // the lines of the picture that have text on them, as bands (a gap of one line is still the same band)
    std::vector<std::pair<int, int>> bands;
    {
        int start = -1, gap = 0;
        for (int y = 0; y < kRefH; ++y) {
            bool any = false;
            for (int x = 0; x < kRefW && !any; x += 1) {
                const uint8_t* p = img + y * st + x * 4;
                any = std::max({p[0], p[1], p[2]}) >= 50;
            }
            if (any) {
                if (start < 0) start = y;
                gap = 0;
            } else if (start >= 0 && ++gap > 1) {
                bands.push_back({start, y - gap});
                start = -1, gap = 0;
            }
        }
        if (start >= 0) bands.push_back({start, kRefH - 1});
    }
    for (int r = 0; r < kRows; ++r) {
        // The row's text as drawn: the band of lit lines nearest to where the grid puts the row. Aircraft place and size their
        // text a little differently (the A319 a few pixels lower and larger than the A330); centring on the band keeps the next
        // row out of the character.
        const int gridY = static_cast<int>(std::lround(L.rowY[static_cast<size_t>(r)]));
        int cy = gridY, top = std::max(gridY - 9, 0), bottom = std::min(gridY + 9, kRefH - 1);
        {
            // every band centred near the row belongs to it: dashes and the dot of "--.-" are two bands a few lines apart
            int u0 = -1, u1 = -1;
            for (const auto& [b0, b1] : bands) {
                if (std::abs((b0 + b1) / 2 - gridY) > 10 || b1 - b0 > 26) continue;
                u0 = u0 < 0 ? b0 : std::min(u0, b0);
                u1 = std::max(u1, b1);
            }
            if (u0 >= 0 && u1 - u0 <= 26) cy = (u0 + u1) / 2, top = u0, bottom = u1;
        }
        uint8_t lit[kMaxRefW];
        for (int x = 0; x < kRefW; ++x) {
            lit[x] = false;
            for (int y = top; y <= bottom && !lit[x]; ++y) {
                const uint8_t* p = img + y * st + x * 4;
                lit[x] = std::max({p[0], p[1], p[2]}) >= 50;
            }
        }
        std::vector<std::pair<int, int>> spans;  // first and last column of each lit run
        for (int x = 0; x < kRefW;) {
            if (!lit[x]) { ++x; continue; }
            const int start = x;
            while (x < kRefW && lit[x]) ++x;
            // a character in two parts (") is one character: join runs closer than 3 pixels when the result still fits a cell
            if (!spans.empty() && start - spans.back().second <= 3 && x - 1 - spans.back().first < kPitchX * 0.85f) spans.back().second = x - 1;
            else spans.push_back({start, x - 1});
        }
        int taken[kMaxCols];
        std::fill(taken, taken + kCols, -1);
        for (const auto& [first, last] : spans) {
            const float width = static_cast<float>(last - first + 1);
            // wider than one character: as many characters as fit, counting the gap a run of touching ones lacks at its end
            // (two touching magenta letters "FL" are about 1.4 cells wide; the widest single letter is about 0.8)
            const int parts = width > kPitchX * 1.15f ? std::max(1, static_cast<int>(std::lround((width + 4.f) / kPitchX))) : 1;
            for (int k = 0; k < parts; ++k) {
                const float cxf = first + width * (k + 0.5f) / parts - 0.5f;
                int c = static_cast<int>(std::lround((cxf - kX0) / kPitchX));
                if (c >= 0 && c < kCols && taken[c] >= 0) {
                    // text half a cell off the grid can round two neighbours to one column ("FL080"): the later one takes the next
                    // column on its side when that one is free
                    const int side = cxf > static_cast<float>(taken[c]) ? c + 1 : c - 1;
                    c = side >= 0 && side < kCols && taken[side] < 0 ? side : -1;
                }
                if (c < 0 || c >= kCols || taken[c] >= 0) continue;
                const int cx = static_cast<int>(std::lround(cxf));
                taken[c] = cx;
                // a character that looks exactly as before is not read again
                uint64_t h = 1469598103934665603ull ^ static_cast<uint64_t>(cx);
                h ^= static_cast<uint64_t>(cy) << 20 ^ static_cast<uint64_t>(top) << 32 ^ static_cast<uint64_t>(bottom) << 44;
                for (int y = std::max(cy - kHalfH, 0); y <= std::min(cy + kHalfH, kRefH - 1); ++y) {
                    const uint8_t* row = img + y * st;
                    for (int x = std::max(cx - kHalfW, 0); x <= std::min(cx + kHalfW, kRefW - 1); ++x)
                        for (int q = 0; q < 3; ++q) h = (h ^ row[x * 4 + q]) * 1099511628211ull;
                }
                Cache& cache = cache_[static_cast<size_t>(r * kMaxCols + c)];
                if (!cache.valid || cache.hash != h) {
                    const size_t dumped = dump_ ? dump_->size() : 0;
                    cache = {h, true, readCell(img, st, r, cx, cy, top, bottom)};
                    if (dump_ && dump_->size() > dumped) dump_->back().col = c;  // only a sample this cell added
                }
                cells[r][c] = cache.cell;
            }
        }
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
        // the unit has 24 columns; the Airbus scratchpad can use a 25th, then the line moves left by one
        const int first = kCols > 24 && !cells[r][kCols - 1].blank ? kCols - 24 : 0;
        // The CJ4 has 15 rows: its message line goes on the unit's last row while the scratchpad (between its brackets) is empty.
        int outRow = r;
        if (L.messageRow && r >= 13) {
            bool typed = false, message = false;
            for (int c = 1; c < kCols - 1; ++c) typed = typed || !cells[13][c].blank;
            for (int c = 0; c < kCols; ++c) message = message || !cells[14][c].blank;
            const int shown = message && !typed ? 14 : 13;
            if (r != shown) {
                for (int c = 0; c < kCols; ++c) res.glyphs += !cells[r][c].blank, res.unsure += !cells[r][c].blank && cells[r][c].unsure;
                continue;
            }
            outRow = 13;
        }
        if (outRow >= kRows || outRow >= 14) continue;
        for (int c = 0; c < kCols; ++c) {
            const CellResult& cell = cells[r][c];
            if (cell.blank) continue;
            ++res.glyphs;
            if (cell.unsure) ++res.unsure;
            const int col = c - first;
            if (col < 0 || col >= 24) continue;
            Cell& out = res.screen.at(outRow, col);
            out.ch = cell.ch;
            out.color = cell.color;
            out.small = cell.small;
        }
    }
    return res;
}

}  // namespace lr::mcdu
