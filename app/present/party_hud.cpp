#include "party_hud.h"

#include <algorithm>
#include <cmath>

#include "hud_canvas.h"

namespace dw3app {

namespace {

using namespace hud;

constexpr uint32_t kBits = rgba(255, 210, 80);

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

std::string read_party_name(const std::function<uint32_t(uint32_t)>& read32, int slot) {
    const uint32_t index = read32(kPartySlots + static_cast<uint32_t>(slot) * 4);
    if (index >= 8) return {};
    const uint32_t entry = kPartyRoster + index * kPartyRosterStride;
    uint8_t name[12];
    for (uint32_t i = 0; i < sizeof name; i += 4) {
        const uint32_t w = read32(entry - 0x18 + i);
        for (uint32_t k = 0; k < 4; ++k) name[i + k] = static_cast<uint8_t>(w >> (8 * k));
    }
    return decode_game_name(name, sizeof name);
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
        m.name = read_party_name(read32, static_cast<int>(slot));
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
