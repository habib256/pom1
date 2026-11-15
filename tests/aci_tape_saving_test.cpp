// ACI tape saving (+ round-trip) smoke test.
//
// End-to-end gate for the SAVE half of the cassette path:
//
//   1. Plant a known 64-byte pattern at $0300-$033F.
//   2. Drive the Woz ACI ROM's WRITE routine (`C100R` then `0300.033FW<CR>`)
//      — every access to $C000-$C0FF flips the output flip-flop, which
//      CassetteDevice::toggleOutput() captures into recordedDurations.
//   3. Dump the capture to both `.aci` and `.wav` via saveTape().
//   4. Re-open each saved file in a FRESH Memory + M6502, drive the
//      READ routine the same way aci_tape_loading_test does, and assert
//      the original bytes round-trip byte-for-byte.
//
// Regression gate for any change to:
//   - CassetteDevice::toggleOutput() (the WRITE-side pulse recorder)
//   - saveAciTape / saveWavTape (file format, timebase)
//   - The round-trip invariant kTapeFileTimebaseHz == POM1_CPU_CLOCK_HZ
//     — if either the save or the load path drifts off CPU cycles, the
//     `.wav` round-trip will fail first because the 1-bit / 0-bit half-
//     periods stop straddling the Woz READBIT threshold.
//   - Memory's $C000-$C0FF write sniffer at Memory.cpp:841 (blanket
//     toggleOutput on any write in that range except $C081)
//
// Shares no code with aci_tape_loading_test.cpp on purpose — the two are
// independent regression gates. If READ regresses we still want SAVE's
// round-trip half to be exercisable by tweaking argv.

#include "Memory.h"
// Memory holds unique_ptrs to these (forward-declared in Memory.h); destructor
// needs the complete types.
#include "A1IO_RTC.h"
#include "CassetteDevice.h"
#include "CFFA1.h"
#include "CpuClock.h"
#include "DisplayDevice.h"
#include "M6502.h"
#include "MicroSD.h"
#include "SID.h"
#include "TMS9918.h"
#include "TerminalCard.h"
#include "WiFiModem.h"
#include "PR40Printer.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class DisplayCapture : public DisplayDevice {
public:
    void onChar(char c) override {
        char ascii = static_cast<char>(c & 0x7F);
        if (ascii == 0x0D) ascii = '\n';
        captured.push_back(ascii);
    }
    std::string captured;
};

void queueWozmonCommand(Memory& mem, const char* cmd)
{
    for (const char* p = cmd; *p; ++p) {
        mem.setKeyPressed(*p);
    }
    mem.setKeyPressed('\r');  // Apple-1 Return -> $8D
}

// Puts the CPU at Wozmon's GETLINE entry point and runs the ACI ROM
// until either `done(cyclesConsumed)` returns true or the cycle budget
// runs out. Returns the number of cycles actually consumed.
template <typename DoneFn>
int64_t runAciDriver(M6502& cpu, int64_t cycleBudget, DoneFn done)
{
    constexpr int kCycleSlice = 50000;
    int64_t cyclesConsumed = 0;
    cpu.hardReset();
    cpu.setProgramCounter(0xFF1F);   // Wozmon GETLINE — parses the queued line
    cpu.start();
    while (cyclesConsumed < cycleBudget) {
        cyclesConsumed += cpu.run(kCycleSlice);
        if (done(cyclesConsumed)) break;
    }
    return cyclesConsumed;
}

