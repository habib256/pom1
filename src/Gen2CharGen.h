#pragma once
// Gen2CharGen.h -- the GEN2 card's character generator, as a pure function:
// a screen byte and a flash phase in, the cell's 8 rows of lit pixels out. No
// file I/O, no renderer state: GraphicsCard loads the ROM and paints the cell,
// every decision about WHICH glyph a byte shows and HOW lives here, where a test
// can reach both the ROM path and the fallback font without hiding a file.
//
// THE CONTRACT is Table 2 of Bernie's spec
// (`doc/reference/ColorGraphicsCard_doc_for_Arnaud.pdf`, "Character sets"):
//
//     $00-$1F  inverted   @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
//     $20-$3F  inverted    !"#$%&'()*+,-./0123456789:;<=>?
//     $40-$5F  flashing   @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
//     $60-$7F  flashing    !"#$%&'()*+,-./0123456789:;<=>?
//     $80-$9F  normal     @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
//     $A0-$BF  normal      !"#$%&'()*+,-./0123456789:;<=>?
//     $C0-$DF  normal     @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
//     $E0-$FF  normal     `abcdefghijklmnopqrstuvwxyz{|}~ + block
//
// The low six bits are the Signetics 2513 code the Apple-1 already uses; bits 7
// and 6 are the attribute; only $E0-$FF is full ASCII.
//
// WHY THIS EXISTS. Uncle Bernie's vertical-split demo (tests/gfx/vsplits.apl,
// Applefritter PM, 16 sept. 2026) fills the text page with $80-$FF and scrolls a
// text window over LORES. On his build "the first lines of the TEXT field when
// it is exposed on the top of the screen" showed "a block of 'O' looking
// characters" instead of @ABCDEFGH. Those rows hold $80-$9F, and two decoders
// disagreed with Table 2 there and next door:
//
//   * The built-in 5x7 font (used whenever roms/apple2e_char.rom is not found)
//     decoded a NORMAL byte as `b & 0x7F`, so $80-$9F became ASCII $00-$1F --
//     control codes, which it drew as a hollow box. Forty of them in a row is
//     Bernie's block of O's. Inverse and flashing bytes already went through the
//     6-bit mapping; normal ones now do too.
//
//   * The Apple IIe ROM POM1 ships in place of the unpublished 2716 dump stores
//     MouseText and inverse lowercase at $40-$7F -- its ALTERNATE set. The GEN2
//     has no alternate set: $40-$7F is the flashing copy of $00-$3F, so the ROM
//     path now draws the NORMAL glyph of the same 6-bit code and inverts it on
//     the flash phase, as the fallback path always has.
//
// Flash semantics are the same on both paths: normal video while `flashPhase`
// is false, inverse while it is true.
//
// NOT MODELLED YET: Bernie's EPROM leaves the BLANK scanline at the TOP of each
// cell (Apple II / 2513 style); the IIe glyphs used here leave it at the bottom,
// with lowercase descenders in row 7. Moving it needs his real glyph table, not
// a shifted guess -- see TODO.md.

#include <array>
#include <cstddef>
#include <cstdint>

namespace pom1::gen2char {

/// A cell: 8 scanlines of 7-bit lit masks, bit 0 = leftmost pixel.
using Rows = std::array<uint8_t, 8>;

enum class Attr : uint8_t { Inverse, Flash, Normal };

/// Bits 7-6 of the screen byte (Table 2).
constexpr Attr attributeOf(uint8_t b)
{
    return b < 0x40 ? Attr::Inverse : (b < 0x80 ? Attr::Flash : Attr::Normal);
}

/// The ASCII character a screen byte shows, whatever its attribute. Below $E0
/// only the low six bits count (2513 code: $00-$1F -> '@'..'_', $20-$3F ->
/// ' '..'?'); $E0-$FF is ASCII $60-$7F.
constexpr uint8_t asciiOf(uint8_t b)
{
    if (b >= 0xE0) return static_cast<uint8_t>(b & 0x7F);
    const uint8_t code = static_cast<uint8_t>(b & 0x3F);
    return code < 0x20 ? static_cast<uint8_t>(0x40 + code) : code;
}

/// The cell of a 4 KB Apple IIe Enhanced char ROM (csbits layout, see
/// normalizeApple2eCharRom) holding the glyph PATTERN a screen byte shows. The
/// ROM's own $00-$3F (inverse, pre-flipped) and $80-$FF (normal) are Table 2
/// already; its $40-$7F is the alternate set, so a flashing byte borrows the
/// normal glyph of its 6-bit code and the attribute is applied on top.
constexpr uint8_t romCellOf(uint8_t b)
{
    return attributeOf(b) == Attr::Flash ? static_cast<uint8_t>(0x80 | (b & 0x3F)) : b;
}

/// The Apple IIe Enhanced 4 KB char ROM stores pixels with inverted polarity
/// (1 = off) and bit 0 leftmost; XOR 0xFF gives 1 = lit (POM2's loadCharRom).
inline void normalizeApple2eCharRom(uint8_t* data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) data[i] ^= 0xFF;
}

