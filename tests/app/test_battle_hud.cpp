// The battle HUD: both sides read from a fake RAM laid out like the game's (values from the first random encounter of a
// new game: the party Kotemon, Renamon and Patamon against one Kunemon), and the panels redrawn only on change.

#include <gtest/gtest.h>

#include <map>

#include "present/battle_hud.h"
#include "present/party_hud.h"

using dw3app::BattleHud;
using dw3app::BattleState;

namespace {

struct FakeRam {
    std::map<uint32_t, uint8_t> bytes;
    void put16(uint32_t a, uint16_t v) {
        bytes[a] = static_cast<uint8_t>(v);
        bytes[a + 1] = static_cast<uint8_t>(v >> 8);
    }
    void put32(uint32_t a, uint32_t v) {
        put16(a, static_cast<uint16_t>(v));
        put16(a + 2, static_cast<uint16_t>(v >> 16));
    }
    uint32_t read32(uint32_t a) const {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 4; ++k)
            if (auto it = bytes.find(a + k); it != bytes.end()) v |= static_cast<uint32_t>(it->second) << (8 * k);
        return v;
    }
    void fighter(uint32_t side, uint32_t i, int number, int hp, int max_hp, int mp, int max_mp) {
        const uint32_t r = dw3app::kBattleRecords + side * dw3app::kBattleSideStride + i * dw3app::kBattleRecordSize;
        put16(r, static_cast<uint16_t>(number));
        put16(r + 6, static_cast<uint16_t>(max_hp));
        put16(r + 8, static_cast<uint16_t>(hp));
        put16(r + 10, static_cast<uint16_t>(max_mp));
        put16(r + 12, static_cast<uint16_t>(mp));
    }
};

FakeRam first_battle() {
    FakeRam ram;
    // Party slot 0 holds roster entry 0, named "Kotemon" in the game's text code; slots 1 and 2 hold no name here.
    ram.put32(dw3app::kPartySlots, 0);
    ram.put32(dw3app::kPartySlots + 4, 6);
    ram.put32(dw3app::kPartySlots + 8, 7);
    const uint8_t kotemon[] = {0x18, 0x36, 0x3B, 0x2C, 0x34, 0x36, 0x35, 0};
    for (uint32_t i = 0; i < sizeof kotemon; ++i) ram.bytes[dw3app::kPartyRoster - 0x18 + i] = kotemon[i];
    ram.fighter(0, 0, 383, 150, 150, 100, 100);
    ram.fighter(0, 1, 373, 140, 140, 200, 200);
    ram.fighter(0, 2, 31, 130, 130, 170, 170);
    ram.fighter(1, 0, 32, 120, 120, 9999, 9999);
    return ram;
}

BattleState read(const FakeRam& ram) {
    return dw3app::read_battle([&](uint32_t a) { return ram.read32(a); });
}

}  // namespace

TEST(BattleHud, ReadsBothSides) {
    const BattleState s = read(first_battle());
    ASSERT_EQ(s.party_count, 3);
    ASSERT_EQ(s.enemy_count, 1);
    EXPECT_EQ(s.party[0].name, "Kotemon");
    EXPECT_EQ(s.party[1].name, "Digimon");  // no name in the fake roster
    EXPECT_EQ(s.party[1].max_mp, 200);
    EXPECT_EQ(s.enemies[0].name, "Enemy");
    EXPECT_EQ(s.enemies[0].hp, 120);
}

TEST(BattleHud, NumbersEnemiesWhenThereAreSeveral) {
    FakeRam ram = first_battle();
    ram.fighter(1, 1, 33, 0, 80, 0, 0);  // defeated: still shown, with an empty bar
    ram.fighter(1, 2, 34, 90, 80, 0, 0); // HP above its maximum: not a record being read right
    const BattleState s = read(ram);
    ASSERT_EQ(s.enemy_count, 2);
    EXPECT_EQ(s.enemies[0].name, "Enemy 1");
    EXPECT_EQ(s.enemies[1].name, "Enemy 2");
    EXPECT_EQ(s.enemies[1].hp, 0);
}

TEST(BattleHud, DrawsPanelsAndRedrawsOnlyOnChange) {
    BattleHud hud;
    BattleState s = read(first_battle());
    const dw3app::OsdImage* cards = hud.party(s, 400);
    ASSERT_NE(cards, nullptr);
    EXPECT_EQ(cards->w, 400);
    EXPECT_GT(cards->h, 3 * 60);
    const dw3app::OsdImage* bars = hud.enemies(s, 400);
    ASSERT_NE(bars, nullptr);
    EXPECT_LT(bars->h, cards->h / 2);
    const uint64_t party = hud.party_serial(), enemies = hud.enemies_serial();
    s.enemies[0].hp = 60;
    hud.party(s, 400);
    hud.enemies(s, 400);
    EXPECT_EQ(hud.party_serial(), party);  // only the enemy changed
    EXPECT_NE(hud.enemies_serial(), enemies);
    EXPECT_EQ(hud.party(BattleState{}, 400), nullptr);
    EXPECT_EQ(hud.enemies(BattleState{}, 400), nullptr);
}
