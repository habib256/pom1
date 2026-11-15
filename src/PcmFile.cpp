// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// The two cassette containers POM1 decodes itself, as pure functions. Lifted
// out of CassetteDevice, which read the file, parsed it, mixed it and stored
// the result in one pass — so neither parser could be tested or fuzzed without
// a whole audio device, and neither could bound what it was about to allocate.
//
// Both formats are chunked: a 12-byte header, then a sequence of
// (4-byte tag, 4-byte size, payload) with word alignment. WAV is
// little-endian, AIFF big-endian, and that is very nearly the whole difference
// until the sample decoding.

#include "PcmFile.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace pom1 {
namespace {

std::uint16_t readLe16(const std::uint8_t* d) { return static_cast<std::uint16_t>(d[0] | (d[1] << 8)); }
std::uint32_t readLe32(const std::uint8_t* d)
{
    return static_cast<std::uint32_t>(d[0]) | (static_cast<std::uint32_t>(d[1]) << 8) |
           (static_cast<std::uint32_t>(d[2]) << 16) | (static_cast<std::uint32_t>(d[3]) << 24);
}
std::uint16_t readBe16(const std::uint8_t* d) { return static_cast<std::uint16_t>((d[0] << 8) | d[1]); }
std::uint32_t readBe32(const std::uint8_t* d)
{
    return (static_cast<std::uint32_t>(d[0]) << 24) | (static_cast<std::uint32_t>(d[1]) << 16) |
           (static_cast<std::uint32_t>(d[2]) << 8) | static_cast<std::uint32_t>(d[3]);
}

/// AIFF stores its sample rate as an 80-bit IEEE 754 extended float, a type no
/// mainstream compiler exposes. Decode the fields by hand and refuse anything
/// that is not plausibly a rate rather than trusting an out-of-range shift.
std::uint32_t readBe80Float(const std::uint8_t* data)
{
    const std::uint16_t expField = readBe16(data);
    const int exponent = static_cast<int>(expField & 0x7FFF);
    std::uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | data[2 + i];
    if (exponent == 0 || exponent == 0x7FFF || mantissa == 0) return 0;

    const int shift = exponent - 16383 - 63;
    // Sample rates live in [1, 1e7]; anything needing a shift outside this
    // window is not a rate, so bail rather than commit UB on the shift itself.
    if (shift > 0 || shift < -63) return 0;
    const double value = static_cast<double>(mantissa) * std::pow(2.0, shift);
    if (value < 1.0 || value > 10000000.0) return 0;
    return static_cast<std::uint32_t>(value + 0.5);
}

/// Float containers can carry any bit pattern, NaN and infinity included, and
/// nothing downstream is prepared for them: the pulse decoder compares each
/// sample against a threshold, and EVERY comparison against a NaN is false — so
/// a corrupt float WAV reads as a flat line and is reported as "no detectable
/// cassette signal", which tells the user nothing about what is wrong. Treat a
/// non-finite sample as silence, count it, and say so.
float sanitize(float v, std::size_t& badSamples)
{
    if (std::isfinite(v)) return v;
    ++badSamples;
    return 0.0f;
}

/// Same as sanitize(), counting only when asked: the channel mixer reads every
/// sample twice (statistics, then mix) and must count each bad one once.
float scrub(float v, std::size_t* badSamples)
{
    if (std::isfinite(v)) return v;
    if (badSamples) ++*badSamples;
    return 0.0f;
}

void noteBadSamples(std::size_t badSamples, std::string& warning)
{
    if (badSamples == 0) return;
    std::ostringstream oss;
    oss << badSamples << " non-finite sample(s) replaced with silence";
    warning = warning.empty() ? oss.str() : warning + "; " + oss.str();
}

PcmAudio fail(std::string message)
{
    PcmAudio out;
    out.ok = false;
    out.error = std::move(message);
    return out;
}

/// Apply the length cap and say so. Returns the number of frames to decode.
///
/// The cap is on FRAMES rather than bytes because that is what bounds the two
/// vectors that actually grow: the mixed-down samples here and the duration
/// list downstream.
std::size_t capFrames(std::size_t frameCount, std::uint64_t maxFrames, std::string& warning)
{
    if (static_cast<std::uint64_t>(frameCount) <= maxFrames) return frameCount;
    std::ostringstream oss;
    oss << "tape is longer than " << (maxFrames / (60ull * 96000ull))
        << " minutes — only the first part was loaded";
    warning = oss.str();
    return static_cast<std::size_t>(maxFrames);
}

/// Fold a multi-channel cassette rip down to the one signal the pulse decoder
/// reads. A cassette carries ONE signal, but a rip of it is not always two
/// clean copies of it:
///
///   * two identical channels (a mono deck into a stereo card) — the average,
///     which is what POM1 always did, and what this still returns exactly;
///   * one channel PHASE-INVERTED against the other (a balanced cable, a
///     swapped pin) — the plain average cancels to silence and the ACI ROM
///     hangs with nothing on screen; aligned on sign, the two add up;
///   * the signal in ONE channel and hiss or nothing in the other — averaging
///     halves the level and mixes the hiss in; the signal channel alone is
///     cleaner.
///
/// So: the channel with the most AC energy (variance — a DC offset carries no
/// signal) is the reference, and every other channel joins it, sign-aligned,
/// only when it is the same signal (|correlation| >= 0.9). Silence on every
/// channel falls back to the plain average.
/// [D] RDK Apple-1 wav.rs Audio::best_channel @ 0ef0d79 picks the most
///     energetic channel alone; the sign-aligned join is POM1's, so that an
///     identical pair still decodes byte-for-byte as before.
/// [F] The 0.9 threshold is a judgement, not a measurement.
///
/// Two passes over `sampleAt(frame, channel, bad)`: statistics (bad = null,
/// nothing counted), then the mix (bad = the counter) — so a parser never holds
/// more than the mono result, and a non-finite sample is counted once, in the
/// pass that actually uses it. Channels past the eighth are
/// ignored for the decision (and get weight 0) rather than growing the matrix.
template <class SampleAt>
void mixChannels(std::size_t frameCount, std::uint16_t channels, SampleAt sampleAt,
                 std::vector<float>& mono, std::size_t& badSamples)
{
    mono.reserve(frameCount);
    if (channels == 1) {
        for (std::size_t f = 0; f < frameCount; ++f) mono.push_back(sampleAt(f, 0, &badSamples));
        return;
    }

    constexpr int kMaxStatChannels = 8;
    const int n = channels < kMaxStatChannels ? channels : kMaxStatChannels;
    double sum[kMaxStatChannels] = {};
    double cross[kMaxStatChannels][kMaxStatChannels] = {};
    for (std::size_t f = 0; f < frameCount; ++f) {
        double x[kMaxStatChannels];
        for (int c = 0; c < n; ++c) x[c] = sampleAt(f, static_cast<std::uint16_t>(c), nullptr);
        for (int i = 0; i < n; ++i) {
            sum[i] += x[i];
            for (int j = i; j < n; ++j) cross[i][j] += x[i] * x[j];
        }
    }
    const double frames = static_cast<double>(frameCount);
    auto cov = [&](int i, int j) {
        if (i > j) std::swap(i, j);
        return cross[i][j] / frames - (sum[i] / frames) * (sum[j] / frames);
    };

    int best = 0;
    for (int c = 1; c < n; ++c)
        if (cov(c, c) > cov(best, best)) best = c;

    float weight[kMaxStatChannels] = {};
    float totalWeight = 0.0f;
    const double bestVar = cov(best, best);
    if (!(bestVar > 1e-12) || !std::isfinite(bestVar)) {
        // Nothing but silence or DC anywhere: the historical plain average.
        for (int c = 0; c < n; ++c) weight[c] = 1.0f;
        totalWeight = static_cast<float>(channels);
    } else {
        for (int c = 0; c < n; ++c) {
            const double var = cov(c, c);
            if (c == best) { weight[c] = 1.0f; totalWeight += 1.0f; continue; }
            if (!(var > 1e-12)) continue;
            const double r = cov(best, c) / std::sqrt(bestVar * var);
            if (r >= 0.9)       { weight[c] = 1.0f;  totalWeight += 1.0f; }
            else if (r <= -0.9) { weight[c] = -1.0f; totalWeight += 1.0f; }
        }
    }

    for (std::size_t f = 0; f < frameCount; ++f) {
        float mixed = 0.0f;
        for (int c = 0; c < n; ++c)
            if (weight[c] != 0.0f)
                mixed += weight[c] * sampleAt(f, static_cast<std::uint16_t>(c), &badSamples);
        // Sanitize the MIX, not only each channel. Two finite channels can
        // sum to a non-finite value — 3.4e38 + 3.4e38 overflows float — so a
        // per-channel scrub leaves a NaN/Inf in the output it just promised to
        // have removed. Found by the nightly fuzz job (pcm/crash-e086c0e6…, an
        // AIFF-C fl32 file): 5 samples scrubbed, and one still non-finite.
        mono.push_back(sanitize(mixed / totalWeight, badSamples));
    }
}

/// Little-endian signed integer of 1..4 bytes, scaled to [-1, 1).
float readLeSigned(const std::uint8_t* p, std::size_t bytes)
{
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < bytes; ++i) raw |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    const int shift = static_cast<int>(32 - 8 * bytes);
    const std::int32_t value = static_cast<std::int32_t>(raw << shift);   // sign in bit 31
    return static_cast<float>(static_cast<double>(value) / 2147483648.0);
}

} // namespace

