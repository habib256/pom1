// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "TapePulseDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pom1 {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHighPassHz = 100.0;
constexpr double kLowPassMaxHz = 6000.0;
constexpr double kFastFollowerSeconds = 0.030;
constexpr double kSlowFollowerSeconds = 0.5;
constexpr double kFastHysteresis = 0.12;
constexpr double kSlowHysteresis = 0.25;
constexpr double kHysteresisFloor = 0.003;   // ≈ -50 dBFS

/// RBJ cookbook low-pass, transposed direct form II.
struct LowPass {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    LowPass(double fs, double f0, double q)
    {
        const double w = 2.0 * kPi * f0 / fs;
        const double alpha = std::sin(w) / (2.0 * q);
        const double cw = std::cos(w);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) / 2.0 / a0;
        b1 = (1.0 - cw) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double run(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace

TapePulses decodeTapePulses(const std::vector<float>& mono, std::uint32_t sampleRate,
                            std::uint32_t timebaseHz)
{
    TapePulses out;
    if (mono.empty()) {
        out.error = "Audio file does not contain samples";
        return out;
    }
    if (sampleRate == 0 || timebaseHz == 0) {
        out.error = "Audio file has an invalid sample rate";
        return out;
    }

    const double fs = static_cast<double>(sampleRate);
    const double rc = 1.0 / (2.0 * kPi * kHighPassHz);
    const double hpAlpha = rc / (rc + 1.0 / fs);
    const bool useLowPass = fs >= 16000.0;
    LowPass lowPass(fs, std::min(fs * 0.45, kLowPassMaxHz), 0.707);
    const double fastDecay = std::exp(-1.0 / (kFastFollowerSeconds * fs));
    const double slowDecay = std::exp(-1.0 / (kSlowFollowerSeconds * fs));

    // Edge times in SAMPLES (fractional). edges[0] is where the signal starts:
    // the level from there to edges[1] is `initialLevel`.
    std::vector<double> edges;
    const double glitchSamples = kTapeGlitchSeconds * fs;

    double prevX = mono[0];
    double hp = 0.0, fast = 0.0, slow = 0.0, prevY = 0.0;
    int state = 0;                         // -1 low, +1 high, 0 not yet locked
    double lastZeroCrossing = -1.0;
    double lastEdge = -std::numeric_limits<double>::infinity();
    bool initialLevel = false;

    for (std::size_t n = 0; n < mono.size(); ++n) {
        const double x = std::isfinite(mono[n]) ? static_cast<double>(mono[n]) : 0.0;
        hp = hpAlpha * (hp + x - prevX);
        prevX = x;
        const double y = useLowPass ? lowPass.run(hp) : hp;

        const double mag = std::fabs(y);
        fast = std::max(mag, fast * fastDecay);
        slow = std::max(mag, slow * slowDecay);
        const double h = std::max({kFastHysteresis * fast, kSlowHysteresis * slow,
                                   kHysteresisFloor});

        if (n > 0 && (y >= 0.0) != (prevY >= 0.0)) {
            const double frac = (prevY != y) ? prevY / (prevY - y) : 0.0;
            lastZeroCrossing = static_cast<double>(n) - 1.0 + frac;
        }
        prevY = y;

        const int next = y > h ? 1 : (y < -h ? -1 : state);
        if (next == state) continue;

        // The edge sits at the zero crossing that led here, unless that
        // crossing is stale (already used, or never seen).
        const double t = (lastZeroCrossing > lastEdge) ? lastZeroCrossing
                                                       : static_cast<double>(n);
        if (state == 0) initialLevel = next > 0;
        state = next;
        lastEdge = t;

        // Glitch filter: a pulse shorter than kTapeGlitchSeconds removes BOTH
        // of its edges. Removing a pair keeps the parity, so `initialLevel`
        // stays right whatever is dropped — even edges[0].
        if (!edges.empty() && t - edges.back() < glitchSamples) {
            edges.pop_back();
            continue;
        }
        edges.push_back(t);
    }

    if (edges.size() < 2) {
        out.error = "Audio file does not contain a detectable cassette signal";
        return out;
    }

    // Round on the absolute timeline, then difference: per-interval rounding
    // would let the error walk, and the interpolated edges are worth keeping.
    const double ticksPerSample = static_cast<double>(timebaseHz) / fs;
    const double maxTicks = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    out.durations.reserve(edges.size() - 1);
    double previous = std::round(edges[0] * ticksPerSample);
    for (std::size_t i = 1; i < edges.size(); ++i) {
        const double current = std::round(edges[i] * ticksPerSample);
        // Clamp in the double domain before narrowing: an out-of-range
        // double-to-uint32 conversion is undefined behaviour, not a wrap.
        const double d = std::min(std::max(current - previous, 1.0), maxTicks);
        out.durations.push_back(static_cast<std::uint32_t>(d));
        previous = current;
    }
    out.initialLevel = initialLevel;
    out.ok = true;
    return out;
}

} // namespace pom1
