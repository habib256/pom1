// pom1::decodeTapePulses (src/TapePulseDecoder.h) — pure, no CPU, no file.
//
// A synthetic Apple-1 tape — leader, start bit, a run of "0" and "1" bits, in
// the CPU-cycle half-periods the Woz ACI WRITE routine produces — is rendered
// to audio, damaged one way at a time, and decoded. Every half-period must come
// back within a tolerance far inside the ROM's own decision margin (a "0" bit
// is ~476 cycles per full cycle, a "1" ~946; the ROM splits them around 700).
//
// Each of these used to fail SILENTLY with the fixed ±0.02 threshold — the ROM
// just waits forever with nothing on screen:
//
//   §1  clean 44.1 kHz, and the edge INTERPOLATION at 11 025 Hz, where one
//       sample is 93 CPU cycles against a 238-cycle "0" half-period;
//   §2  a recording at -36 dBFS (under the old absolute threshold);
//   §3  a DC offset of +0.3;
//   §4  50 Hz mains hum at 0.7 of the signal level;
//   §5  white noise at ~20 dB SNR;
//   §6  20 µs spikes — a glitch drops BOTH of its edges;
//   §7  an inverted recording: same durations, opposite initial level;
//   §8  refusals: nothing, silence, a rate of 0 — no durations escape.

#include "TapePulseDecoder.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr double kCpuHz = 1022727.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<std::uint32_t> tapeDurations()
{
    std::vector<std::uint32_t> d;
    for (int i = 0; i < 1600; ++i) d.push_back(592);       // ~0.93 s leader
    d.push_back(180); d.push_back(238);                     // start bit
    for (int i = 0; i < 400; ++i) {                         // 400 bits
        const bool one = ((i * 7) % 5) < 2;
        const std::uint32_t half = one ? 473 : 238;
        d.push_back(half); d.push_back(half);
    }
    return d;
}

// Render with a box filter over each sample period — what an ADC's anti-alias
// stage does to an edge: the sample that straddles it carries its position.
std::vector<float> render(const std::vector<std::uint32_t>& d, double fs, float amp,
                          double leadSeconds = 0.25, double tailSeconds = 0.25)
{
    std::vector<double> edges;     // seconds
    double t = leadSeconds;
    edges.push_back(t);
    for (std::uint32_t c : d) { t += c / kCpuHz; edges.push_back(t); }
    const double end = t + tailSeconds;
    std::vector<float> out(static_cast<std::size_t>(end * fs), 0.0f);
    std::size_t e = 0;
    for (std::size_t n = 0; n < out.size(); ++n) {
        const double a = n / fs, b = (n + 1) / fs;
        double acc = 0.0;
        double cursor = a;
        while (cursor < b) {
            while (e < edges.size() && edges[e] <= cursor) ++e;
            const double next = (e < edges.size()) ? std::min(edges[e], b) : b;
            // Level in [cursor, next): silent before edges[0] and after the
            // last; otherwise +1 for even intervals, -1 for odd.
            double level = 0.0;
            if (e > 0 && e < edges.size()) level = ((e - 1) % 2 == 0) ? 1.0 : -1.0;
            acc += level * (next - cursor);
            cursor = next;
        }
        out[n] = static_cast<float>(amp * acc * fs);
    }
    return out;
}

struct Lcg {
    std::uint32_t s = 12345;
    float next() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f * 2.0f - 1.0f; }
};

constexpr std::size_t kStartBit = 1600;   // index of the 180-cycle half in truth

// Worst |decoded - true|, aligned on the START BIT — the one place a tape says
// "data begins here", and the place the ROM itself syncs. Edges a defect draws
// BEFORE the leader (hum in the lead silence, say) are noise the ROM ignores
// while it waits for the leader, so they must not shift the comparison. The
// first 100 leader halves are skipped while the level followers settle, and
// the last half-period, which ends in the tail, is not compared.
// Returns -1 when the start bit is not found or the data is cut short.
double worstError(const pom1::TapePulses& p, const std::vector<std::uint32_t>& truth)
{
    if (!p.ok) return -1.0;
    const auto& d = p.durations;
    auto near = [](std::uint32_t got, std::uint32_t want) {
        return std::fabs(static_cast<double>(got) - want) < 60.0;
    };
    // A start bit counts only after 32 leader halves: noise in the lead
    // silence (§5) draws random edges, and three in a row can look like one.
    std::size_t at = 0;
    for (std::size_t i = 32; i + 1 < d.size() && at == 0; ++i) {
        if (!near(d[i], 180) || !near(d[i + 1], 238)) continue;
        bool leader = true;
        for (std::size_t k = 1; k <= 32 && leader; ++k) leader = near(d[i - k], 592);
        if (leader) at = i;
    }
    // The last recorded half-period ends in silence, which draws no edge.
    if (at < kStartBit - 100 || at - kStartBit + truth.size() - 1 > d.size()) return -1.0;
    // A real leader also reaches back 100 halves before the start bit; edges
    // the noise drew ahead of it are allowed, missing leader is not.
    double worst = 0.0;
    for (std::size_t i = 100; i + 1 < truth.size(); ++i) {
        const std::size_t j = at - kStartBit + i;
        worst = std::max(worst, std::fabs(static_cast<double>(d[j]) - truth[i]));
    }
    return worst;
}

