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
constexpr uint32_t kMaxCount = 240;  // up to 0x800072A0; the RAM after it is kept for the forms' DV conditions
constexpr uint32_t kNameIndex = 0x55;

constexpr uint32_t kRookieCount = 8;
constexpr uint32_t kDvPointers = 0x800072A0u;
constexpr uint32_t kDvConditions = 0x800072C0u;
constexpr uint32_t kDvConditionSize = 16;
constexpr uint32_t kDvConditionCount = 200;  // per rookie; ends at 0x8000D6C0
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

}  // namespace dw3::host::forms_table
