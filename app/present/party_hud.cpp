#include "party_hud.h"

#include <algorithm>
#include <cmath>

#include "hud_art.h"

namespace dw3app {

namespace {

constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) { return r | (g << 8) | (b << 16) | (a << 24); }

constexpr uint32_t kTrack = rgba(0, 0, 0, 170);
constexpr uint32_t kName = rgba(245, 245, 245);
constexpr uint32_t kDim = rgba(170, 190, 210);
constexpr uint32_t kMp = rgba(70, 150, 255);
constexpr uint32_t kBits = rgba(255, 210, 80);

uint32_t hp_colour(int hp, int max) {
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

}  // namespace

std::string decode_game_name(const uint8_t* bytes, std::size_t size) {
    std::string out;
    for (std::size_t i = 0; i < size && bytes[i] != 0; ++i) {
        const uint8_t b = bytes[i];
        if (b >= 0x0E && b <= 0x27) out += static_cast<char>('A' + (b - 0x0E));
        else if (b >= 0x28 && b <= 0x41) out += static_cast<char>('a' + (b - 0x28));
        else if (b >= 0x04 && b <= 0x0D) out += static_cast<char>('0' + (b - 0x04));
        else out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

PartyState read_party(const std::function<uint32_t(uint32_t)>& read32) {
    PartyState s;
    s.bits = read32(kPartyPurse);
    auto half = [&](uint32_t addr) { return static_cast<int>((read32(addr & ~3u) >> ((addr & 2u) * 8)) & 0xFFFF); };
    for (uint32_t slot = 0; slot < 3; ++slot) {
        const uint32_t index = read32(kPartySlots + slot * 4);
        if (index >= 8) continue;
        const uint32_t entry = kPartyRoster + index * kPartyRosterStride;
        PartyMember m;
        m.level = half(entry + 4);
        m.hp = half(entry + 8);
        m.max_hp = half(entry + 10);
        m.mp = half(entry + 12);
        m.max_mp = half(entry + 14);
        if (m.max_hp <= 0 || m.max_hp > 9999 || m.hp > m.max_hp || m.max_mp > 9999 || m.mp > m.max_mp || m.level <= 0 ||
            m.level > 99)
            continue;
        uint8_t name[12];
        for (uint32_t i = 0; i < sizeof name; i += 4) {
            const uint32_t w = read32(entry - 0x18 + i);
            for (uint32_t k = 0; k < 4; ++k) name[i + k] = static_cast<uint8_t>(w >> (8 * k));
        }
        m.name = decode_game_name(name, sizeof name);
        if (m.name.empty()) m.name = "Digimon";
        s.members[static_cast<std::size_t>(s.count++)] = std::move(m);
    }
    return s;
}

const OsdImage* PartyHud::frame(const PartyState& state, int width) {
    if (state.count == 0 || width < 60) return nullptr;
    if (state == drawn_ && width == drawn_width_) return &image_;
    drawn_ = state;
    drawn_width_ = width;

    const int s = std::max(1, width / 160);   // font pixels per glyph pixel
    const int glyph = kOsdGlyph * s;
    const int line = glyph + 2 * s, hp_h = 3 * s, mp_h = 2 * s, row_gap = 4 * s;
    const int row = line + hp_h + s + mp_h + row_gap;
    // The content sits inside the frame's inner border: about 7% of the width in from each side, 14% of the height
    // from the top and 12% from the bottom.
    const int content_h = row * state.count + line;
    const int pad_x = width * 7 / 100;
    image_.w = width;
    image_.h = content_h * 100 / 74 + 1;
    const int top = image_.h * 14 / 100;
    image_.rgba.assign(static_cast<std::size_t>(image_.w) * static_cast<std::size_t>(image_.h), 0u);
    Canvas c(image_);
    c.picture(hud_art::frame(), 0, 0, image_.w, image_.h);

    const int inner = image_.w - 2 * pad_x;
    int y = top;
    for (int i = 0; i < state.count; ++i) {
        const PartyMember& m = state.members[static_cast<std::size_t>(i)];
        const std::string level = "Lv" + std::to_string(m.level);
        const std::string hp = std::to_string(m.hp) + "/" + std::to_string(m.max_hp);
        const int hp_w = static_cast<int>(hp.size()) * glyph;
        // The name gets what the HP figures and the level leave over.
        const int room = std::max(0, (inner - hp_w - glyph) / glyph - static_cast<int>(level.size()) - 1);
        const std::string name = m.name.substr(0, static_cast<std::size_t>(room));
        int x = pad_x + c.text(name, pad_x, y, s, kName);
        c.text(level, x + glyph / 2, y, s, kDim);
        c.text(hp, pad_x + inner - hp_w, y, s, kName);
        y += line;
        c.bar(pad_x, y, inner, hp_h, m.hp, m.max_hp, hp_colour(m.hp, m.max_hp));
        y += hp_h + s;
        c.bar(pad_x, y, inner, mp_h, m.mp, m.max_mp, kMp);
        y += mp_h + row_gap;
    }
    const int coin = glyph + 2 * s;
    c.picture(hud_art::coin(), pad_x, y - s, coin, coin);
    c.text(std::to_string(state.bits) + " bits", pad_x + coin + glyph / 2, y, s, kBits);
    ++serial_;
    return &image_;
}

}  // namespace dw3app
