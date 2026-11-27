#include "dw3/forms_table.h"

#include <algorithm>
#include <cstring>

#include "dw3/recomp.h"

namespace dw3::host::forms_table {

namespace {

std::vector<uint8_t> g_extra;
uint32_t g_count = kOriginalCount;

uint32_t phys(uint32_t addr) { return addr & 0x1FFFFFu; }

int32_t find(RecompContext* ctx, uint16_t id) {
    for (uint32_t i = 0; i < g_count; ++i)
        if (dw3_read16(ctx, kTable + kRecordSize * i) == id) return static_cast<int32_t>(i);
    return -1;
}

// Both are leaves in the game (no calls, no stack use beyond their own frame), so they finish by returning.
void lookup_index(RecompContext* ctx) { ctx->r[2] = static_cast<uint32_t>(find(ctx, static_cast<uint16_t>(ctx->r[4]))); }
void lookup_record(RecompContext* ctx) {
    int32_t i = find(ctx, static_cast<uint16_t>(ctx->r[4]));
    ctx->r[2] = i < 0 ? 0 : kTable + kRecordSize * static_cast<uint32_t>(i);
}

}  // namespace

void attach(Machine& machine, std::vector<uint8_t> extra_records) {
    std::size_t n = std::min<std::size_t>(extra_records.size() / kRecordSize, kMaxCount - kOriginalCount);
    extra_records.resize(n * kRecordSize);
    g_extra = std::move(extra_records);
    g_count = kOriginalCount + static_cast<uint32_t>(n);
    machine.override_function(kLookupIndex, &lookup_index);
    machine.override_function(kLookupRecord, &lookup_record);
}

void install(Machine& machine) {
    uint8_t* ram = machine.ctx().ram;
    std::memmove(ram + phys(kTable), ram + phys(kOriginal), kOriginalCount * kRecordSize);
    if (!g_extra.empty()) std::memcpy(ram + phys(kTable) + kOriginalCount * kRecordSize, g_extra.data(), g_extra.size());
    std::memset(ram + phys(kTable) + g_count * kRecordSize, 0, (kMaxCount - g_count) * kRecordSize);
}

uint32_t count() { return g_count; }

}  // namespace dw3::host::forms_table