bool verifyRoundTrip(const std::string& tapePath,
                     uint16_t loadFrom, uint16_t loadTo,
                     const std::vector<uint8_t>& expected)
{
    std::printf("\n--- ROUND-TRIP read via %s ---\n", tapePath.c_str());
    Memory memory;
    M6502  cpu(&memory);
    DisplayCapture display;
    memory.setDisplayDevice(&display);
    memory.setACIEnabled(true);

    uint8_t* mem = memory.getMemoryPointerMutable();
    std::memset(mem + loadFrom, 0x00, (loadTo - loadFrom) + 1);

    CassetteDevice& tape = memory.getCassetteDevice();
    if (!tape.loadTape(tapePath)) {
        std::fprintf(stderr, "  loadTape(%s) failed: %s\n",
                     tapePath.c_str(), tape.getLastError().c_str());
        return false;
    }
    tape.playTape();

    queueWozmonCommand(memory, "C100R");
    char aciCmd[32];
    std::snprintf(aciCmd, sizeof(aciCmd), "%04X.%04XR", loadFrom, loadTo);
    queueWozmonCommand(memory, aciCmd);

    const int loadSize = (loadTo - loadFrom) + 1;
    int64_t cycles = runAciDriver(cpu, 200'000'000LL, [&](int64_t) {
        // Early-exit once the full pattern matches — no need to
        // wait for the tape tail.
        return std::memcmp(mem + loadFrom, expected.data(), loadSize) == 0;
    });

    bool ok = std::memcmp(mem + loadFrom, expected.data(), loadSize) == 0;
    std::printf("  read complete: %lld cycles, match=%s\n",
                static_cast<long long>(cycles), ok ? "yes" : "no");
    if (!ok) {
        std::printf("  expected: ");
        for (int i = 0; i < std::min(16, loadSize); ++i)
            std::printf("%02X ", expected[i]);
        std::printf("\n  got     : ");
        for (int i = 0; i < std::min(16, loadSize); ++i)
            std::printf("%02X ", mem[loadFrom + i]);
        std::printf("\n");
    }
    return ok;
}

