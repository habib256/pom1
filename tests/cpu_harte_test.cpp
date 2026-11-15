// Tom Harte "65x02 ProcessorTests" cycle-exact oracle for POM1's M6502 (Route B).
//
// Reads tests/cpu/harte_6502.bin (built by tools/gen_harte_fixture.py from
// github.com/SingleStepTests/ProcessorTests, 6502/v1 — real-hardware-validated,
// 100 cases for each of the 151 documented opcodes). For every case it seeds the
// CPU + RAM to the given initial state, executes ONE instruction, and asserts the
// final registers, the touched RAM bytes, AND the cycle count all match. This
// validates per-opcode cycle counts (incl. page-cross / branch-taken penalties)
// on top of the functional behaviour Klaus already covers.
//
// Asynchronous IRQ/NMI *line* timing (which Harte's opcode cases don't exercise)
// is covered by the companion Route-A test, cpu_cycle_count_test.cpp.
//
// SECOND PHASE — the NMOS DUMMY BUS CYCLES (M6502.cpp, "Dummy bus cycles"),
// which the fixture's final-state oracle cannot see: the read an indexed store
// makes at the un-carried address, the same read on a page-crossing indexed
// load, and the read-modify-write's write-back of the unmodified value. They
// are observed the way a program on the real machine would suffer them —
// through devices that react to a bus access:
//   * the ACI flip-flop toggles on every $C0xx access, so the PARITY of the
//     access count shows up in the output level;
//   * a TMS9918 data-port read advances the VRAM address, so `STA $CC00,X`
//     (X = 0) writes every other VRAM byte — the bug the TMS9918 guide's old
//     "Option B" would have shipped to silicon;
// and the debug instruments must NOT see them (a read-watch, --ram-trap).
// The model itself was checked offline against the Harte bus traces: all
// 10 000 cases each of 91/99/9D/BD/FE match, cycle by cycle.

#include "M6502.h"
#include "Memory.h"
// Same peripheral headers the Klaus runner includes (Memory holds them).
#include "A1IO_RTC.h"
#include "CFFA1.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"
#include "CassetteDevice.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>

namespace {

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    uint8_t  u8()  { if (p + 1 > end) { ok = false; return 0; } return *p++; }
    uint16_t u16() { if (p + 2 > end) { ok = false; return 0; } uint16_t v = uint16_t(p[0]) | (uint16_t(p[1]) << 8); p += 2; return v; }
    uint32_t u32() { if (p + 4 > end) { ok = false; return 0; } uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); p += 4; return v; }
};

struct State {
    uint16_t pc = 0;
    uint8_t  s = 0, a = 0, x = 0, y = 0, p = 0;
    std::vector<std::pair<uint16_t, uint8_t>> ram;
};

State readState(Reader& r) {
    State st;
    st.pc = r.u16(); st.s = r.u8(); st.a = r.u8(); st.x = r.u8(); st.y = r.u8(); st.p = r.u8();
    uint16_t n = r.u16();
    st.ram.reserve(n);
    for (uint16_t i = 0; i < n && r.ok; ++i) { uint16_t a = r.u16(); uint8_t v = r.u8(); st.ram.emplace_back(a, v); }
    return st;
}

// Run one instruction from $0300 on a non-test-mode machine.
// The code goes in through memWrite, so --ram-trap counts it as initialised.
int stepAt0300(M6502& cpu, Memory& memory, std::initializer_list<uint8_t> code,
               uint8_t a, uint8_t x, uint8_t y)
{
    uint16_t pc = 0x0300;
    for (uint8_t b : code) memory.memWrite(pc++, b);
    cpu.setProgramCounter(0x0300);
    cpu.setAccumulator(a);
    cpu.setXRegister(x);
    cpu.setYRegister(y);
    cpu.setIRQ(0);
    cpu.step();
    return cpu.getCurrentInstructionCycles();
}

