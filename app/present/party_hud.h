// The party HUD (gameplay.party_hud): a small panel in the field with each party Digimon's name, level and HP/MP
// bars, and the bits in the purse, read from the game's RAM. Drawn over the window like the minimap; nothing in the
// game changes. No SDL dependency.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "osd.h"

namespace dw3app {

struct PartyMember {
    std::string name;
    int level = 0;
    int hp = 0, max_hp = 0, mp = 0, max_mp = 0;
    bool operator==(const PartyMember&) const = default;
};

struct PartyState {
    std::array<PartyMember, 3> members{};
    int count = 0;  // members in use, from the front
    uint32_t bits = 0;
    bool operator==(const PartyState&) const = default;
};

// RAM layout (USA): the purse at 0x80048554, then the roster indices of the three party members (words); the roster
// of 8 Digimon at 0x80048C68, 0x3DC bytes each (level at +4, HP +8, max HP +10, MP +12, max MP +14, halfwords). A
// Digimon's name is stored 0x18 bytes before its roster entry, in the game's own text code.
inline constexpr uint32_t kPartyPurse = 0x80048554, kPartySlots = 0x80048558;
inline constexpr uint32_t kPartyRoster = 0x80048C68, kPartyRosterStride = 0x3DC;

// The game's text code for a name: 'A'..'Z' are 0x0E..0x27, 'a'..'z' 0x28..0x41, '0'..'9' 0x04..0x0D; 0 ends it.
std::string decode_game_name(const uint8_t* bytes, std::size_t size);

// Reads the party from RAM through `read32` (an aligned 32-bit read). Members with an index or stats out of range are
// left out.
PartyState read_party(const std::function<uint32_t(uint32_t)>& read32);
// The name of the Digimon in party slot `slot` (0..2), empty when the slot holds none.
std::string read_party_name(const std::function<uint32_t(uint32_t)>& read32, int slot);

class PartyHud {
public:
    // The panel for `state`, `width` pixels wide (its height follows), or null when there is no member to show.
    // Redrawn only when the state or the width changes; `serial()` changes whenever the pixels do.
    const OsdImage* frame(const PartyState& state, int width);
    uint64_t serial() const { return serial_; }

private:
    PartyState drawn_{};
    int drawn_width_ = -1;
    OsdImage image_;
    uint64_t serial_ = 0;
};

}  // namespace dw3app
