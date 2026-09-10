// rom_swap_reset_smoke_test.cpp -- a ROM swap under a live CPU must press RESET.
//
// EmulationController::applyCardConfiguration can rewrite $E000-$FFFF (the
// preset's system ROM profile, Krusader, the CFFA1 firmware) on a machine whose
// CPU is mid-program. The program counter it stops at is an offset into the ROM
// that is being replaced, and the old and new images do not agree on where
// instructions begin: resuming that PC lands MID-INSTRUCTION and decodes the new
// bytes from the wrong boundary.
//
// The observed defect: `--headless --preset 6` (Krusader) printed a 6502
// register dump and a disassembly line before its prompt, as if it had received
// a keystroke nobody sent. The stock Woz Monitor had reached GETLINE by the time
// the preset transaction ran, and at that offset Krusader's bytes decoded as
// `LDX $99FE,Y` then $00 = BRK, so the machine entered Krusader's BRK handler.
// How far the provisional monitor had got depends on WALL-CLOCK startup time,
// which is why the nightly ThreadSanitizer job saw it about two runs in three
// and an ordinary build did not in fifteen.
//
// What is pinned here is the rule, not the symptom, because the symptom is a
// race: a transaction that rewrites the ROM map re-vectors through RESET, and a
// transaction that does not leaves the program counter exactly where it was.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "EmulationController.h"
#include "MachineCoordinator.h"

#include <cstdio>
#include <memory>

namespace {

// sizeof(EmulationSnapshot) is 260 KB against Windows' 1 MB thread stack, so
// snapshots live on the heap here — see CLAUDE.md, "Testing".
uint16_t pcAfter(EmulationController& emu, EmulationSnapshot& scratch)
{
    emu.copySnapshot(scratch);
    return scratch.programCounter;
}

int failures = 0;

void expectPc(const char* what, uint16_t got, uint16_t want)
{
    if (got == want) {
        std::printf("  ok   %-44s PC=$%04X\n", what, got);
        return;
    }
    std::printf("  FAIL %-44s PC=$%04X, expected $%04X\n", what, got, want);
    ++failures;
}

} // namespace

int main()
{
    auto scratch = std::make_unique<EmulationSnapshot>();

    // No audio service: a bare test core opens no OS sound device.
    EmulationController emu(nullptr);
    emu.stopCpu();

    // Bring the machine to the Woz Monitor the way a preset does, and note the
    // reset vector the ROM itself carries.
    pom1::CardConfigurationRequest boot;
    boot.systemRomProfile =
        pom1::CardConfigurationRequest::SystemRomProfile::MonitorOnly;
    boot.coldReset = true;
    boot.animateBoot = false;
    const pom1::CardConfigurationResult booted = emu.applyCardConfiguration(boot);
    if (!booted) {
        std::printf("  FAIL could not load the Woz Monitor: %s\n",
                    booted.message.c_str());
        return 1;
    }
    emu.stopCpu();
    const uint16_t resetVector = pcAfter(emu, *scratch);
    expectPc("cold reset vectors through $FFFC", resetVector, 0xFF00);

    // Run the monitor far enough that the program counter is a ROM offset and
    // nothing else — this is the state the defect needed. runCyclesSync is
    // deterministic and leaves the CPU stopped.
    emu.runCyclesSync(200000);
    const uint16_t pcRunning = pcAfter(emu, *scratch);
    if (pcRunning == resetVector) {
        std::printf("  FAIL the CPU never left the reset vector; the rest of "
                    "this test would prove nothing\n");
        return 1;
    }
    std::printf("  ok   the monitor is mid-program at PC=$%04X\n", pcRunning);

    // 1. A transaction that rewrites nothing must not move the PC. This is the
    //    control: a plain card toggle is not a reason to reset the machine.
    pom1::CardConfigurationRequest preserve;
    preserve.systemRomProfile =
        pom1::CardConfigurationRequest::SystemRomProfile::Preserve;
    const pom1::CardConfigurationResult kept = emu.applyCardConfiguration(preserve);
    if (!kept) {
        std::printf("  FAIL preserving transaction refused: %s\n",
                    kept.message.c_str());
        ++failures;
    }
    expectPc("Preserve leaves the program counter alone",
             pcAfter(emu, *scratch), pcRunning);

    // 2. A transaction that rewrites the ROM map re-vectors through RESET, and
    //    does so WITHOUT a cold reset — the preset path sets no coldReset, and
    //    that is precisely the path the defect lived on.
    auto before = std::make_unique<EmulationSnapshot>();
    emu.copySnapshot(*before);

    pom1::CardConfigurationRequest krusader;
    krusader.systemRomProfile =
        pom1::CardConfigurationRequest::SystemRomProfile::MonitorOnly;
    krusader.loadKrusader = true;
    const pom1::CardConfigurationResult swapped =
        emu.applyCardConfiguration(krusader);
    if (!swapped) {
        std::printf("  FAIL Krusader transaction refused: %s\n",
                    swapped.message.c_str());
        return failures + 1;
    }
    // Krusader owns $E000-$FFFF, vectors included, so it carries its own RESET
    // target. Whatever it is, the CPU must be sitting on it rather than on the
    // offset the previous ROM's monitor had reached.
    const uint16_t pcAfterSwap = pcAfter(emu, *scratch);
    if (pcAfterSwap == pcRunning) {
        std::printf("  FAIL a ROM swap resumed the old program counter "
                    "($%04X) — this is the defect\n", pcAfterSwap);
        ++failures;
    } else {
        std::printf("  ok   the ROM swap re-vectored to PC=$%04X\n", pcAfterSwap);
    }

    // 3. …and the vector it landed on is the one the NEW image carries, read
    //    back from memory rather than assumed.
    auto after = std::make_unique<EmulationSnapshot>();
    emu.copySnapshot(*after);
    const uint16_t vectorInRom = static_cast<uint16_t>(
        after->memory[0xFFFC] | (after->memory[0xFFFD] << 8));
    expectPc("PC is the new ROM's own $FFFC vector", pcAfterSwap, vectorInRom);

    // 4. RESET on a 6502 clears no RAM: softReset, not hardReset. A program
    //    already injected into memory must survive the swap, so user RAM below
    //    the ROM window has to come through the transaction byte for byte.
    size_t disturbed = 0;
    for (size_t addr = 0x0200; addr < 0x2000; ++addr) {
        if (after->memory[addr] != before->memory[addr]) ++disturbed;
    }
    if (disturbed != 0) {
        std::printf("  FAIL the ROM swap disturbed %zu byte(s) of user RAM "
                    "in $0200-$1FFF — that is a hardReset, not a RESET\n",
                    disturbed);
        ++failures;
    } else {
        std::printf("  ok   user RAM $0200-$1FFF survived the swap intact\n");
    }

    if (failures != 0) {
        std::printf("rom_swap_reset_smoke: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("rom_swap_reset_smoke: OK\n");
    return 0;
}
