#pragma once
// Gen2FieldRing.h -- every GEN2 field the emulator completes, and which one the
// screen shows next. Pure: no ImGui, no GLFW, no Memory. The emulation thread
// fills the ring (SnapshotPublisher), the GEN2 window drains it through a
// Gen2FieldPacer, and a test can drive both halves without either thread.
//
// WHY THIS EXISTS. Uncle Bernie's vertical-split demo scrolls a text window one
// scanline per field (Applefritter, 16 sept. 2026): "The fine scrolling action
// should be smooth (as on the real hardware) but on POM1 it's staggering along,
// as if it could not do 60 frames per second." The emulation was exact --
// gen2_vsplits_smoke proves one line per field -- but the SCREEN was not. At
// each refresh the window drew the LATEST completed field. Fields end on the
// emulation thread's clock, refreshes on the monitor's, and a few milliseconds
// of jitter on either side made one refresh see no new field and the next see
// two: the band stood still, then jumped two lines. Simulated
// (gen2_field_pacer_smoke), 4 % of refreshes with 0.5 ms of jitter and 28 % with
// 4 ms -- what an old laptop delivers.
//
// The cure is a jitter buffer. The emulation thread keeps the last few fields,
// numbered; the pacer shows field N+1 at every refresh, holding a margin of two
// fields so a late field or a late refresh changes nothing. It only repeats a
// field when none is ready, and only skips when four are queued -- the two
// clocks are then a whole field apart, which on a 60 Hz monitor happens once
// every ~18 s (the GEN2's 60.05 Hz against the monitor's 60 Hz), not several
// times a second. Same simulation: 7 hiccups in two minutes at 4 ms of jitter,
// where showing the latest field gave 2 010.
//
// A field carries what the renderer reads, frozen when the field completed:
// the HGR pages (the per-scanline beam latch, $2000-$5FFF), the TEXT/LORES
// pages ($0400-$0BFF), the soft-switch journal and the state it started from.
// The text pages used to be read live at render time, so a program rewriting
// them could show one field's switches over the next field's text.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Gen2VideoScanner.h"

namespace pom1 {

struct Gen2Field {
    uint64_t seq = 0;                                   // 1, 2, 3... in completion order
    std::array<uint8_t, 0x4000> hgr{};                  // $2000-$5FFF (both HGR pages)
    std::array<uint8_t, 0x0800> text{};                 // $0400-$0BFF (both TEXT/LORES pages)
    std::vector<Gen2VideoScanner::Event> events;        // the field's soft-switch journal
    Gen2VideoScanner::DisplayState frameStart{};        // state when the field began
    Gen2VideoScanner::DisplayState endState{};          // state when it ended
    bool fiftyHz = false;
};

/// Oldest first, strictly increasing `seq`. Immutable fields, shared between
/// the emulation thread and every snapshot copy -- a copy costs a refcount.
using Gen2FieldRing = std::vector<std::shared_ptr<const Gen2Field>>;

/// How many completed fields the emulation thread keeps. The pacer never asks
/// for a field older than the newest minus kMargin, so three would do; six
/// leaves room to spare.
inline constexpr std::size_t kGen2FieldRingCapacity = 6;

/// The soft-switch state a field ends in: its start state with every journaled
/// flip applied, in order.
inline Gen2VideoScanner::DisplayState gen2EndState(const Gen2VideoScanner::DisplayState& start,
                                                   const std::vector<Gen2VideoScanner::Event>& events)
{
    Gen2VideoScanner::DisplayState s = start;
    for (const auto& e : events) {
        switch (e.kind) {
            case Gen2VideoScanner::EventKind::TextMode:  s.textMode  = e.value; break;
            case Gen2VideoScanner::EventKind::MixedMode: s.mixedMode = e.value; break;
            case Gen2VideoScanner::EventKind::Page2:     s.page2     = e.value; break;
            case Gen2VideoScanner::EventKind::HiRes:     s.hiRes     = e.value; break;
        }
    }
    return s;
}

/// Which field to show at this refresh, as a decision over sequence numbers.
class Gen2FieldPacer {
public:
    /// Fields kept in reserve behind the newest one, in steady state.
    static constexpr uint64_t kMargin = 2;
    /// Queued fields past the one shown that trigger a skip back to kMargin.
    static constexpr uint64_t kSkipAt = 4;

    /// The seq to show, given the newest completed one (0 = none yet).
    uint64_t pickSeq(uint64_t newest)
    {
        if (newest == 0) return shown_;
        if (shown_ == 0 || newest < shown_) {        // first field, or a new timeline
            shown_ = newest > kMargin ? newest - kMargin : 1;
            return shown_;
        }
        const uint64_t queued = newest - shown_;
        if (queued >= kSkipAt) shown_ = newest - kMargin;   // clocks a whole field apart
        else if (queued > 0) ++shown_;                      // the normal case: the next one
        // queued == 0: nothing new has completed; show the same field again
        return shown_;
    }

    /// The field to show from `ring`, or nullptr when it is empty. The seq the
    /// pacer asks for is always in the ring: it is never older than the newest
    /// minus kMargin, and the ring keeps kGen2FieldRingCapacity > kMargin.
    const Gen2Field* pick(const Gen2FieldRing& ring)
    {
        if (ring.empty()) return nullptr;
        const uint64_t want = pickSeq(ring.back()->seq);
        for (auto it = ring.rbegin(); it != ring.rend(); ++it)
            if ((*it)->seq <= want) return it->get();
        return ring.front().get();
    }

    /// The 64 KB address space the GEN2 renderer indexes, holding `field`'s
    /// pages: TEXT/LORES at $0400, HGR at $2000. Nothing else is read.
    const uint8_t* compose(const Gen2Field& field)
    {
        if (memory_.empty()) memory_.assign(0x10000, 0);
        std::copy(field.text.begin(), field.text.end(), memory_.begin() + 0x0400);
        std::copy(field.hgr.begin(), field.hgr.end(), memory_.begin() + 0x2000);
        return memory_.data();
    }

    uint64_t shownSeq() const { return shown_; }

private:
    uint64_t shown_ = 0;
    std::vector<uint8_t> memory_;
};

} // namespace pom1
