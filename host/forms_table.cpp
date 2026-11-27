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

// ---- Learned forms ----

constexpr uint32_t kRoster = 0x80048C50u, kRosterSize = 0x3DC, kSlotOffset = 0x50, kSlotSize = 20;
constexpr uint32_t kEquippedOffset = 0x48;  // 3 x s16
constexpr uint32_t kTotalSlots = kGameSlots + kExtSlots;
constexpr uint32_t kSlotCallback = 0x8005C448u;  // function pointer called with the id of a newly learned form
uint32_t g_page = 0;

uint8_t* slot_ptr(RecompContext* ctx, uint32_t p, uint32_t k) {
    if (k < kGameSlots) return ctx->ram + phys(kRoster + p * kRosterSize + kSlotOffset + k * kSlotSize);
    return ctx->ram + phys(kLearnedExt) + (p * kExtSlots + (k - kGameSlots)) * 4;
}
int16_t slot_id(RecompContext* ctx, uint32_t p, uint32_t k) {
    const uint8_t* s = slot_ptr(ctx, p, k);
    return static_cast<int16_t>(s[0] | s[1] << 8);
}
void set_slot_id(RecompContext* ctx, uint32_t p, uint32_t k, uint16_t id) {
    uint8_t* s = slot_ptr(ctx, p, k);
    s[0] = static_cast<uint8_t>(id);
    s[1] = static_cast<uint8_t>(id >> 8);
}
// Partners past the extension array only get the game's own slots.
uint32_t slots_for(uint32_t p) { return p < kExtPartners ? kTotalSlots : kGameSlots; }

// Slot of form `id` (ids below 3 are never forms), or -1.
int32_t find_slot(RecompContext* ctx, uint32_t p, int16_t id) {
    for (uint32_t k = 0; k < slots_for(p); ++k) {
        int16_t v = slot_id(ctx, p, k);
        if (v >= 3 && v == id) return static_cast<int32_t>(k);
    }
    return -1;
}
void put16(RecompContext* ctx, uint32_t addr, uint16_t v) { dw3_write16(ctx, addr, v); }

// 80017348: refresh the three equipped forms; out[] gets those still learned (-1 after them). Returns how many.
void learned_equipped(RecompContext* ctx) {
    uint32_t p = ctx->r[4], out = ctx->r[5], n = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        int16_t e = static_cast<int16_t>(dw3_read16(ctx, kRoster + p * kRosterSize + kEquippedOffset + 2 * i));
        if (e < 3) continue;
        int32_t k = find_slot(ctx, p, e);
        if (k < 0 || slot_id(ctx, p, static_cast<uint32_t>(k)) < 3) continue;
        put16(ctx, out + 2 * n, static_cast<uint16_t>(slot_id(ctx, p, static_cast<uint32_t>(k))));
        ++n;
    }
    for (uint32_t i = n; i < 3; ++i) put16(ctx, out + 2 * i, 0xFFFF);
    ctx->r[2] = n;
}
// 8001746C: set the equipped forms from ids[3] (-1 when not learned).
void learned_set_equipped(RecompContext* ctx) {
    uint32_t p = ctx->r[4], ids = ctx->r[5];
    for (uint32_t i = 0; i < 3; ++i) {
        int16_t id = static_cast<int16_t>(dw3_read16(ctx, ids + 2 * i));
        int32_t k = find_slot(ctx, p, id);
        put16(ctx, kRoster + p * kRosterSize + kEquippedOffset + 2 * i,
              k < 0 ? 0xFFFF : static_cast<uint16_t>(slot_id(ctx, p, static_cast<uint32_t>(k))));
    }
}
// 80017534: the learned ids into out[44] (zero padded): the window of the learned list for the current page.
void learned_list(RecompContext* ctx) {
    uint32_t p = ctx->r[4], out = ctx->r[5], seen = 0, n = 0;
    for (uint32_t k = 0; k < slots_for(p); ++k) {
        int16_t id = slot_id(ctx, p, k);
        if (id < 3) continue;
        if (seen++ < g_page * kGameSlots) continue;
        if (n < kGameSlots) put16(ctx, out + 2 * n++, static_cast<uint16_t>(id));
    }
    for (uint32_t i = n; i < kGameSlots; ++i) put16(ctx, out + 2 * i, 0);
    ctx->r[2] = n;
}
// 800175C0: learn form a1 if not yet; returns 1 when added.
void learned_add(RecompContext* ctx) {
    uint32_t p = ctx->r[4];
    int16_t id = static_cast<int16_t>(ctx->r[5]);
    ctx->r[2] = 0;
    if (find_slot(ctx, p, id) != -1) return;
    for (uint32_t k = 0; k < slots_for(p); ++k) {
        if (slot_id(ctx, p, k) != 0) continue;
        ctx->r[4] = static_cast<uint32_t>(static_cast<uint16_t>(id));
        dw3_call(ctx, dw3_read32(ctx, kSlotCallback));
        uint8_t* s = slot_ptr(ctx, p, k);
        s[0] = static_cast<uint8_t>(id);
        s[1] = static_cast<uint8_t>(id >> 8);
        s[2] = 1;
        ctx->r[2] = 1;
        return;
    }
}
// 800176B8 / 80017750: copy the 20-byte slot of form a1 to / from the buffer a2. Returns the slot or -1.
void learned_get(RecompContext* ctx) {
    uint32_t p = ctx->r[4], buf = ctx->r[6];
    int32_t k = find_slot(ctx, p, static_cast<int16_t>(ctx->r[5]));
    ctx->r[2] = static_cast<uint32_t>(k);
    if (k < 0) return;
    const uint8_t* s = slot_ptr(ctx, p, static_cast<uint32_t>(k));
    if (static_cast<uint32_t>(k) < kGameSlots) {
        for (uint32_t i = 0; i < kSlotSize; ++i) dw3_write8(ctx, buf + i, s[i]);
    } else {
        for (uint32_t i = 0; i < kSlotSize; ++i) dw3_write8(ctx, buf + i, i < 3 ? s[i] : (i == 4 ? s[3] : 0));
    }
}
void learned_put(RecompContext* ctx) {
    uint32_t p = ctx->r[4], buf = ctx->r[6];
    int32_t k = find_slot(ctx, p, static_cast<int16_t>(ctx->r[5]));
    ctx->r[2] = static_cast<uint32_t>(k);
    if (k < 0) return;
    uint8_t* s = slot_ptr(ctx, p, static_cast<uint32_t>(k));
    if (static_cast<uint32_t>(k) < kGameSlots) {
        for (uint32_t i = 0; i < kSlotSize; ++i) s[i] = static_cast<uint8_t>(dw3_read8(ctx, buf + i));
    } else {
        for (uint32_t i : {0u, 1u, 2u}) s[i] = static_cast<uint8_t>(dw3_read8(ctx, buf + i));
        s[3] = static_cast<uint8_t>(dw3_read8(ctx, buf + 4));
    }
}

}  // namespace

void set_page(uint32_t page) { g_page = page; }
uint32_t page() { return g_page; }

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
    machine.override_function(0x80017348u, &learned_equipped);
    machine.override_function(0x8001746Cu, &learned_set_equipped);
    machine.override_function(0x80017534u, &learned_list);
    machine.override_function(0x800175C0u, &learned_add);
    machine.override_function(0x800176B8u, &learned_get);
    machine.override_function(0x80017750u, &learned_put);
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
