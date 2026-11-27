#include "dw3/digipool.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "psxdisc/iso9660.h"

namespace dw3::host::digipool {

namespace {

constexpr uint32_t kSmdlFile = 446;              // AAA/PRO/SMDLDATA.PRO
constexpr uint32_t kPartnerBlockSize = 196;
constexpr uint32_t kEnemyBlockSize = 72;
constexpr uint8_t kFirstEnemyKind = 58;
constexpr uint16_t kDonorId = 0x17F;             // Kotemon: its partner block is the template for new ones
constexpr uint32_t kEnemyStatsSize = 70;         // AAA/PRO/SDIGIEDT.PRO records
constexpr uint32_t kHiddenRootLba = 473;         // the "AAA" tree, not linked from the ISO root
constexpr int kPartnerSize = 1450;               // target model extent for the partner camera

// Enemy ids that are boss cores or objects rather than Digimon.
bool excluded(uint16_t id) { return (id >= 0x1C9 && id <= 0x1D0) || id == 0x1D2 || id == 0x1D3; }

uint32_t rd32(std::span<const uint8_t> b, std::size_t o) {
    return uint32_t{b[o]} | uint32_t{b[o + 1]} << 8 | uint32_t{b[o + 2]} << 16 | uint32_t{b[o + 3]} << 24;
}
uint16_t rd16(std::span<const uint8_t> b, std::size_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }
int16_t rds16(std::span<const uint8_t> b, std::size_t o) { return static_cast<int16_t>(rd16(b, o)); }
void wr32(std::vector<uint8_t>& b, std::size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = static_cast<uint8_t>(v >> (8 * i));
}
void wr16(std::vector<uint8_t>& b, std::size_t o, uint16_t v) {
    b[o] = static_cast<uint8_t>(v);
    b[o + 1] = static_cast<uint8_t>(v >> 8);
}

// Offsets of the sub-files of a packed file (offset table up to the first sub-file).
bool sub_offsets(std::span<const uint8_t> d, std::vector<uint32_t>& offs) {
    offs.clear();
    if (d.size() < 4) return false;
    uint32_t first = rd32(d, 0);
    if (first == 0 || first % 4 || first > d.size()) return false;
    for (uint32_t i = 0; i < first; i += 4) offs.push_back(rd32(d, i));
    return true;
}

struct PartRef {
    uint32_t parent, geometry, animation;
};

bool parts_of(std::span<const uint8_t> m, const std::vector<uint32_t>& offs, uint32_t header_sub, std::vector<PartRef>& parts) {
    if (header_sub >= offs.size()) return false;
    std::size_t h = offs[header_sub];
    if (h + 8 > m.size()) return false;
    uint32_t count = rd32(m, h + 4);
    if (count == 0 || count > 64 || h + 8 + 12 * count > m.size()) return false;
    parts.clear();
    for (uint32_t k = 0; k < count; ++k) {
        PartRef p{rd32(m, h + 8 + 12 * k), rd32(m, h + 12 + 12 * k), rd32(m, h + 16 + 12 * k)};
        if (p.parent > count || p.geometry >= offs.size() || p.animation >= offs.size()) return false;
        parts.push_back(p);
    }
    return true;
}

}  // namespace

bool model_extent(std::span<const uint8_t> m, uint32_t header_sub, int& extent, int& root_height) {
    std::vector<uint32_t> offs;
    std::vector<PartRef> parts;
    if (!sub_offsets(m, offs) || !parts_of(m, offs, header_sub, parts)) return false;
    extent = 0;
    root_height = 0;
    for (const auto& p : parts) {
        std::size_t g = offs[p.geometry];
        if (g + 8 > m.size()) return false;
        uint32_t a = rd32(m, g), b = rd32(m, g + 4);
        for (std::size_t k = g + a; k + 8 <= std::min<std::size_t>(g + b, m.size()); k += 8)
            for (int c = 0; c < 3; ++c) extent = std::max(extent, std::abs(int{rds16(m, k + 2 * c)}));
    }
    for (const auto& p : parts) {
        if (p.parent) continue;
        std::size_t o = offs[p.animation];
        if (o + 16 > m.size()) return false;
        std::size_t t = o + rd32(m, o);
        if (t + 8 > m.size()) return false;
        root_height = std::max(root_height, std::abs(int{rds16(m, t + 2)}));
    }
    return true;
}

std::vector<uint8_t> scale_model(std::span<const uint8_t> model, uint32_t header_sub, float s) {
    std::vector<uint8_t> d(model.begin(), model.end());
    std::vector<uint32_t> offs;
    std::vector<PartRef> parts;
    if (!sub_offsets(d, offs) || !parts_of(d, offs, header_sub, parts)) return d;
    std::set<uint32_t> done;
    for (const auto& p : parts) {
        if (p.parent || !done.insert(p.animation).second) continue;
        std::size_t o = offs[p.animation];
        if (o + 16 > d.size()) continue;
        uint32_t t[4] = {rd32(d, o), rd32(d, o + 4), rd32(d, o + 8), rd32(d, o + 12)};
        // Translation keys [t0, t1) and scale keys [t2, t3): i16 x, y, z, u16 frame.
        for (auto [lo, hi] : {std::pair{t[0], t[1]}, std::pair{t[2], t[3]}}) {
            for (std::size_t k = o + lo; k + 8 <= o + hi && k + 8 <= d.size(); k += 8)
                for (int c = 0; c < 3; ++c) {
                    long v = std::lround(rds16(d, k + 2 * c) * s);
                    wr16(d, k + 2 * c, static_cast<uint16_t>(static_cast<int16_t>(std::clamp(v, -32768L, 32767L))));
                }
        }
    }
    return d;
}

bool build(psxdisc::Disc& disc, std::span<const uint8_t> exe, Build& out, std::string& error) {
    out = Build{};
    // Disc files by LBA, to find the foreign-language files the USA game never opens and the stats file.
    std::map<uint32_t, std::string> path_at;
    for (const auto& f : psxdisc::walk(disc, kHiddenRootLba, psxdisc::kForm1DataSize, "AAA/")) path_at[f.lba] = f.path;
    std::vector<uint32_t> spare;
    uint32_t stats_file = UINT32_MAX;
    for (uint32_t i = 0; i < kFileCount; ++i) {
        auto it = path_at.find(game_file_lba(exe, i));
        if (it == path_at.end()) continue;
        const std::string& p = it->second;
        if (p == "AAA/PRO/SDIGIEDT.PRO") stats_file = i;
        if (p.find("/COUNTRY/") != std::string::npos && p.find("/USA/") == std::string::npos &&
            p.find("CNTSEL") == std::string::npos)
            spare.push_back(i);
    }
    if (stats_file == UINT32_MAX) {
        error = "SDIGIEDT.PRO not found";
        return false;
    }
    std::vector<uint8_t> smdl = read_game_file(disc, exe, kSmdlFile);
    std::vector<uint8_t> stats = read_game_file(disc, exe, stats_file);
    if (smdl.size() < 16 || rd32(smdl, 0) != 0x80082448u) {
        error = "SMDLDATA.PRO not recognised";
        return false;
    }
    uint32_t hdr[4] = {rd32(smdl, 0), rd32(smdl, 4), rd32(smdl, 8), rd32(smdl, 12)};

    struct Entry {
        uint16_t id;
        uint8_t idx, kind;
    };
    std::vector<Entry> entries;
    for (std::size_t o = hdr[1]; o + 4 <= smdl.size(); o += 4) {
        uint16_t id = rd16(smdl, o);
        if (id == 0) break;
        entries.push_back({id, smdl[o + 2], smdl[o + 3]});
    }
    std::set<uint16_t> partner_ids;
    uint32_t partner_blocks = 0;
    const Entry* donor = nullptr;
    for (const auto& e : entries)
        if (e.kind < kFirstEnemyKind) {
            partner_ids.insert(e.id);
            partner_blocks = std::max<uint32_t>(partner_blocks, e.idx + 1u);
            if (e.id == kDonorId) donor = &e;
        }
    if (!donor) {
        error = "no donor partner entry";
        return false;
    }
    std::map<uint16_t, uint16_t> name_of;
    for (std::size_t o = 0; o + kEnemyStatsSize <= stats.size(); o += kEnemyStatsSize) {
        uint16_t id = rd16(stats, o);
        if (id == 0) break;
        name_of.emplace(id, rd16(stats, o + 6));
    }

    std::vector<Entry> added;
    std::vector<uint8_t> added_blocks;
    std::size_t next_spare = 0;
    std::set<uint16_t> seen;
    for (const auto& e : entries) {
        if (e.kind < kFirstEnemyKind || partner_ids.count(e.id) || excluded(e.id) || !seen.insert(e.id).second) continue;
        std::size_t eb = hdr[3] + std::size_t{e.idx} * kEnemyBlockSize;
        if (eb + kEnemyBlockSize > smdl.size()) continue;
        uint16_t header_sub = rd16(smdl, eb), model_file = rd16(smdl, eb + 2);
        bool same_file = rd16(smdl, eb + 6) == model_file && rd16(smdl, eb + 10) == model_file;
        if (!same_file || model_file >= kFileCount) {
            out.notes.push_back("id " + std::to_string(e.id) + ": model spread over several files");
            continue;
        }
        if (partner_blocks + added.size() > 255) {
            out.notes.push_back("id " + std::to_string(e.id) + ": no partner block left");
            continue;
        }
        if (next_spare >= spare.size()) {
            out.notes.push_back("id " + std::to_string(e.id) + ": no spare file left");
            continue;
        }
        std::vector<uint8_t> model = read_game_file(disc, exe, model_file);
        int extent = 0, root = 0;
        if (!model_extent(model, header_sub, extent, root)) {
            out.notes.push_back("id " + std::to_string(e.id) + ": model not recognised");
            continue;
        }
        float s = std::min(1.0f, static_cast<float>(kPartnerSize) / static_cast<float>(std::max({extent, root, 1})));
        uint32_t slot = spare[next_spare++];
        out.mod.files[slot] = scale_model(model, header_sub, s);

        std::vector<uint8_t> block(smdl.begin() + hdr[2] + std::size_t{donor->idx} * kPartnerBlockSize,
                                   smdl.begin() + hdr[2] + std::size_t{donor->idx} * kPartnerBlockSize + kPartnerBlockSize);
        std::memcpy(block.data(), smdl.data() + eb, 16);
        for (int k : {2, 6, 10}) wr16(block, k, static_cast<uint16_t>(slot));
        added_blocks.insert(added_blocks.end(), block.begin(), block.end());
        uint16_t form = form_id_for(e.id);
        added.push_back({form, static_cast<uint8_t>(partner_blocks + added.size()), donor->kind});
        auto nm = name_of.find(e.id);
        out.forms.push_back({e.id, form, nm == name_of.end() ? uint16_t{0} : nm->second, slot, s});
    }

    // New SMDLDATA: the original, then the whole list again with the new entries, then all partner blocks again
    // with the new ones; the header points at the copies. Block pointers are absolute, so nothing else moves.
    std::vector<uint8_t> nsm = smdl;
    uint32_t list_off = static_cast<uint32_t>(nsm.size());
    auto put_entry = [&](const Entry& e) {
        uint8_t b[4] = {static_cast<uint8_t>(e.id), static_cast<uint8_t>(e.id >> 8), e.idx, e.kind};
        nsm.insert(nsm.end(), b, b + 4);
    };
    for (const auto& e : entries) put_entry(e);
    for (const auto& e : added) put_entry(e);
    nsm.insert(nsm.end(), 4, 0);
    while (nsm.size() % 4) nsm.push_back(0);
    uint32_t blocks_off = static_cast<uint32_t>(nsm.size());
    nsm.insert(nsm.end(), smdl.begin() + hdr[2], smdl.begin() + hdr[2] + std::size_t{partner_blocks} * kPartnerBlockSize);
    nsm.insert(nsm.end(), added_blocks.begin(), added_blocks.end());
    wr32(nsm, 4, list_off);
    wr32(nsm, 8, blocks_off);
    out.mod.files[kSmdlFile] = std::move(nsm);
    return true;
}

bool build(const std::string& disc_path, Build& out, std::string& error) {
    auto disc = psxdisc::Disc::open(disc_path);
    if (!disc) {
        error = "cannot open disc image " + disc_path;
        return false;
    }
    auto root = psxdisc::read_root(*disc);
    std::vector<uint8_t> exe;
    if (root)
        for (const auto& f : psxdisc::walk(*disc, root->lba, root->size))
            if (f.path == "SLUS_014.36") exe = disc->read_file(f.lba, f.size);
    if (exe.size() < kExeHeaderSize + (kFileTableAddress - kExeLoadAddress) + 6 * kFileCount) {
        error = "SLUS_014.36 not found on disc";
        return false;
    }
    return build(*disc, exe, out, error);
}

}  // namespace dw3::host::digipool
