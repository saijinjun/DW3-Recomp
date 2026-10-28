// On-screen controls for touch screens (the Android build): a d-pad, the four face buttons, L1/L2/R1/R2,
// SELECT/START and a few app buttons (fast-forward, auto-battle, save and load state). Fingers are given in window pixels; the
// controls report the console buttons they hold and draw themselves into a window-sized RGBA image that the
// presenters blend over the picture. No SDL dependency.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "osd.h"

namespace dw3app {

enum class TouchAction { FastForward, SaveState, LoadState, AutoBattle };

class TouchControls {
public:
    TouchControls();

    // Hidden controls neither draw nor take fingers (a gamepad is in use). Touching the screen shows them again.
    void set_visible(bool visible);
    bool visible() const { return visible_; }

    void finger_down(uint64_t finger, float x, float y, int window_w, int window_h);
    void finger_move(uint64_t finger, float x, float y, int window_w, int window_h);
    void finger_up(uint64_t finger);
    void release_all();

    // Console pad bits (dw3/sio.h) held by fingers right now.
    uint16_t buttons() const;
    // App buttons tapped since the last call.
    std::vector<TouchAction> take_actions();
    // Shows the fast-forward and auto-battle buttons lit.
    void set_fast_forward(bool on);
    void set_auto_battle(bool on);
    // Counts fingers that landed on a control (for a short vibration per press).
    uint32_t presses() const { return presses_; }

    // The controls drawn for a window of this size (nothing visible is transparent), at 1/`downscale` of the window's
    // resolution. `serial` changes whenever the pixels do, so callers upload the image only then.
    const OsdImage& image(int window_w, int window_h, int downscale = 1);
    uint64_t serial() const { return serial_; }

    // A control on screen, in window pixels. Exposed for tests.
    struct Control {
        enum class Shape { DPad, Round, Rect } shape;
        uint16_t pad_bit = 0;  // 0 for the d-pad (its bits depend on the finger) and the app buttons
        int action = -1;       // TouchAction for app buttons, -1 otherwise
        std::string label;
        float x = 0, y = 0;  // centre
        float rx = 0, ry = 0;  // radius (round, d-pad) or half size (rect)
        int group = 0;  // a finger that slides moves to another control of the same group (face buttons)
    };
    const std::vector<Control>& controls(int window_w, int window_h);

private:
    struct Finger {
        int control = -1;
        float x = 0, y = 0;
    };
    void layout(int w, int h);
    int hit(float x, float y, bool generous) const;
    uint16_t dpad_bits(const Control& c, float x, float y) const;
    void draw(int downscale);

    bool visible_ = true;
    bool fast_forward_ = false;
    bool auto_battle_ = false;
    uint32_t presses_ = 0;
    int w_ = 0, h_ = 0;
    std::vector<Control> controls_;
    std::map<uint64_t, Finger> fingers_;
    std::vector<TouchAction> actions_;
    OsdImage image_;
    uint64_t drawn_state_ = ~0ull;  // what image_ shows: pressed controls and flags
    uint64_t serial_ = 0;
};

}  // namespace dw3app
