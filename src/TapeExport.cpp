// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "TapeExport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pom1 {

namespace {

char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

bool endsWithIgnoreCase(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() &&
           equalsIgnoreCase(s.substr(s.size() - suffix.size()), suffix);
}

void putLe16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void putLe32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
}

void putBe16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void putBe32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFF));
}

void putTag(std::vector<std::uint8_t>& out, const char (&tag)[5])
{
    out.insert(out.end(), tag, tag + 4);
}

/// AIFF's sample rate is an 80-bit IEEE 754 extended float. For a positive
/// integer the encoding is exact and needs no floating point: the exponent is
/// the index of the top set bit, the 64-bit mantissa (explicit integer bit) is
/// the value shifted up to bit 63. PcmFile.cpp's readBe80Float is the inverse.
void putBe80Rate(std::vector<std::uint8_t>& out, std::uint32_t rate)
{
    int top = 31;
    while (top > 0 && !(rate & (1u << top))) --top;
    const std::uint16_t exponent = static_cast<std::uint16_t>(16383 + top);
    const std::uint64_t mantissa = static_cast<std::uint64_t>(rate) << (63 - top);
    putBe16(out, exponent);
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((mantissa >> shift) & 0xFF));
}

std::uint64_t msToSamples(std::uint32_t ms, std::uint32_t rate)
{
    return static_cast<std::uint64_t>(ms) * rate / 1000u;
}

} // namespace

TapeContainer tapeContainerForPath(std::string_view path)
{
    if (endsWithIgnoreCase(path, ".wav")) return TapeContainer::Wav;
    if (endsWithIgnoreCase(path, ".aiff") || endsWithIgnoreCase(path, ".aif"))
        return TapeContainer::Aiff;
    return TapeContainer::Aci;
}

TapeExportProfile resolveTapeExportProfile(TapeExportProfile requested,
                                           TapeContainer container)
{
    if (requested != TapeExportProfile::Auto) return requested;
    return container == TapeContainer::Aiff ? TapeExportProfile::Smartphone
                                            : TapeExportProfile::Computer;
}

TapeRenderSettings tapeRenderSettings(TapeExportProfile profile)
{
    TapeRenderSettings s;  // the defaults ARE the Computer profile
    if (profile == TapeExportProfile::Smartphone) {
        s.sampleRate = 48000;
        s.channels = 2;
        s.peak = 22937;           // 0.70 × 32767
        s.leadSilenceMs = 1000;
        s.tailHoldMs = 100;
        s.tailSilenceMs = 1000;
    }
    return s;
}

bool parseTapeExportProfile(std::string_view text, TapeExportProfile& out)
{
    if (equalsIgnoreCase(text, "auto"))     { out = TapeExportProfile::Auto;       return true; }
    if (equalsIgnoreCase(text, "computer")) { out = TapeExportProfile::Computer;   return true; }
    if (equalsIgnoreCase(text, "phone") || equalsIgnoreCase(text, "smartphone")) {
        out = TapeExportProfile::Smartphone;
        return true;
    }
    return false;
}

const char* tapeExportProfileName(TapeExportProfile profile)
{
    switch (profile) {
        case TapeExportProfile::Computer:   return "computer";
        case TapeExportProfile::Smartphone: return "phone";
        case TapeExportProfile::Auto:       break;
    }
    return "auto";
}

TapeAudioFile renderTapeAudio(const std::vector<std::uint32_t>& durations,
                              bool initialLevel,
                              std::uint32_t timebaseHz,
                              const TapeRenderSettings& settings,
                              TapeContainer container)
{
    TapeAudioFile result;
    if (durations.empty()) {
        result.error = "No cassette output has been recorded yet";
        return result;
    }
    if (container != TapeContainer::Wav && container != TapeContainer::Aiff) {
        result.error = "Audio export needs a .wav or .aiff container";
        return result;
    }
    if (timebaseHz == 0 || settings.sampleRate == 0 || settings.channels == 0 ||
        settings.peak <= 0) {
        result.error = "Invalid tape export settings";
        return result;
    }

    // Mono frames first; channels are duplicated at write time.
    const std::int16_t hi = settings.peak;
    const std::int16_t lo = static_cast<std::int16_t>(-settings.peak);
    std::vector<std::int16_t> frames;
    frames.insert(frames.end(), msToSamples(settings.leadSilenceMs, settings.sampleRate), 0);
    bool level = initialLevel;
    for (std::uint32_t duration : durations) {
        const double exact = static_cast<double>(duration) *
                             static_cast<double>(settings.sampleRate) /
                             static_cast<double>(timebaseHz);
        const std::size_t count =
            std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(exact)));
        frames.insert(frames.end(), count, level ? hi : lo);
        level = !level;
    }
    frames.insert(frames.end(), msToSamples(settings.tailHoldMs, settings.sampleRate),
                  level ? hi : lo);
    frames.insert(frames.end(), msToSamples(settings.tailSilenceMs, settings.sampleRate), 0);

    const std::uint64_t blockAlign = 2ull * settings.channels;
    const std::uint64_t dataSize = static_cast<std::uint64_t>(frames.size()) * blockAlign;
    // The largest fixed header is AIFF's (54 bytes); both size fields are 32-bit.
    if (dataSize > std::numeric_limits<std::uint32_t>::max() - 64ull) {
        result.error = "Recording too large to save as audio";
        return result;
    }
    const std::uint32_t data32 = static_cast<std::uint32_t>(dataSize);

    std::vector<std::uint8_t>& out = result.bytes;
    out.reserve(static_cast<std::size_t>(dataSize) + 64);

    if (container == TapeContainer::Wav) {
        putTag(out, "RIFF");
        putLe32(out, 36 + data32);
        putTag(out, "WAVE");
        putTag(out, "fmt ");
        putLe32(out, 16);
        putLe16(out, 1);                                     // PCM
        putLe16(out, settings.channels);
        putLe32(out, settings.sampleRate);
        putLe32(out, static_cast<std::uint32_t>(settings.sampleRate * blockAlign));
        putLe16(out, static_cast<std::uint16_t>(blockAlign));
        putLe16(out, 16);
        putTag(out, "data");
        putLe32(out, data32);
        for (std::int16_t s : frames)
            for (std::uint16_t c = 0; c < settings.channels; ++c)
                putLe16(out, static_cast<std::uint16_t>(s));
    } else {
        // COMM (18) + SSND header (8 + 8): FORM's size counts everything after
        // its own 8-byte header. 16-bit frames keep the data even-sized, so no
        // pad byte is ever needed.
        putTag(out, "FORM");
        putBe32(out, 4 + (8 + 18) + (8 + 8) + data32);
        putTag(out, "AIFF");
        putTag(out, "COMM");
        putBe32(out, 18);
        putBe16(out, settings.channels);
        putBe32(out, static_cast<std::uint32_t>(frames.size()));
        putBe16(out, 16);
        putBe80Rate(out, settings.sampleRate);
        putTag(out, "SSND");
        putBe32(out, 8 + data32);
        putBe32(out, 0);                                     // offset
        putBe32(out, 0);                                     // block size
        for (std::int16_t s : frames)
            for (std::uint16_t c = 0; c < settings.channels; ++c)
                putBe16(out, static_cast<std::uint16_t>(s));
    }

    result.ok = true;
    return result;
}

} // namespace pom1
