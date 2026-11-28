// Drawing helpers shared by the HUD panels (party_hud.h, battle_hud.h): a canvas over an OsdImage with fills, bars,
// the HUD art and text in the OSD font, and the HUD's colours. No SDL dependency.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "hud_art.h"
#include "osd.h"

namespace dw3app::hud {

constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) { return r | (g << 8) | (b << 16) | (a << 24); }

constexpr uint32_t kTrack = rgba(0, 0, 0, 170);
constexpr uint32_t kName = rgba(245, 245, 245);
constexpr uint32_t kDim = rgba(170, 190, 210);
constexpr uint32_t kMp = rgba(70, 150, 255);

inline uint32_t hp_colour(int hp, int max) {
    const float f = max > 0 ? static_cast<float>(hp) / static_cast<float>(max) : 0.0f;
    if (f > 0.5f) return rgba(80, 220, 110);
    if (f > 0.25f) return rgba(240, 200, 60);
    return rgba(240, 70, 60);
}

class Canvas {
public:
    explicit Canvas(OsdImage& img) : img_(img) {}

    void fill(int x0, int y0, int x1, int y1, uint32_t c) {
        for (int y = std::max(0, y0); y < std::min(img_.h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(img_.w, x1); ++x) at(x, y) = c;
    }
    // A rectangle with rounded corners of radius r (anti-aliased edge), filled with `c`.
    void rounded(int x0, int y0, int x1, int y1, float r, uint32_t c) {
        for (int y = std::max(0, y0); y < std::min(img_.h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(img_.w, x1); ++x) {
                const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                const float qx = std::max({static_cast<float>(x0) + r - px, px - (static_cast<float>(x1) - r), 0.0f});
                const float qy = std::max({static_cast<float>(y0) + r - py, py - (static_cast<float>(y1) - r), 0.0f});
                const float cover = std::clamp(r - std::sqrt(qx * qx + qy * qy) + 0.5f, 0.0f, 1.0f);
                if (cover <= 0.0f) continue;
                at(x, y) = (c & 0x00FFFFFFu) | (static_cast<uint32_t>(std::lround(static_cast<float>(c >> 24) * cover)) << 24);
            }
    }
    // A bar `w` wide, filled to hp/max, on a dark track, with a lighter top line on the fill.
    void bar(int x, int y, int w, int h, int value, int max, uint32_t c) {
        fill(x, y, x + w, y + h, kTrack);
        const int filled = max > 0 ? static_cast<int>(std::lround(static_cast<double>(w) * std::clamp(value, 0, max) / max)) : 0;
        if (value > 0 && filled == 0) fill(x, y, x + 1, y + h, c);
        fill(x, y, x + filled, y + h, c);
        const uint32_t light = (c & 0xFF000000u) | ((((c & 0xFEFEFEu) >> 1) + 0x7F7F7Fu) & 0xFFFFFFu);
        if (h >= 3) fill(x, y, x + filled, y + std::max(1, h / 4), light);
    }
    // `pic` scaled into the w x h box at (x0, y0) (bilinear), blended over what is there.
    void picture(const hud_art::Picture& pic, int x0, int y0, int w, int h) {
        hud_art::blit(img_.rgba.data(), img_.w, img_.h, pic, x0, y0, w, h);
    }
    // Text in the OSD font with its top-left at (x, y). Returns its width.
    int text(const std::string& s, int x, int y, int scale, uint32_t c) {
        const OsdImage t = osd_render_text(s, scale, 1 << 20);
        if (t.w == 0) return 0;
        const int pad = osd_padding(scale);
        for (int ty = pad; ty < t.h - pad; ++ty)
            for (int tx = pad; tx < t.w - pad; ++tx) {
                if (t.rgba[static_cast<std::size_t>(ty) * static_cast<std::size_t>(t.w) + static_cast<std::size_t>(tx)] != kOsdTextColor) continue;
                const int px = x + tx - pad, py = y + ty - pad;
                if (px >= 0 && py >= 0 && px < img_.w && py < img_.h) at(px, py) = c;
            }
        return t.w - 2 * pad;
    }

private:
    uint32_t& at(int x, int y) { return img_.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(img_.w) + static_cast<std::size_t>(x)]; }
    OsdImage& img_;
};

}  // namespace dw3app::hud
