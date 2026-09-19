// gen2_chargen_smoke -- the GEN2 character generator honours Table 2 of Bernie's
// spec on BOTH of its paths: the Apple IIe char ROM POM1 ships, and the built-in
// 5x7 font it falls back to when that ROM is not found.
//
// The defect this exists for (Uncle Bernie, 16 sept. 2026): his vertical-split
// demo fills the text page with $80-$FF, and the rows holding $80-$9F showed "a
// block of 'O' looking characters" instead of @ABCDEFGH. The fallback font
// decoded a normal byte as `b & 0x7F`, landing $80-$9F on control codes it drew
// as a hollow box. The ROM path had the neighbouring defect: its $40-$7F is the
// IIe ALTERNATE set (MouseText, inverse lowercase), which the GEN2 does not have
// -- there, $40-$7F is the flashing copy of $00-$3F.
//
// Every assertion is an EQUIVALENCE Table 2 states ("$80-$9F shows what $C0-$DF
// shows", "$00-$1F is that, inverted"), so it holds for any glyph artwork --
// including Bernie's own EPROM, the day POM1 carries it.
//
// ROM path: $POM1_CHAR_ROM, else roms/apple2e_char.rom (ctest runs from the
// source tree).

#include "Gen2CharGen.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {

namespace cg = pom1::gen2char;

int failures = 0;

void check(bool cond, const char* path, const char* what, int b)
{
    if (cond) return;
    ++failures;
    if (failures <= 20)
        std::printf("FAIL [%s] %s (screen byte $%02X)\n", path, what, b);
}

cg::Rows inverted(const cg::Rows& r)
{
    cg::Rows out{};
    for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(~r[i] & 0x7F);
    return out;
}

bool isBlank(const cg::Rows& r)
{
    for (uint8_t v : r) if (v) return false;
    return true;
}

// The hollow box the fallback draws for a character it has no glyph for.
bool isBox(const cg::Rows& r)
{
    const cg::Rows box = cg::glyphRows(0xFB, false, nullptr);   // '{': no 5x7 glyph
    return r == box;
}

void checkTable2(const uint8_t* rom, const char* path)
{
    for (int i = 0; i < 0x40; ++i) {
        // The normal glyph of 2513 code i: $C0-$DF for '@'..'_', $A0-$BF for
        // ' '..'?' -- the one normal band nobody disputes.
        const uint8_t normalRef = static_cast<uint8_t>(i < 0x20 ? 0xC0 + i : 0x80 + i);
        const cg::Rows ref = cg::glyphRows(normalRef, false, rom);

        // $80-$BF: normal, same character as the reference. THE Bernie bug.
        check(cg::glyphRows(static_cast<uint8_t>(0x80 + i), false, rom) == ref,
              path, "$80-$BF must show the normal glyph of its 6-bit code", 0x80 + i);
        // $00-$3F: the same character, inverted.
        check(cg::glyphRows(static_cast<uint8_t>(i), false, rom) == inverted(ref),
              path, "$00-$3F must be the inverse of the same character", i);
        // $40-$7F: flashing -- normal in one phase, inverse in the other.
        check(cg::glyphRows(static_cast<uint8_t>(0x40 + i), false, rom) == ref,
              path, "$40-$7F must show the normal glyph off-phase", 0x40 + i);
        check(cg::glyphRows(static_cast<uint8_t>(0x40 + i), true, rom) == inverted(ref),
              path, "$40-$7F must show the inverse glyph on-phase", 0x40 + i);
        // Flash is the ONLY band the phase touches.
        check(cg::glyphRows(static_cast<uint8_t>(0x80 + i), true, rom) == ref,
              path, "a normal byte must not flash", 0x80 + i);
        check(cg::glyphRows(static_cast<uint8_t>(i), true, rom) == inverted(ref),
              path, "an inverse byte must not flash", i);
    }

    // Letters are not blank and not the no-glyph box: '@'..'Z' in every band.
    for (int i = 0; i < 0x1B; ++i) {
        for (int band : {0x00, 0x40, 0x80, 0xC0}) {
            const cg::Rows r = cg::glyphRows(static_cast<uint8_t>(band + i), false, rom);
            const cg::Rows lit = band < 0x40 ? inverted(r) : r;
            check(!isBlank(lit), path, "a letter must light pixels", band + i);
            check(!isBox(lit), path, "a letter must not be the no-glyph box", band + i);
        }
    }
    // '@' and 'O' differ -- the literal symptom: a row of O's where @ABC... belongs.
    check(cg::glyphRows(0x80, false, rom) != cg::glyphRows(0xCF, false, rom),
          path, "$80 ('@') must not look like 'O'", 0x80);
    // Space is blank in normal video, solid in inverse.
    check(isBlank(cg::glyphRows(0xA0, false, rom)), path, "normal space is blank", 0xA0);
    check(isBlank(inverted(cg::glyphRows(0x20, false, rom))), path, "inverse space is solid", 0x20);
}

