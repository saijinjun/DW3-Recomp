// Gameplay options that change what the game does: random encounters, the party's invulnerability and experience.
// Each is a host override of one game function (see docs/ARCHITECTURE.md, "Host overrides"); the options are read from
// the controller every time the function runs, so a change takes effect at once.
#pragma once

#include "controller.h"
#include "dw3/machine.h"

namespace dw3app::ingame {

class GameplayHooks {
public:
    // At most one instance per process may exist at a time; the machine and the controller must outlive it.
    GameplayHooks(dw3::Machine& machine, SettingsController& controller);
    ~GameplayHooks();
    GameplayHooks(const GameplayHooks&) = delete;
    GameplayHooks& operator=(const GameplayHooks&) = delete;

    // Once per frame: options that act on what a frame changed rather than inside a game function (the bits
    // multiplier, which scales the reward the game adds to the purse after a battle's results).
    void tick();

private:
    friend struct GameplayHook;
    dw3::Machine& machine_;
    SettingsController& controller_;
    bool in_results_ = false;
    uint32_t bits_before_ = 0;  // the purse when the results screens began
    int bits_window_ = 0;       // frames left in which a rise of the purse counts as the battle's reward
};

}  // namespace dw3app::ingame
