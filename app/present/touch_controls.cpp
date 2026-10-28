#include "touch_controls.h"

#include <algorithm>
#include <cmath>

#include "dw3/sio.h"

namespace dw3app {

namespace {

constexpr float kPi = 3.14159265f;

// Colours: RGBA8 with red in the lowest byte, straight alpha.
constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return r | (g << 8) | (b << 16) | (a << 24); }
constexpr uint32_t kFill = rgba(20, 24, 34, 96);
constexpr uint32_t kFillPressed = rgba(90, 140, 230, 170);
constexpr uint32_t kEdge = rgba(235, 240, 250, 150);
constexpr uint32_t kMark = rgba(240, 244, 252, 200);
constexpr uint32_t kCross = rgba(124, 178, 232, 230), kCircle = rgba(230, 100, 100, 230), kSquare = rgba(220, 140, 200, 230),
                   kTriangle = rgba(80, 200, 160, 230);

// Blends `color` over the pixel with coverage 0..1 ("over" with straight alpha).
void blend(uint32_t& dst, uint32_t color, float coverage) {
    const float sa = static_cast<float>(color >> 24) / 255.0f * std::clamp(coverage, 0.0f, 1.0f);
    if (sa <= 0.0f) return;
    const float da = static_cast<float>(dst >> 24) / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    auto channel = [&](int shift) {
        const float s = static_cast<float>((color >> shift) & 0xFF), d = static_cast<float>((dst >> shift) & 0xFF);
        return static_cast<uint32_t>(std::lround((s * sa + d * da * (1.0f - sa)) / oa)) & 0xFF;
    };
    dst = channel(0) | (channel(8) << 8) | (channel(16) << 16) | (static_cast<uint32_t>(std::lround(oa * 255.0f)) << 24);
}

class Canvas {
public:
    explicit Canvas(OsdImage& img) : img_(img) {}

    // Calls f(px, py, x, y) for every pixel centre in the box, x and y relative to (cx, cy).
    template <typename F>
    void each(float cx, float cy, float rx, float ry, F f) {
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - rx - 1))), x1 = std::min(img_.w - 1, static_cast<int>(std::ceil(cx + rx + 1)));
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - ry - 1))), y1 = std::min(img_.h - 1, static_cast<int>(std::ceil(cy + ry + 1)));
        for (int py = y0; py <= y1; ++py)
            for (int px = x0; px <= x1; ++px) f(img_.rgba[static_cast<std::size_t>(py) * static_cast<std::size_t>(img_.w) + static_cast<std::size_t>(px)],
                                                 static_cast<float>(px) + 0.5f - cx, static_cast<float>(py) + 0.5f - cy);
    }

    void disc(float cx, float cy, float r, uint32_t color) {
        each(cx, cy, r, r, [&](uint32_t& p, float x, float y) { blend(p, color, r - std::sqrt(x * x + y * y) + 0.5f); });
    }
    void ring(float cx, float cy, float r, float width, uint32_t color) {
        each(cx, cy, r + width, r + width, [&](uint32_t& p, float x, float y) {
            blend(p, color, width * 0.5f - std::abs(std::sqrt(x * x + y * y) - r) + 0.5f);
        });
    }
    // Rounded rectangle (half sizes hx, hy, corner radius rr), filled or as an outline of `width`.
    void rounded(float cx, float cy, float hx, float hy, float rr, uint32_t color, float width = 0) {
        each(cx, cy, hx, hy, [&](uint32_t& p, float x, float y) {
            const float qx = std::abs(x) - (hx - rr), qy = std::abs(y) - (hy - rr);
            const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
            const float d = outside + std::min(std::max(qx, qy), 0.0f) - rr;  // signed distance, negative inside
            blend(p, color, width > 0 ? width * 0.5f - std::abs(d + width * 0.5f) + 0.5f : -d + 0.5f);
        });
    }
    void line(float ax, float ay, float bx, float by, float width, uint32_t color) {
        const float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f;
        const float hx = std::abs(bx - ax) * 0.5f + width, hy = std::abs(by - ay) * 0.5f + width;
        const float dx = bx - ax, dy = by - ay, len2 = std::max(dx * dx + dy * dy, 1e-6f);
        each(cx, cy, hx, hy, [&](uint32_t& p, float x, float y) {
            const float px = x + cx - ax, py = y + cy - ay;
            const float t = std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f);
            const float ex = px - t * dx, ey = py - t * dy;
            blend(p, color, width * 0.5f - std::sqrt(ex * ex + ey * ey) + 0.5f);
        });
    }
    // Text in the OSD font, centred, transparent background.
    void text(const std::string& s, float cx, float cy, int scale, uint32_t color) {
        const OsdImage t = osd_render_text(s, scale, 1 << 20);
        if (t.w == 0) return;
        const int x0 = static_cast<int>(std::lround(cx - static_cast<float>(t.w) * 0.5f));
        const int y0 = static_cast<int>(std::lround(cy - static_cast<float>(t.h) * 0.5f));
        for (int y = 0; y < t.h; ++y)
            for (int x = 0; x < t.w; ++x) {
                if (t.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(t.w) + static_cast<std::size_t>(x)] != kOsdTextColor) continue;
                const int px = x0 + x, py = y0 + y;
                if (px < 0 || py < 0 || px >= img_.w || py >= img_.h) continue;
                blend(img_.rgba[static_cast<std::size_t>(py) * static_cast<std::size_t>(img_.w) + static_cast<std::size_t>(px)], color, 1.0f);
            }
    }