// The WRITE routine's timing, in CPU cycles, read back from the .aci the deck
// saved — what the Woz ACI ROM actually produces on POM1's cycle-exact 6502.
// Until now nothing pinned it: a CPU timing change could shift every bit of
// every exported tape and only the round trip (which shares the error with
// itself) would have run.
//
//   header half  592   one LDY $C000,X toggle per WDELAY pass ($C1E0-$C1EF)
//                596   once per 256 halves: the outer loop's ADC #$FE / BCS
//                      ($C1D5) when X wraps — real ROM behaviour
//   start bit    180 then 238
//   "0" half     238   "1" half 473
//                226 / 461 on the first half of a byte's first bit (the next
//                byte's LDA (zp,X) is fetched in the gap), 477 once on the
//                very first data half
//
// [C] ACI ROM $C170-$C1F0 executed on POM1's 6502 (Klaus-tested). The same
//     header/start/0/1 values as [D] RDK Apple-1 tape_roundtrip.rs
//     aci_rom_timing @ 0ef0d79.
//
// No other value. Until November 2026 a 1184 appeared once per 256 halves:
// the header toggles by READING $C000,X with X running through all 256
// values, so it reads $C081 once per 256 — and POM1's $C081 returned the tape
// input without flipping the output, merging two halves into one. [C] The
// ROM's own header loop is the evidence that every $C0xx access toggles.
// CassetteDevice::toggleOutputOnInputRead() is the fix, and why a READ (which
// polls $C081 continuously) still does not start a recording.
bool verifyWriteTiming(const std::string& aciPath, const std::vector<uint8_t>& pattern)
{
    std::FILE* f = std::fopen(aciPath.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "FAIL: cannot reopen %s\n", aciPath.c_str()); return false; }
    std::vector<uint8_t> b;
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    auto le32 = [&](size_t o) {
        return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) |
               (uint32_t(b[o + 3]) << 24);
    };
    if (b.size() < 16) { std::fprintf(stderr, "FAIL: .aci too short\n"); return false; }
    const uint32_t count = le32(12);
    if (b.size() < 16ull + 4ull * count) { std::fprintf(stderr, "FAIL: .aci truncated\n"); return false; }
    std::vector<uint32_t> d(count);
    for (uint32_t i = 0; i < count; ++i) d[i] = le32(16 + 4 * i);

    auto fail = [](const char* what, size_t i, uint32_t v) {
        std::fprintf(stderr, "FAIL: ACI WRITE timing — %s at half #%zu: %u cycles\n", what, i, v);
        return false;
    };

    // Header: everything before the 180-cycle start bit.
    size_t start = 0;
    while (start < d.size() && d[start] != 180) ++start;
    if (start < 1000 || start + 2 + pattern.size() * 16 != d.size())
        return fail("start bit not found where expected", start, start < d.size() ? d[start] : 0);
    size_t outerLoops = 0;
    for (size_t i = 0; i < start; ++i) {
        if (d[i] == 592) continue;
        if (d[i] == 596) { ++outerLoops; continue; }
        return fail("header half is not 592/596 (1184 = a $C081 edge lost)", i, d[i]);
    }
    if (d[start + 1] != 238) return fail("start bit second half is not 238", start + 1, d[start + 1]);

    // Data: two halves per bit, MSB first; decode and compare to the pattern.
    size_t i = start + 2;
    for (size_t byte = 0; byte < pattern.size(); ++byte) {
        uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit, i += 2) {
            const uint32_t a = d[i], c = d[i + 1];
            const bool one = a > 350;
            const bool firstHalfOk = one ? (a == 473 || a == 461 || a == 477)
                                         : (a == 238 || a == 226);
            if (!firstHalfOk) return fail("unexpected first half of a bit", i, a);
            if (c != (one ? 473u : 238u)) return fail("unexpected second half of a bit", i + 1, c);
            value = static_cast<uint8_t>((value << 1) | (one ? 1 : 0));
        }
        if (value != pattern[byte]) return fail("decoded byte differs from RAM", i - 16, value);
    }
    std::printf("  WRITE timing: %zu header halves (%zu outer-loop 596s), "
                "start 180/238, %zu bytes decode from 238/473 halves\n",
                start, outerLoops, pattern.size());
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    // ── Unit regression: first $C000 toggle at cycle 0 ───────────────────────
    // A fresh deck has currentCycle == 0. The recorder must NOT overload
    // `lastOutputToggleCycle == 0` as its "not started" sentinel — otherwise the
    // second toggle re-initialises the session and the first edge interval [0,T]
    // is silently dropped (recordedInitialLevel is clobbered too). Toggles at
    // cycles 0/100/200 must yield BOTH intervals.
    {
        CassetteDevice cd;                       // ctor → reset(): currentCycle = 0
        cd.toggleOutput();                       // first edge AT cycle 0
        cd.advanceCycles(100);
        cd.toggleOutput();                       // edge at cycle 100 → interval [0,100]
        cd.advanceCycles(100);
        cd.toggleOutput();                       // edge at cycle 200 → interval [100,200]
        const size_t n = cd.getRecordedTransitionCount();
        if (n != 2) {
            std::fprintf(stderr,
                "FAIL: cycle-0 recording dropped an edge — got %zu intervals, expected 2\n", n);
            return 1;
        }
        std::printf("cycle-0 recording: OK (2 intervals captured)\n");
    }

    // Target range is configurable so the test can be narrowed/widened for
    // debugging. Defaults: 64 bytes at $0300-$033F — enough to exercise
    // multi-byte framing while keeping the test under ~2 s.
    const uint16_t from = (argc > 1) ? static_cast<uint16_t>(std::stoul(argv[1], nullptr, 16)) : 0x0300;
    const uint16_t to   = (argc > 2) ? static_cast<uint16_t>(std::stoul(argv[2], nullptr, 16)) : 0x033F;
    const int size = (to - from) + 1;
    std::printf("ACI save round-trip test: range $%04X-$%04X (%d bytes)\n",
                from, to, size);

    // Pattern that avoids trivial constant runs so framing bugs are visible
    // (XOR with the offset decorrelates adjacent bytes).
    std::vector<uint8_t> pattern(size);
    for (int i = 0; i < size; ++i) {
        pattern[i] = static_cast<uint8_t>((0xA5 ^ (i * 37)) & 0xFF);
    }

    // -- PHASE 1: WRITE to tape --------------------------------------------
    std::printf("\n--- PHASE 1: WRITE ---\n");
    Memory memory;
    M6502  cpu(&memory);
    DisplayCapture display;
    memory.setDisplayDevice(&display);
    memory.setACIEnabled(true);
    assert(memory.isACIEnabled());

    uint8_t* mem = memory.getMemoryPointerMutable();
    std::memcpy(mem + from, pattern.data(), size);

    CassetteDevice& tape = memory.getCassetteDevice();
    assert(!tape.hasRecordedTape() && "deck must start empty");

    queueWozmonCommand(memory, "C100R");
    char aciCmd[32];
    std::snprintf(aciCmd, sizeof(aciCmd), "%04X.%04XW", from, to);
    queueWozmonCommand(memory, aciCmd);

    // WRITE stops toggling $C000 when it hits end-address. We detect that
    // by watching recordedDurations: once it stops growing for ~5 M cycles
    // (~5 s of emulated wallclock), we consider the capture complete.
    size_t lastCount = 0;
    int64_t cyclesSinceGrowth = 0;
    const int64_t writeCycles = runAciDriver(cpu, 200'000'000LL, [&](int64_t) {
        const size_t now = tape.getRecordedTransitionCount();
        if (now != lastCount) {
            lastCount = now;
            cyclesSinceGrowth = 0;
            return false;
        }
        cyclesSinceGrowth += 50000;
        return now > 100 && cyclesSinceGrowth > 5'000'000;
    });
    std::printf("  write complete: %lld cycles, %zu transitions captured\n",
                static_cast<long long>(writeCycles),
                tape.getRecordedTransitionCount());

    if (tape.getRecordedTransitionCount() < 100) {
        std::fprintf(stderr,
            "FAIL: ACI WRITE produced only %zu transitions. Either the "
            "ACI ROM never reached the WRITE routine, or $C000-$C0FF "
            "writes aren't reaching CassetteDevice::toggleOutput().\n"
            "=== Display capture ===\n%s\n=== End ===\n",
            tape.getRecordedTransitionCount(), display.captured.c_str());
        return 1;
    }

    // -- PHASE 2: save both formats ----------------------------------------
    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path();
    const std::string aciPath = (tmpDir / "pom1_aci_roundtrip.aci").string();
    const std::string wavPath = (tmpDir / "pom1_aci_roundtrip.wav").string();
    // The two Smartphone renderings (TapeExport.h): .aiff takes the profile by
    // default, the .wav by explicit request. 48 kHz, two channels and a second
    // of digital silence ahead of the leader — the READ routine must still
    // lock onto the leader and land every byte.
    const std::string aiffPath = (tmpDir / "pom1_aci_roundtrip_phone.aiff").string();
    const std::string phoneWavPath = (tmpDir / "pom1_aci_roundtrip_phone.wav").string();

    if (!tape.saveTape(aiffPath) ||
        !tape.saveTape(phoneWavPath, pom1::TapeExportProfile::Smartphone)) {
        std::fprintf(stderr, "FAIL: saveTape(smartphone) -> %s\n",
                     tape.getLastError().c_str());
        return 1;
    }
    const bool okAiff = verifyRoundTrip(aiffPath, from, to, pattern);
    const bool okPhoneWav = verifyRoundTrip(phoneWavPath, from, to, pattern);

    if (!tape.saveTape(aciPath)) {
        std::fprintf(stderr, "FAIL: saveTape(.aci) -> %s\n",
                     tape.getLastError().c_str());
        return 1;
    }
    if (!tape.saveTape(wavPath)) {
        std::fprintf(stderr, "FAIL: saveTape(.wav) -> %s\n",
                     tape.getLastError().c_str());
        return 1;
    }
    if (!verifyWriteTiming(aciPath, pattern)) return 1;
    std::printf("  saved %s (%lld bytes)\n", aciPath.c_str(),
                static_cast<long long>(fs::file_size(aciPath)));
    std::printf("  saved %s (%lld bytes)\n", wavPath.c_str(),
                static_cast<long long>(fs::file_size(wavPath)));

    // -- PHASE 3: read both back in fresh instances ------------------------
    bool okAci = verifyRoundTrip(aciPath, from, to, pattern);
    bool okWav = verifyRoundTrip(wavPath, from, to, pattern);

    // Cleanup (best-effort — leave the files on disk if somehow in use so
    // the developer can inspect them).
    std::error_code ec;
    fs::remove(aciPath, ec);
    fs::remove(wavPath, ec);
    fs::remove(aiffPath, ec);
    fs::remove(phoneWavPath, ec);

    if (!okAiff || !okPhoneWav) {
        std::fprintf(stderr,
            "FAIL: smartphone round-trip mismatch (aiff=%d wav=%d) — the "
            "48 kHz / stereo / lead-silence rendering in TapeExport.cpp no "
            "longer reads back through the ACI READ routine.\n",
            okAiff ? 1 : 0, okPhoneWav ? 1 : 0);
        return 1;
    }
    if (!okAci) {
        std::fprintf(stderr,
            "FAIL: .aci round-trip mismatch — recordedDurations are not "
            "surviving the save/load cycle. Check saveAciTape / "
            "loadAciTape framing.\n");
        return 1;
    }
    if (!okWav) {
        std::fprintf(stderr,
            "FAIL: .wav round-trip mismatch — saveWavTape is emitting "
            "durations at the wrong sample-rate ratio, or pcmToDurations "
            "isn't re-extracting them cleanly.\n");
        return 1;
    }

    std::puts("ACI tape saving round-trip test: OK");
    return 0;
}
