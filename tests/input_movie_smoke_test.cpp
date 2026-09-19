// input_movie_smoke -- an input movie recorded on a live machine replays to the
// same state, cycle for cycle, and says so; a tampered one says it did not.
//
// Issue #40 (re-recording for longplays) stands on this: keys dated to the
// emulated cycle, handed back on the same cycle, from a snapshot that carries
// everything a cycle-exact resume needs (v7's RUN section).
//
// The program under test polls the keyboard and, for every key, logs the key
// AND the low byte of a loop counter at the moment it read it. A key delivered
// even one poll late logs a different counter -- so equal logs mean the keys
// landed on the same turns of the loop, not merely in the same order.
//
// Sections:
//   1. the file format round-trips and refuses what it cannot trust
//   2. the session: due keys, where the CPU must stop, the verdict
//   3. record on a LIVE machine with DRAM refresh on, keys typed in real time;
//      replay on a fresh live machine and on a deterministic one: identical
//      logs, verdict Verified (the refresh setting and phase come back through
//      the snapshot -- the replay machines start with refresh OFF)
//   4. a tampered movie replays to a Diverged verdict

#include "EmulationController.h"
#include "InputMovie.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace mv = pom1::movie;
using Mode = EmulationController::ExecutionMode;

//   0280 AD 11 D0  LDA $D011      key ready?
//   0283 10 10     BPL $0295
//   0285 AD 10 D0  LDA $D010      the key
//   0288 A6 13     LDX $13        log index
//   028A 9D 00 03  STA $0300,X    log the key
//   028D A5 10     LDA $10
//   028F 9D 00 04  STA $0400,X    log the counter when it was read
//   0292 E8        INX
//   0293 86 13     STX $13
//   0295 E6 10     INC $10        count loop turns
//   0297 D0 E7     BNE $0280
//   0299 E6 11     INC $11
//   029B 4C 80 02  JMP $0280
const uint8_t kProgram[] = {
    0xAD, 0x11, 0xD0, 0x10, 0x10, 0xAD, 0x10, 0xD0, 0xA6, 0x13, 0x9D, 0x00, 0x03,
    0xA5, 0x10, 0x9D, 0x00, 0x04, 0xE8, 0x86, 0x13, 0xE6, 0x10, 0xD0, 0xE7, 0xE6,
    0x11, 0x4C, 0x80, 0x02,
};

struct Log {
    std::vector<uint8_t> keys, counters;
    bool operator==(const Log& o) const { return keys == o.keys && counters == o.counters; }
};

Log readLog(EmulationController& emu)
{
    EmulationSnapshot s;
    emu.copySnapshot(s);
    Log l;
    const int n = s.memory[0x13];
    l.keys.assign(s.memory.begin() + 0x0300, s.memory.begin() + 0x0300 + n);
    l.counters.assign(s.memory.begin() + 0x0400, s.memory.begin() + 0x0400 + n);
    return l;
}

EmulationSnapshot snap(EmulationController& emu)
{
    EmulationSnapshot s;
    emu.copySnapshot(s);
    return s;
}