private:
    OsdImage& img_;
};

}  // namespace

TouchControls::TouchControls() = default;

void TouchControls::set_visible(bool visible) {
    if (visible == visible_) return;
    visible_ = visible;
    if (!visible) release_all();
}

void TouchControls::set_fast_forward(bool on) { fast_forward_ = on; }
void TouchControls::set_auto_battle(bool on) { auto_battle_ = on; }
void TouchControls::set_encounters_off(bool off) { encounters_off_ = off; }

void TouchControls::set_style(int size_percent, int opacity_percent) {
    const float scale = static_cast<float>(size_percent) / 100.0f, opacity = static_cast<float>(opacity_percent) / 100.0f;
    if (scale == scale_ && opacity == opacity_) return;
    if (scale != scale_) controls_.clear();  // laid out again on the next use
    scale_ = scale;
    opacity_ = opacity;
    drawn_state_ = ~0ull;
}

const std::vector<TouchControls::Control>& TouchControls::controls(int window_w, int window_h) {
    layout(window_w, window_h);
    return controls_;
}

void TouchControls::layout(int w, int h) {
    if (w == w_ && h == h_ && !controls_.empty()) return;
    w_ = w;
    h_ = h;
    controls_.clear();
    // Everything is sized from the short side so it stays thumb-sized on any screen; the long side spreads it out.
    // The size setting scales the controls but not the margin.
    const float u = static_cast<float>(std::min(w, h)) * scale_;
    const float fw = static_cast<float>(w), fh = static_cast<float>(h);
    const float margin = static_cast<float>(std::min(w, h)) * 0.05f;
    using S = Control::Shape;
    // Left: the d-pad.
    const float dpad_r = u * 0.19f;
    controls_.push_back({S::DPad, 0, -1, "", margin + dpad_r + u * 0.04f, fh - margin - dpad_r - u * 0.04f, dpad_r, dpad_r, 0});
    // Right: the face buttons in a diamond.
    const float br = u * 0.075f, spread = u * 0.125f;
    const float fx = fw - margin - spread - br - u * 0.03f, fy = fh - margin - spread - br - u * 0.04f;
    controls_.push_back({S::Round, dw3::kPadTriangle, -1, "triangle", fx, fy - spread, br, br, 1});
    controls_.push_back({S::Round, dw3::kPadCross, -1, "cross", fx, fy + spread, br, br, 1});
    controls_.push_back({S::Round, dw3::kPadSquare, -1, "square", fx - spread, fy, br, br, 1});
    controls_.push_back({S::Round, dw3::kPadCircle, -1, "circle", fx + spread, fy, br, br, 1});
    // Shoulders in the top corners: L2/R2 above L1/R1.
    const float sw = u * 0.12f, sh = u * 0.05f;
    controls_.push_back({S::Rect, dw3::kPadL2, -1, "L2", margin + sw, margin + sh, sw, sh, 0});
    controls_.push_back({S::Rect, dw3::kPadL1, -1, "L1", margin + sw, margin + sh * 3 + u * 0.02f, sw, sh, 0});
    controls_.push_back({S::Rect, dw3::kPadR2, -1, "R2", fw - margin - sw, margin + sh, sw, sh, 0});
    controls_.push_back({S::Rect, dw3::kPadR1, -1, "R1", fw - margin - sw, margin + sh * 3 + u * 0.02f, sw, sh, 0});
    // SELECT and START at the bottom centre.
    const float mw = u * 0.09f, mh = u * 0.04f, my = fh - margin - mh;
    controls_.push_back({S::Rect, dw3::kPadSelect, -1, "SELECT", fw * 0.5f - mw - u * 0.03f, my, mw, mh, 0});
    controls_.push_back({S::Rect, dw3::kPadStart, -1, "START", fw * 0.5f + mw + u * 0.03f, my, mw, mh, 0});
    // App buttons at the top centre, at the standard size whatever the size setting: five in a row leave no room to grow
    // between the shoulder buttons on a 16:9 screen.
    const float u0 = static_cast<float>(std::min(w, h));
    const float aw = u0 * 0.075f, ah = u0 * 0.04f, ay = margin + ah;
    const float gap = aw * 2 + u0 * 0.03f;
    controls_.push_back({S::Rect, 0, static_cast<int>(TouchAction::SaveState), "SAVE", fw * 0.5f - gap * 2.0f, ay, aw, ah, 0});
    controls_.push_back({S::Rect, 0, static_cast<int>(TouchAction::FastForward), ">>", fw * 0.5f - gap, ay, aw, ah, 0});
    controls_.push_back({S::Rect, 0, static_cast<int>(TouchAction::AutoBattle), "AUTO", fw * 0.5f, ay, aw, ah, 0});
    controls_.push_back({S::Rect, 0, static_cast<int>(TouchAction::Encounters), "ENC", fw * 0.5f + gap, ay, aw, ah, 0});
    controls_.push_back({S::Rect, 0, static_cast<int>(TouchAction::LoadState), "LOAD", fw * 0.5f + gap * 2.0f, ay, aw, ah, 0});
    fingers_.clear();
}

