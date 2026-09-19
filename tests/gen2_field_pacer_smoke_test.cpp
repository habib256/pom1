// gen2_field_pacer_smoke -- the GEN2 window shows one emulated field per
// refresh, and that is what makes a fine scroll smooth.
//
// Uncle Bernie (16 sept. 2026): his one-line-per-field scroll "is staggering
// along, as if it could not do 60 frames per second". The emulation was exact;
// the window drew whichever field was the LATEST at each refresh, so jitter on
// either clock made some refreshes see no new field and others two.
// Gen2FieldPacer (Gen2FieldRing.h) shows field N+1 at every refresh from a
// ring of recent fields, with two in reserve.
//
// Sections:
//   1. the policy, step by step: margin, steady state, stall, catch-up, reset
//   2. pick() finds the field in a ring; compose() lays its pages out
//   3. a field's end state is its start state with its flips applied
//   4. the claim itself, by simulation: the two clocks as they really run
//      (slices of ~1 ms on the emulation side, jittered refreshes on the
//      host side), counting refreshes that do not advance by exactly one field

#include "Gen2FieldRing.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using pom1::Gen2Field;
using pom1::Gen2FieldPacer;
using pom1::Gen2FieldRing;
using Kind = Gen2VideoScanner::EventKind;

// Deterministic randomness: xorshift64* and Box-Muller.
struct Rng {
    uint64_t s;
    double uniform()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<double>((s * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
    }
    double gauss()
    {
        const double u1 = uniform() + 1e-12, u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
};

struct Tally { int hiccups = 0; int refreshes = 0; };

// The emulation thread publishes after every slice (~1.06 ms of sleep, a bit
// more under load); a field completes every 17 030 cycles. The UI samples the
// newest completed field at each refresh, `jitterMs` late or early.
Tally simulate(double cpuHz, double refreshHz, double jitterMs, bool usePacer,
               double seconds = 120.0)
{
    Rng rng{0x9E3779B97F4A7C15ULL};
    std::vector<double> times;
    std::vector<uint64_t> newestAt;
    for (double t = 0.0; t < seconds;) {
        t += 1.06e-3 * (0.95 + 0.35 * rng.uniform());
        times.push_back(t);
        newestAt.push_back(static_cast<uint64_t>(t * cpuHz / 17030.0) + 1);  // seq from 1
    }
    Gen2FieldPacer pacer;
    Tally tally;
    uint64_t last = 0;
    std::size_t cursor = 0;
    for (int k = 0;; ++k) {
        const double ts = k / refreshHz + rng.gauss() * jitterMs * 1e-3;
        if (ts >= seconds - 0.1) break;
        // newest field published at or before ts (times is sorted; ts wanders
        // by a few ms, so search from a cursor kept a little behind).
        while (cursor > 0 && times[cursor] > ts) --cursor;
        while (cursor + 1 < times.size() && times[cursor + 1] <= ts) ++cursor;
        if (times[cursor] > ts) continue;
        const uint64_t newest = newestAt[cursor];
        const uint64_t shown = usePacer ? pacer.pickSeq(newest) : newest;
        if (last != 0) {
            ++tally.refreshes;
            if (shown != last + 1) ++tally.hiccups;
        }
        last = shown;
    }
    return tally;
}

std::shared_ptr<const Gen2Field> field(uint64_t seq)
{
    auto f = std::make_shared<Gen2Field>();
    f->seq = seq;
    f->text[0] = static_cast<uint8_t>(0xC0 + seq);        // $0400
    f->hgr[0]  = static_cast<uint8_t>(seq);               // $2000
    f->hgr[0x2000] = static_cast<uint8_t>(0x80 + seq);    // $4000
    return f;
}

} // namespace

int main()
{
    // ---- 1: the policy ----------------------------------------------------------
    {
        Gen2FieldPacer p;
        assert(p.pickSeq(0) == 0 && "nothing completed, nothing to show");
        assert(p.pickSeq(10) == 8 && "start two fields behind the newest");
        assert(p.pickSeq(11) == 9 && p.pickSeq(12) == 10 && "steady: one per refresh");
        // A stall on the emulation side spends the reserve, then repeats.
        assert(p.pickSeq(12) == 11 && p.pickSeq(12) == 12 && p.pickSeq(12) == 12);
        // Fields keep coming at one per refresh: one per refresh, from where we are.
        assert(p.pickSeq(13) == 13 && p.pickSeq(14) == 14);
        // Two fields in one refresh: still ONE step -- the reserve refills.
        assert(p.pickSeq(16) == 15 && p.pickSeq(17) == 16);
        // Four queued: the clocks are a whole field apart; skip back to the margin.
        assert(p.pickSeq(20) == 18 && "four queued -> newest minus the margin");
        // A new timeline (snapshot restore, a new controller): start over.
        assert(p.pickSeq(3) == 1 && p.pickSeq(4) == 2);
        std::printf("[1] policy: margin 2, one step per refresh, skip at 4 queued\n");
    }

    // ---- 2: pick() and compose() --------------------------------------------------
    {
        Gen2FieldPacer p;
        Gen2FieldRing ring;
        assert(p.pick(ring) == nullptr);
        for (uint64_t s = 1; s <= 6; ++s) ring.push_back(field(s));
        const Gen2Field* f = p.pick(ring);
        assert(f && f->seq == 4 && "newest 6 -> field 4");
        const uint8_t* mem = p.compose(*f);
        assert(mem[0x0400] == 0xC4 && mem[0x2000] == 4 && mem[0x4000] == 0x84
               && "TEXT at $0400, HGR page 1 at $2000, page 2 at $4000");
        ring.push_back(field(7));
        ring.erase(ring.begin());
        f = p.pick(ring);
        assert(f && f->seq == 5);
        std::printf("[2] pick + compose: field %llu laid out at $0400/$2000/$4000\n",
                    static_cast<unsigned long long>(f->seq));
    }

    // ---- 3: end state ----------------------------------------------------------------
    {
        Gen2VideoScanner::DisplayState start;             // cold: GRAPHICS + HIRES
        const std::vector<Gen2VideoScanner::Event> flips = {
            {100, Kind::HiRes, false}, {200, Kind::TextMode, true},
            {300, Kind::TextMode, false}, {400, Kind::Page2, true},
        };
        const auto end = pom1::gen2EndState(start, flips);
        assert(!end.textMode && !end.hiRes && end.page2 && !end.mixedMode);
        assert(pom1::gen2EndState(start, {}) == start);
        std::printf("[3] end state folds the journal in order\n");
    }

    // ---- 4: the claim ------------------------------------------------------------------
    //
    // The Apple-1's crystal against a 60 Hz monitor: fields at 60.055 Hz, so the
    // two clocks drift a whole field apart every ~18 s -- about 7 unavoidable
    // skips in 120 s whatever the display does. Showing the latest field adds
    // hundreds to thousands on top, growing with the jitter; the pacer must stay
    // at the unavoidable few.
    {
        const double cpuHz = 1022727.0;
        std::printf("[4] 120 s at 60 Hz, refreshes that do not advance by exactly one field:\n");
        for (double jitter : {0.5, 2.0, 4.0}) {
            const Tally latest = simulate(cpuHz, 60.0, jitter, false);
            const Tally paced  = simulate(cpuHz, 60.0, jitter, true);
            std::printf("    jitter %.1f ms: latest %4d (%.1f %%), paced %d\n", jitter,
                        latest.hiccups, 100.0 * latest.hiccups / latest.refreshes, paced.hiccups);
            assert(paced.hiccups <= 10 && "paced: only the clock drift, ~7 in 120 s");
            assert(latest.hiccups >= 20 * paced.hiccups && "the latest-field form is the stagger");
        }
        // A monitor running at the field rate itself: nothing to skip at all.
        const Tally locked = simulate(cpuHz, cpuHz / 17030.0, 4.0, true);
        std::printf("    monitor at the field rate, 4 ms jitter: paced %d\n", locked.hiccups);
        assert(locked.hiccups <= 1);
    }

    std::printf("gen2_field_pacer_smoke: OK\n");
    return 0;
}
