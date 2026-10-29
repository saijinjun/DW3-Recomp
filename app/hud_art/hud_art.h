// Embedded overlay art (see CMakeLists.txt): pixels as RGBA8 with red in the lowest byte, like OsdImage.
#pragma once

#include <cstdint>

namespace dw3app::hud_art {

struct Picture {
    const uint32_t* rgba;
    int w, h;
};

Picture frame();       // the party HUD's panel frame
Picture coin();        // the bits coin
Picture dpad();        // touch controls: the d-pad, a disc with the four arms
Picture face();        // a face button, blank (the symbol is drawn over it)
Picture shoulder_l();  // L1/L2, slanted to the left
Picture shoulder_r();  // R1/R2
Picture pill();        // the other buttons (SELECT, START, the app's own); its round ends are pill.h / 2 wide

// Draws the part of `pic` from source x `src_x0` to `src_x1` (pixels; the full height) scaled into the w x h box at
// (x0, y0) of the dw x dh image `dst` (bilinear), blended over what is there with straight alpha.
void blit(uint32_t* dst, int dw, int dh, const Picture& pic, int x0, int y0, int w, int h, int src_x0 = 0, int src_x1 = -1);

// `pic` into the box with its left and right `cap` source pixels kept in proportion (scaled by h / pic.h) and the
// middle stretched to fill: for pill shapes drawn at another width.
void blit_capped(uint32_t* dst, int dw, int dh, const Picture& pic, int cap, int x0, int y0, int w, int h);

}  // namespace dw3app::hud_art
