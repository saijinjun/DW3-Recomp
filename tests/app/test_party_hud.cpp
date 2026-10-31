// The party HUD: the party read from a fake RAM laid out like the game's (values from a new game in Asuka City),
// the names decoded from the game's text code, and the panel redrawn only when something changed.

#include <gtest/gtest.h>

#include <map>

#include "present/party_hud.h"

using dw3app::PartyHud;
using dw3app::PartyState;

namespace {

struct FakeRam {
    std::map<uint32_t, uint8_t> bytes;
    void put8(uint32_t a, uint8_t v) { bytes[a] = v; }
    void put16(uint32_t a, uint16_t v) {
        put8(a, static_cast<uint8_t>(v));
        put8(a + 1, static_cast<uint8_t>(v >> 8));
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
    void member(uint32_t index, const std::vector<uint8_t>& name, int level, int hp, int max_hp, int mp, int max_mp) {
        const uint32_t e = dw3app::kPartyRoster + index * dw3app::kPartyRosterStride;
        for (std::size_t i = 0; i < name.size(); ++i) put8(e - 0x18 + static_cast<uint32_t>(i), name[i]);
        put16(e + 4, static_cast<uint16_t>(level));
        put16(e + 8, static_cast<uint16_t>(hp));
        put16(e + 10, static_cast<uint16_t>(max_hp));
        put16(e + 12, static_cast<uint16_t>(mp));
        put16(e + 14, static_cast<uint16_t>(max_mp));
    }
};

FakeRam asuka_city() {
    FakeRam ram;
    ram.put32(dw3app::kPartyPurse, 80);
    ram.put32(dw3app::kPartySlots, 0);
    ram.put32(dw3app::kPartySlots + 4, 6);
    ram.put32(dw3app::kPartySlots + 8, 7);
    ram.member(0, {0x18, 0x36, 0x3B, 0x2C, 0x34, 0x36, 0x35}, 1, 150, 150, 100, 100);  // Kotemon
    ram.member(6, {0x1F, 0x2C, 0x35, 0x28, 0x34, 0x36, 0x35}, 1, 140, 140, 200, 200);  // Renamon
    ram.member(7, {0x1D, 0x28, 0x3B, 0x28, 0x34, 0x36, 0x35}, 1, 30, 130, 170, 170);   // Patamon
    return ram;
}

PartyState read(const FakeRam& ram) {
    return dw3app::read_party([&](uint32_t a) { return ram.read32(a); });
}

}  // namespace

TEST(PartyHud, DecodesGameNames) {
    const uint8_t agumon[] = {0x0E, 0x2E, 0x3C, 0x34, 0x36, 0x35, 0, 0x30};
    EXPECT_EQ(dw3app::decode_game_name(agumon, sizeof agumon), "Agumon");
    const uint8_t digits[] = {0x04, 0x0D};
    EXPECT_EQ(dw3app::decode_game_name(digits, sizeof digits), "09");
}

TEST(PartyHud, ReadsThePartyFromRam) {
    const PartyState s = read(asuka_city());
    ASSERT_EQ(s.count, 3);
    EXPECT_EQ(s.bits, 80u);
    EXPECT_EQ(s.members[0].name, "Kotemon");
    EXPECT_EQ(s.members[1].name, "Renamon");
    EXPECT_EQ(s.members[1].max_mp, 200);
    EXPECT_EQ(s.members[2].name, "Patamon");
    EXPECT_EQ(s.members[2].hp, 30);
    EXPECT_EQ(s.members[2].level, 1);
}

TEST(PartyHud, LeavesOutSlotsThatAreNotDigimon) {
    FakeRam ram = asuka_city();
    ram.put32(dw3app::kPartySlots + 4, 0xFFFFFFFF);  // empty slot
    ram.member(7, {0x1D}, 1, 200, 100, 0, 0);        // HP above its maximum: not a roster entry being read right
    const PartyState s = read(ram);
    ASSERT_EQ(s.count, 1);
    EXPECT_EQ(s.members[0].name, "Kotemon");
}

TEST(PartyHud, DrawsAPanelAndRedrawsOnlyOnChange) {
    PartyHud hud;
    PartyState s = read(asuka_city());
    const dw3app::OsdImage* image = hud.frame(s, 360);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->w, 360);
    EXPECT_GT(image->h, 60);
    EXPECT_LT(image->h, 360);
    const uint64_t serial = hud.serial();
    hud.frame(s, 360);
    EXPECT_EQ(hud.serial(), serial);
    s.members[0].hp = 10;
    hud.frame(s, 360);
    EXPECT_NE(hud.serial(), serial);
    // Low HP draws the bar red: some pixel of the panel is the warning colour.
    bool red = false;
    for (uint32_t p : hud.frame(s, 360)->rgba) red = red || p == (240u | (70u << 8) | (60u << 16) | (255u << 24));
    EXPECT_TRUE(red);
    EXPECT_EQ(hud.frame(PartyState{}, 360), nullptr);
}