int TouchControls::hit(float x, float y, bool generous) const {
    // A little extra reach around each control: thumbs land near, not on.
    const float extra = generous ? 1.35f : 1.0f;
    int best = -1;
    float best_d = 1e30f;
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        const Control& c = controls_[i];
        const float dx = (x - c.x) / (c.rx * extra), dy = (y - c.y) / (c.ry * extra);
        const float d = c.shape == Control::Shape::Rect ? std::max(std::abs(dx), std::abs(dy)) : std::sqrt(dx * dx + dy * dy);
        if (d <= 1.0f && d < best_d) {
            best = static_cast<int>(i);
            best_d = d;
        }
    }
    return best;
}

uint16_t TouchControls::dpad_bits(const Control& c, float x, float y) const {
    const float dx = x - c.x, dy = y - c.y;
    if (std::sqrt(dx * dx + dy * dy) < c.rx * 0.22f) return 0;  // dead centre
    // Eight 45-degree sectors; the diagonals press two directions.
    float angle = std::atan2(dy, dx) * 180.0f / kPi;  // 0 = right, 90 = down
    if (angle < 0) angle += 360.0f;
    const int sector = static_cast<int>(std::floor((angle + 22.5f) / 45.0f)) % 8;
    static constexpr uint16_t kBits[8] = {
        dw3::kPadRight, dw3::kPadRight | dw3::kPadDown, dw3::kPadDown, dw3::kPadDown | dw3::kPadLeft,
        dw3::kPadLeft,  dw3::kPadLeft | dw3::kPadUp,    dw3::kPadUp,   dw3::kPadUp | dw3::kPadRight,
    };
    return kBits[sector];
}

