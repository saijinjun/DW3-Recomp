#include "hud_art.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>

#include "battle_card_data.h"
#include "battle_enemy_data.h"

#include "hud_coin_data.h"
#include "hud_frame_data.h"
#include "minimap_frame_data.h"
#include "touch_dpad_data.h"
#include "touch_face_data.h"
#include "touch_pill_data.h"
#include "touch_shoulder_l_data.h"
#include "touch_shoulder_r_data.h"

namespace dw3app::hud_art {

static_assert(dw3::hud_art::hud_frame_size == 480 * 205 * 4, "hud_frame.rgba must be 480x205 RGBA");
static_assert(dw3::hud_art::hud_coin_size == 64 * 64 * 4, "hud_coin.rgba must be 64x64 RGBA");
static_assert(dw3::hud_art::touch_dpad_size == 256 * 256 * 4, "touch_dpad.rgba must be 256x256 RGBA");
static_assert(dw3::hud_art::touch_face_size == 128 * 128 * 4, "touch_face.rgba must be 128x128 RGBA");
static_assert(dw3::hud_art::touch_shoulder_l_size == 256 * 96 * 4, "touch_shoulder_l.rgba must be 256x96 RGBA");
static_assert(dw3::hud_art::touch_shoulder_r_size == 256 * 96 * 4, "touch_shoulder_r.rgba must be 256x96 RGBA");
static_assert(dw3::hud_art::battle_card_size == 512 * 102 * 4, "battle_card.rgba must be 512x102 RGBA");
static_assert(dw3::hud_art::battle_enemy_size == 512 * 66 * 4, "battle_enemy.rgba must be 512x66 RGBA");
static_assert(dw3::hud_art::minimap_frame_size == 343 * 318 * 4, "minimap_frame.rgba must be 343x318 RGBA");
static_assert(dw3::hud_art::touch_pill_size == 192 * 64 * 4, "touch_pill.rgba must be 192x64 RGBA");

// The arrays are 4-byte aligned (EmbedBinary.cmake) and little-endian RGBA bytes read as one word put red lowest.
#define DW3_PICTURE(name, symbol, w, h) \
    Picture name() { return {reinterpret_cast<const uint32_t*>(dw3::hud_art::symbol), w, h}; }
DW3_PICTURE(frame, hud_frame, 480, 205)
DW3_PICTURE(coin, hud_coin, 64, 64)
DW3_PICTURE(dpad, touch_dpad, 256, 256)
DW3_PICTURE(face, touch_face, 128, 128)
DW3_PICTURE(shoulder_l, touch_shoulder_l, 256, 96)
DW3_PICTURE(shoulder_r, touch_shoulder_r, 256, 96)
DW3_PICTURE(pill, touch_pill, 192, 64)
DW3_PICTURE(battle_card, battle_card, 512, 102)
DW3_PICTURE(battle_enemy, battle_enemy, 512, 66)
DW3_PICTURE(minimap_frame, minimap_frame, 343, 318)
#undef DW3_PICTURE

void blit(uint32_t* dst, int dw, int dh, const Picture& pic, int x0, int y0, int w, int h, Part part) {
    const int src_x0 = part.x0, src_x1 = part.x1 < 0 ? pic.w : part.x1;
    const int src_y0 = part.y0, src_y1 = part.y1 < 0 ? pic.h : part.y1;
    const int sw = src_x1 - src_x0, sh = src_y1 - src_y0;
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    for (int y = std::max(0, y0); y < std::min(dh, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(dw, x0 + w); ++x) {
            const float sx = std::clamp(static_cast<float>(src_x0) + (static_cast<float>(x - x0) + 0.5f) * static_cast<float>(sw) / static_cast<float>(w) - 0.5f,
                                        static_cast<float>(src_x0), static_cast<float>(src_x1 - 1));
            const float sy = std::clamp(static_cast<float>(src_y0) + (static_cast<float>(y - y0) + 0.5f) * static_cast<float>(sh) / static_cast<float>(h) - 0.5f,
                                        static_cast<float>(src_y0), static_cast<float>(src_y1 - 1));
            const int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
            const float fx = sx - static_cast<float>(ix), fy = sy - static_cast<float>(iy);
            float ch[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const int px = std::min(ix + (k & 1), pic.w - 1), py = std::min(iy + (k >> 1), pic.h - 1);
                const uint32_t c = pic.rgba[static_cast<std::size_t>(py) * static_cast<std::size_t>(pic.w) + static_cast<std::size_t>(px)];
                const float wgt = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
                const float a = static_cast<float>(c >> 24) / 255.0f;
                for (int i = 0; i < 3; ++i) ch[i] += static_cast<float>((c >> (8 * i)) & 0xFF) * a * wgt;  // premultiplied
                ch[3] += a * wgt;
            }
            if (ch[3] <= 0.0f) continue;
            uint32_t& d = dst[static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)];
            const float da = static_cast<float>(d >> 24) / 255.0f, sa = std::min(ch[3], 1.0f);
            const float oa = sa + da * (1.0f - sa);
            uint32_t out = static_cast<uint32_t>(std::lround(oa * 255.0f)) << 24;
            for (int i = 0; i < 3; ++i) {
                const float dc = static_cast<float>((d >> (8 * i)) & 0xFF);
                out |= static_cast<uint32_t>(std::clamp(std::lround((ch[i] + dc * da * (1.0f - sa)) / oa), 0l, 255l)) << (8 * i);
            }
            d = out;
        }
}

void blit_capped(uint32_t* dst, int dw, int dh, const Picture& pic, int cap, int x0, int y0, int w, int h) {
    const int dcap = std::min(w / 2, static_cast<int>(std::lround(static_cast<double>(cap) * h / pic.h)));
    blit(dst, dw, dh, pic, x0, y0, dcap, h, {0, 0, cap, -1});
    blit(dst, dw, dh, pic, x0 + dcap, y0, w - 2 * dcap, h, {cap, 0, pic.w - cap, -1});
    blit(dst, dw, dh, pic, x0 + w - dcap, y0, dcap, h, {pic.w - cap, 0, pic.w, -1});
}

void blit_nine(uint32_t* dst, int dw, int dh, const Picture& pic, int corner, int x0, int y0, int w, int h, int dst_corner) {
    const int dc = std::min({dst_corner, w / 2, h / 2});
    const int sx[4] = {0, corner, pic.w - corner, pic.w}, sy[4] = {0, corner, pic.h - corner, pic.h};
    const int dx[4] = {x0, x0 + dc, x0 + w - dc, x0 + w}, dy[4] = {y0, y0 + dc, y0 + h - dc, y0 + h};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            blit(dst, dw, dh, pic, dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j], {sx[i], sy[j], sx[i + 1], sy[j + 1]});
}

}  // namespace dw3app::hud_art
