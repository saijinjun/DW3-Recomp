#include "minimap.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "hud_art.h"

namespace dw3app {

namespace {

using dw3::texrep::Image;
using dw3::texrep::StageLibrary;

constexpr uint32_t kPanel = 0xC0201810u;   // translucent dark blue-grey, like the notifications
constexpr uint32_t kView = 0xF0FFFFFFu;
constexpr uint32_t kDot = 0xFF2850F0u;     // red-orange
constexpr uint32_t kDotRing = 0xFFFFFFFFu;

void put(OsdImage& img, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= img.w || y >= img.h) return;
    img.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) + static_cast<std::size_t>(x)] = c;
}

void outline(OsdImage& img, int x0, int y0, int x1, int y1, int t, uint32_t c) {
    for (int k = 0; k < t; ++k) {
        for (int x = x0; x <= x1; ++x) {
            put(img, x, y0 + k, c);
            put(img, x, y1 - k, c);
        }
        for (int y = y0; y <= y1; ++y) {
            put(img, x0 + k, y, c);
            put(img, x1 - k, y, c);
        }
    }
}

void disc(OsdImage& img, float cx, float cy, float r, uint32_t c) {
    for (int y = static_cast<int>(std::floor(cy - r)); y <= static_cast<int>(std::ceil(cy + r)); ++y)
        for (int x = static_cast<int>(std::floor(cx - r)); x <= static_cast<int>(std::ceil(cx + r)); ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) put(img, x, y, c);
        }
}

}  // namespace

Image Minimap::compose(StageLibrary& library, int map) {
    Image out;
    if (map < 0 || static_cast<std::size_t>(map) >= library.map_count()) return out;
    const StageLibrary::Map& m = library.map(static_cast<uint32_t>(map));
    out.w = m.cols * dw3::texrep::kStageCell;
    out.h = m.rows * dw3::texrep::kStageCell;
    if (out.w <= 0 || out.h <= 0) return out;
    out.rgba.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h), 0u);
    Image picture;
    for (uint32_t t = 0; t < m.tiles.size(); ++t) {
        const StageLibrary::Tile& tile = m.tiles[t];
        if (!library.tile_image(static_cast<uint32_t>(map), t, picture)) continue;
        // The far scenery first, then the ground, then what stands in front of the characters.
        for (const int layer : {2, 0, 1})
            for (const dw3::texrep::StagePiece& p : tile.pieces) {
                if (p.layer != layer) continue;
                const int ox = tile.col * dw3::texrep::kStageCell + p.x, oy = tile.row * dw3::texrep::kStageCell + p.y;
                for (int y = 0; y < p.h; ++y) {
                    const int sy = p.ay + y, dy = oy + y;
                    if (sy < 0 || sy >= picture.h || dy < 0 || dy >= out.h) continue;
                    for (int x = 0; x < p.w; ++x) {
                        const int sx = p.ax + x, dx = ox + x;
                        if (sx < 0 || sx >= picture.w || dx < 0 || dx >= out.w) continue;
                        const uint32_t c = picture.rgba[static_cast<std::size_t>(sy) * static_cast<std::size_t>(picture.w) + static_cast<std::size_t>(sx)];
                        if (c >> 24) out.rgba[static_cast<std::size_t>(dy) * static_cast<std::size_t>(out.w) + static_cast<std::size_t>(dx)] = c | 0xFF000000u;
                    }
                }
            }
    }
    return out;
}

