#pragma once

// The game's forms table: one 88-byte record per Digimon a partner can be (the 8 rookies, then the 44 forms), with
// the id at +0 and the name index (into USDIGNAM) at +0x55. The executable has it at kOriginal; the recompiled code
// reads it at kTable instead (config/dw3.toml [[patch]]), in kernel RAM the HLE BIOS leaves unused, so that it can
// grow. The host copies the original there after the executable is loaded and after every save state load, followed
// by any extra forms, and answers the game's id lookups over the whole table.

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

// The game's lookups by id (called only through its function table): index of the record, or -1; and a pointer to
// the record, or 0.
constexpr uint32_t kLookupIndex = 0x80013484u;
constexpr uint32_t kLookupRecord = 0x800134C0u;

// Installs the overrides of the lookups on `machine` and remembers the extra records (whole 88-byte records).
// One machine per process, as with the other hooks.
void attach(Machine& machine, std::vector<uint8_t> extra_records);
// Writes the table into RAM: the original records from the loaded executable, then the extra ones.
void install(Machine& machine);
// Number of records the lookups search.
uint32_t count();

}  // namespace dw3::host::forms_table