int dummyBusCycles()
{
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  dummy cycles: %-62s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++failures;
    };

    Memory memory;
    M6502 cpu(&memory);
    memory.setCpuForIrq(&cpu);            // Memory asks the CPU "is this a dummy cycle?"
    cpu.start();

    // --- ACI: odd number of $C0xx accesses <=> the level a probe read sees
    //     before and after the instruction is the same.
    memory.setACIEnabled(true);
    auto oddAccesses = [&](std::initializer_list<uint8_t> code, uint8_t x) {
        const uint8_t before = memory.memRead(0xC000);
        stepAt0300(cpu, memory, code, 0x00, x, 0);
        const uint8_t after = memory.memRead(0xC000);
        return before == after;
    };
    check(oddAccesses({0xAD, 0x00, 0xC0}, 0),        "LDA $C000      : 1 access (control)");
    check(!oddAccesses({0x9D, 0x00, 0xC0}, 0),       "STA $C000,X    : 2 accesses (dummy read + write)");
    check(oddAccesses({0xEE, 0x00, 0xC0}, 0),        "INC $C000      : 3 accesses (read, old, new)");
    check(oddAccesses({0xBD, 0xFF, 0xC0}, 1),        "LDA $C0FF,X X=1: dummy read at $C000 on page cross");
    check(oddAccesses({0xBD, 0x10, 0xC0}, 1),        "LDA $C010,X X=1: no page cross, no dummy read");

    // --- TMS9918: STA $CC00,X skips a VRAM byte per write; STA $CC00 does not.
    memory.setTMS9918Enabled(true);
    TMS9918& vdp = memory.getTMS9918();
    vdp.setSiliconStrictMode(false);      // this is about the bus, not slot timing
    auto vramWriteAddress = [&](uint16_t addr) {
        memory.memWrite(0xCC01, static_cast<uint8_t>(addr & 0xFF));
        memory.memWrite(0xCC01, static_cast<uint8_t>(0x40 | ((addr >> 8) & 0x3F)));
    };
    vramWriteAddress(0x1000);
    stepAt0300(cpu, memory, {0x8D, 0x00, 0xCC}, 0x11, 0, 0);   // STA $CC00
    stepAt0300(cpu, memory, {0x8D, 0x00, 0xCC}, 0x22, 0, 0);
    const uint8_t* v = vdp.vramData();
    check(v[0x1000] == 0x11 && v[0x1001] == 0x22, "STA $CC00 x2  : VRAM $1000,$1001 (control)");
    vramWriteAddress(0x2000);
    stepAt0300(cpu, memory, {0x9D, 0x00, 0xCC}, 0x33, 0, 0);   // STA $CC00,X
    stepAt0300(cpu, memory, {0x9D, 0x00, 0xCC}, 0x44, 0, 0);
    check(v[0x2001] == 0x33 && v[0x2003] == 0x44,
          "STA $CC00,X x2: VRAM $2001,$2003 — the dummy read skips a byte");

    // --- Debug instruments stay quiet about dummy cycles.
    memory.clearWatchTrip();
    memory.setWatchpoint(0x0400, /*onRead=*/true, /*onWrite=*/false);
    stepAt0300(cpu, memory, {0x9D, 0x00, 0x04}, 0x55, 0, 0);   // STA $0400,X: dummy read only
    check(!memory.isWatchpointTripped(), "read-watch $0400 ignores STA $0400,X's dummy read");
    stepAt0300(cpu, memory, {0xAD, 0x00, 0x04}, 0x00, 0, 0);   // LDA $0400: a real read
    check(memory.isWatchpointTripped(),  "read-watch $0400 fires on LDA $0400 (control)");
    memory.clearWatchTrip();
    memory.setWatchpoint(0x0400, false, false);

    memory.setRamWriteTrap(true);
    memory.resetMemory();                 // what arms the trap's tables
    const uint64_t hits0 = memory.ramWriteTrapHits();
    stepAt0300(cpu, memory, {0x9D, 0x00, 0x05}, 0x66, 0, 0);   // STA $0500,X: unwritten $0500
    check(memory.ramWriteTrapHits() == hits0, "--ram-trap ignores a dummy read of unwritten RAM");
    stepAt0300(cpu, memory, {0xAD, 0x00, 0x06}, 0x00, 0, 0);   // LDA $0600: unwritten, real
    check(memory.ramWriteTrapHits() == hits0 + 1, "--ram-trap flags a real read of unwritten RAM (control)");
    memory.setRamWriteTrap(false);

    // --- Cycle counts are what they were (the Harte phase pins all of them).
    check(stepAt0300(cpu, memory, {0x9D, 0x00, 0x04}, 0, 0, 0) == 5, "STA abs,X still 5 cycles");
    check(stepAt0300(cpu, memory, {0xFE, 0x00, 0x04}, 0, 0, 0) == 7, "INC abs,X still 7 cycles");
    memory.setCpuForIrq(nullptr);
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: test_cpu_harte <harte_6502.bin>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (blob.size() < 8 || std::memcmp(blob.data(), "HRT1", 4) != 0) {
        std::fprintf(stderr, "bad fixture (magic/size)\n"); return 2;
    }

    Reader r{blob.data() + 4, blob.data() + blob.size()};
    const uint32_t count = r.u32();

    Memory memory;
    memory.setTestMode(true);
    memory.setWriteInRom(true);
    uint8_t* ram = memory.getMemoryPointerMutable();
    M6502 cpu(&memory);
    cpu.start();

    uint32_t pass = 0, fail = 0, funcFail = 0, cycOnlyFail = 0;
    int opFail[256] = {0}, opTotal[256] = {0}, opCycFail[256] = {0};
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        const uint8_t  op  = r.u8();
        const State    ini = readState(r);
        const State    fin = readState(r);
        const uint8_t  expCyc = r.u8();
        if (!r.ok) break;
        ++opTotal[op];

        for (const auto& e : ini.ram) ram[e.first] = e.second;
        cpu.setProgramCounter(ini.pc);
        cpu.setStackPointer(ini.s);
        cpu.setAccumulator(ini.a);
        cpu.setXRegister(ini.x);
        cpu.setYRegister(ini.y);
        cpu.setStatusRegister(ini.p);
        cpu.setIRQ(0);

        cpu.step();
        const int gotCyc = cpu.getCurrentInstructionCycles();

        bool regsGood = cpu.getProgramCounter() == fin.pc
                 && cpu.getStackPointer()   == fin.s
                 && cpu.getAccumulator()    == fin.a
                 && cpu.getXRegister()      == fin.x
                 && cpu.getYRegister()      == fin.y
                 && cpu.getStatusRegister() == fin.p;
        bool ramGood = true;
        for (const auto& e : fin.ram) if (ram[e.first] != e.second) { ramGood = false; break; }
        const bool funcGood = regsGood && ramGood;
        const bool cycGood  = (gotCyc == int(expCyc));

        if (funcGood && cycGood) { ++pass; continue; }
        ++fail;
        ++opFail[op];
        if (!funcGood) ++funcFail; else { ++cycOnlyFail; ++opCycFail[op]; }
        static bool opShown[256] = {false};
        if (!opShown[op]) {
            opShown[op] = true;
            std::fprintf(stderr,
                "FAIL op=%02X %s pc %04X->%04X(exp %04X) a=%02X(%02X) x=%02X(%02X) y=%02X(%02X) "
                "s=%02X(%02X) p=%02X(%02X) cyc=%d(exp%d)\n",
                op, funcGood ? "[cyc]" : "[FUNC]",
                ini.pc, cpu.getProgramCounter(), fin.pc,
                cpu.getAccumulator(), fin.a, cpu.getXRegister(), fin.x, cpu.getYRegister(), fin.y,
                cpu.getStackPointer(), fin.s, cpu.getStatusRegister(), fin.p, gotCyc, int(expCyc));
        }
    }

    std::printf("Harte 65x02 oracle: %u passed, %u failed (of %u) — functional=%u, cycle-only=%u\n",
                pass, fail, count, funcFail, cycOnlyFail);
    if (fail) {
        std::printf("per-opcode mismatches (op: fail/total, cyc-only):\n");
        for (int o = 0; o < 256; ++o)
            if (opFail[o]) std::printf("  %02X: %d/%d  (cyc %d)\n", o, opFail[o], opTotal[o], opCycFail[o]);
    }
    const int dummyFailures = dummyBusCycles();
    return (fail == 0 && dummyFailures == 0) ? 0 : 1;
}
