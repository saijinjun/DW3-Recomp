// Embedded overlay art (see CMakeLists.txt): pixels as RGBA8 with red in the lowest byte, like OsdImage.
#pragma once

#include <cstdint>

namespace dw3app::hud_art {

struct Picture {
    const uint32_t* rgba;
    int w, h;
};

Picture frame();  // the party HUD's panel frame
Picture coin();   // the bits coin

}  // namespace dw3app::hud_art
