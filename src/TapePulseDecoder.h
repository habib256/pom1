// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_TAPE_PULSE_DECODER_H
#define POM1_TAPE_PULSE_DECODER_H

// Cassette audio → the level transitions the ACI's input comparator would see.
//
// PURE: mono samples in, half-period durations out. No file, no device, no log
// sink. CassetteDevice feeds the result to `$C081` one transition at a time and
// the Woz ACI ROM does the actual bit decoding — this stage only has to find
// the EDGES, and find them where a real ACI would.
//
// The real ACI front end is an AC-coupled input into a comparator with
// hysteresis, so it never saw the DC offset, mains hum or level of a recording
// the way a fixed ±0.02 threshold on raw samples did. That threshold — POM1's
// decoder until November 2026 — failed SILENTLY (the ROM just waits forever)
// on four ordinary real-world defects: a recording at -36 dBFS, a DC offset of
// +0.3, 50 Hz hum at 0.7 of the signal, and (in the channel mixdown, now fixed
// in PcmFile.cpp) a phase-inverted stereo pair. The chain below handles all of
// them; `tape_pulse_decoder_smoke` and `aci_tape_robustness` pin each one.
//
//   1. High-pass, 1 pole at 100 Hz — the coupling capacitor's job: DC gone,
//      hum attenuated. (A 20 Hz corner left a 0.7× hum able to flip edges.)
//   2. Low-pass, biquad Q=0.707 at min(0.45·fs, 6 kHz), when fs >= 16 kHz —
//      hiss above the ACI's 1-2 kHz band.
//   3. Hysteresis RELATIVE to the signal: max(12 % of a 30 ms peak follower,
//      25 % of a 0.5 s one, an absolute floor at -50 dBFS). The slow follower is
//      a squelch: once the tape stops, background noise stays under a quarter
//      of the recent level and draws no edges.
//   4. The edge is placed at the interpolated ZERO CROSSING that preceded the
//      hysteresis decision, not at the sample that made it — at 11 025 Hz one
//      sample is 93 CPU cycles, against a 238-cycle "0" half-period.
//   5. A pulse shorter than 80 µs (≈ 82 cycles; the shortest legal ACI
//      half-period is the 180-cycle start bit) is a glitch: both of its edges
//      are dropped.
//
// [D] RDK Apple-1 crates/apple1-core/src/tape.rs detect_edges() and deglitch()
//     @ 0ef0d79 (Old-Wirehead/rdk-apple1) — every constant above, including the
//     100 Hz corner, raised there from 20 Hz for the hum case (its issue #47).
// [F] All of them are tuned, not measured on an ACI. Settled by: a scope on a
//     real ACI's comparator output while playing degraded recordings.

#include <cstdint>
#include <string>
#include <vector>

namespace pom1 {

struct TapePulses {
    bool ok = false;
    /// Successive half-periods, in units of the caller's `timebaseHz`.
    std::vector<std::uint32_t> durations;
    /// The level durations[0] is spent at.
    bool initialLevel = false;
    /// Set when `ok` is false.
    std::string error;
};

/// Shortest pulse kept, in seconds (see 5. above).
inline constexpr double kTapeGlitchSeconds = 0.000080;

/// Decode `mono` (any level, any DC, sampled at `sampleRate`) into transition
/// durations of `timebaseHz` ticks each. Durations are differences of edge
/// times rounded on an ABSOLUTE timeline, so the rounding never accumulates;
/// each is at least 1. Fails — with no durations — when no signal stands out
/// of the floor.
TapePulses decodeTapePulses(const std::vector<float>& mono, std::uint32_t sampleRate,
                            std::uint32_t timebaseHz);

} // namespace pom1

#endif // POM1_TAPE_PULSE_DECODER_H
