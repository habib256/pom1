// Cassette export profiles (src/TapeExport.h) — pure, no CPU, no file.
//
// What this pins, and the defect each part exists to prevent:
//
//   A. Container + profile resolution: Auto must give AIFF the Smartphone
//      profile and WAV the Computer one, and an explicit profile must never be
//      overridden by the extension.
//   B. The Computer WAV is BYTE-IDENTICAL to what CassetteDevice::saveWavTape()
//      wrote before profiles existed. Every .wav already circulating was made
//      that way; a "harmless" header tweak would be a silent format change.
//      The reference below is that function's body, transcribed.
//   C. The Smartphone AIFF: 48 kHz in an exact 80-bit rate, two IDENTICAL
//      channels (a phone cable ties L and R together — a difference between
//      them is a different signal at the ACI), 70 % peak, one second of digital
//      silence at each end, and it parses back through POM1's own reader.
//   D. Every half-period survives: the edges in the rendered audio, read back
//      with the same threshold the pulse decoder uses, are the recorded ones.
//   E. Refusals carry no bytes.
//
// The end-to-end proof — the Woz ACI READ routine loading a Smartphone file
// back into RAM — is aci_tape_saving's job, not this test's.

#include "PcmFile.h"
#include "TapeExport.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using pom1::TapeContainer;
using pom1::TapeExportProfile;

namespace {

constexpr std::uint32_t kTimebase = 1022727;  // POM1_CPU_CLOCK_HZ

// A small but realistic recording: a few leader cycles, a start bit, and a run
// of "0" and "1" half-periods, with the cycle counts the Woz ACI WRITE routine
// actually produces on POM1.
std::vector<std::uint32_t> sampleDurations()
{
    std::vector<std::uint32_t> d;
    for (int i = 0; i < 40; ++i) d.push_back(592);     // leader
    d.push_back(180); d.push_back(238);                 // start bit
    for (int i = 0; i < 16; ++i) {
        const bool one = (i % 3) == 0;
        d.push_back(one ? 473 : 238);                   // bit, both halves
        d.push_back(one ? 473 : 238);
    }
    return d;
}

// --- B's reference: CassetteDevice::saveWavTape() before TapeExport existed --
void putLe16(std::vector<std::uint8_t>& o, std::uint16_t v)
{
    o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    o.push_back(static_cast<std::uint8_t>(v >> 8));
}
void putLe32(std::vector<std::uint8_t>& o, std::uint32_t v)
{
    for (int s = 0; s < 32; s += 8) o.push_back(static_cast<std::uint8_t>((v >> s) & 0xFF));
}
std::vector<std::uint8_t> legacyWav(const std::vector<std::uint32_t>& durations, bool initial)
{
    constexpr std::uint32_t kRate = 44100;
    std::vector<std::int16_t> pcm;
    bool level = initial;
    for (std::uint32_t duration : durations) {
        const std::uint32_t n = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(
            std::llround(static_cast<double>(duration) * kRate / static_cast<double>(kTimebase))));
        pcm.insert(pcm.end(), n, level ? 14000 : -14000);
        level = !level;
    }
    pcm.insert(pcm.end(), kRate / 10, level ? 14000 : -14000);
    const std::uint32_t dataSize = static_cast<std::uint32_t>(pcm.size() * 2);
    std::vector<std::uint8_t> o;
    o.insert(o.end(), {'R', 'I', 'F', 'F'}); putLe32(o, 36 + dataSize);
    o.insert(o.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); putLe32(o, 16);
    putLe16(o, 1); putLe16(o, 1); putLe32(o, kRate); putLe32(o, kRate * 2);
    putLe16(o, 2); putLe16(o, 16);
    o.insert(o.end(), {'d', 'a', 't', 'a'}); putLe32(o, dataSize);
    for (std::int16_t s : pcm) putLe16(o, static_cast<std::uint16_t>(s));
    return o;
}

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t o)
{
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) |
           (std::uint32_t(b[o + 2]) << 8) | std::uint32_t(b[o + 3]);
}
std::uint16_t le16(const std::vector<std::uint8_t>& b, std::size_t o)
{
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}

// Edges as CassetteDevice::pcmToDurations sees them: ±0.02 hysteresis, leading
// near-silence skipped. Returns the run lengths between edges, in samples.
std::vector<std::size_t> edgeRuns(const std::vector<float>& mono)
{
    constexpr float kThreshold = 0.02f;
    std::size_t i = 0;
    while (i < mono.size() && std::fabs(mono[i]) < kThreshold) ++i;
    std::vector<std::size_t> runs;
    if (i == mono.size()) return runs;
    bool level = mono[i] >= 0.0f;
    std::size_t last = i;
    for (++i; i < mono.size(); ++i) {
        bool next = level;
        if (mono[i] >= kThreshold) next = true;
        else if (mono[i] <= -kThreshold) next = false;
        if (next != level) { runs.push_back(i - last); last = i; level = next; }
    }
    return runs;
}

