// A small map of the field being walked (graphics.minimap): the whole map, drawn from the disc's own map pictures (the
// stage library, see dw3/texrep/stage_library.h) and scaled down, with the part on screen outlined and a dot at its
// centre, where the camera keeps the party. The map and scroll come from the GPU renderer, which recognises them from
// the background sprites the game draws. No SDL dependency.
#pragma once

#include <cstdint>

#include "dw3/texrep/stage_library.h"
#include "osd.h"

namespace dw3app {

class Minimap {
public:
    // The picture for map `map` of `library` seen with its top-left at (scroll_x, scroll_y) and `view_w` x `view_h` map
    // pixels on screen, at most `size` pixels on each side. Null when the map has no picture. The map is drawn once
    // (when it or `size` changes); the outline and the dot are redrawn when the scroll changes. `serial()` changes
    // whenever the pixels do.
    const OsdImage* frame(dw3::texrep::StageLibrary& library, int map, int scroll_x, int scroll_y, int view_w, int view_h,
                          int size);
    uint64_t serial() const { return serial_; }

    // The map drawn whole at full size, RGBA, transparent where no cell has a picture. Exposed for tests.
    static dw3::texrep::Image compose(dw3::texrep::StageLibrary& library, int map);

private:
    bool build(dw3::texrep::StageLibrary& library, int map, int size);

    int map_ = -1, size_ = 0;
    bool ok_ = false;
    OsdImage base_;           // the framed map without the view marks
    int crop_x_ = 0, crop_y_ = 0;  // map pixel at the top-left of the drawn part
    float scale_ = 1.0f;      // panel pixels per map pixel
    int border_ = 0;          // panel pixels around the map
    OsdImage image_;
    int drawn_sx_ = 0, drawn_sy_ = 0, drawn_vw_ = 0, drawn_vh_ = 0;
    uint64_t serial_ = 0;
};

}  // namespace dw3app
