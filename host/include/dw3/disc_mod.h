#pragma once

// Changes to the game's files made on the device from the player's own disc (nothing derived from the disc is ever
// shipped). The disc image is left untouched: replaced files are served from sectors appended after its last one,
// and the boot executable's file table is pointed at them.

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "dw3/cdrom.h"
#include "psxdisc/disc.h"

namespace dw3::host {

// The game's file table in SLUS_014.36: kFileCount u32 start LBAs, then kFileCount u16 sizes in sectors.
constexpr uint32_t kExeLoadAddress = 0x80010000u;
constexpr uint32_t kExeHeaderSize = 0x800u;
constexpr uint32_t kFileTableAddress = 0x80044B78u;
constexpr uint32_t kFileCount = 2199;

struct DiscMod {
    // New contents for entries of the game's file table, by index.
    std::map<uint32_t, std::vector<uint8_t>> files;
    // Byte patches to the executable's loaded image, by RAM address (applied before the file table changes).
    std::map<uint32_t, std::vector<uint8_t>> exe_patches;
    // Forms added after the game's own in the forms table (whole 88-byte records, see forms_table.h).
    std::vector<uint8_t> extra_forms;

    bool empty() const { return files.empty() && exe_patches.empty() && extra_forms.empty(); }
};

// Serves the disc, plus the replaced files as Mode 2 Form 1 sectors from `first_lba` on.
class ModSectorSource final : public SectorSource {
public:
    ModSectorSource(SectorSource& base, uint32_t first_lba) : base_(base), first_(first_lba) {}
    // Appends `data` (padded to whole sectors); returns its first LBA.
    uint32_t add(std::span<const uint8_t> data);
    uint32_t sector_count() const override { return first_ + static_cast<uint32_t>(sectors_.size()); }
    bool read_raw(uint32_t lba, uint8_t out[2352]) override;

private:
    SectorSource& base_;
    uint32_t first_;
    std::vector<std::vector<uint8_t>> sectors_;  // 2048-byte user data each
};

// Applies `mod` to the executable image `exe` (with its 0x800-byte header) and adds the replaced files to `src`.
// Returns an error message, empty on success.
std::string apply_disc_mod(const DiscMod& mod, std::vector<uint8_t>& exe, ModSectorSource& src);

// Reads entry `index` of the game's file table straight from the disc, using the table in `exe`.
std::vector<uint8_t> read_game_file(psxdisc::Disc& disc, std::span<const uint8_t> exe, uint32_t index);
uint32_t game_file_lba(std::span<const uint8_t> exe, uint32_t index);

}  // namespace dw3::host
