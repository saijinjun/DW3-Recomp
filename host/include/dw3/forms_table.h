#pragma once

// The game's forms table: one 88-byte record per Digimon a partner can be (the 8 rookies, then the 44 forms), with
// the id at +0 and the name index (into USDIGNAM) at +0x55. The executable has it at kOriginal; the recompiled code
// reads it at kTable instead (config/dw3.toml [[patch]]), in kernel RAM the HLE BIOS leaves unused, so that it can
// grow. The host copies the original there after the executable is loaded and after every save state load, followed
// by any extra forms, and answers the game's id lookups over the whole table.
//
// The digivolution conditions grow with it. After a battle, STFGTREP.PRO checks for each rookie a list of 44
// 16-byte conditions (form index + 1, up to two required forms with their levels, a stat requirement), found through
// a table of 8 pointers. The recompiled check reads the pointers at kDvPointers and kDvConditionCount entries per
// rookie instead; the host writes there the game's 44 (read from the disc), then the extra ones, then copies of the
// first entry as filler (a form already learned is skipped).

#include <cstdint>
#include <span>
#include <vector>

#include "dw3/machine.h"

namespace dw3::host::forms_table {

constexpr uint32_t kOriginal = 0x8003EB68u;
constexpr uint32_t kTable = 0x80002000u;
constexpr uint32_t kRecordSize = 88;
constexpr uint32_t kOriginalCount = 52;
constexpr uint32_t kMaxCount = 190;  // up to 0x80006150; the RAM after it is kept for the DV conditions and learned forms
constexpr uint32_t kNameIndex = 0x55;

constexpr uint32_t kRookieCount = 8;
constexpr uint32_t kDvPointers = 0x80006150u;
constexpr uint32_t kDvConditions = 0x80006180u;
constexpr uint32_t kDvConditionSize = 16;
constexpr uint32_t kDvConditionCount = 178;  // per rookie (44 + 134 new forms); the lists end at 0x8000BA80
constexpr uint32_t kOriginalConditionCount = 44;
// STFGTREP.PRO: loaded at kOverlayBase, its pointer table at kDvPointerTable.
constexpr uint32_t kOverlayBase = 0x80082448u;
constexpr uint32_t kDvPointerTable = 0x80088728u;

// The game's lookups by id (called only through its function table): index of the record, or -1; and a pointer to
// the record, or 0.
constexpr uint32_t kLookupIndex = 0x80013484u;
constexpr uint32_t kLookupRecord = 0x800134C0u;

// The game's digivolution conditions from STFGTREP.PRO: kRookieCount x kOriginalConditionCount entries, rookie by
// rookie. Empty when the file does not look as expected.
std::vector<uint8_t> original_conditions(std::span<const uint8_t> stfgtrep);

// Installs the overrides of the lookups on `machine` and remembers the extra records (whole 88-byte records) and the
// conditions (`original` from original_conditions(), `extra` per rookie). One machine per process, as with the other
// hooks.
void attach(Machine& machine, std::vector<uint8_t> extra_records, std::vector<uint8_t> original,
            const std::vector<uint8_t> (&extra)[kRookieCount]);
// Writes the table into RAM: the original records from the loaded executable, then the extra ones; and the
// conditions.
void install(Machine& machine);
// Number of records the lookups search.
uint32_t count();


// Learned forms. The game keeps, per partner (roster entry p, 0x3DC bytes from 0x80048C50), 44 slots of 20 bytes at
// +0x50 (id s16, DV level u8 at +2, a counter u32 at +4); the code touching them is six functions called through
// the table at 0x8004AC14. They are replaced by versions that treat slots 44.. as a second array of kExtSlots
// 4-byte entries (id s16, level u8, counter u8) per partner at kLearnedExt, so a partner can learn every form. The
// array is in RAM, so save states keep it; the game's own saves only hold the first 44.
constexpr uint32_t kGameSlots = 44;
constexpr uint32_t kExtSlots = 136;
constexpr uint32_t kExtPartners = 8;
constexpr uint32_t kLearnedExt = 0x8000BA80u;  // kExtPartners x kExtSlots x 4 bytes, ends at 0x8000CB80
// The forms list shown by the DV lab holds 44; page n shows learned forms 44n..44n+43.
void set_page(uint32_t page);
uint32_t page();

}  // namespace dw3::host::forms_table

// (appended below the namespace close in the original file; see the learned forms section)
