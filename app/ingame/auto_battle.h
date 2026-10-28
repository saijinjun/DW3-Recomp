// Auto-battle (gameplay.auto_battle): a pad that passes the player's buttons through and, while a battle or its
// results run, taps CROSS. In a battle CROSS picks Fight, the first target and the attack each turn; on the results
// screens it moves on. Any button of the player's pauses the tapping for two seconds, so a turn can be taken by hand.
#pragma once

#include "controller.h"
#include "dw3/machine.h"
#include "dw3/recomp.h"
#include "dw3/sio.h"

namespace dw3app::ingame {

// What the pad adds on a frame. Pure, for tests: `resume` is the first frame tapping may continue at.
inline uint16_t auto_battle_buttons(bool enabled, uint32_t game_mode, uint16_t player, uint64_t frame, uint64_t& resume) {
    constexpr uint32_t kBattleMode = 6, kResultsMode = 0x14;
    constexpr uint64_t kPauseFrames = 120, kTapPeriod = 20, kTapFrames = 3;
    if (!enabled || (game_mode != kBattleMode && game_mode != kResultsMode)) return 0;
    if (player != 0) {
        resume = frame + kPauseFrames;
        return 0;
    }
    return frame >= resume && frame % kTapPeriod < kTapFrames ? dw3::kPadCross : 0;
}

class AutoBattlePad final : public dw3::PadSource {
public:
    // `player` is the pad the game reads otherwise; all three must outlive this.
    AutoBattlePad(dw3::PadSource& player, dw3::Machine& machine, const SettingsController& controller)
        : player_(player), machine_(machine), controller_(controller) {}
    bool connected(int port) override { return player_.connected(port); }
    uint16_t buttons(int port) override {
        const uint16_t own = player_.buttons(port);
        if (port != 0) return own;
        constexpr uint32_t kGameMode = 0x800554D0;
        const uint64_t frame = machine_.video().frame_count();
        return own | auto_battle_buttons(controller_.active().gameplay.auto_battle, dw3_read32(&machine_.ctx(), kGameMode), own,
                                         frame, resume_);
    }

private:
    dw3::PadSource& player_;
    dw3::Machine& machine_;
    const SettingsController& controller_;
    uint64_t resume_ = 0;
};

}  // namespace dw3app::ingame