void expectClose(const char* label, const std::vector<float>& audio, double fs,
                 const std::vector<std::uint32_t>& truth, double tolerance,
                 bool expectHighFirst = true)
{
    const pom1::TapePulses p = pom1::decodeTapePulses(audio, static_cast<std::uint32_t>(fs),
                                                      static_cast<std::uint32_t>(kCpuHz));
    const double worst = worstError(p, truth);
    std::printf("  %-34s %6zu edges  worst error %6.1f cycles (limit %.0f)\n", label,
                p.durations.size(), worst, tolerance);
    if (worst < 0.0 || worst > tolerance || p.initialLevel != expectHighFirst) {
        std::fprintf(stderr, "FAIL: %s (ok=%d, %zu durations for %zu recorded, initial=%d)\n",
                     label, p.ok ? 1 : 0, p.durations.size(), truth.size(),
                     p.initialLevel ? 1 : 0);
        std::exit(1);
    }
}

} // namespace

int main()
{
    const std::vector<std::uint32_t> truth = tapeDurations();

    // §1 Clean, and interpolation at a low rate.
    expectClose("clean 44.1 kHz", render(truth, 44100, 0.7f), 44100, truth, 12);
    expectClose("clean 48 kHz", render(truth, 48000, 0.7f), 48000, truth, 12);
    expectClose("clean 11025 Hz (1 sample = 93 cyc)", render(truth, 11025, 0.7f), 11025, truth, 40);

    // §2 -36 dBFS: the old ±0.02 threshold never saw it.
    expectClose("level -36 dBFS", render(truth, 44100, 0.0158f), 44100, truth, 12);

    // §3 DC offset.
    {
        auto a = render(truth, 44100, 0.5f);
        for (float& v : a) v += 0.3f;
        expectClose("DC offset +0.3", a, 44100, truth, 12);
    }

    // §4 Mains hum at 0.7 of the signal.
    {
        auto a = render(truth, 44100, 0.5f);
        for (std::size_t n = 0; n < a.size(); ++n)
            a[n] += 0.35f * static_cast<float>(std::sin(2.0 * kPi * 50.0 * n / 44100.0));
        expectClose("50 Hz hum at 0.7x", a, 44100, truth, 30);
    }

    // §5 Noise, ~20 dB SNR.
    {
        auto a = render(truth, 44100, 0.5f);
        Lcg rng;
        for (float& v : a) v += 0.05f * rng.next() * 1.7f;
        expectClose("white noise ~20 dB SNR", a, 44100, truth, 30);
    }

    // §6 Glitches: a 20 µs spike against the level every ~40 ms.
    {
        auto a = render(truth, 44100, 0.5f);
        const std::size_t first = static_cast<std::size_t>(0.3 * 44100);
        for (std::size_t n = first; n + 1 < a.size(); n += 1764) {
            // Only inside a flat run, 5 samples (113 µs) clear of any edge on
            // each side. A spike closer than 80 µs to a real edge MERGES with
            // it — the pair dropped is (real edge, spike start) and the edge
            // moves by the spike's width — which is the filter's documented
            // behaviour, not what this section pins.
            if (n < 5 || n + 6 > a.size()) continue;
            bool flat = true;
            for (std::size_t k = n - 5; k <= n + 5 && flat; ++k)
                flat = std::fabs(a[k]) >= 0.45f && (a[k] > 0) == (a[n] > 0);
            if (!flat) continue;
            a[n] = -a[n];                                  // one sample = 22.7 µs
        }
        expectClose("20 us spikes (glitch filter)", a, 44100, truth, 12);
    }

    // §7 Inverted recording.
    {
        auto a = render(truth, 44100, 0.7f);
        for (float& v : a) v = -v;
        expectClose("inverted polarity", a, 44100, truth, 12, /*expectHighFirst=*/false);
    }

    // §8 Refusals carry nothing.
    {
        auto refused = [](const pom1::TapePulses& p) {
            return !p.ok && p.durations.empty() && !p.error.empty();
        };
        assert(refused(pom1::decodeTapePulses({}, 44100, 1022727)));
        assert(refused(pom1::decodeTapePulses(std::vector<float>(44100, 0.0f), 44100, 1022727)));
        assert(refused(pom1::decodeTapePulses(std::vector<float>(44100, 0.001f), 44100, 1022727)));
        assert(refused(pom1::decodeTapePulses(render(truth, 44100, 0.5f), 0, 1022727)));
        assert(refused(pom1::decodeTapePulses(render(truth, 44100, 0.5f), 44100, 0)));
        // Non-finite samples are silence, not a crash.
        auto a = render(truth, 44100, 0.5f);
        a[100] = std::nanf("");
        a[200] = INFINITY;
        assert(pom1::decodeTapePulses(a, 44100, 1022727).ok);
    }

    std::puts("tape_pulse_decoder_smoke: OK");
    return 0;
}