// Wait (bounded) for a live replay to reach its end and deliver a verdict.
uint8_t awaitVerdict(EmulationController& emu)
{
    for (int i = 0; i < 1000; ++i) {
        const EmulationSnapshot s = snap(emu);
        if (s.movieState == 0 && s.movieVerdict != 0) return s.movieVerdict;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

std::vector<uint8_t> tiny(uint64_t end)
{
    mv::Movie m;
    m.snapshot = {1, 2, 3};
    m.keys = {{10, 'A'}, {10, 'B'}, {40, 'C'}};
    m.endCycle = end;
    m.endHash = 0x1234;
    return mv::serialize(m);
}

bool parses(const std::vector<uint8_t>& b)
{
    mv::Movie m;
    std::string e;
    return mv::parse(b.data(), b.size(), m, e);
}

} // namespace

int main()
{
    // ---- 1: format ------------------------------------------------------------
    {
        const std::vector<uint8_t> b = tiny(50);
        mv::Movie m;
        std::string e;
        assert(mv::parse(b.data(), b.size(), m, e));
        assert(m.snapshot.size() == 3 && m.keys.size() == 3 && m.keys[2] == (mv::KeyEvent{40, 'C'}));
        assert(m.endCycle == 50 && m.endHash == 0x1234);
        assert(mv::serialize(m) == b && "round trip");

        std::vector<uint8_t> bad = b; bad[0] = 'X';
        assert(!parses(bad) && "magic");
        bad = b; bad[8] = 2;
        assert(!parses(bad) && "version");
        bad = b; bad.pop_back();
        assert(!parses(bad) && "truncated");
        bad = b; bad.push_back(0);
        assert(!parses(bad) && "trailing bytes");
        assert(!parses(tiny(39)) && "a key past the end");
        bad = b; bad[8 + 4 + 4 + 3] = 0xFF;   // key count low byte -> huge
        assert(!parses(bad) && "count larger than the file");
        mv::Movie back = m; back.keys[1].cycle = 5;
        assert(!parses(mv::serialize(back)) && "cycles going backwards");
        std::printf("[1] format: round trip, 7 refusals\n");
    }

    // ---- 2: session -----------------------------------------------------------
    {
        mv::Movie m;
        m.keys = {{100, 'A'}, {100, 'B'}, {250, 'C'}};
        m.endCycle = 300;
        m.endHash = 42;
        mv::Session s;
        s.startPlaying(m, 1000);                         // counter already at 1000
        std::string got;
        auto type = [&got](uint8_t k) { got.push_back(static_cast<char>(k)); };
        assert(s.cyclesToNextStop(1000) == 100);
        s.deliverDue(1099, type);
        assert(got.empty() && "not yet");
        s.deliverDue(1100, type);
        assert(got == "AB" && "both keys of cycle 100, in order");
        assert(s.cyclesToNextStop(1100) == 150);
        s.deliverDue(1260, type);                        // overshoot: still delivered
        assert(got == "ABC" && s.cyclesToNextStop(1260) == 40);
        assert(!s.reachedEnd(1299) && s.reachedEnd(1300));
        s.finishPlaying(41);
        assert(s.verdict() == mv::Session::Verdict::Diverged);
        s.startPlaying(m, 0);
        s.deliverDue(300, type);
        s.finishPlaying(42);
        assert(s.verdict() == mv::Session::Verdict::Verified);
        std::printf("[2] session: due keys, stops, verdicts\n");
    }

    // ---- 3: record live, replay live and deterministic ---------------------------
    std::vector<uint8_t> recording;
    Log recorded;
    {
        EmulationController emu(nullptr, false, nullptr, Mode::Live);
        emu.setDramRefreshEnabled(true);   // the replays start with it OFF
        std::vector<std::pair<uint16_t, uint8_t>> writes;
        for (std::size_t i = 0; i < sizeof kProgram; ++i)
            writes.emplace_back(static_cast<uint16_t>(0x0280 + i), kProgram[i]);
        writes.emplace_back(0x0013, 0);
        emu.writeMemoryBatch(writes);
        emu.jumpTo(0x0280);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::string err;
        assert(emu.startInputMovieRecording(err));
        for (char c : std::string("HELLO")) {
            emu.queueKey(c);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        assert(emu.stopInputMovie(&recording) && !recording.empty());
        recorded = readLog(emu);
        assert(recorded.keys.size() == 5 && "all five keys reached the program");
    }
    mv::Movie parsed;
    {
        std::string e;
        assert(mv::parse(recording.data(), recording.size(), parsed, e));
        assert(parsed.keys.size() == 5 && parsed.endCycle > parsed.keys.back().cycle);
    }
    {
        EmulationController live(nullptr, false, nullptr, Mode::Live);
        std::string err;
        assert(live.playInputMovie(recording, err));
        const uint8_t v = awaitVerdict(live);
        const Log l = readLog(live);
        std::printf("[3] live replay: verdict %u, keys %zu\n", v, l.keys.size());
        assert(v == 1 && "a live replay reaches the recorded state");
        assert(l == recorded && "same keys read on the same loop turns");
    }
    {
        EmulationController det(nullptr, false, nullptr, Mode::Deterministic);
        std::string err;
        assert(det.playInputMovie(recording, err));
        det.runCyclesSync(parsed.endCycle + 5000);
        const EmulationSnapshot s = snap(det);
        std::printf("    deterministic replay: verdict %u\n", s.movieVerdict);
        assert(s.movieState == 0 && s.movieVerdict == 1);
        assert(readLog(det) == recorded);
    }

    // ---- 4: tampering is caught ----------------------------------------------------
    {
        mv::Movie t = parsed;
        t.keys[2].key = 'J';               // a different key...
        t.keys[3].cycle += 200000;          // ...and one typed much later
        t.keys[4].cycle = std::max(t.keys[4].cycle, t.keys[3].cycle);
        t.endCycle = std::max(t.endCycle, t.keys[4].cycle);
        EmulationController det(nullptr, false, nullptr, Mode::Deterministic);
        std::string err;
        assert(det.playInputMovie(mv::serialize(t), err));
        det.runCyclesSync(t.endCycle + 5000);
        const EmulationSnapshot s = snap(det);
        std::printf("[4] tampered replay: verdict %u\n", s.movieVerdict);
        assert(s.movieVerdict == 2 && "a replay that did not reach the recorded state says so");
    }

    std::printf("input_movie_smoke: OK\n");
    return 0;
}
