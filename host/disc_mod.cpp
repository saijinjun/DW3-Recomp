#include "dw3/disc_mod.h"

#include <cstring>

#include "psxdisc/iso9660.h"

namespace dw3::host {

namespace {

uint32_t exe_offset(uint32_t address) { return address - kExeLoadAddress + kExeHeaderSize; }

uint32_t rd32(std::span<const uint8_t> b, uint32_t o) {
    return uint32_t{b[o]} | uint32_t{b[o + 1]} << 8 | uint32_t{b[o + 2]} << 16 | uint32_t{b[o + 3]} << 24;
}
uint16_t rd16(std::span<const uint8_t> b, uint32_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }
void wr32(std::vector<uint8_t>& b, uint32_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = static_cast<uint8_t>(v >> (8 * i));
}
void wr16(std::vector<uint8_t>& b, uint32_t o, uint16_t v) {
    b[o] = static_cast<uint8_t>(v);
    b[o + 1] = static_cast<uint8_t>(v >> 8);
}

}  // namespace

uint32_t ModSectorSource::add(std::span<const uint8_t> data) {
    uint32_t lba = sector_count();
    for (std::size_t off = 0; off < data.size() || off == 0; off += 2048) {
        std::vector<uint8_t> s(2048, 0);
        std::size_t n = std::min<std::size_t>(2048, data.size() - std::min(off, data.size()));
        if (n) std::memcpy(s.data(), data.data() + off, n);
        sectors_.push_back(std::move(s));
        if (data.empty()) break;
    }
    return lba;
}

bool ModSectorSource::read_raw(uint32_t lba, uint8_t out[2352]) {
    if (lba < first_) return base_.read_raw(lba, out);
    uint32_t i = lba - first_;
    if (i >= sectors_.size()) return false;
    std::memset(out, 0, 2352);
    static const uint8_t sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    std::memcpy(out, sync, 12);
    uint8_t m, s, f;
    psxdisc::lba_to_msf(lba, m, s, f);
    out[12] = m;
    out[13] = s;
    out[14] = f;
    out[15] = 2;  // Mode 2
    // XA subheader (two copies): file 0, channel 0, data sector, Form 1.
    out[18] = out[22] = psxdisc::kSubmodeData;
    std::memcpy(out + 24, sectors_[i].data(), 2048);
    return true;
}

uint32_t game_file_lba(std::span<const uint8_t> exe, uint32_t index) {
    return rd32(exe, exe_offset(kFileTableAddress) + 4 * index);
}

std::vector<uint8_t> read_game_file(psxdisc::Disc& disc, std::span<const uint8_t> exe, uint32_t index) {
    uint32_t lba = game_file_lba(exe, index);
    uint32_t sectors = rd16(exe, exe_offset(kFileTableAddress) + 4 * kFileCount + 2 * index);
    return disc.read_file(lba, sectors * 2048);
}

std::optional<uint32_t> game_file_index(psxdisc::Disc& disc, std::span<const uint8_t> exe, const std::string& path) {
    uint32_t lba = UINT32_MAX;
    for (const auto& f : psxdisc::walk(disc, kHiddenRootLba, psxdisc::kForm1DataSize, "AAA/"))
        if (f.path == path) lba = f.lba;
    if (lba == UINT32_MAX) return std::nullopt;
    for (uint32_t i = 0; i < kFileCount; ++i)
        if (game_file_lba(exe, i) == lba) return i;
    return std::nullopt;
}

std::string apply_disc_mod(const DiscMod& mod, std::vector<uint8_t>& exe, ModSectorSource& src) {
    for (const auto& [addr, bytes] : mod.exe_patches) {
        if (addr < kExeLoadAddress || exe_offset(addr) + bytes.size() > exe.size()) return "executable patch out of range";
        std::memcpy(exe.data() + exe_offset(addr), bytes.data(), bytes.size());
    }
    for (const auto& [index, data] : mod.files) {
        if (index >= kFileCount) return "file index out of range";
        uint32_t sectors = static_cast<uint32_t>((data.size() + 2047) / 2048);
        if (sectors > 0xFFFF) return "replacement file too large";
        uint32_t lba = src.add(data);
        wr32(exe, exe_offset(kFileTableAddress) + 4 * index, lba);
        wr16(exe, exe_offset(kFileTableAddress) + 4 * kFileCount + 2 * index, static_cast<uint16_t>(sectors));
    }
    return {};
}

}  // namespace dw3::host
