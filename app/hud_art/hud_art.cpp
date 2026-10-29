#include "hud_art.h"

#include "hud_coin_data.h"
#include "hud_frame_data.h"

namespace dw3app::hud_art {

static_assert(dw3::hud_art::hud_frame_size == 480 * 205 * 4, "hud_frame.rgba must be 480x205 RGBA");
static_assert(dw3::hud_art::hud_coin_size == 64 * 64 * 4, "hud_coin.rgba must be 64x64 RGBA");

// The arrays are 4-byte aligned (EmbedBinary.cmake) and little-endian RGBA bytes read as one word put red lowest.
Picture frame() { return {reinterpret_cast<const uint32_t*>(dw3::hud_art::hud_frame), 480, 205}; }
Picture coin() { return {reinterpret_cast<const uint32_t*>(dw3::hud_art::hud_coin), 64, 64}; }

}  // namespace dw3app::hud_art
