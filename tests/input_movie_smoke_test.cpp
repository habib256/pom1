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
//   5. re-recording: a rewind while recording takes the movie back with the
//      machine; the re-recorded movie replays Verified (issue #40)
//   6. a replay taken over mid-way becomes a recording; the branch replays
//   7. a movie recorded on a GEN2 machine brings the card back on replay
// Given a directory, it leaves three movies there for movie_play_cli.

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

// What the program logs for typed keys: $D010 reads with bit 7 set.
std::vector<uint8_t> typed(const char* keys)
{
    std::vector<uint8_t> v;
    for (; *keys; ++keys) v.push_back(static_cast<uint8_t>(*keys | 0x80));
    return v;
}

mv::Clock C(uint64_t cycles) { return {cycles, 0}; }

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

int main(int argc, char** argv)
{
    // argv[1]: a directory to leave movies in for movie_play_cli (tools/test_movie_play.py).
    const std::string fixtures = argc > 1 ? argv[1] : "";
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
        s.startPlaying(m, C(1000));                      // counter already at 1000
        std::string got;
        auto type = [&got](uint8_t k) { got.push_back(static_cast<char>(k)); };
        assert(s.cyclesToNextStop(C(1000)) == 100);
        s.deliverDue(C(1099), type);
        assert(got.empty() && "not yet");
        s.deliverDue(C(1100), type);
        assert(got == "AB" && "both keys of cycle 100, in order");
        assert(s.cyclesToNextStop(C(1100)) == 150);
        s.deliverDue(C(1260), type);                     // overshoot: still delivered
        assert(got == "ABC" && s.cyclesToNextStop(C(1260)) == 40);
        assert(!s.reachedEnd(C(1299)) && s.reachedEnd(C(1300)));
        s.finishPlaying(41);
        assert(s.verdict() == mv::Session::Verdict::Diverged);
        s.startPlaying(m, C(0));
        s.deliverDue(C(300), type);
        s.finishPlaying(42);
        assert(s.verdict() == mv::Session::Verdict::Verified);
        std::printf("[2] session: due keys, stops, verdicts\n");
    }

    // ---- 2b: re-recording, pure -------------------------------------------------
    {
        using R = mv::Session::Rewind;
        const std::vector<uint8_t> frame = {9, 9};
        mv::Session s;
        s.startRecording({1}, {1000, 7});                // 7 keys typed before the start
        s.recordKey({1100, 7}, 'A');
        s.recordKey({1200, 8}, 'X');
        s.recordKey({1300, 9}, 'Y');
        assert(s.keys() == 3);
        // Back to a frame taken after A, before X: the mistake is off the timeline...
        assert(s.rewindTo({1150, 8}, frame) == R::Followed && s.keys() == 1);
        // ...but still there if the user goes forward again without typing.
        assert(s.rewindTo({1350, 10}, frame) == R::Followed && s.keys() == 3);
        assert(s.rewindTo({1150, 8}, frame) == R::Followed);
        s.recordKey({1500, 8}, 'B');                     // a new key ends that future
        assert(s.keys() == 2);
        assert(s.rewindTo({1600, 10}, frame) == R::Restarted
               && "a frame beyond the keys this timeline holds cannot be followed");
        mv::Movie m = s.finishRecording({1700, 10}, 5);
        assert(m.snapshot == frame && m.keys.empty() && m.endCycle == 100);

        s.startRecording({1}, {1000, 7});
        s.recordKey({1100, 7}, 'A');
        s.recordKey({1200, 8}, 'B');
        // Same cycle as the start, but the key count says the frame precedes it.
        assert(s.rewindTo({1000, 6}, frame) == R::Restarted && s.keys() == 0);
        s.recordKey({1050, 6}, 'Q');
        m = s.finishRecording({1100, 7}, 5);
        assert(m.keys.size() == 1 && m.keys[0] == (mv::KeyEvent{50, 'Q'}) && m.snapshot == frame);

        // Playing: a rewind resumes the replay from that point; before its
        // start, it stops the replay.
        mv::Movie p;
        p.keys = {{100, 'A'}, {200, 'B'}, {300, 'C'}};
        p.endCycle = 400;
        s.startPlaying(p, {0, 0});
        std::string got;
        auto type = [&got](uint8_t k) { got.push_back(static_cast<char>(k)); };
        s.deliverDue({250, 0}, type);
        assert(got == "AB" && s.keysPlayed() == 2);
        assert(s.rewindTo({150, 1}, frame) == R::Followed && s.keysPlayed() == 1);
        s.deliverDue({250, 1}, type);
        assert(got == "ABB");
        // Take over at key 2: the played keys start a recording.
        assert(s.takeOver() && s.state() == mv::Session::State::Recording && s.keys() == 2);
        s.recordKey({260, 2}, 'Z');
        m = s.finishRecording({400, 3}, 1);
        assert(m.keys.size() == 3 && m.keys[2] == (mv::KeyEvent{260, 'Z'}) && m.endCycle == 400);
        s.startPlaying(p, {1000, 5});
        assert(s.rewindTo({900, 5}, frame) == R::Stopped
               && s.state() == mv::Session::State::Idle);
        assert(!s.takeOver() && "nothing to take over");
        std::printf("[2b] re-recording: follow, keep the future, restart, take over\n");
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

    // ---- 5: re-record over a mistake with the rewind timeline -----------------------
    // Type A, let a rewind frame be taken, type a wrong key, rewind to that frame,
    // type B. The movie must hold A then B -- the mistake gone -- and replay to
    // exactly the machine that was left running.
    std::vector<uint8_t> rerecorded;
    Log rerecordedLog;
    {
        EmulationController emu(nullptr, false, nullptr, Mode::Live);
        std::vector<std::pair<uint16_t, uint8_t>> writes;
        for (std::size_t i = 0; i < sizeof kProgram; ++i)
            writes.emplace_back(static_cast<uint16_t>(0x0280 + i), kProgram[i]);
        writes.emplace_back(0x0013, 0);
        emu.writeMemoryBatch(writes);
        emu.jumpTo(0x0280);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::string err;
        emu.setRewindEnabled(true);
        assert(emu.startInputMovieRecording(err));
        emu.queueKey('A');
        std::this_thread::sleep_for(std::chrono::milliseconds(600));   // >= 2 captures
        const std::size_t afterA = emu.getRewindStatus().frameCount - 1;
        assert(afterA >= 1 && "a rewind frame was taken after A");
        emu.queueKey('X');
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        assert(readLog(emu).keys == typed("AX"));
        emu.rewindResumeHere(afterA);
        assert(readLog(emu).keys == typed("A") && "the machine went back");
        EmulationSnapshot st = snap(emu);
        assert(st.movieState == 1 && st.movieKeys == 1 && "the movie went back with it");
        emu.queueKey('B');
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(emu.stopInputMovie(&rerecorded));
        rerecordedLog = readLog(emu);
        assert(rerecordedLog.keys == typed("AB"));
        emu.setRewindEnabled(false);
    }
    {
        mv::Movie m;
        std::string e;
        assert(mv::parse(rerecorded.data(), rerecorded.size(), m, e));
        assert(m.keys.size() == 2 && m.keys[0].key == 'A' && m.keys[1].key == 'B');
        EmulationController det(nullptr, false, nullptr, Mode::Deterministic);
        std::string err;
        assert(det.playInputMovie(rerecorded, err));
        det.runCyclesSync(m.endCycle + 5000);
        const EmulationSnapshot s = snap(det);
        std::printf("[5] re-recorded over a mistake: verdict %u, log %zu keys\n",
                    s.movieVerdict, readLog(det).keys.size());
        assert(s.movieVerdict == 1 && "the re-recorded movie replays to the machine it ended on");
        assert(readLog(det) == rerecordedLog);
    }

    // ---- 6: take over a replay, branch, and replay the branch ------------------------
    {
        std::vector<uint8_t> branch;
        Log branchLog;
        {
            EmulationController det(nullptr, false, nullptr, Mode::Deterministic);
            std::string err;
            assert(det.playInputMovie(recording, err));
            det.runCyclesSync(parsed.keys[1].cycle + 100);   // H and E played
            assert(snap(det).movieKeysPlayed == 2);
            assert(det.startInputMovieRecording(err) && "take over");
            assert(snap(det).movieState == 1);
            det.runCyclesSync(3000);
            det.queueKey('Z');
            det.deliverQueuedKeys();
            det.runCyclesSync(20000);
            assert(det.stopInputMovie(&branch));
            branchLog = readLog(det);
            assert(branchLog.keys == typed("HEZ"));
        }
        EmulationController det(nullptr, false, nullptr, Mode::Deterministic);
        std::string err;
        assert(det.playInputMovie(branch, err));
        mv::Movie m;
        assert(mv::parse(branch.data(), branch.size(), m, err));
        det.runCyclesSync(m.endCycle + 5000);
        const EmulationSnapshot s = snap(det);
        std::printf("[6] branch taken over a replay: verdict %u\n", s.movieVerdict);
        assert(s.movieVerdict == 1 && readLog(det) == branchLog);
    }

    // ---- 7: a movie on a GEN2 machine (the --movie-frames fixture) --------------------
    std::vector<uint8_t> gen2Movie;
    {
        EmulationController det(nullptr, false, nullptr, Mode::Deterministic);
        det.setCardEnabled(pom1::CardId::Gen2, true);
        std::vector<std::pair<uint16_t, uint8_t>> writes;
        for (std::size_t i = 0; i < sizeof kProgram; ++i)
            writes.emplace_back(static_cast<uint16_t>(0x0280 + i), kProgram[i]);
        writes.emplace_back(0x0013, 0);
        det.writeMemoryBatch(writes);
        det.jumpTo(0x0280);
        std::string err;
        assert(det.startInputMovieRecording(err));
        det.runCyclesSync(40000);
        det.queueKey('G');
        det.deliverQueuedKeys();
        det.runCyclesSync(60000);
        assert(det.stopInputMovie(&gen2Movie));
        assert(snap(det).gen2Enabled);
        EmulationController replay(nullptr, false, nullptr, Mode::Deterministic);
        assert(replay.playInputMovie(gen2Movie, err));
        replay.runCyclesSync(110000);
        const EmulationSnapshot s = snap(replay);
        std::printf("[7] GEN2 machine movie: verdict %u, card %s\n", s.movieVerdict,
                    s.gen2Enabled ? "plugged" : "MISSING");
        assert(s.movieVerdict == 1 && s.gen2Enabled && "the snapshot brings the card back");
    }

    if (!fixtures.empty()) {
        const auto write = [&fixtures](const char* name, const std::vector<uint8_t>& bytes) {
            const std::string path = fixtures + "/" + name;
            FILE* f = std::fopen(path.c_str(), "wb");
            assert(f && "fixture directory must exist");
            const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            assert(written == bytes.size());
        };
        mv::Movie tampered;
        std::string e;
        assert(mv::parse(rerecorded.data(), rerecorded.size(), tampered, e));
        tampered.endHash ^= 1;
        write("rerecorded.p1m", rerecorded);
        write("tampered.p1m", mv::serialize(tampered));
        write("gen2.p1m", gen2Movie);
        std::printf("fixtures written to %s\n", fixtures.c_str());
    }

    std::printf("input_movie_smoke: OK\n");
    return 0;
}