std::size_t expectedRun(std::uint32_t duration, std::uint32_t rate)
{
    return std::max<std::size_t>(1, static_cast<std::size_t>(
        std::llround(static_cast<double>(duration) * rate / kTimebase)));
}

void testResolution()
{
    assert(pom1::tapeContainerForPath("a.wav") == TapeContainer::Wav);
    assert(pom1::tapeContainerForPath("A.WAV") == TapeContainer::Wav);
    assert(pom1::tapeContainerForPath("dir.wav/a.aiff") == TapeContainer::Aiff);
    assert(pom1::tapeContainerForPath("a.AIF") == TapeContainer::Aiff);
    assert(pom1::tapeContainerForPath("a.aci") == TapeContainer::Aci);
    assert(pom1::tapeContainerForPath("a.wav.bak") == TapeContainer::Aci);
    assert(pom1::tapeContainerForPath("wav") == TapeContainer::Aci);
    assert(pom1::tapeContainerForPath("") == TapeContainer::Aci);

    using R = TapeExportProfile;
    assert(pom1::resolveTapeExportProfile(R::Auto, TapeContainer::Aiff) == R::Smartphone);
    assert(pom1::resolveTapeExportProfile(R::Auto, TapeContainer::Wav) == R::Computer);
    assert(pom1::resolveTapeExportProfile(R::Auto, TapeContainer::Aci) == R::Computer);
    assert(pom1::resolveTapeExportProfile(R::Computer, TapeContainer::Aiff) == R::Computer);
    assert(pom1::resolveTapeExportProfile(R::Smartphone, TapeContainer::Wav) == R::Smartphone);

    const auto computer = pom1::tapeRenderSettings(R::Computer);
    assert(computer.sampleRate == 44100 && computer.channels == 1 && computer.peak == 14000);
    assert(computer.leadSilenceMs == 0 && computer.tailHoldMs == 100 && computer.tailSilenceMs == 0);
    const auto autoS = pom1::tapeRenderSettings(R::Auto);
    assert(autoS.sampleRate == computer.sampleRate && autoS.channels == computer.channels);
    const auto phone = pom1::tapeRenderSettings(R::Smartphone);
    assert(phone.sampleRate == 48000 && phone.channels == 2);
    assert(phone.peak == 22937);                       // 0.70 of full scale
    assert(phone.leadSilenceMs == 1000 && phone.tailSilenceMs == 1000);

    // The UI labels are indexed by the enum's value.
    static_assert(pom1::kTapeExportProfileCount == 3);
    volatile int labelCount = pom1::kTapeExportProfileCount;
    for (int i = 0; i < labelCount; ++i) assert(pom1::kTapeExportProfileLabels[i][0] != '\0');
    assert(std::string(pom1::kTapeExportProfileLabels[static_cast<int>(R::Smartphone)]).find("Smartphone") == 0);

    TapeExportProfile p = R::Computer;
    for (R r : {R::Auto, R::Computer, R::Smartphone}) {
        assert(pom1::parseTapeExportProfile(pom1::tapeExportProfileName(r), p) && p == r);
    }
    assert(pom1::parseTapeExportProfile("SmartPhone", p) && p == R::Smartphone);
    assert(pom1::parseTapeExportProfile("AUTO", p) && p == R::Auto);
    p = R::Computer;
    assert(!pom1::parseTapeExportProfile("phones", p) && p == R::Computer);
    assert(!pom1::parseTapeExportProfile("", p) && p == R::Computer);
}

void testComputerWavIsLegacy()
{
    const auto d = sampleDurations();
    for (bool initial : {false, true}) {
        const auto file = pom1::renderTapeAudio(
            d, initial, kTimebase, pom1::tapeRenderSettings(TapeExportProfile::Computer),
            TapeContainer::Wav);
        assert(file.ok && file.error.empty());
        assert(file.bytes == legacyWav(d, initial));
    }
}

