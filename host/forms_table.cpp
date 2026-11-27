#include "dw3/forms_table.h"

#include <algorithm>
#include <cstring>

#include "dw3/recomp.h"

namespace dw3::host::forms_table {

namespace {

std::vector<uint8_t> g_extra;
uint32_t g_count = kOriginalCount;
std::vector<uint8_t> g_conditions;  // kRookieCount x kDvConditionCount entries, ready to copy

uint32_t rd32(std::span<const uint8_t> b, std::size_t o) {
    return uint32_t{b[o]} | uint32_t{b[o + 1]} << 8 | uint32_t{b[o + 2]} << 16 | uint32_t{b[o + 3]} << 24;
}

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

std::vector<uint8_t> original_conditions(std::span<const uint8_t> f) {
    constexpr std::size_t per_rookie = kOriginalConditionCount * kDvConditionSize;
    const std::size_t table = kDvPointerTable - kOverlayBase;
    if (f.size() < table + 4 * kRookieCount) return {};
    std::vector<uint8_t> out;
    for (uint32_t r = 0; r < kRookieCount; ++r) {
        uint32_t p = rd32(f, table + 4 * r);
        if (p < kOverlayBase || p - kOverlayBase + per_rookie > f.size()) return {};
        auto first = f.begin() + (p - kOverlayBase);
        out.insert(out.end(), first, first + per_rookie);
    }
    return out;
}

void attach(Machine& machine, std::vector<uint8_t> extra_records, std::vector<uint8_t> original,
            const std::vector<uint8_t> (&extra)[kRookieCount]) {
    std::size_t n = std::min<std::size_t>(extra_records.size() / kRecordSize, kMaxCount - kOriginalCount);
    extra_records.resize(n * kRecordSize);
    g_extra = std::move(extra_records);
    g_count = kOriginalCount + static_cast<uint32_t>(n);
    g_conditions.assign(std::size_t{kRookieCount} * kDvConditionCount * kDvConditionSize, 0);
    if (original.size() == std::size_t{kRookieCount} * kOriginalConditionCount * kDvConditionSize) {
        for (uint32_t r = 0; r < kRookieCount; ++r) {
            uint8_t* dst = g_conditions.data() + std::size_t{r} * kDvConditionCount * kDvConditionSize;
            std::memcpy(dst, original.data() + std::size_t{r} * kOriginalConditionCount * kDvConditionSize,
                        kOriginalConditionCount * kDvConditionSize);
            std::size_t more = std::min<std::size_t>(extra[r].size() / kDvConditionSize, kDvConditionCount - kOriginalConditionCount);
            std::memcpy(dst + kOriginalConditionCount * kDvConditionSize, extra[r].data(), more * kDvConditionSize);
            for (std::size_t i = kOriginalConditionCount + more; i < kDvConditionCount; ++i)
                std::memcpy(dst + i * kDvConditionSize, dst, kDvConditionSize);
        }
    }
    machine.override_function(kLookupIndex, &lookup_index);
    machine.override_function(kLookupRecord, &lookup_record);
}

void install(Machine& machine) {
    uint8_t* ram = machine.ctx().ram;
    std::memmove(ram + phys(kTable), ram + phys(kOriginal), kOriginalCount * kRecordSize);
    if (!g_extra.empty()) std::memcpy(ram + phys(kTable) + kOriginalCount * kRecordSize, g_extra.data(), g_extra.size());
    std::memset(ram + phys(kTable) + g_count * kRecordSize, 0, (kMaxCount - g_count) * kRecordSize);
    for (uint32_t r = 0; r < kRookieCount; ++r)
        dw3_write32(&machine.ctx(), kDvPointers + 4 * r, kDvConditions + r * kDvConditionCount * kDvConditionSize);
    if (!g_conditions.empty()) std::memcpy(ram + phys(kDvConditions), g_conditions.data(), g_conditions.size());
}

uint32_t count() { return g_count; }

}  // namespace dw3::host::forms_table