void TouchControls::finger_down(uint64_t finger, float x, float y, int window_w, int window_h) {
    layout(window_w, window_h);
    if (!visible_) set_visible(true);
    const int c = hit(x, y, true);
    fingers_[finger] = {c, x, y};
    if (c >= 0) ++presses_;
    if (c >= 0 && controls_[static_cast<std::size_t>(c)].action >= 0)
        actions_.push_back(static_cast<TouchAction>(controls_[static_cast<std::size_t>(c)].action));
}

void TouchControls::finger_move(uint64_t finger, float x, float y, int window_w, int window_h) {
    layout(window_w, window_h);
    auto it = fingers_.find(finger);
    if (it == fingers_.end()) return;
    it->second.x = x;
    it->second.y = y;
    // A finger on a face button can roll onto its neighbour (pressing two at once on the way, like a real pad).
    const int current = it->second.control;
    if (current >= 0 && controls_[static_cast<std::size_t>(current)].group != 0) {
        const int next = hit(x, y, false);
        if (next >= 0 && controls_[static_cast<std::size_t>(next)].group == controls_[static_cast<std::size_t>(current)].group)
            it->second.control = next;
    }
}

void TouchControls::finger_up(uint64_t finger) { fingers_.erase(finger); }

void TouchControls::release_all() { fingers_.clear(); }

uint16_t TouchControls::buttons() const {
    if (!visible_) return 0;
    uint16_t bits = 0;
    for (const auto& [id, f] : fingers_) {
        if (f.control < 0) continue;
        const Control& c = controls_[static_cast<std::size_t>(f.control)];
        if (c.shape == Control::Shape::DPad) bits |= dpad_bits(c, f.x, f.y);
        else bits |= c.pad_bit;
    }
    return bits;
}

std::vector<TouchAction> TouchControls::take_actions() {
    std::vector<TouchAction> out;
    out.swap(actions_);
    return out;
}

const OsdImage& TouchControls::image(int window_w, int window_h, int downscale) {
    layout(window_w, window_h);
    downscale = std::max(1, downscale);
    // What the picture depends on: the window, visibility, the lit app buttons and every held control.
    uint64_t state = (static_cast<uint64_t>(visible_) << 63) | (static_cast<uint64_t>(fast_forward_) << 62) |
                     (static_cast<uint64_t>(auto_battle_) << 61) | (static_cast<uint64_t>(encounters_off_) << 60) |
                     (static_cast<uint64_t>(buttons()) << 40);
    for (const auto& [id, f] : fingers_)
        if (f.control >= 0) state |= 1ull << std::min(f.control, 39);
    const int iw = window_w / downscale, ih = window_h / downscale;
    state ^= (static_cast<uint64_t>(static_cast<uint32_t>(iw)) << 20) ^ static_cast<uint64_t>(static_cast<uint32_t>(ih));
    if (state != drawn_state_ || image_.w != iw || image_.h != ih) {
        drawn_state_ = state;
        draw(downscale);
        ++serial_;
    }
    return image_;
}

