// deterministic_run_smoke -- a Deterministic controller reaches the same state
// after the same calls, however long the host takes between them.
//
// The defect (TODO.md, "--paste-at-cycle promet un déterminisme qu'il n'a
// pas"): the headless driver built a Live controller, whose emulation thread
// runs the CPU in real time from construction and again after every --load or
// --run, until the first runCyclesSync stops it. `--dump-after-cycles N` thus
// meant N cycles plus however many the thread had run meanwhile -- a number set
// by the host's speed and load. On an idle desktop that head start is a few
// hundred cycles and rarely shows; on a loaded CI runner it is not bounded.
// Graphics goldens, --paste-at-cycle, and anything later built on "the same
// input at the same cycle" (input movies, issue #40) all stand on this.
//
// The test plays one script -- write a program, jump to it, queue a key, run a
// counted number of cycles -- with the host sleeping 0 ms, then 120 ms, between
// every step:
//   1. Deterministic: the two runs end byte-identical (64 KB + registers), and
//      the queued key reached the program (runCyclesSync delivers it).
//   2. Live, the control: the same script DIVERGES, because the thread ran the
//      program while the host slept. Without this the test could pass against a
//      controller that never ran anything at all.

#include "EmulationController.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Mode = EmulationController::ExecutionMode;

// $0280: poll the keyboard; on a key, store it in $12. Count loop turns in
// $10/$11. The counter moves with every cycle run, so any uncounted cycle shows.
//   0280 AD 11 D0  LDA $D011   ; key ready? (bit 7)
//   0283 10 05     BPL $028A
//   0285 AD 10 D0  LDA $D010   ; read it (clears the strobe)
//   0288 85 12     STA $12
//   028A E6 10     INC $10
//   028C D0 F2     BNE $0280
//   028E E6 11     INC $11
//   0290 4C 80 02  JMP $0280
const uint8_t kProgram[] = {
    0xAD, 0x11, 0xD0, 0x10, 0x05, 0xAD, 0x10, 0xD0, 0x85, 0x12,
    0xE6, 0x10, 0xD0, 0xF2, 0xE6, 0x11, 0x4C, 0x80, 0x02,
};

struct Result {
    std::vector<uint8_t> memory;
    uint16_t pc; uint8_t a, x, y, sp, p;
    bool operator==(const Result& o) const
    {
        return memory == o.memory && pc == o.pc && a == o.a && x == o.x && y == o.y
            && sp == o.sp && p == o.p;
    }
};

Result play(Mode mode, int hostSleepMs)
{
    const auto pause = [hostSleepMs] {
        std::this_thread::sleep_for(std::chrono::milliseconds(hostSleepMs));
    };
    EmulationController emu(nullptr, false, nullptr, mode);
    pause();
    std::vector<std::pair<uint16_t, uint8_t>> writes;
    for (std::size_t i = 0; i < sizeof kProgram; ++i)
        writes.emplace_back(static_cast<uint16_t>(0x0280 + i), kProgram[i]);
    emu.writeMemoryBatch(writes);
    emu.jumpTo(0x0280);            // a Live machine starts running HERE
    pause();
    emu.queueKey('A');
    pause();
    emu.runCyclesSync(1'000'000);
    EmulationSnapshot s;
    emu.copySnapshot(s);
    return {s.memory, s.programCounter, s.accumulator, s.xRegister, s.yRegister,
            s.stackPointer, s.statusRegister};
}

unsigned counter(const Result& r) { return r.memory[0x10] | (r.memory[0x11] << 8); }

} // namespace

int main()
{
    // ---- 1: Deterministic -------------------------------------------------------
    const Result fast = play(Mode::Deterministic, 0);
    const Result slow = play(Mode::Deterministic, 120);
    std::printf("[1] deterministic: counter %u (host 0 ms) vs %u (host 3 x 120 ms)\n",
                counter(fast), counter(slow));
    assert(fast == slow && "same calls, same state -- whatever the host did in between");
    assert((fast.memory[0x12] & 0x7F) == 'A' && (fast.memory[0x12] & 0x80)
           && "runCyclesSync delivered the queued key to the program");
    assert(counter(fast) > 0 && "the program actually ran");

    // ---- 2: Live, the control ----------------------------------------------------
    const Result liveFast = play(Mode::Live, 0);
    const Result liveSlow = play(Mode::Live, 120);
    std::printf("[2] live (control): counter %u (host 0 ms) vs %u (host 3 x 120 ms)\n",
                counter(liveFast), counter(liveSlow));
    assert(!(liveFast == liveSlow)
           && "a Live controller runs while the host sleeps -- the defect this mode removes");

    std::printf("deterministic_run_smoke: OK\n");
    return 0;
}