Minimap::Base Minimap::build(StageLibrary& library, int map, int size) {
    Base out;
    const Image full = compose(library, map);
    // Only the part of the map that has pictures.
    int x0 = full.w, y0 = full.h, x1 = -1, y1 = -1;
    for (int y = 0; y < full.h; ++y)
        for (int x = 0; x < full.w; ++x)
            if (full.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(full.w) + static_cast<std::size_t>(x)] >> 24) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    if (x1 < x0 || y1 < y0) return out;
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    // The frame art drawn so its left side is 7% of `size` thick; the map fills its opening.
    const float f = static_cast<float>(size) * 0.07f / static_cast<float>(hud_art::kMinimapFrameLeft);
    const auto frame_px = [f](int art_px) { return std::max(1, static_cast<int>(std::lround(static_cast<float>(art_px) * f))); };
    out.left = frame_px(hud_art::kMinimapFrameLeft);
    out.top = frame_px(hud_art::kMinimapFrameTop);
    out.right = frame_px(hud_art::kMinimapFrameRight);
    out.bottom = frame_px(hud_art::kMinimapFrameBottom);
    const int inner_w = std::max(8, size - out.left - out.right), inner_h = std::max(8, size - out.top - out.bottom);
    const float scale = std::min({1.0f, static_cast<float>(inner_w) / static_cast<float>(cw), static_cast<float>(inner_h) / static_cast<float>(ch)});
    const int mw = std::max(1, static_cast<int>(std::lround(static_cast<float>(cw) * scale)));
    const int mh = std::max(1, static_cast<int>(std::lround(static_cast<float>(ch) * scale)));
    out.crop_x = x0;
    out.crop_y = y0;
    out.scale = scale;
    OsdImage& base = out.image;
    base.w = mw + out.left + out.right;
    base.h = mh + out.top + out.bottom;
    base.rgba.assign(static_cast<std::size_t>(base.w) * static_cast<std::size_t>(base.h), 0u);
    // The panel colour behind the map reaches under the frame's inner edge, not out to its cut corners.
    for (int y = out.top / 2; y < base.h - out.bottom / 2; ++y)
        for (int x = out.left / 2; x < base.w - out.right / 2; ++x) put(base, x, y, kPanel);
    // Area average of the map pixels that fall in each panel pixel (transparent ones count as the panel colour).
    for (int py = 0; py < mh; ++py) {
        const int sy0 = y0 + static_cast<int>(static_cast<float>(py) / scale);
        const int sy1 = std::max(sy0 + 1, std::min(y1 + 1, y0 + static_cast<int>(static_cast<float>(py + 1) / scale)));
        for (int px = 0; px < mw; ++px) {
            const int sx0 = x0 + static_cast<int>(static_cast<float>(px) / scale);
            const int sx1 = std::max(sx0 + 1, std::min(x1 + 1, x0 + static_cast<int>(static_cast<float>(px + 1) / scale)));
            uint32_t r = 0, g = 0, b = 0, n = 0, opaque = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx) {
                    const uint32_t c = full.rgba[static_cast<std::size_t>(sy) * static_cast<std::size_t>(full.w) + static_cast<std::size_t>(sx)];
                    ++n;
                    if (!(c >> 24)) continue;
                    ++opaque;
                    r += c & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += (c >> 16) & 0xFF;
                }
            if (opaque * 2 < n) continue;
            put(base, out.left + px, out.top + py, 0xFF000000u | (r / opaque) | ((g / opaque) << 8) | ((b / opaque) << 16));
        }
    }
    hud_art::blit_nine(base.rgba.data(), base.w, base.h, hud_art::minimap_frame(), hud_art::kMinimapFrameCorner, 0, 0, base.w, base.h,
                       frame_px(hud_art::kMinimapFrameCorner));
    out.ok = true;
    return out;
}

const OsdImage* Minimap::frame(StageLibrary& library, int map, int scroll_x, int scroll_y, int view_w, int view_h, int party_x,
                               int party_y, int size) {
    if (map != map_ || size != size_) {
        if (pending_.valid()) pending_.wait();  // a drawing still running for the previous map
        map_ = map;
        size_ = size;
        base_ = {};
        pending_ = std::async(std::launch::async, [&library, map, size] { return build(library, map, size); });
    }
    if (pending_.valid()) {
        if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
        base_ = pending_.get();
        drawn_vw_ = -1;
    }
    if (!base_.ok) return nullptr;
    if (scroll_x == drawn_sx_ && scroll_y == drawn_sy_ && view_w == drawn_vw_ && view_h == drawn_vh_ && party_x == drawn_px_ &&
        party_y == drawn_py_)
        return &image_;
    drawn_sx_ = scroll_x;
    drawn_sy_ = scroll_y;
    drawn_vw_ = view_w;
    drawn_vh_ = view_h;
    drawn_px_ = party_x;
    drawn_py_ = party_y;
    image_ = base_.image;
    const auto to_x = [&](int mx) { return map_x(mx); };
    const auto to_y = [&](int my) { return map_y(my); };
    const int x_max = image_.w - base_.right - 1, y_max = image_.h - base_.bottom - 1;
    const int vx0 = std::clamp(to_x(scroll_x), base_.left, x_max);
    const int vy0 = std::clamp(to_y(scroll_y), base_.top, y_max);
    const int vx1 = std::clamp(to_x(scroll_x + view_w) - 1, vx0, x_max);
    const int vy1 = std::clamp(to_y(scroll_y + view_h) - 1, vy0, y_max);
    outline(image_, vx0, vy0, vx1, vy1, 1, kView);
    const float r = std::max(2.5f, static_cast<float>(size_) / 45.0f);
    const float cx = static_cast<float>(std::clamp(to_x(party_x), base_.left, x_max));
    const float cy = static_cast<float>(std::clamp(to_y(party_y), base_.top, y_max));
    disc(image_, cx, cy, r + 1.0f, kDotRing);
    disc(image_, cx, cy, r, kDot);
    ++serial_;
    return &image_;
}

int Minimap::map_x(int mx) const {
    return base_.left + static_cast<int>(std::lround(static_cast<float>(mx - base_.crop_x) * base_.scale));
}
int Minimap::map_y(int my) const {
    return base_.top + static_cast<int>(std::lround(static_cast<float>(my - base_.crop_y) * base_.scale));
}

}  // namespace dw3app