std::vector<uint8_t> loadRom()
{
    const char* env = std::getenv("POM1_CHAR_ROM");
    const char* path = env ? env : "roms/apple2e_char.rom";
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> rom(4096);
    if (!f.read(reinterpret_cast<char*>(rom.data()), 4096)) {
        std::printf("FAIL: cannot read 4096 bytes from %s\n", path);
        return {};
    }
    cg::normalizeApple2eCharRom(rom.data(), rom.size());
    return rom;
}

} // namespace

// The byte-level decode, folded at compile time AND called at run time (a
// constexpr seam proved only by static_assert reads as uncovered).
static_assert(cg::attributeOf(0x3F) == cg::Attr::Inverse);
static_assert(cg::attributeOf(0x40) == cg::Attr::Flash);
static_assert(cg::attributeOf(0x80) == cg::Attr::Normal);
static_assert(cg::asciiOf(0x80) == '@' && cg::asciiOf(0x9A) == 'Z');
static_assert(cg::asciiOf(0x00) == '@' && cg::asciiOf(0x41) == 'A');
static_assert(cg::asciiOf(0xA1) == '!' && cg::asciiOf(0xE1) == 'a');
static_assert(cg::romCellOf(0x41) == 0x81 && cg::romCellOf(0x61) == 0xA1);
static_assert(cg::romCellOf(0x01) == 0x01 && cg::romCellOf(0x81) == 0x81);

int main()
{
    volatile uint8_t probe = 0x80;
    check(cg::asciiOf(probe) == '@', "decode", "asciiOf($80) == '@'", probe);
    probe = 0x41;
    check(cg::romCellOf(probe) == 0x81, "decode", "romCellOf($41) == $81", probe);
    check(cg::attributeOf(probe) == cg::Attr::Flash, "decode", "attributeOf($41)", probe);

    checkTable2(nullptr, "5x7 fallback");

    const std::vector<uint8_t> rom = loadRom();
    if (rom.empty()) return 1;
    checkTable2(rom.data(), "IIe ROM");

    // The ROM path keeps what only it can draw: lowercase in $E0-$FF.
    check(cg::glyphRows(0xE1, false, rom.data()) != cg::glyphRows(0xC1, false, rom.data()),
          "IIe ROM", "$E1 must be lowercase 'a', not 'A'", 0xE1);
    // ...and the MouseText at the ROM's own $40 must never reach the screen.
    {
        cg::Rows mouseText{};
        for (int gy = 0; gy < 8; ++gy) mouseText[gy] = rom[0x40 * 8 + gy] & 0x7F;
        check(cg::glyphRows(0x40, false, rom.data()) != mouseText,
              "IIe ROM", "$40 must not draw the ROM's MouseText cell", 0x40);
    }

    if (failures) {
        std::printf("gen2_chargen_smoke: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("gen2_chargen_smoke: OK\n");
    return 0;
}