void TouchControls::draw(int downscale) {
    image_.w = w_ / downscale;
    image_.h = h_ / downscale;
    image_.rgba.assign(static_cast<std::size_t>(std::max(image_.w, 0)) * static_cast<std::size_t>(std::max(image_.h, 0)), 0u);
    if (!visible_ || image_.w <= 0 || image_.h <= 0) return;
    std::vector<bool> held(controls_.size(), false);
    for (const auto& [id, f] : fingers_)
        if (f.control >= 0) held[static_cast<std::size_t>(f.control)] = true;
    const uint16_t bits = buttons();
    const float k = 1.0f / static_cast<float>(downscale);
    const float u = static_cast<float>(std::min(image_.w, image_.h));
    const float edge = std::max(1.5f, u * 0.004f);
    const int text_scale = std::max(2, static_cast<int>(u / 300.0f));
    Canvas cv(image_);
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        Control c = controls_[i];  // in image pixels
        c.x *= k;
        c.y *= k;
        c.rx *= k;
        c.ry *= k;
        const bool lit = held[i] || (c.action == static_cast<int>(TouchAction::FastForward) && fast_forward_) ||
                         (c.action == static_cast<int>(TouchAction::AutoBattle) && auto_battle_) ||
                         (c.action == static_cast<int>(TouchAction::Encounters) && encounters_off_);
        switch (c.shape) {
            case Control::Shape::DPad: {
                // A plus shape; the pressed arms light up.
                const float arm = c.rx * 0.36f;
                cv.disc(c.x, c.y, c.rx, rgba(20, 24, 34, 50));
                cv.rounded(c.x, c.y, c.rx * 0.95f, arm, arm * 0.3f, kFill);
                cv.rounded(c.x, c.y, arm, c.rx * 0.95f, arm * 0.3f, kFill);
                const float reach = c.rx * 0.6f;
                if (bits & dw3::kPadUp) cv.rounded(c.x, c.y - reach, arm, arm * 1.3f, arm * 0.3f, kFillPressed);
                if (bits & dw3::kPadDown) cv.rounded(c.x, c.y + reach, arm, arm * 1.3f, arm * 0.3f, kFillPressed);
                if (bits & dw3::kPadLeft) cv.rounded(c.x - reach, c.y, arm * 1.3f, arm, arm * 0.3f, kFillPressed);
                if (bits & dw3::kPadRight) cv.rounded(c.x + reach, c.y, arm * 1.3f, arm, arm * 0.3f, kFillPressed);
                cv.rounded(c.x, c.y, c.rx * 0.95f, arm, arm * 0.3f, kEdge, edge);
                cv.rounded(c.x, c.y, arm, c.rx * 0.95f, arm * 0.3f, kEdge, edge);
                // Arrow marks on the arms.
                const float t = arm * 0.45f, tip = c.rx * 0.8f;
                for (int d = 0; d < 4; ++d) {
                    const float ax = d == 2 ? -1.0f : d == 3 ? 1.0f : 0.0f, ay = d == 0 ? -1.0f : d == 1 ? 1.0f : 0.0f;
                    const float px = -ay, py = ax;  // perpendicular
                    const float bx = c.x + ax * (tip - t), by = c.y + ay * (tip - t);
                    cv.line(c.x + ax * tip, c.y + ay * tip, bx + px * t, by + py * t, edge * 1.5f, kMark);
                    cv.line(c.x + ax * tip, c.y + ay * tip, bx - px * t, by - py * t, edge * 1.5f, kMark);
                }
                break;
            }
            case Control::Shape::Round: {
                cv.disc(c.x, c.y, c.rx, lit ? kFillPressed : kFill);
                cv.ring(c.x, c.y, c.rx - edge * 0.5f, edge, kEdge);
                const float s = c.rx * 0.45f, w = std::max(2.0f, c.rx * 0.11f);
                if (c.pad_bit == dw3::kPadCross) {
                    cv.line(c.x - s, c.y - s, c.x + s, c.y + s, w, kCross);
                    cv.line(c.x - s, c.y + s, c.x + s, c.y - s, w, kCross);
                } else if (c.pad_bit == dw3::kPadCircle) {
                    cv.ring(c.x, c.y, s, w, kCircle);
                } else if (c.pad_bit == dw3::kPadSquare) {
                    cv.rounded(c.x, c.y, s * 0.85f, s * 0.85f, 1.0f, kSquare, w);
                } else if (c.pad_bit == dw3::kPadTriangle) {
                    const float top = c.y - s, base = c.y + s * 0.7f, half = s * 1.0f;
                    cv.line(c.x, top, c.x - half, base, w, kTriangle);
                    cv.line(c.x, top, c.x + half, base, w, kTriangle);
                    cv.line(c.x - half, base, c.x + half, base, w, kTriangle);
                }
                break;
            }
            case Control::Shape::Rect: {
                const float rr = std::min(c.rx, c.ry) * 0.6f;
                cv.rounded(c.x, c.y, c.rx, c.ry, rr, lit ? kFillPressed : kFill);
                cv.rounded(c.x, c.y, c.rx, c.ry, rr, kEdge, edge);
                cv.text(c.label, c.x, c.y, text_scale, kMark);
                break;
            }
        }
    }
    if (opacity_ < 1.0f) {
        const float o = std::max(opacity_, 0.0f);
        for (uint32_t& p : image_.rgba)
            p = (p & 0x00FFFFFFu) | (static_cast<uint32_t>(std::lround(static_cast<float>(p >> 24) * o)) << 24);
    }
}

}  // namespace dw3app
