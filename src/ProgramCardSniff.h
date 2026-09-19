#pragma once
// ProgramCardSniff.h -- which card a program needs, judged by the I/O its code
// addresses. The fallback for a file that comes from nowhere POM1 knows.
//
// WHY THIS EXISTS. POM1 plugs a program's card from the folder it was loaded
// from (SoftwareDirRules.h): anything under `software/Graphic HGR/` gets the
// GEN2. Dropping Uncle Bernie's vsplits.apl onto the window from anywhere else
// loaded it and started it -- on the default machine, which has no graphics
// card. Its first loop waits for the GEN2's HST0 flag in D7 of a $C25x read;
// with no card there, nothing ever sets it, so the program spun forever behind
// a blank Apple-1 screen: "it loads, then it hangs". A developer's own folder
// is exactly where a program in progress lives, so the folder cannot be the
// only clue.
//
// The code is the other clue. A GEN2 program reads the card's soft switches
// with absolute instructions -- `BIT $C250`, `LDA $C250,X` -- and those three
// bytes are specific enough to trust: the GEN2 is the only thing on the bus at
// $C250-$C257. The sniff only runs when no folder rule matched, and its answer
// goes through the same rule the folder would have picked, so a sniffed GEN2
// program is treated exactly like one from `software/Graphic HGR/`: card
// plugged BEFORE the load, its window raised, storage cards evicted.
//
// Deliberately GEN2 only. The other cards' windows overlap (TMS9918 and the A1
// SID both answer at $CC00), so their bytes alone would not name one card.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CardTypes.h"
#include "FileBytes.h"
#include "MemoryImageLoader.h"

namespace pom1::cardsniff {

/// 6502 instructions whose operand is a 16-bit address they read or write:
/// absolute, absolute,X and absolute,Y forms of the loads, stores, ALU ops,
/// compares, BIT and the read-modify-writes. JMP/JSR are left out -- jumping
/// INTO an I/O page is not using the device.
inline constexpr bool isAbsoluteDataOpcode(uint8_t op)
{
    switch (op) {
        case 0xAD: case 0xBD: case 0xB9:   // LDA abs / abs,X / abs,Y
        case 0xAE: case 0xBE:              // LDX abs / abs,Y
        case 0xAC: case 0xBC:              // LDY abs / abs,X
        case 0x8D: case 0x9D: case 0x99:   // STA abs / abs,X / abs,Y
        case 0x8E: case 0x8C:              // STX abs, STY abs
        case 0x2C:                         // BIT abs
        case 0x0D: case 0x1D: case 0x19:   // ORA
        case 0x2D: case 0x3D: case 0x39:   // AND
        case 0x4D: case 0x5D: case 0x59:   // EOR
        case 0x6D: case 0x7D: case 0x79:   // ADC
        case 0xED: case 0xFD: case 0xF9:   // SBC
        case 0xCD: case 0xDD: case 0xD9:   // CMP
        case 0xEC: case 0xCC:              // CPX, CPY
        case 0xEE: case 0xFE: case 0xCE: case 0xDE:   // INC, DEC
        case 0x0E: case 0x1E: case 0x4E: case 0x5E:   // ASL, LSR
        case 0x2E: case 0x3E: case 0x6E: case 0x7E:   // ROL, ROR
            return true;
        default:
            return false;
    }
}

/// True when `code` holds an absolute access to a GEN2 soft switch,
/// $C250-$C257 (operand little-endian: low byte $50-$57, high byte $C2).
inline bool addressesGen2SoftSwitches(const uint8_t* code, std::size_t size)
{
    for (std::size_t i = 0; i + 2 < size; ++i)
        if (isAbsoluteDataOpcode(code[i]) && (code[i + 1] & 0xF8) == 0x50 && code[i + 2] == 0xC2)
            return true;
    return false;
}

/// The card `code` needs, by the I/O it addresses; nothing when it names none.
inline std::optional<CardId> cardAddressedBy(const uint8_t* code, std::size_t size)
{
    if (addressesGen2SoftSwitches(code, size)) return CardId::Gen2;
    return std::nullopt;
}

/// What to tell the user when a card was plugged because the code asked for it.
inline const char* sniffedReason(CardId card)
{
    return card == CardId::Gen2 ? "GEN2 plugged: the program uses its soft switches" : "";
}

/// Same, over every run a parsed memory image writes.
inline std::optional<CardId> cardAddressedBy(const MemoryImage& image)
{
    for (const auto& span : image.writes)
        if (auto card = cardAddressedBy(span.bytes.data(), span.bytes.size())) return card;
    return std::nullopt;
}

/// Same, for a file about to be loaded: a raw binary is code as it stands, a
/// hex dump is parsed first (PURE parse, nothing reaches memory). An unreadable
/// or unparsable file names no card -- the load itself reports why.
inline std::optional<CardId> cardAddressedByFile(const std::string& path, bool rawBinary)
{
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFileBounded(path, kMaxMemoryImageBytes, "memory image", bytes, error)) return std::nullopt;
    if (rawBinary) return cardAddressedBy(bytes.data(), bytes.size());
    const std::string name = std::filesystem::path(path).filename().string();
    const MemoryImage image = parseMemoryImage(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), name);
    return image.ok ? cardAddressedBy(image) : std::nullopt;
}

} // namespace pom1::cardsniff
