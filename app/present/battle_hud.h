// The battle HUD (gameplay.battle_hud): during a battle, a card per party Digimon with its HP and MP as they change
// and a bar per enemy with its HP in figures (the game shows only the bar of the enemy being targeted), drawn beside
// the picture in the owner's HUD art. Read from the game's RAM; nothing in the game changes. No SDL dependency.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "osd.h"

namespace dw3app {

struct BattleFighter {
    std::string name;
    int hp = 0, max_hp = 0, mp = 0, max_mp = 0;
    bool operator==(const BattleFighter&) const = default;
};

struct BattleState {
    std::array<BattleFighter, 3> party{}, enemies{};
    int party_count = 0, enemy_count = 0;  // in use, from the front
    bool operator==(const BattleState&) const = default;
};

// RAM layout (USA), in battle: 32-byte records, three per side, the party's first at 0x800A31F8 in party order and the
// enemies' 96 bytes after; each holds the Digimon's number at +0, then max HP +6, HP +8, max MP +10, MP +12
// (halfwords). An unused record is all zero.
inline constexpr uint32_t kBattleRecords = 0x800A31F8, kBattleRecordSize = 32, kBattleSideStride = 96;

// Reads both sides through `read32` (an aligned 32-bit read); the party's names come from the roster (party_hud.h).
// Records with stats out of range are left out.
BattleState read_battle(const std::function<uint32_t(uint32_t)>& read32);

class BattleHud {
public:
    // The party's cards / the enemies' bars for `state`, `width` pixels wide (the height follows), or null when there
    // is nobody to show. Each is redrawn only when its side or the width changes; its serial changes whenever its pixels do.
    const OsdImage* party(const BattleState& state, int width);
    const OsdImage* enemies(const BattleState& state, int width);
    uint64_t party_serial() const { return party_.serial; }
    uint64_t enemies_serial() const { return enemies_.serial; }

private:
    struct Panel {
        std::array<BattleFighter, 3> drawn{};
        int count = -1, width = -1;
        OsdImage image;
        uint64_t serial = 0;
        bool same(const std::array<BattleFighter, 3>& side, int n, int w) const { return side == drawn && n == count && w == width; }
    };
    Panel party_, enemies_;
};

}  // namespace dw3app
