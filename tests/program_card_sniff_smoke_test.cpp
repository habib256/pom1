// program_card_sniff_smoke -- a program that addresses the GEN2's soft switches
// gets the GEN2, from wherever it was loaded.
//
// Dropping Uncle Bernie's vsplits.apl onto POM1 from outside
// `software/Graphic HGR/` loaded and started it on the default machine, which
// has no graphics card: its V-blank wait read $C25x for an HST0 flag nothing
// would ever set, and the program spun forever behind a blank screen. The
// folder was the only clue POM1 looked at; ProgramCardSniff.h reads the code.
//
// Sections:
//   1. the byte pattern: absolute data instructions on $C250-$C257, nothing else
//   2. Bernie's own file, parsed as POM1 parses it, names the GEN2
//   3. no false positive: no shipped program from another card's folder, or
//      from the games, is taken for a GEN2 program
//
// Runs from the source tree (software/).

#include "ProgramCardSniff.h"
#include "HexDumpFile.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using pom1::CardId;
using pom1::cardsniff::cardAddressedBy;

bool gen2(std::vector<uint8_t> code)
{
    const auto c = cardAddressedBy(code.data(), code.size());
    return c && *c == CardId::Gen2;
}

} // namespace

int main()
{
    // ---- 1: the pattern -----------------------------------------------------------
    assert(gen2({0x2C, 0x50, 0xC2}) && "BIT $C250 (vsplits' switch setup)");
    assert(gen2({0xBD, 0x54, 0xC2}) && "LDA $C254,X (vsplits' HST0 wait)");
    assert(gen2({0xEA, 0xEA, 0xAD, 0x57, 0xC2}) && "LDA $C257, anywhere in the code");
    assert(gen2({0x8D, 0x51, 0xC2}) && "a write is still addressing the card");
    assert(!gen2({0x20, 0x50, 0xC2}) && "JSR $C250 is a jump, not an access");
    assert(!gen2({0x4C, 0x50, 0xC2}) && "JMP neither");
    assert(!gen2({0xAD, 0x58, 0xC2}) && "$C258 is past the switches");
    assert(!gen2({0xAD, 0x4F, 0xC2}) && "$C24F is before them");
    assert(!gen2({0xAD, 0x50, 0xC3}) && "another page");
    assert(!gen2({0x50, 0xC2}) && !gen2({}) && "too short to hold an instruction");
    assert(!gen2({0xAD, 0x00, 0xCC}) && "TMS9918/SID territory names no card");
    std::printf("[1] pattern: absolute data access to $C250-$C257 only\n");

    // ---- 2: Bernie's file --------------------------------------------------------
    {
        // The sniff reads the code, never the path: that this copy ships in
        // Graphic HGR/ changes nothing for one saved anywhere else.
        const auto c = pom1::cardsniff::cardAddressedByFile("software/Graphic HGR/vsplits.apl", false);
        assert(c && *c == CardId::Gen2 && "vsplits.apl must name the GEN2");
        assert(std::string(pom1::cardsniff::sniffedReason(CardId::Gen2)).find("GEN2") == 0);
        assert(!pom1::cardsniff::cardAddressedByFile("tests/gfx/does-not-exist.apl", false));
        std::printf("[2] vsplits.apl -> GEN2\n");
    }

    // ---- 3: no false positive in the shipped corpus --------------------------------
    //
    // Every program in these folders is, by the folder's own definition, NOT a
    // GEN2 program. Measured when this was written: 122 programs shipped
    // (software/ + sdcard/), 18 detected -- the 18 of software/Graphic HGR/.
    {
        namespace fs = std::filesystem;
        int scanned = 0;
        for (const char* dir : {"software/Graphic TMS9918", "software/SOUND SID",
                                "software/Apple-1 games", "software/Apple-1 demos",
                                "software/NET", "software/Graphic gt-6144",
                                "software/Integer_basic", "software/a1io_rtc"}) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
                if (!e.is_regular_file()) continue;
                const std::string p = e.path().string();
                const bool hex = pom1::isHexDumpPath(p);
                if (!hex && e.path().extension() != ".bin") continue;
                ++scanned;
                const auto c = pom1::cardsniff::cardAddressedByFile(p, !hex);
                if (c) std::printf("FALSE POSITIVE: %s\n", p.c_str());
                assert(!c && "a program from another card's folder taken for GEN2");
            }
        }
        assert(scanned >= 50 && "the corpus is where this test expects it");
        std::printf("[3] %d shipped non-GEN2 programs, none taken for GEN2\n", scanned);
    }

    std::printf("program_card_sniff_smoke: OK\n");
    return 0;
}
