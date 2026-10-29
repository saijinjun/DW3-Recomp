// Auto-battle's taps: only in battles and their results, paused after the player presses a button.

#include <gtest/gtest.h>

#include "auto_battle.h"

using dw3app::ingame::auto_battle_buttons;

TEST(AutoBattle, TapsCrossOnlyInBattleModes) {
    uint64_t resume = 0;
    EXPECT_EQ(auto_battle_buttons(true, 6, 0, 0, resume), dw3::kPadCross);
    EXPECT_EQ(auto_battle_buttons(true, 0x14, 0, 0, resume), dw3::kPadCross);
    EXPECT_EQ(auto_battle_buttons(true, 2, 0, 0, resume), 0);  // the field
    EXPECT_EQ(auto_battle_buttons(false, 6, 0, 0, resume), 0);
}

TEST(AutoBattle, ReleasesBetweenTaps) {
    uint64_t resume = 0;
    int down = 0;
    for (uint64_t f = 0; f < 120; ++f) down += auto_battle_buttons(true, 6, 0, f, resume) != 0;
    EXPECT_EQ(down, 18);  // 3 of every 20 frames
    EXPECT_EQ(auto_battle_buttons(true, 6, 0, 5, resume), 0);
}

TEST(AutoBattle, PlayerInputPausesIt) {
    uint64_t resume = 0;
    EXPECT_EQ(auto_battle_buttons(true, 6, dw3::kPadUp, 100, resume), 0);
    EXPECT_EQ(auto_battle_buttons(true, 6, 0, 200, resume), 0);  // a tap frame, but paused
    EXPECT_EQ(auto_battle_buttons(true, 6, 0, 230, resume), 0);
    EXPECT_EQ(auto_battle_buttons(true, 6, 0, 240, resume), dw3::kPadCross);
}
