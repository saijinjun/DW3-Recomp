#include "battle_hud.h"

#include <algorithm>

#include "hud_canvas.h"
#include "party_hud.h"

namespace dw3app {

namespace {

using namespace hud;

constexpr uint32_t kEnemyFill = rgba(240, 90, 50);
constexpr uint32_t kEnemyDown = rgba(120, 120, 130);
constexpr uint32_t kInitial = rgba(120, 220, 255);

// Where things sit in the art, as fractions of its width or height (measured on the owner's sheet).
// Card: the ring's centre and inner radius, and the panel the text and bars go in.
constexpr float kCardRingX = 0.106f, kCardRingY = 0.51f, kCardRingR = 0.06f;
constexpr float kCardLeft = 0.25f, kCardRight = 0.94f, kCardTop = 0.27f, kCardBottom = 0.80f;
// Enemy bar: the ring and the channel for the HP.
constexpr float kEnemyRingX = 0.085f, kEnemyRingY = 0.48f;
constexpr float kEnemyLeft = 0.137f, kEnemyRight = 0.955f, kEnemyTop = 0.32f, kEnemyBottom = 0.72f;

int px(float f, int size) { return static_cast<int>(std::lround(f * static_cast<float>(size))); }

void centred_text(Canvas& c, const std::string& s, int cx, int cy, int scale, uint32_t colour) {
    const int w = static_cast<int>(s.size()) * kOsdGlyph * scale, h = kOsdGlyph * scale;
    c.text(s, cx - w / 2, cy - h / 2, scale, colour);
}

}  // namespace

BattleState read_battle(const std::function<uint32_t(uint32_t)>& read32) {
    BattleState s;
    auto half = [&](uint32_t addr) { return static_cast<int>((read32(addr & ~3u) >> ((addr & 2u) * 8)) & 0xFFFF); };
    for (uint32_t side = 0; side < 2; ++side)
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t r = kBattleRecords + side * kBattleSideStride + i * kBattleRecordSize;
            BattleFighter f;
            f.max_hp = half(r + 6);
            f.hp = half(r + 8);
            f.max_mp = half(r + 10);
            f.mp = half(r + 12);
            if (f.max_hp <= 0 || f.max_hp > 9999 || f.hp > f.max_hp || f.max_mp > 9999 || f.mp > f.max_mp) continue;
            if (side == 0) {
                f.name = read_party_name(read32, static_cast<int>(i));
                if (f.name.empty()) f.name = "Digimon";
                s.party[static_cast<std::size_t>(s.party_count++)] = std::move(f);
            } else {
                f.name = "Enemy";
                s.enemies[static_cast<std::size_t>(s.enemy_count++)] = std::move(f);
            }
        }
    if (s.enemy_count > 1)
        for (int i = 0; i < s.enemy_count; ++i) s.enemies[static_cast<std::size_t>(i)].name += " " + std::to_string(i + 1);
    return s;
}

const OsdImage* BattleHud::party(const BattleState& state, int width) {
    if (state.party_count == 0 || width < 80) return nullptr;
    if (party_.same(state.party, state.party_count, width)) return &party_.image;
    party_.drawn = state.party;
    party_.count = state.party_count;
    party_.width = width;
    const hud_art::Picture art = hud_art::battle_card();
    const int card_h = width * art.h / art.w, gap = std::max(2, card_h / 12);
    OsdImage& img = party_.image;
    img.w = width;
    img.h = state.party_count * card_h + (state.party_count - 1) * gap;
    img.rgba.assign(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h), 0u);
    Canvas c(img);
    const int s = std::max(1, width / 170), glyph = kOsdGlyph * s;
    for (int i = 0; i < state.party_count; ++i) {
        const BattleFighter& f = state.party[static_cast<std::size_t>(i)];
        const int y0 = i * (card_h + gap);
        c.picture(art, 0, y0, width, card_h);
        // The Digimon's initial in the ring.
        const int big = std::max(1, px(kCardRingR * 1.3f, width) / kOsdGlyph);
        centred_text(c, f.name.substr(0, 1), px(kCardRingX, width), y0 + px(kCardRingY, card_h), big, kInitial);
        const int left = px(kCardLeft, width), right = px(kCardRight, width), inner = right - left;
        const int top = y0 + px(kCardTop, card_h), bottom = y0 + px(kCardBottom, card_h);
        const std::string hp = std::to_string(f.hp) + "/" + std::to_string(f.max_hp);
        const int hp_w = static_cast<int>(hp.size()) * glyph;
        const int room = std::max(0, (inner - hp_w) / glyph - 1);
        c.text(f.name.substr(0, static_cast<std::size_t>(room)), left, top, s, kName);
        c.text(hp, right - hp_w, top, s, kName);
        // The bars share what is left under the text: HP thicker than MP, a small gap between.
        const int bars = std::max(4, bottom - (top + glyph + 2 * s));
        const int hp_h = std::max(2, bars / 2), mp_h = std::max(1, bars * 3 / 10);
        c.bar(left, bottom - bars, inner, hp_h, f.hp, f.max_hp, hp_colour(f.hp, f.max_hp));
        c.bar(left, bottom - mp_h, inner, mp_h, f.mp, f.max_mp, kMp);
    }
    ++party_.serial;
    return &img;
}

const OsdImage* BattleHud::enemies(const BattleState& state, int width) {
    if (state.enemy_count == 0 || width < 80) return nullptr;
    if (enemies_.same(state.enemies, state.enemy_count, width)) return &enemies_.image;
    enemies_.drawn = state.enemies;
    enemies_.count = state.enemy_count;
    enemies_.width = width;
    const hud_art::Picture art = hud_art::battle_enemy();
    const int bar_h = width * art.h / art.w, gap = std::max(2, bar_h / 8);
    OsdImage& img = enemies_.image;
    img.w = width;
    img.h = state.enemy_count * bar_h + (state.enemy_count - 1) * gap;
    img.rgba.assign(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h), 0u);
    Canvas c(img);
    for (int i = 0; i < state.enemy_count; ++i) {
        const BattleFighter& f = state.enemies[static_cast<std::size_t>(i)];
        const int y0 = i * (bar_h + gap);
        const int left = px(kEnemyLeft, width), right = px(kEnemyRight, width);
        const int top = y0 + px(kEnemyTop, bar_h), bottom = y0 + px(kEnemyBottom, bar_h);
        // The channel's fill goes under the frame so the frame's edge stays crisp.
        c.bar(left, top, right - left, bottom - top, f.hp, f.max_hp, f.hp > 0 ? kEnemyFill : kEnemyDown);
        c.picture(art, 0, y0, width, bar_h);
        const int s = std::max(1, (bottom - top - 2) / kOsdGlyph);
        centred_text(c, std::to_string(i + 1), px(kEnemyRingX, width), y0 + px(kEnemyRingY, bar_h), s, kName);
        const std::string hp = std::to_string(f.hp) + "/" + std::to_string(f.max_hp);
        const int ty = (top + bottom) / 2 - kOsdGlyph * s / 2;
        c.text(f.name, left + kOsdGlyph * s / 2, ty, s, kName);
        c.text(hp, right - static_cast<int>(hp.size() + 1) * kOsdGlyph * s, ty, s, kName);
    }
    ++enemies_.serial;
    return &img;
}

}  // namespace dw3app
