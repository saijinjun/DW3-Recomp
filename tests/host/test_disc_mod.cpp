#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

#include "dw3/digipool.h"
#include "dw3/disc_mod.h"
#include "game_disc.h"

using namespace dw3::host;

namespace {

struct FakeDisc : dw3::SectorSource {
    uint32_t count;
    explicit FakeDisc(uint32_t n) : count(n) {}
    uint32_t sector_count() const override { return count; }
    bool read_raw(uint32_t lba, uint8_t out[2352]) override {
        if (lba >= count) return false;
        std::memset(out, static_cast<int>(lba & 0xFF), 2352);
        return true;
    }
};

void put32(std::vector<uint8_t>& b, std::size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = static_cast<uint8_t>(v >> (8 * i));
}
void put16(std::vector<uint8_t>& b, std::size_t o, uint16_t v) {
    b[o] = static_cast<uint8_t>(v);
    b[o + 1] = static_cast<uint8_t>(v >> 8);
}
uint32_t get32(const std::vector<uint8_t>& b, std::size_t o) {
    return uint32_t{b[o]} | uint32_t{b[o + 1]} << 8 | uint32_t{b[o + 2]} << 16 | uint32_t{b[o + 3]} << 24;
}
uint16_t get16(const std::vector<uint8_t>& b, std::size_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }

constexpr std::size_t kTable = kFileTableAddress - kExeLoadAddress + kExeHeaderSize;

}  // namespace

TEST(ModSectorSource, ServesTheDiscThenAddedFilesAsForm1Sectors) {
    FakeDisc disc(100);
    ModSectorSource src(disc, 100);
    std::vector<uint8_t> data(3000);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7);
    EXPECT_EQ(src.add(data), 100u);
    EXPECT_EQ(src.sector_count(), 102u);

    uint8_t raw[2352];
    ASSERT_TRUE(src.read_raw(5, raw));
    EXPECT_EQ(raw[100], 5);
    ASSERT_TRUE(src.read_raw(101, raw));
    EXPECT_EQ(raw[0], 0x00);
    EXPECT_EQ(raw[1], 0xFF);
    EXPECT_EQ(raw[15], 2);           // Mode 2
    EXPECT_EQ(raw[18], 0x08);        // data submode, Form 1
    uint8_t m, s, f;
    psxdisc::lba_to_msf(101, m, s, f);
    EXPECT_EQ(raw[12], m);
    EXPECT_EQ(raw[13], s);
    EXPECT_EQ(raw[14], f);
    EXPECT_EQ(std::memcmp(raw + 24, data.data() + 2048, 3000 - 2048), 0);
    EXPECT_EQ(raw[24 + 3000 - 2048], 0);  // padding
    EXPECT_FALSE(src.read_raw(102, raw));
}

TEST(DiscMod, PointsTheFileTableAtTheAddedSectorsAndPatchesTheExe) {
    std::vector<uint8_t> exe(kTable + 6 * kFileCount + 64, 0);
    put32(exe, kTable + 4 * 7, 1234);
    put16(exe, kTable + 4 * kFileCount + 2 * 7, 3);
    FakeDisc disc(500);
    ModSectorSource src(disc, 500);
    DiscMod mod;
    mod.files[9] = std::vector<uint8_t>(5000, 1);
    mod.exe_patches[kFileTableAddress + 6 * kFileCount] = {0xAB, 0xCD};
    ASSERT_EQ(apply_disc_mod(mod, exe, src), "");
    EXPECT_EQ(get32(exe, kTable + 4 * 9), 500u);
    EXPECT_EQ(get16(exe, kTable + 4 * kFileCount + 2 * 9), 3u);
    EXPECT_EQ(get32(exe, kTable + 4 * 7), 1234u);  // untouched entry
    EXPECT_EQ(exe[kTable + 6 * kFileCount], 0xAB);
    EXPECT_EQ(src.sector_count(), 503u);

    DiscMod bad;
    bad.files[kFileCount] = {1};
    EXPECT_NE(apply_disc_mod(bad, exe, src), "");
}

// A two-part model (a root and its child): offset table, header sub-file, geometry, one animation per part.
TEST(Digipool, ScalesOnlyTheRootPartsKeys) {
    std::vector<uint8_t> m(16 + 32 + 2 * 24 + 2 * 40, 0);
    uint32_t hdr = 16, geo = 48, anim_root = 72, anim_child = 112;
    put32(m, 0, hdr);
    put32(m, 4, geo);
    put32(m, 8, anim_root);
    put32(m, 12, anim_child);
    put32(m, hdr + 0, 0);  // texture sub-file
    put32(m, hdr + 4, 2);  // parts
    put32(m, hdr + 8, 0);  // root: parent 0, geometry 1, animation 2
    put32(m, hdr + 12, 1);
    put32(m, hdr + 16, 2);
    // second part: child of the root (parents are 1-based), its own animation
    put32(m, hdr + 20, 1);
    put32(m, hdr + 24, 1);
    put32(m, hdr + 28, 3);
    // geometry at 48: vertex range [8, 16) relative, one vertex (1000, -2000, 300)
    put32(m, geo + 0, 8);
    put32(m, geo + 4, 16);
    put16(m, geo + 8, 1000);
    put16(m, geo + 10, static_cast<uint16_t>(-2000));
    put16(m, geo + 12, 300);
    for (uint32_t a : {anim_root, anim_child}) {
        put32(m, a + 0, 16);  // translation keys [16, 24)
        put32(m, a + 4, 24);  // rotation keys [24, 24): none
        put32(m, a + 8, 24);  // scale keys [24, 32)
        put32(m, a + 12, 32);
        put16(m, a + 16, 100);
        put16(m, a + 18, static_cast<uint16_t>(-400));
        put16(m, a + 20, 50);
        put16(m, a + 24, 4096);
    }
    int extent = 0, root = 0;
    ASSERT_TRUE(digipool::model_extent(m, 0, extent, root));
    EXPECT_EQ(extent, 2000);
    EXPECT_EQ(root, 400);

    auto s = digipool::scale_model(m, 0, 0.5f);
    EXPECT_EQ(static_cast<int16_t>(get16(s, anim_root + 18)), -200);  // root translation halved
    EXPECT_EQ(get16(s, anim_root + 16), 50);
    EXPECT_EQ(get16(s, anim_root + 24), 2048);                         // root scale key (in [t2, t3)) halved
    EXPECT_EQ(static_cast<int16_t>(get16(s, anim_child + 18)), -400);  // child untouched
    EXPECT_EQ(get16(s, geo + 8), 1000);                                // geometry untouched
}

TEST(Digipool, BuildsPartnerFormsFromTheRealDisc) {
    DW3_REQUIRE_DISC();
    digipool::Build b;
    std::string error;
    ASSERT_TRUE(digipool::build(dw3test::game_cue().string(), b, error)) << error;
    EXPECT_GE(b.forms.size(), 180u);
    ASSERT_TRUE(b.mod.files.count(446));  // SMDLDATA.PRO
    for (const auto& f : b.forms) {
        EXPECT_EQ(f.form_id, digipool::form_id_for(f.enemy_id));
        EXPECT_GT(f.name_index, 0u);
        EXPECT_TRUE(b.mod.files.count(f.model_file));
        EXPECT_GT(f.scale, 0.02f);
        EXPECT_LE(f.scale, 1.0f);
    }
}