/// Built-in 5x7 font for printable ASCII $20-$60, 8 bytes per glyph top to
/// bottom, bits 4..0 = pixels left to right. The no-ROM safety net: lowercase
/// falls back to uppercase, and { | } ~ have no glyph.
inline constexpr uint8_t kAscii5x7[65 * 8] = {
    0,0,0,0,0,0,0,0,                              // 0x20 ' '
    0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00,      // 0x21 '!'
    0x0A,0x0A,0x0A,0,0,0,0,0,                     // 0x22 '"'
    0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0,         // 0x23 '#'
    0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0,         // 0x24 '$'
    0x19,0x19,0x02,0x04,0x08,0x13,0x13,0,         // 0x25 '%'
    0x08,0x14,0x14,0x08,0x15,0x12,0x0D,0,         // 0x26 '&'
    0x04,0x04,0x08,0,0,0,0,0,                     // 0x27 '\''
    0x02,0x04,0x08,0x08,0x08,0x04,0x02,0,         // 0x28 '('
    0x08,0x04,0x02,0x02,0x02,0x04,0x08,0,         // 0x29 ')'
    0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0,         // 0x2A '*'
    0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0,         // 0x2B '+'
    0,0,0,0,0,0x04,0x04,0x08,                     // 0x2C ','
    0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0,         // 0x2D '-'
    0,0,0,0,0,0x0C,0x0C,0,                        // 0x2E '.'
    0x01,0x01,0x02,0x04,0x08,0x10,0x10,0,         // 0x2F '/'
    0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0,         // 0x30 '0'
    0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0,         // 0x31 '1'
    0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0,         // 0x32 '2'
    0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,0,         // 0x33 '3'
    0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0,         // 0x34 '4'
    0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0,         // 0x35 '5'
    0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0,         // 0x36 '6'
    0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0,         // 0x37 '7'
    0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0,         // 0x38 '8'
    0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0,         // 0x39 '9'
    0,0,0x0C,0x0C,0,0x0C,0x0C,0,                  // 0x3A ':'
    0,0,0x0C,0x0C,0,0x0C,0x04,0x08,               // 0x3B ';'
    0x02,0x04,0x08,0x10,0x08,0x04,0x02,0,         // 0x3C '<'
    0,0,0x1F,0,0x1F,0,0,0,                        // 0x3D '='
    0x08,0x04,0x02,0x01,0x02,0x04,0x08,0,         // 0x3E '>'
    0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0,         // 0x3F '?'
    0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E,0,         // 0x40 '@'
    0x0E,0x11,0x11,0x11,0x1F,0x11,0x11,0,         // 0x41 'A'
    0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0,         // 0x42 'B'
    0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0,         // 0x43 'C'
    0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0,         // 0x44 'D'
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0,         // 0x45 'E'
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0,         // 0x46 'F'
    0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0,         // 0x47 'G'
    0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0,         // 0x48 'H'
    0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0,         // 0x49 'I'
    0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0,         // 0x4A 'J'
    0x11,0x12,0x14,0x18,0x14,0x12,0x11,0,         // 0x4B 'K'
    0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0,         // 0x4C 'L'
    0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0,         // 0x4D 'M'
    0x11,0x11,0x19,0x15,0x13,0x11,0x11,0,         // 0x4E 'N'
    0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0,         // 0x4F 'O'
    0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0,         // 0x50 'P'
    0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0,         // 0x51 'Q'
    0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0,         // 0x52 'R'
    0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0,         // 0x53 'S'
    0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0,         // 0x54 'T'
    0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0,         // 0x55 'U'
    0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0,         // 0x56 'V'
    0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0,         // 0x57 'W'
    0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0,         // 0x58 'X'
    0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0,         // 0x59 'Y'
    0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0,         // 0x5A 'Z'
    0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0,         // 0x5B '['
    0x10,0x10,0x08,0x04,0x02,0x01,0x01,0,         // 0x5C '\\'
    0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0,         // 0x5D ']'
    0x04,0x0A,0x11,0,0,0,0,0,                     // 0x5E '^'
    0,0,0,0,0,0,0x1F,0,                           // 0x5F '_'
    0x08,0x04,0x02,0,0,0,0,0,                     // 0x60 '`'
};

/// The 5x7 pattern for an ASCII code, or the hollow box the fallback draws for
/// a character it has no glyph for ({ | } ~), or the block for $7F.
inline std::array<uint8_t, 8> fallback5x7(uint8_t ascii)
{
    if (ascii >= 0x61 && ascii <= 0x7A) ascii = static_cast<uint8_t>(ascii - 0x20);
    std::array<uint8_t, 8> g{};
    if (ascii >= 0x20 && ascii <= 0x60) {
        for (int i = 0; i < 8; ++i) g[i] = kAscii5x7[(ascii - 0x20) * 8 + i];
    } else if (ascii == 0x7F) {
        g = { 0x15, 0x0A, 0x15, 0x0A, 0x15, 0x0A, 0x15, 0 };
    } else {
        g = { 0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0 };
    }
    return g;
}

/// The cell for screen byte `b`. `rom` is a normalized 4 KB Apple IIe Enhanced
/// char ROM, or nullptr for the built-in 5x7 font (centred in the 7-dot cell).
inline Rows glyphRows(uint8_t b, bool flashPhase, const uint8_t* rom)
{
    const Attr attr = attributeOf(b);
    const bool flashInverse = attr == Attr::Flash && flashPhase;
    Rows rows{};
    if (rom) {
        const uint8_t mask = flashInverse ? 0x7F : 0x00;
        const std::size_t off = static_cast<std::size_t>(romCellOf(b)) * 8u;
        for (int gy = 0; gy < 8; ++gy)
            rows[gy] = static_cast<uint8_t>((rom[off + gy] ^ mask) & 0x7F);
        return rows;
    }
    const bool invert = attr == Attr::Inverse || flashInverse;
    const std::array<uint8_t, 8> glyph = fallback5x7(asciiOf(b));
    for (int gy = 0; gy < 8; ++gy) {
        uint8_t bits = 0;
        for (int gx = 0; gx < 7; ++gx) {
            bool lit = gx >= 1 && gx <= 5 && ((glyph[gy] >> (5 - gx)) & 1);
            if (invert) lit = !lit;
            if (lit) bits |= static_cast<uint8_t>(1u << gx);
        }
        rows[gy] = bits;
    }
    return rows;
}

} // namespace pom1::gen2char
