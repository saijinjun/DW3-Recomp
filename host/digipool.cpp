#include "dw3/digipool.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#include "dw3/forms_table.h"
#include "psxdisc/iso9660.h"

namespace dw3::host::digipool {

namespace {

constexpr uint32_t kSmdlFile = 446;              // AAA/PRO/SMDLDATA.PRO
constexpr uint32_t kPartnerBlockSize = 196;
constexpr uint32_t kEnemyBlockSize = 72;
constexpr uint8_t kFirstEnemyKind = 58;
constexpr uint16_t kDonorId = 0x17F;             // Kotemon: its partner block is the template for new ones
constexpr uint32_t kEnemyStatsSize = 70;         // AAA/PRO/SDIGIEDT.PRO records
constexpr int kPartnerSize = 1450;               // target model extent for the partner camera


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

enum class Stage : uint8_t { Rookie, Champion, Ultimate, Mega };

// Stage of each enemy-only species that becomes a form (canon stages). Species not listed are left out: colour
// variants of a listed one, enemy versions of the game's own partner forms, and boss cores.
struct Species {
    uint16_t id;
    Stage stage;
};
constexpr Species kSpecies[] = {
    {0x020, Stage::Rookie},  // Kunemon
    {0x0CE, Stage::Rookie},  // Tapirmon
    {0x0C5, Stage::Rookie},  // Crabmon
    {0x084, Stage::Rookie},  // Gizamon
    {0x0D9, Stage::Rookie},  // DemiDevimon
    {0x004, Stage::Rookie},  // Betamon
    {0x019, Stage::Champion},  // Vegiemon
    {0x033, Stage::Champion},  // Kuwagamon
    {0x043, Stage::Rookie},  // Yanmamon
    {0x0C6, Stage::Champion},  // Kiwimon
    {0x0A6, Stage::Champion},  // Dokugumon
    {0x050, Stage::Rookie},  // Goburimon
    {0x115, Stage::Ultimate},  // Baronmon
    {0x0AC, Stage::Champion},  // Minotarumon
    {0x089, Stage::Champion},  // Flymon
    {0x0DE, Stage::Champion},  // Woodmon
    {0x008, Stage::Champion},  // Tyrannomon
    {0x0CB, Stage::Ultimate},  // Triceramon
    {0x007, Stage::Champion},  // Airdramon
    {0x06E, Stage::Champion},  // Gekomon
    {0x031, Stage::Champion},  // Coelamon
    {0x025, Stage::Champion},  // Bakemon
    {0x0DF, Stage::Ultimate},  // Phantomon
    {0x032, Stage::Champion},  // Kokatorimon
    {0x0CF, Stage::Champion},  // Apemon
    {0x023, Stage::Champion},  // Shellmon
    {0x0D0, Stage::Champion},  // Tortomon
    {0x0D2, Stage::Champion},  // Dolphmon
    {0x0DC, Stage::Champion},  // Gesomon
    {0x0DD, Stage::Champion},  // Octomon
    {0x0F1, Stage::Champion},  // Divermon
    {0x077, Stage::Ultimate},  // ShogunGekomon
    {0x00A, Stage::Champion},  // Seadramon
    {0x0D4, Stage::Ultimate},  // Mammothmon
    {0x02A, Stage::Ultimate},  // Etemon
    {0x022, Stage::Champion},  // Ogremon
    {0x088, Stage::Champion},  // Tuskmon
    {0x06C, Stage::Champion},  // Rockmon
    {0x111, Stage::Ultimate},  // Quetzalmon
    {0x01C, Stage::Ultimate},  // Vademon
    {0x14E, Stage::Ultimate},  // Maildramon
    {0x00D, Stage::Ultimate},  // Mamemon
    {0x1B3, Stage::Champion},  // Guardromon
    {0x0B0, Stage::Champion},  // Thundermon
    {0x07A, Stage::Rookie},  // Hagurumon
    {0x08F, Stage::Champion},  // Clockmon
    {0x028, Stage::Ultimate},  // Andromon
    {0x00B, Stage::Champion},  // Numemon
    {0x027, Stage::Champion},  // Sukamon
    {0x0E3, Stage::Ultimate},  // Garbagemon
    {0x0AB, Stage::Champion},  // Roachmon
    {0x08B, Stage::Champion},  // Raremon
    {0x086, Stage::Champion},  // Cyclonemon
    {0x036, Stage::Ultimate},  // Megadramon
    {0x00E, Stage::Ultimate},  // Monzaemon
    {0x04C, Stage::Champion},  // Tankmon
    {0x035, Stage::Champion},  // Nanimon
    {0x0AD, Stage::Champion},  // Musyamon
    {0x0AF, Stage::Champion},  // Kurisarimon
    {0x0EE, Stage::Champion},  // Snimon
    {0x0C8, Stage::Ultimate},  // Blossomon
    {0x079, Stage::Ultimate},  // Cherrymon
    {0x12E, Stage::Champion},  // Gargoylemon
    {0x16C, Stage::Ultimate},  // Mummymon
    {0x16D, Stage::Ultimate},  // Arukenimon
    {0x0E2, Stage::Ultimate},  // Okuwamon
    {0x08C, Stage::Ultimate},  // MetalTyrannomon
    {0x08A, Stage::Champion},  // Deltamon
    {0x018, Stage::Champion},  // Whamon
    {0x0E0, Stage::Ultimate},  // MarineDevimon
    {0x0E1, Stage::Mega},  // Dragomon
    {0x03D, Stage::Ultimate},  // MegaSeadramon
    {0x0A5, Stage::Ultimate},  // Ebidramon
    {0x0AA, Stage::Champion},  // Vilemon
    {0x0F4, Stage::Ultimate},  // SkullSatamon
    {0x07E, Stage::Ultimate},  // Brachiomon
    {0x026, Stage::Champion},  // Drimogemon
    {0x045, Stage::Champion},  // Flarerizamon
    {0x009, Stage::Champion},  // Meramon
    {0x034, Stage::Champion},  // Mojyamon
    {0x017, Stage::Champion},  // Frigimon
    {0x05E, Stage::Champion},  // Icemon
    {0x029, Stage::Ultimate},  // Giromon
    {0x10E, Stage::Mega},  // Vikemon
    {0x147, Stage::Champion},  // Lynxmon
    {0x0BD, Stage::Ultimate},  // SkullMeramon
    {0x087, Stage::Champion},  // Devidramon
    {0x0ED, Stage::Ultimate},  // Kimeramon
    {0x0E5, Stage::Mega},  // Pukumon
    {0x0CC, Stage::Mega},  // MetalSeadramon
    {0x119, Stage::Champion},  // Tylomon
    {0x0CA, Stage::Ultimate},  // Scorpiomon
    {0x168, Stage::Ultimate},  // Antylamon
    {0x03C, Stage::Mega},  // H0MKabuterimon
    {0x0E7, Stage::Mega},  // Puppetmon
    {0x0D7, Stage::Mega},  // SkullMammothmon
    {0x10D, Stage::Mega},  // Valkyrimon
    {0x0B1, Stage::Ultimate},  // LadyDevimon
    {0x041, Stage::Mega},  // MetalEtemon
    {0x0FA, Stage::Mega},  // KingEtemon
    {0x0C1, Stage::Mega},  // Gryphonmon
    {0x0F5, Stage::Ultimate},  // Infermon
    {0x110, Stage::Champion},  // Shadramon
    {0x0BE, Stage::Mega},  // Boltmon
    {0x0E4, Stage::Mega},  // Piedmon
    {0x073, Stage::Mega},  // Machinedramon
    {0x0EC, Stage::Mega},  // VenomMyotismon
    {0x0FB, Stage::Mega},  // Apokarimon
    {0x138, Stage::Mega},  // Ghoulmon
    {0x0F7, Stage::Mega},  // Creepymon
    {0x17E, Stage::Mega},  // Armageddemon
    {0x04D, Stage::Champion},  // RedVegiemon
    {0x059, Stage::Champion},  // Fugamon
    {0x068, Stage::Ultimate},  // BlueMeramon
    {0x166, Stage::Mega},  // Pharaohmon
    {0x07C, Stage::Ultimate},  // MasterTyrannomon
    {0x151, Stage::Mega},  // Zanbamon
    {0x164, Stage::Mega},  // HiAndromon
    {0x046, Stage::Ultimate},  // WaruMonzaemon
    {0x08D, Stage::Ultimate},  // Datamon
    {0x0B2, Stage::Ultimate},  // Knightmon
    {0x1B4, Stage::Ultimate},  // Bulbmon
    {0x1B5, Stage::Ultimate},  // Persiamon
    {0x1B6, Stage::Ultimate},  // Raidenmon
    {0x1B7, Stage::Ultimate},  // Fujinmon
    {0x1B8, Stage::Ultimate},  // Suijinmon
    {0x1B9, Stage::Ultimate},  // Raijinmon
    {0x1BA, Stage::Mega},  // Galacticmon
    {0x1BF, Stage::Mega},  // BK00Seraphimon
    {0x1BB, Stage::Mega},  // BK00MegaGargomon
    {0x1BC, Stage::Mega},  // BK00Imperialdramon
    {0x1BD, Stage::Ultimate},  // BK00WarGrowlmon
    {0x1BE, Stage::Mega},  // BK00KingNumemon
    {0x1D1, Stage::Mega},  // Vemmon
};

// The game's own forms by stage, used as templates for the new records.
constexpr uint16_t kChampions[] = {0x182, 0x103, 0x183, 0x176, 0x14, 0x16F, 0x184, 0x5, 0x104, 0xEA};
constexpr uint16_t kUltimates[] = {0x186, 0x187, 0x185, 0xD3, 0xC, 0x1B, 0x177, 0x13, 0x38, 0x170, 0xFE, 0x1A, 0x6};
constexpr uint16_t kMegas[] = {0x188, 0x189, 0x18A, 0xD5, 0x94, 0x171, 0x178, 0xD6, 0xC4, 0x90, 0x10B, 0x3B, 0x42};

// Forms table record (88 bytes): id; str, def, spt, wis, spd, chr; 7 resistances (fire, water, ice, wind, thunder,
// machine, dark); attack; 5 techs; signature tech; DNA tech; ...; DNA partner index at +0x3D; name index at +0x55.
constexpr std::size_t kRecStats = 2, kRecRes = 0x0E, kRecAttack = 0x1C, kRecSignature = 0x28, kRecDnaTech = 0x2A,
                      kRecDnaIndex = 0x3D, kRecName = 0x55;
// Enemy stats record (70 bytes): id, drop item, drop rate, name index, attack, move 1, move 2, str, def, spt, wis,
// spd, 7 resistances, ...
constexpr std::size_t kEnName = 6, kEnAttack = 8, kEnMove1 = 10, kEnStats = 14, kEnRes = 24;

std::span<const uint16_t> templates_for(Stage s) {
    switch (s) {
        case Stage::Ultimate: return kUltimates;
        case Stage::Mega: return kMegas;
        default: return kChampions;
    }
}

uint8_t stage_index(Stage s) { return static_cast<uint8_t>(s); }

// Index of the highest of `n` signed 16-bit values at `o`.
int highest(std::span<const uint8_t> b, std::size_t o, int n) {
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (rds16(b, o + 2 * i) > rds16(b, o + 2 * best)) best = i;
    return best;
}

// Canon predecessor of each new form: the Digimon (a new form's id, or one of the game's own) it digivolves from.
// Forms not listed, and ones whose predecessor is one of the 8 rookies, need only a partner level.
struct Line {
    uint16_t form;
    uint16_t from;
};
constexpr Line kLines[] = {
    {0x219, 0x204},  // Vegiemon <- Betamon
    {0x233, 0x220},  // Kuwagamon <- Kunemon
    {0x2C6, 0x2CE},  // Kiwimon <- Tapirmon
    {0x2A6, 0x220},  // Dokugumon <- Kunemon
    {0x2AC, 0x250},  // Minotarumon <- Goburimon
    {0x289, 0x220},  // Flymon <- Kunemon
    {0x2DE, 0x2CE},  // Woodmon <- Tapirmon
    {0x208, 0x003},  // Tyrannomon <- Agumon
    {0x207, 0x27A},  // Airdramon <- Hagurumon
    {0x26E, 0x204},  // Gekomon <- Betamon
    {0x231, 0x284},  // Coelamon <- Gizamon
    {0x225, 0x2D9},  // Bakemon <- DemiDevimon
    {0x232, 0x2CE},  // Kokatorimon <- Tapirmon
    {0x2CF, 0x2CE},  // Apemon <- Tapirmon
    {0x223, 0x284},  // Shellmon <- Gizamon
    {0x2D0, 0x284},  // Tortomon <- Gizamon
    {0x2D2, 0x284},  // Dolphmon <- Gizamon
    {0x2DC, 0x2C5},  // Gesomon <- Crabmon
    {0x2DD, 0x2C5},  // Octomon <- Crabmon
    {0x2F1, 0x2C5},  // Divermon <- Crabmon
    {0x20A, 0x204},  // Seadramon <- Betamon
    {0x222, 0x250},  // Ogremon <- Goburimon
    {0x288, 0x250},  // Tuskmon <- Goburimon
    {0x26C, 0x250},  // Rockmon <- Goburimon
    {0x3B3, 0x27A},  // Guardromon <- Hagurumon
    {0x2B0, 0x27A},  // Thundermon <- Hagurumon
    {0x28F, 0x27A},  // Clockmon <- Hagurumon
    {0x20B, 0x204},  // Numemon <- Betamon
    {0x227, 0x204},  // Sukamon <- Betamon
    {0x2AB, 0x220},  // Roachmon <- Kunemon
    {0x28B, 0x204},  // Raremon <- Betamon
    {0x286, 0x220},  // Cyclonemon <- Kunemon
    {0x24C, 0x27A},  // Tankmon <- Hagurumon
    {0x235, 0x204},  // Nanimon <- Betamon
    {0x2AD, 0x250},  // Musyamon <- Goburimon
    {0x2AF, 0x220},  // Kurisarimon <- Kunemon
    {0x2EE, 0x220},  // Snimon <- Kunemon
    {0x32E, 0x2D9},  // Gargoylemon <- DemiDevimon
    {0x28A, 0x2D9},  // Deltamon <- DemiDevimon
    {0x218, 0x284},  // Whamon <- Gizamon
    {0x2AA, 0x2D9},  // Vilemon <- DemiDevimon
    {0x226, 0x250},  // Drimogemon <- Goburimon
    {0x245, 0x250},  // Flarerizamon <- Goburimon
    {0x209, 0x250},  // Meramon <- Goburimon
    {0x234, 0x250},  // Mojyamon <- Goburimon
    {0x217, 0x2CE},  // Frigimon <- Tapirmon
    {0x25E, 0x2CE},  // Icemon <- Tapirmon
    {0x347, 0x2CE},  // Lynxmon <- Tapirmon
    {0x287, 0x2D9},  // Devidramon <- DemiDevimon
    {0x319, 0x284},  // Tylomon <- Gizamon
    {0x310, 0x27A},  // Shadramon <- Hagurumon
    {0x24D, 0x2CE},  // RedVegiemon <- Tapirmon
    {0x259, 0x250},  // Fugamon <- Goburimon
    {0x315, 0x182},  // Baronmon <- Dinohumon
    {0x2CB, 0x208},  // Triceramon <- Tyrannomon
    {0x2DF, 0x225},  // Phantomon <- Bakemon
    {0x277, 0x26E},  // ShogunGekomon <- Gekomon
    {0x2D4, 0x288},  // Mammothmon <- Tuskmon
    {0x22A, 0x222},  // Etemon <- Ogremon
    {0x311, 0x207},  // Quetzalmon <- Airdramon
    {0x21C, 0x27A},  // Vademon <- Hagurumon
    {0x34E, 0x207},  // Maildramon <- Airdramon
    {0x20D, 0x28F},  // Mamemon <- Clockmon
    {0x228, 0x3B3},  // Andromon <- Guardromon
    {0x2E3, 0x227},  // Garbagemon <- Sukamon
    {0x236, 0x287},  // Megadramon <- Devidramon
    {0x20E, 0x20B},  // Monzaemon <- Numemon
    {0x2C8, 0x2DE},  // Blossomon <- Woodmon
    {0x279, 0x2DE},  // Cherrymon <- Woodmon
    {0x36C, 0x28B},  // Mummymon <- Raremon
    {0x36D, 0x2A6},  // Arukenimon <- Dokugumon
    {0x2E2, 0x233},  // Okuwamon <- Kuwagamon
    {0x28C, 0x208},  // MetalTyrannomon <- Tyrannomon
    {0x2E0, 0x2DD},  // MarineDevimon <- Octomon
    {0x23D, 0x20A},  // MegaSeadramon <- Seadramon
    {0x2A5, 0x2F1},  // Ebidramon <- Divermon
    {0x2F4, 0x32E},  // SkullSatamon <- Gargoylemon
    {0x27E, 0x2D0},  // Brachiomon <- Tortomon
    {0x229, 0x28F},  // Giromon <- Clockmon
    {0x2BD, 0x209},  // SkullMeramon <- Meramon
    {0x2ED, 0x287},  // Kimeramon <- Devidramon
    {0x2CA, 0x233},  // Scorpiomon <- Kuwagamon
    {0x368, 0x347},  // Antylamon <- Lynxmon
    {0x2B1, 0x2AA},  // LadyDevimon <- Vilemon
    {0x2F5, 0x2AA},  // Infermon <- Vilemon
    {0x268, 0x209},  // BlueMeramon <- Meramon
    {0x27C, 0x28C},  // MasterTyrannomon <- MetalTyrannomon
    {0x246, 0x20B},  // WaruMonzaemon <- Numemon
    {0x28D, 0x3B3},  // Datamon <- Guardromon
    {0x2B2, 0x3B3},  // Knightmon <- Guardromon
    {0x3B4, 0x2DE},  // Bulbmon <- Woodmon
    {0x3B5, 0x347},  // Persiamon <- Lynxmon
    {0x3B6, 0x2B0},  // Raidenmon <- Thundermon
    {0x3B7, 0x286},  // Fujinmon <- Cyclonemon
    {0x3B8, 0x319},  // Suijinmon <- Tylomon
    {0x3B9, 0x2B0},  // Raijinmon <- Thundermon
    {0x3BD, 0x16F},  // BK00WarGrowlmon <- Growlmon
    {0x2E1, 0x2E0},  // Dragomon <- MarineDevimon
    {0x30E, 0x218},  // Vikemon <- Whamon
    {0x2E5, 0x218},  // Pukumon <- Whamon
    {0x2CC, 0x23D},  // MetalSeadramon <- MegaSeadramon
    {0x23C, 0x013},  // H0MKabuterimon <- Kabuterimon
    {0x2E7, 0x2DE},  // Puppetmon <- Woodmon
    {0x2D7, 0x2D4},  // SkullMammothmon <- Mammothmon
    {0x30D, 0x2B1},  // Valkyrimon <- LadyDevimon
    {0x241, 0x22A},  // MetalEtemon <- Etemon
    {0x2FA, 0x22A},  // KingEtemon <- Etemon
    {0x2C1, 0x311},  // Gryphonmon <- Quetzalmon
    {0x2BE, 0x228},  // Boltmon <- Andromon
    {0x2E4, 0x36C},  // Piedmon <- Mummymon
    {0x273, 0x228},  // Machinedramon <- Andromon
    {0x2EC, 0x042},  // VenomMyotismon <- Myotismon
    {0x2FB, 0x2E4},  // Apokarimon <- Piedmon
    {0x338, 0x2DF},  // Ghoulmon <- Phantomon
    {0x2F7, 0x2DF},  // Creepymon <- Phantomon
    {0x37E, 0x2E1},  // Armageddemon <- Dragomon
    {0x366, 0x36C},  // Pharaohmon <- Mummymon
    {0x351, 0x2B2},  // Zanbamon <- Knightmon
    {0x364, 0x228},  // HiAndromon <- Andromon
    {0x3BA, 0x2BE},  // Galacticmon <- Boltmon
    {0x3BF, 0x014},  // BK00Seraphimon <- Angemon
    {0x3BB, 0x189},  // BK00MegaGargomon <- Cannondramon
    {0x3BC, 0x3C2},  // BK00Imperialdramon <- Paildramon
    {0x3BE, 0x20E},  // BK00KingNumemon <- Monzaemon
};

// A digivolution condition (16 bytes, as STFGTREP.PRO has them): the form's index + 1; up to two required forms
// (index + 1) with the level each must have reached; a requirement type (7: partner level) and its value.
void put_condition(std::vector<uint8_t>& out, uint32_t record_index, int from_record, int from_level, int partner_level) {
    uint8_t c[16] = {};
    auto put = [&](int o, uint32_t v, int n) {
        for (int i = 0; i < n; ++i) c[o + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put(0, record_index + 1, 4);
    put(4, from_record >= 0 ? static_cast<uint32_t>(from_record) + 1 : 0, 2);
    put(6, from_record >= 0 ? static_cast<uint32_t>(from_level) : 1, 2);
    put(10, 1, 2);  // no second form
    put(12, from_record >= 0 ? 0 : 7, 2);
    put(14, from_record >= 0 ? 1 : static_cast<uint32_t>(partner_level), 2);
    out.insert(out.end(), c, c + 16);
}

// A form record for enemy record `en` (70 bytes), from the game's own form `tpl` of the same stage: the template's
// stat and resistance totals, spread like the enemy's; the enemy's attack, and its first move as the signature.
std::vector<uint8_t> make_record(std::span<const uint8_t> tpl, std::span<const uint8_t> en, uint16_t form_id, Stage stage) {
    std::vector<uint8_t> r(tpl.begin(), tpl.end());
    wr16(r, 0, form_id);
    auto spread = [&](std::size_t dst, std::size_t src, int n, float scale, long hi) {
        long tsum = 0, esum = 0;
        for (int i = 0; i < n; ++i) {
            tsum += rd16(tpl, dst + 2 * i);
            esum += std::max<int>(0, rds16(en, src + 2 * i));
        }
        if (esum == 0) return;
        for (int i = 0; i < n; ++i) {
            float v = static_cast<float>(tsum) * scale * static_cast<float>(std::max<int>(0, rds16(en, src + 2 * i))) /
                      static_cast<float>(esum);
            wr16(r, dst + 2 * i, static_cast<uint16_t>(std::clamp<long>(std::lround(v / 5.0f) * 5, 0, hi)));
        }
    };
    spread(kRecStats, kEnStats, 5, stage == Stage::Rookie ? 0.7f : 1.0f, 999);
    spread(kRecRes, kEnRes, 7, 1.0f, 150);
    wr16(r, kRecAttack, rd16(en, kEnAttack));
    if (uint16_t m = rd16(en, kEnMove1)) wr16(r, kRecSignature, m);
    wr16(r, kRecDnaTech, 0);  // no DNA digivolution for the new forms
    r[kRecDnaIndex] = 0;
    r[kRecName] = static_cast<uint8_t>(rd16(en, kEnName));
    return r;
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
    std::map<uint16_t, std::size_t> stats_at;
    for (std::size_t o = 0; o + kEnemyStatsSize <= stats.size(); o += kEnemyStatsSize) {
        uint16_t id = rd16(stats, o);
        if (id == 0) break;
        stats_at.emplace(id, o);
    }
    std::map<uint16_t, Stage> stage_of;
    for (const auto& sp : kSpecies) stage_of.emplace(sp.id, sp.stage);
    // The game's own forms, as templates for the new records.
    const std::size_t table = forms_table::kOriginal - kExeLoadAddress + kExeHeaderSize;
    if (exe.size() < table + forms_table::kOriginalCount * forms_table::kRecordSize) {
        error = "forms table not found";
        return false;
    }
    std::map<uint16_t, std::span<const uint8_t>> own_form;
    for (uint32_t i = 0; i < forms_table::kOriginalCount; ++i) {
        auto rec = exe.subspan(table + i * forms_table::kRecordSize, forms_table::kRecordSize);
        own_form.emplace(rd16(rec, 0), rec);
    }

    std::vector<Entry> added;
    std::vector<uint8_t> added_blocks;
    std::size_t next_spare = 0;
    std::set<uint16_t> seen;
    for (const auto& e : entries) {
        if (e.kind < kFirstEnemyKind || partner_ids.count(e.id) || !stage_of.count(e.id) || !stats_at.count(e.id) ||
            !seen.insert(e.id).second)
            continue;
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
        auto en = std::span<const uint8_t>(stats).subspan(stats_at[e.id], kEnemyStatsSize);
        Stage stage = stage_of[e.id];
        // Template: an own form of the same stage and element (highest resistance), else the first of the stage.
        std::span<const uint8_t> tpl;
        for (uint16_t t : templates_for(stage)) {
            auto it = own_form.find(t);
            if (it == own_form.end()) continue;
            if (tpl.empty()) tpl = it->second;
            if (highest(it->second, kRecRes, 7) == highest(en, kEnRes, 7)) {
                tpl = it->second;
                break;
            }
        }
        if (tpl.empty()) {
            error = "no template form";
            return false;
        }
        auto rec = make_record(tpl, en, form, stage);
        out.mod.extra_forms.insert(out.mod.extra_forms.end(), rec.begin(), rec.end());
        out.forms.push_back({e.id, form, rd16(en, kEnName), slot, s, stage_index(stage)});
    }

    // Digivolution conditions of the new forms, in the order of the records. A predecessor that is one of the 8
    // rookies cannot be a required form (rookies are never learned): the form then needs only a partner level.
    {
        std::map<uint16_t, int> record_of;
        for (uint32_t i = 0; i < forms_table::kOriginalCount; ++i)
            record_of[rd16(exe.subspan(table + i * forms_table::kRecordSize, 2), 0)] = static_cast<int>(i);
        for (std::size_t i = 0; i < out.forms.size(); ++i) record_of[out.forms[i].form_id] = static_cast<int>(forms_table::kOriginalCount + i);
        std::map<uint16_t, uint16_t> line;
        for (const auto& l : kLines) line[l.form] = l.from;
        std::vector<uint8_t> conds;
        for (std::size_t i = 0; i < out.forms.size(); ++i) {
            const auto& f = out.forms[i];
            int from = -1;
            auto lit = line.find(f.form_id);
            if (lit != line.end()) {
                auto rit = record_of.find(lit->second);
                if (rit != record_of.end() && rit->second >= static_cast<int>(forms_table::kRookieCount)) from = rit->second;
            }
            static const int kLevel[4] = {3, 10, 25, 40};  // partner level, or level the required form must have reached
            put_condition(conds, forms_table::kOriginalCount + static_cast<uint32_t>(i), from, kLevel[f.stage], kLevel[f.stage]);
        }
        for (auto& c : out.mod.extra_conditions) c = conds;
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
