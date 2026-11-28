#pragma once

// "More Digimon": makes the enemy-only Digimon on the player's disc usable as partner forms. Built on the device
// from the imported disc; nothing it produces is shipped.
//
// Battle models of player Digimon are found through AAA/PRO/SMDLDATA.PRO: a list of (id, block index, kind) where
// kind < 58 selects a 196-byte partner block and kind >= 58 a 72-byte enemy block. An enemy-only Digimon gets a new
// id with a partner entry and block: a copy of a donor partner block whose first 16 bytes (model sub-files, model
// file, animation sequence) come from its enemy block. Its model file is a copy with the root parts scaled down to
// partner size, served in place of an unused foreign-language file.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dw3/disc_mod.h"
#include "psxdisc/disc.h"

namespace dw3::host::digipool {

struct NewForm {
    uint16_t enemy_id = 0;   // the Digimon's id as an enemy
    uint16_t form_id = 0;    // its new id as a partner form
    uint16_t name_index = 0; // index into USDIGNAM (from its enemy stats)
    uint32_t model_file = 0; // file-table index serving its scaled model
    float scale = 1.0f;
    uint8_t stage = 0;       // 0 rookie, 1 champion, 2 ultimate, 3 mega
};

struct Build {
    DiscMod mod;  // includes the new forms' records (mod.extra_forms, in the order of `forms`)
    std::vector<NewForm> forms;
    std::vector<std::string> notes;  // Digimon left out and why
};

// The id a Digimon gets as a partner form.
constexpr uint16_t form_id_for(uint16_t enemy_id) { return static_cast<uint16_t>(0x200 + enemy_id); }

// Builds the partner entries and scaled models for every usable enemy-only Digimon. `exe` is SLUS_014.36 with its
// header. Returns false with `error` set when the disc does not look like DW3 (USA).
bool build(psxdisc::Disc& disc, std::span<const uint8_t> exe, Build& out, std::string& error);
// Same, opening the disc image at `disc_path` (a .cue or .bin).
bool build(const std::string& disc_path, Build& out, std::string& error);

// Model helpers (exposed for tests).
// Largest vertex coordinate of the model and height of its root parts, from the model's header sub-file.
bool model_extent(std::span<const uint8_t> model, uint32_t header_sub, int& extent, int& root_height);
// Copy of `model` with the translation and scale keys of its root parts multiplied by `s`.
std::vector<uint8_t> scale_model(std::span<const uint8_t> model, uint32_t header_sub, float s);

}  // namespace dw3::host::digipool
