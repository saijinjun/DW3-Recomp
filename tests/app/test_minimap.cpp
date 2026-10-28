// The field minimap: the map drawn whole from its cell pictures, scaled into the panel, and the view marked on it.
// Runs on a synthetic map; nothing here needs the game's files.

#include <gtest/gtest.h>

#include "dw3/texrep/stage_library.h"
#include "present/minimap.h"

using dw3::texrep::StageLibrary;
using dw3::texrep::StagePack;
using dw3::texrep::StageTile;
using dw3app::Minimap;

namespace {

// Two cells side by side. Each picture is palette entry `index` everywhere; one full main-layer piece per cell.
StageTile tile(int col, uint8_t index, uint16_t colour) {
    StageTile t;
    t.col = col;
    t.width_words = 64;
    t.rows = 128;
    t.words.assign(64 * 128, static_cast<uint16_t>(index | (index << 8)));
    t.clut[index] = colour;
    t.block_hash = 10u + static_cast<uint64_t>(col);
    t.clut_hash = 3;
    dw3::texrep::StagePiece p;
    p.w = 128;
    p.h = 128;
    t.pieces = {p};
    return t;
}

StagePack pack() {
    StagePack p;
    p.cols = 2;
    p.rows = 1;
    p.tiles = {tile(0, 1, 0x001F), tile(1, 2, 0x7C00)};  // red, blue (15-bit BGR)
    return p;
}

StageLibrary& library() {
    static StageLibrary lib([](const std::string&, StagePack& out, std::string&) {
        out = pack();
        return true;
    });
    static bool added = [] {
        lib.add_map("S001", pack());
        lib.finish();
        return true;
    }();
    (void)added;
    return lib;
}

uint32_t at(const dw3app::OsdImage& img, int x, int y) { return img.rgba[static_cast<std::size_t>(y * img.w + x)]; }

}  // namespace

TEST(Minimap, ComposesTheCellsInPlace) {
    const auto full = Minimap::compose(library(), 0);
    ASSERT_EQ(full.w, 256);
    ASSERT_EQ(full.h, 128);
    const uint32_t left = full.rgba[64 * 256 + 64], right = full.rgba[64 * 256 + 192];
    EXPECT_EQ(left >> 24, 0xFFu);
    EXPECT_GT(left & 0xFF, 200u);               // red
    EXPECT_GT((right >> 16) & 0xFF, 200u);      // blue
    EXPECT_TRUE(Minimap::compose(library(), 5).rgba.empty());
}

TEST(Minimap, ScalesIntoThePanelAndMarksTheView) {
    Minimap m;
    const dw3app::OsdImage* img = m.frame(library(), 0, 0, 0, 128, 96, 100);
    ASSERT_NE(img, nullptr);
    EXPECT_LE(img->w, 100);
    EXPECT_LT(img->h, img->w);  // the map is twice as wide as it is tall
    const uint64_t serial = m.serial();
    m.frame(library(), 0, 0, 0, 128, 96, 100);
    EXPECT_EQ(m.serial(), serial);  // nothing moved
    // The dot sits at the middle of the view: the left half of the map, upper part.
    img = m.frame(library(), 0, 128, 0, 128, 96, 100);
    EXPECT_NE(m.serial(), serial);
    const int dot_x = img->w * 3 / 4, dot_y = img->h * 96 / 2 / 128;
    EXPECT_EQ(at(*img, dot_x, dot_y), 0xFF2850F0u);
    EXPECT_EQ(m.frame(library(), 7, 0, 0, 128, 96, 100), nullptr);
}
