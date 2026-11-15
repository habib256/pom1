// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_TAPE_EXPORT_H
#define POM1_TAPE_EXPORT_H

// Cassette EXPORT: the deck's recorded half-periods rendered as a WAV or AIFF
// file meant to be played into a real Apple-1's ACI.
//
// PURE: durations in, file bytes out. No file, no device, no log sink — the
// mirror image of PcmFile.h, which is the import side of the same containers.
//
// Two PROFILES, because the two playback devices people actually use want
// different files:
//
//   * Computer   — what POM1 has always written: 44.1 kHz, 16-bit, MONO, the
//                  signal from the first sample, the final level held 100 ms.
//                  Byte-identical to the pre-profile saveWavTape(), which is
//                  what every .wav already circulating was made with.
//   * Smartphone — 48 kHz, 16-bit, TWO IDENTICAL channels, 70 % of full scale,
//                  one second of digital silence at each end.
//
// The Smartphone figures, and why each one is there:
//   48 kHz        — the native rate of the Android audio output: a 44.1 kHz
//                   file is resampled on the phone, and a resampler is a
//                   low-pass filter sitting on a square wave.
//   2 channels    — the usual cable joins L and R on the tip of the ACI's jack;
//                   a mono file played as "left only" or as an inverted pair
//                   is a different signal at the ACI input.
//   70 % FS       — leaves headroom for the phone's output stage.
//   1 s silences  — the phone's output wakes up / fades out without eating the
//                   start of the leader or the end of the last record.
// [D] RDK Apple-1 crates/apple1-core/src/aciwav.rs ExportOptions::for_format
//     @ 0ef0d79 (Old-Wirehead/rdk-apple1) — all four values, the 70 % being
//     attributed there to Uncle Bernie (Oct 2026).
// [F] None of it has been played into a real ACI from a phone by POM1's
//     authors. Settled by: a capture of a real Apple-1 loading a Smartphone
//     export from an Android phone at maximum volume.
//
// The profile is independent of the container: either profile can be written
// as WAV or as AIFF. When the caller does not choose (TapeExportProfile::Auto),
// the extension does — .aiff/.aif is the phone/iPod container (Mike Willegal's
// Mimeo ACI guide recommends AIFF for exactly that use), .wav the computer one.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pom1 {

/// What `saveTape()` writes, decided by the file extension.
enum class TapeContainer : std::uint8_t {
    Aci,   ///< POM1's own pulse dump (.aci, and any unrecognised extension)
    Wav,   ///< RIFF/WAVE, little-endian PCM
    Aiff,  ///< AIFF, big-endian PCM
};

/// How the audio is rendered. Auto lets the container decide.
enum class TapeExportProfile : std::uint8_t {
    Auto,
    Computer,
    Smartphone,
};

/// UI labels, indexed by the enum's value (Auto, Computer, Smartphone).
inline constexpr int kTapeExportProfileCount = 3;
inline constexpr const char* kTapeExportProfileLabels[kTapeExportProfileCount] = {
    "Auto (.aiff = smartphone, .wav = computer)",
    "Computer / tape deck - 44.1 kHz mono",
    "Smartphone - 48 kHz stereo, 1 s silences",
};

/// Everything the renderer needs to know about a profile.
struct TapeRenderSettings {
    std::uint32_t sampleRate = 44100;
    std::uint16_t channels = 1;        ///< written as identical copies
    std::int16_t  peak = 14000;        ///< square-wave amplitude, ± this
    std::uint32_t leadSilenceMs = 0;   ///< zero samples before the first edge
    std::uint32_t tailHoldMs = 100;    ///< final level held after the last edge
    std::uint32_t tailSilenceMs = 0;   ///< zero samples after the hold
};

/// Container from the path's extension, case-insensitively. Anything that is
/// not .wav / .aiff / .aif is an .aci dump — the historical saveTape() rule.
TapeContainer tapeContainerForPath(std::string_view path);

/// Resolve Auto against a container: AIFF → Smartphone, everything else →
/// Computer. A concrete profile is returned unchanged.
TapeExportProfile resolveTapeExportProfile(TapeExportProfile requested,
                                           TapeContainer container);

/// The settings of a concrete profile (Auto is treated as Computer).
TapeRenderSettings tapeRenderSettings(TapeExportProfile profile);

/// `auto`, `computer`, `phone` (alias `smartphone`), case-insensitively.
/// Returns false and leaves `out` alone on anything else.
bool parseTapeExportProfile(std::string_view text, TapeExportProfile& out);

/// The canonical spelling parseTapeExportProfile() accepts.
const char* tapeExportProfileName(TapeExportProfile profile);

/// Rendered file, or the reason there is none.
struct TapeAudioFile {
    bool ok = false;
    std::vector<std::uint8_t> bytes;
    std::string error;
};

/// Render recorded half-periods into a complete WAV or AIFF file.
///
/// `durations` are the times between successive output toggles, in units of
/// `timebaseHz` (CPU cycles, for CassetteDevice); `initialLevel` is the level
/// the first one is spent at. Each duration becomes
/// max(1, round(d × sampleRate / timebaseHz)) samples — rounded per half-period,
/// not on absolute time, so every bit keeps the same length in the file.
///
/// `container` must be Wav or Aiff. Nothing partial escapes: a refused render
/// carries no bytes.
TapeAudioFile renderTapeAudio(const std::vector<std::uint32_t>& durations,
                              bool initialLevel,
                              std::uint32_t timebaseHz,
                              const TapeRenderSettings& settings,
                              TapeContainer container);

} // namespace pom1

#endif // POM1_TAPE_EXPORT_H