std::vector<float> mixTapeChannels(const float* interleaved, std::size_t frames,
                                   std::uint16_t channels)
{
    std::vector<float> mono;
    if (interleaved == nullptr || channels == 0) return mono;
    std::size_t bad = 0;
    auto sampleAt = [&](std::size_t frame, std::uint16_t ch, std::size_t* counter) -> float {
        return scrub(interleaved[frame * channels + ch], counter);
    };
    mixChannels(frames, channels, sampleAt, mono, bad);
    return mono;
}

PcmAudio parseWavPcm(const std::uint8_t* bytes, std::size_t size,
                     std::uint64_t maxFrames)
{
    if (size > kMaxPcmFileBytes)
        return fail("WAV file is too large to be a cassette tape");
    // 44 = the shortest possible RIFF header + fmt + data headers.
    if (size < 44 || std::memcmp(bytes, "RIFF", 4) != 0 ||
        std::memcmp(bytes + 8, "WAVE", 4) != 0)
        return fail("Invalid WAV file");

    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    const std::uint8_t* dataChunk = nullptr;
    std::uint32_t dataSize = 0;

    std::size_t offset = 12;
    while (offset + 8 <= size) {
        const std::uint8_t* chunk = bytes + offset;
        const std::uint32_t chunkSize = readLe32(chunk + 4);
        offset += 8;
        // Compare against the REMAINING byte count, never offset+chunkSize: on a
        // 32-bit size_t (wasm32) a crafted chunkSize near 0xFFFFFFFF wraps the
        // sum below size and slips straight past the guard.
        if (chunkSize > size - offset) break;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat   = readLe16(bytes + offset + 0);
            channels      = readLe16(bytes + offset + 2);
            sampleRate    = readLe32(bytes + offset + 4);
            bitsPerSample = readLe16(bytes + offset + 14);
            // WAVE_FORMAT_EXTENSIBLE: the real format is the first two bytes
            // of the SubFormat GUID (KSDATAFORMAT_SUBTYPE_PCM = 1,
            // _IEEE_FLOAT = 3). What any 24-bit or multichannel export
            // writes — refused outright until now.
            // [B] Microsoft, "WAVEFORMATEXTENSIBLE structure" (mmreg.h).
            if (audioFormat == 0xFFFE) {
                audioFormat = (chunkSize >= 40) ? readLe16(bytes + offset + 24) : 0;
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataChunk = bytes + offset;
            dataSize  = chunkSize;
        }

        offset += chunkSize + (chunkSize & 1u);   // RIFF chunks are word-aligned
    }

    if (dataChunk == nullptr || channels == 0 || sampleRate == 0)
        return fail("WAV file is missing format or data chunks");
    if (sampleRate < kMinPcmSampleRate || sampleRate > kMaxPcmSampleRate)
        return fail("WAV sample rate is not plausible for a cassette recording");
    if (audioFormat != 1 && audioFormat != 3)
        return fail("Unsupported WAV format (only PCM and float are supported)");

    // Decide the whole sample layout BEFORE decoding a single frame. The old
    // in-place version discovered an unsupported width from inside the decode
    // loop, after it had already mixed and stored every earlier frame.
    const std::size_t bytesPerSample = bitsPerSample / 8u;
    if (bytesPerSample == 0 || (bitsPerSample % 8u) != 0 ||
        dataSize < static_cast<std::uint64_t>(bytesPerSample) * channels)
        return fail("Unsupported WAV sample format");
    const bool pcm8   = audioFormat == 1 && bitsPerSample == 8;
    const bool pcmInt = audioFormat == 1 && bitsPerSample >= 16 && bitsPerSample <= 32;
    const bool flt32  = audioFormat == 3 && bitsPerSample == 32;
    const bool flt64  = audioFormat == 3 && bitsPerSample == 64;
    if (!pcm8 && !pcmInt && !flt32 && !flt64)
        return fail("Only WAV PCM 8/16/24/32-bit and float 32/64-bit are supported");

    PcmAudio out;
    const std::size_t stride = bytesPerSample * channels;
    const std::size_t frameCount = capFrames(dataSize / stride, maxFrames, out.warning);
    if (frameCount == 0) return fail("WAV file contains no audio frames");

    std::size_t badSamples = 0;
    auto sampleAt = [&](std::size_t frame, std::uint16_t ch, std::size_t* bad) -> float {
        const std::uint8_t* p = dataChunk + frame * stride + ch * bytesPerSample;
        if (pcm8) {
            // WAV's 8-bit samples are UNSIGNED — the one width where it
            // differs from AIFF, which is signed at every width.
            return (static_cast<int>(p[0]) - 128) / 128.0f;
        }
        if (pcmInt) return readLeSigned(p, bytesPerSample);
        if (flt32) {
            float f = 0.0f;
            std::memcpy(&f, p, sizeof(float));
            return scrub(f, bad);
        }
        double d = 0.0;
        std::memcpy(&d, p, sizeof(double));
        // Range-check in double: a finite double far past float's range would
        // narrow to infinity (and that conversion is undefined behaviour).
        if (!std::isfinite(d) || d > 1.0e30 || d < -1.0e30) return scrub(NAN, bad);
        return static_cast<float>(d);
    };
    mixChannels(frameCount, channels, sampleAt, out.mono, badSamples);
    noteBadSamples(badSamples, out.warning);

    out.ok = true;
    out.sampleRate = sampleRate;
    return out;
}

