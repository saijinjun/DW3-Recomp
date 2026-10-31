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
Picture battle_card();   // the battle HUD's card for a party Digimon: a ring on the left, a dark panel to the right
Picture battle_enemy();  // an enemy's bar: a red frame with a ring on the left and an open channel for the HP
Picture minimap_frame();  // the minimap's frame, compass at the top right; see kMinimapFrame* for its opening

// The minimap frame's corners (kept whole when the frame is stretched) and how far in its opening starts on each
// side, in its own pixels.
inline constexpr int kMinimapFrameCorner = 100;
inline constexpr int kMinimapFrameLeft = 37, kMinimapFrameTop = 50, kMinimapFrameRight = 34, kMinimapFrameBottom = 31;

// A part of a picture in its own pixels: [x0, x1) x [y0, y1); -1 for x1 or y1 is the picture's width or height.
struct Part {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
};

// Draws `part` of `pic` scaled into the w x h box at (x0, y0) of the dw x dh image `dst` (bilinear), blended over what
// is there with straight alpha.
void blit(uint32_t* dst, int dw, int dh, const Picture& pic, int x0, int y0, int w, int h, Part part = {});

// `pic` into the box with its left and right `cap` source pixels kept in proportion (scaled by h / pic.h) and the
// middle stretched to fill: for pill shapes drawn at another width.
void blit_capped(uint32_t* dst, int dw, int dh, const Picture& pic, int cap, int x0, int y0, int w, int h);

// `pic` stretched into the box with its four `corner` x `corner` source corners kept whole, `dst_corner` pixels on
// each side (made smaller when the box is too small for them), and the edges and middle stretched between them.
void blit_nine(uint32_t* dst, int dw, int dh, const Picture& pic, int corner, int x0, int y0, int w, int h, int dst_corner);

}  // namespace dw3app::hud_art