void testSmartphoneAiff()
{
    const auto d = sampleDurations();
    const auto s = pom1::tapeRenderSettings(TapeExportProfile::Smartphone);
    const auto file = pom1::renderTapeAudio(d, true, kTimebase, s, TapeContainer::Aiff);
    assert(file.ok);
    const auto& b = file.bytes;

    // FORM size covers the whole file; COMM says 2 ch, 16 bit, 48 000 Hz.
    assert(b[0] == 'F' && b[1] == 'O' && b[2] == 'R' && b[3] == 'M');
    assert(be32(b, 4) == b.size() - 8);
    assert(b[12] == 'C' && b[13] == 'O' && b[14] == 'M' && b[15] == 'M');
    assert(b[20] == 0 && b[21] == 2);                             // channels
    const std::uint32_t frames = be32(b, 22);
    assert(b[26] == 0 && b[27] == 16);                            // bits
    // 48000 = 0xBB80 = 1.0111011 × 2^15 → exponent 0x400E, mantissa 0xBB80 << 48.
    const std::uint8_t rate48k[10] = {0x40, 0x0E, 0xBB, 0x80, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 10; ++i) assert(b[28 + i] == rate48k[i]);
    assert(b[38] == 'S' && b[39] == 'S' && b[40] == 'N' && b[41] == 'D');
    assert(be32(b, 42) == 8 + frames * 4u);
    const std::size_t data = 54;
    assert(data + frames * 4u == b.size());

    // Both channels identical, frame by frame.
    for (std::uint32_t f = 0; f < frames; ++f) {
        const std::size_t o = data + f * 4u;
        assert(b[o] == b[o + 2] && b[o + 1] == b[o + 3]);
    }

    // Read back through POM1's own reader.
    const pom1::PcmAudio audio = pom1::parseAiffPcm(b.data(), b.size());
    assert(audio.ok && audio.sampleRate == 48000 && audio.mono.size() == frames);
    for (std::size_t i = 0; i < 48000; ++i) assert(audio.mono[i] == 0.0f);   // lead
    for (std::size_t i = frames - 48000; i < frames; ++i) assert(audio.mono[i] == 0.0f);
    assert(std::fabs(audio.mono[48000] - 22937.0f / 32768.0f) < 1e-4f);      // first edge, high

    // D. Every half-period, at 48 kHz. The last run is the tail hold, ended by
    // nothing — the decoder never sees it close, exactly as on the ACI.
    const auto runs = edgeRuns(audio.mono);
    assert(runs.size() == d.size());
    for (std::size_t i = 0; i < d.size(); ++i) assert(runs[i] == expectedRun(d[i], 48000));

    // 44 100 Hz must take the exact encoding Apple's own tools write.
    pom1::TapeRenderSettings s441 = s;
    s441.sampleRate = 44100;
    const auto f441 = pom1::renderTapeAudio(d, true, kTimebase, s441, TapeContainer::Aiff);
    const std::uint8_t rate441[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 10; ++i) assert(f441.bytes[28 + i] == rate441[i]);
    assert(pom1::parseAiffPcm(f441.bytes.data(), f441.bytes.size()).sampleRate == 44100);
}

void testSmartphoneWavMatchesAiff()
{
    const auto d = sampleDurations();
    const auto s = pom1::tapeRenderSettings(TapeExportProfile::Smartphone);
    const auto wav = pom1::renderTapeAudio(d, false, kTimebase, s, TapeContainer::Wav);
    const auto aiff = pom1::renderTapeAudio(d, false, kTimebase, s, TapeContainer::Aiff);
    assert(wav.ok && aiff.ok);
    assert(le16(wav.bytes, 22) == 2);                             // channels
    assert(le16(wav.bytes, 32) == 4);                             // block align
    const auto a = pom1::parseWavPcm(wav.bytes.data(), wav.bytes.size());
    const auto b = pom1::parseAiffPcm(aiff.bytes.data(), aiff.bytes.size());
    assert(a.ok && b.ok && a.sampleRate == 48000);
    assert(a.mono.size() == b.mono.size());
    for (std::size_t i = 0; i < a.mono.size(); ++i) assert(std::fabs(a.mono[i] - b.mono[i]) < 1e-6f);
    assert(edgeRuns(a.mono).size() == d.size());
}

void testRefusals()
{
    const auto s = pom1::tapeRenderSettings(TapeExportProfile::Computer);
    const auto d = sampleDurations();
    auto refused = [](const pom1::TapeAudioFile& f) {
        return !f.ok && f.bytes.empty() && !f.error.empty();
    };
    assert(refused(pom1::renderTapeAudio({}, false, kTimebase, s, TapeContainer::Wav)));
    assert(refused(pom1::renderTapeAudio(d, false, kTimebase, s, TapeContainer::Aci)));
    assert(refused(pom1::renderTapeAudio(d, false, 0, s, TapeContainer::Wav)));
    pom1::TapeRenderSettings bad = s;
    bad.channels = 0;
    assert(refused(pom1::renderTapeAudio(d, false, kTimebase, bad, TapeContainer::Aiff)));
    bad = s; bad.sampleRate = 0;
    assert(refused(pom1::renderTapeAudio(d, false, kTimebase, bad, TapeContainer::Wav)));
    bad = s; bad.peak = 0;
    assert(refused(pom1::renderTapeAudio(d, false, kTimebase, bad, TapeContainer::Wav)));

    // A zero-length duration still becomes one sample: an edge is never lost.
    const auto tiny = pom1::renderTapeAudio({0, 1, 592}, true, kTimebase, s, TapeContainer::Wav);
    assert(tiny.ok);
    const auto audio = pom1::parseWavPcm(tiny.bytes.data(), tiny.bytes.size());
    assert(audio.ok && edgeRuns(audio.mono).size() == 3);
}

} // namespace

int main()
{
    testResolution();
    testComputerWavIsLegacy();
    testSmartphoneAiff();
    testSmartphoneWavMatchesAiff();
    testRefusals();
    std::puts("tape_export_smoke: OK");
    return 0;
}
