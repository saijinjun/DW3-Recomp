// Arithmetic behind the gameplay options that scale numbers the game computes. Pure functions, tested without the game.
#pragma once

#include <algorithm>
#include <cstdint>

namespace dw3app {

// Experience multiplier for a gameplay.xp_multiplier choice (1x, 2x, 3x, 5x, 10x).
inline int xp_multiplier(int choice) {
    constexpr int kMultipliers[] = {1, 2, 3, 5, 10};
    return kMultipliers[std::clamp(choice, 0, 4)];
}

// The game keeps a Digimon's experience in a field that tops out at this value.
constexpr uint32_t kMaxExperience = 999999;

// An experience award multiplied by `multiplier`, never more than the field can hold.
inline uint32_t scaled_experience(uint32_t amount, int multiplier) {
    const uint64_t scaled = static_cast<uint64_t>(amount) * static_cast<uint64_t>(std::max(multiplier, 1));
    return static_cast<uint32_t>(std::min<uint64_t>(scaled, kMaxExperience));
}

// Bits (money) multiplier for a gameplay.bits_multiplier choice: the same steps as experience.
inline int bits_multiplier(int choice) { return xp_multiplier(choice); }

// The largest purse a scaled battle reward fills up to (the screens that show bits print six digits safely).
constexpr uint32_t kMaxBits = 999999;

// The purse after a battle reward `gained` on top of `before` is multiplied by `multiplier`. It never drops below
// what the game itself granted and never passes kMaxBits by scaling.
inline uint32_t scaled_bits(uint32_t before, uint32_t gained, int multiplier) {
    const uint64_t granted = static_cast<uint64_t>(before) + gained;
    const uint64_t scaled = static_cast<uint64_t>(before) + static_cast<uint64_t>(gained) * static_cast<uint64_t>(std::max(multiplier, 1));
    return static_cast<uint32_t>(std::max(granted, std::min<uint64_t>(scaled, kMaxBits)));
}

// Difficulty (gameplay.difficulty: normal, easy, hard): enemies start a battle with this percentage of their HP.
inline int enemy_hp_percent(int choice) { return choice == 1 ? 60 : choice == 2 ? 130 : 100; }

// Enemy HP scaled by `percent`, at least 1 and never past what the battle's HP field is kept to (0x3FFF).
inline uint32_t scaled_enemy_hp(uint32_t hp, int percent) {
    const uint64_t scaled = (static_cast<uint64_t>(hp) * static_cast<uint64_t>(std::max(percent, 1)) + 50) / 100;
    return static_cast<uint32_t>(std::clamp<uint64_t>(scaled, 1, 0x3FFF));
}

// Text speed (gameplay.text_speed): normal, fast, instant.
enum class TextSpeed { Normal, Fast, Instant };
constexpr int kFastTextChars = 3;  // characters a window prints per frame at "fast"

inline TextSpeed text_speed(int choice) { return choice == 1 ? TextSpeed::Fast : choice == 2 ? TextSpeed::Instant : TextSpeed::Normal; }

// How far a message window has printed before its own tick of this frame. `shown` is the position printed so far and
// `page_end` where the current box ends. Fast moves ahead by kFastTextChars - 1 (the window's tick adds the last one)
// without passing the end; instant prints the whole box.
inline uint32_t text_position_before_tick(TextSpeed speed, uint32_t shown, uint32_t page_end) {
    switch (speed) {
        case TextSpeed::Instant: return std::max(shown, page_end);
        case TextSpeed::Fast: {
            const uint32_t last_before_end = page_end > 0 ? page_end - 1 : 0;
            return std::max(shown, std::min(shown + (kFastTextChars - 1), last_before_end));
        }
        default: return shown;
    }
}

}  // namespace dw3app