PcmAudio parseAiffPcm(const std::uint8_t* bytes, std::size_t size,
                      std::uint64_t maxFrames)
{
    if (size > kMaxPcmFileBytes)
        return fail("AIFF file is too large to be a cassette tape");

    const bool isAifc = size >= 12 && std::memcmp(bytes + 8, "AIFC", 4) == 0;
    if (size < 12 || std::memcmp(bytes, "FORM", 4) != 0 ||
        (std::memcmp(bytes + 8, "AIFF", 4) != 0 && !isAifc))
        return fail("Invalid AIFF file");

    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t commFrames = 0;
    bool haveComm = false;
    bool littleEndianSamples = false;   // AIFF-C 'sowt'
    bool floatSamples = false;          // AIFF-C 'fl32'
    const std::uint8_t* soundData = nullptr;
    std::uint32_t soundSize = 0;

    std::size_t offset = 12;
    while (offset + 8 <= size) {
        const std::uint8_t* chunk = bytes + offset;
        const std::uint32_t chunkSize = readBe32(chunk + 4);
        offset += 8;
        // Same overflow-safe form as the WAV parser above, for the same reason.
        if (chunkSize > size - offset) break;

        if (std::memcmp(chunk, "COMM", 4) == 0 && chunkSize >= 18) {
            const std::uint8_t* c = bytes + offset;
            channels      = readBe16(c + 0);
            commFrames    = readBe32(c + 2);
            bitsPerSample = readBe16(c + 6);
            sampleRate    = readBe80Float(c + 8);
            haveComm      = true;
            if (isAifc && chunkSize >= 22) {
                const std::uint8_t* comp = c + 18;
                if (std::memcmp(comp, "sowt", 4) == 0)      littleEndianSamples = true;
                else if (std::memcmp(comp, "fl32", 4) == 0 ||
                         std::memcmp(comp, "FL32", 4) == 0) floatSamples = true;
                else if (std::memcmp(comp, "NONE", 4) != 0)
                    return fail("Unsupported AIFF-C compression (expected NONE/sowt/fl32)");
            }
        } else if (std::memcmp(chunk, "SSND", 4) == 0 && chunkSize >= 8) {
            // SSND leads with offset + blockSize; the frames start after them,
            // displaced by `ssndOffset` bytes of alignment padding.
            const std::uint32_t ssndOffset = readBe32(bytes + offset);
            if (static_cast<std::uint64_t>(ssndOffset) + 8 > chunkSize)
                return fail("Malformed AIFF SSND chunk");
            soundData = bytes + offset + 8 + ssndOffset;
            soundSize = chunkSize - 8 - ssndOffset;
        }

        offset += chunkSize + (chunkSize & 1u);   // IFF chunks are word-aligned
    }

    if (!haveComm || soundData == nullptr || channels == 0 || sampleRate == 0)
        return fail("AIFF file is missing COMM or SSND chunks");
    if (sampleRate < kMinPcmSampleRate || sampleRate > kMaxPcmSampleRate)
        return fail("AIFF sample rate is not plausible for a cassette recording");
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 &&
        bitsPerSample != 32)
        return fail("Unsupported AIFF sample width (expected 8/16/24/32-bit)");
    if (floatSamples && bitsPerSample != 32)
        return fail("AIFF fl32 requires 32-bit samples");

    PcmAudio out;
    const std::size_t bytesPerSample = bitsPerSample / 8u;
    const std::size_t stride = bytesPerSample * channels;
    std::size_t frameCount = soundSize / stride;
    // COMM's frame count wins when it is the SMALLER of the two — trailing
    // garbage after the last frame is common in synthesised files.
    if (commFrames != 0 && commFrames < frameCount) frameCount = commFrames;
    frameCount = capFrames(frameCount, maxFrames, out.warning);
    if (frameCount == 0) return fail("AIFF file contains no audio frames");

    std::size_t badSamples = 0;
    auto sampleAt = [&](std::size_t frame, std::uint16_t ch, std::size_t* bad) -> float {
        const std::uint8_t* p = soundData + frame * stride + ch * bytesPerSample;
        if (floatSamples) {
            std::uint8_t le[4] = {p[3], p[2], p[1], p[0]};   // fl32 is big-endian
            if (littleEndianSamples) std::memcpy(le, p, 4);
            float value = 0.0f;
            std::memcpy(&value, le, sizeof(float));
            return scrub(value, bad);
        }
        if (bitsPerSample == 8) {
            // AIFF PCM is SIGNED at every width, including 8-bit — the one
            // place it differs from WAV, whose 8-bit samples are unsigned.
            return static_cast<float>(static_cast<std::int8_t>(p[0])) / 128.0f;
        }
        if (littleEndianSamples) return readLeSigned(p, bytesPerSample);
        std::uint8_t le[4] = {0, 0, 0, 0};                    // big-endian → LE
        for (std::size_t i = 0; i < bytesPerSample; ++i) le[i] = p[bytesPerSample - 1 - i];
        return readLeSigned(le, bytesPerSample);
    };
    mixChannels(frameCount, channels, sampleAt, out.mono, badSamples);
    noteBadSamples(badSamples, out.warning);

    out.ok = true;
    out.sampleRate = sampleRate;
    return out;
}

} // namespace pom1
