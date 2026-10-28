// A small map of the field being walked (gameplay.minimap): the whole map, drawn from the disc's own map pictures (the
// stage library, see dw3/texrep/stage_library.h) and scaled down, with the part on screen outlined and a dot where the
// party stands. The map and scroll come from the GPU renderer, which recognises them from the background sprites the
// game draws; the party's position in the field is in map pixels too. No SDL dependency.
#pragma once

#include <cstdint>
#include <future>

#include "dw3/texrep/stage_library.h"
#include "osd.h"

namespace dw3app {

class Minimap {
public:
    // The picture for map `map` of `library` seen with its top-left at (scroll_x, scroll_y) and `view_w` x `view_h` map
    // pixels on screen, with the party at map pixel (party_x, party_y), at most `size` pixels on each side. Null when the map has no picture, and while it is being
    // drawn: that happens once (when the map or `size` changes) on another thread, as reading a large map's pictures
    // from the disc takes a while; the outline and the dot are redrawn when the scroll changes. `serial()` changes
    // whenever the pixels do. `library` must outlive the drawing.
    const OsdImage* frame(dw3::texrep::StageLibrary& library, int map, int scroll_x, int scroll_y, int view_w, int view_h,
                          int party_x, int party_y, int size);
    uint64_t serial() const { return serial_; }

    // The map drawn whole at full size, RGBA, transparent where no cell has a picture. Exposed for tests.
    static dw3::texrep::Image compose(dw3::texrep::StageLibrary& library, int map);

    // Waits for a drawing in progress (tests).
    void wait() {
        if (pending_.valid()) pending_.wait();
    }

private:
    struct Base {
        bool ok = false;
        OsdImage image;             // the framed map without the view marks
        int crop_x = 0, crop_y = 0; // map pixel at the top-left of the drawn part
        float scale = 1.0f;         // panel pixels per map pixel
        int border = 0;             // panel pixels around the map
    };
    static Base build(dw3::texrep::StageLibrary& library, int map, int size);

    int map_ = -1, size_ = 0;
    std::future<Base> pending_;
    Base base_;
    OsdImage image_;
    int drawn_sx_ = 0, drawn_sy_ = 0, drawn_vw_ = 0, drawn_vh_ = 0, drawn_px_ = 0, drawn_py_ = 0;
    uint64_t serial_ = 0;
};

}  // namespace dw3app
