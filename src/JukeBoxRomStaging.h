// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_JUKEBOX_ROM_STAGING_H
#define POM1_JUKEBOX_ROM_STAGING_H

// Plugging the Juke-Box reads its ROM (256 KB) from disk, and every plug path
// runs under stateMutex — so a cold page cache parked the emulation, render and
// audio threads behind one disk read: 208-339 ms on GitHub's Linux runners,
// against concurrent_frontends_smoke's 100 ms maxStateHoldNs gate.
//
// The fix splits the load in two. stageJukeBoxRom() does the disk read BEFORE
// the lock; installStagedJukeBoxRom() hands the bytes to the chip under it,
// and Memory::setJukeBoxEnabled() then finds a resident ROM and reads nothing.
// Whatever stage cannot settle (no file, a refused image, a chip swapped in
// between) falls through to Memory's own read, which is the old behaviour.

#include "FileBytes.h"
#include "JukeBox.h"
#include "ResourceLocator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pom1 {

struct StagedJukeBoxRom {
    std::string          path;   // empty = nothing staged
    std::vector<uint8_t> bytes;
};

// WITHOUT stateMutex. `wanted` = the caller is about to plug the Juke-Box.
// Reads nothing when the chip already holds a ROM (isRomResident is the one
// JukeBox accessor that is safe unlocked).
inline StagedJukeBoxRom stageJukeBoxRom(const ResourceLocator& where,
                                        const JukeBox& chip, bool wanted)
{
    StagedJukeBoxRom staged;
    if (!wanted || chip.isRomResident()) return staged;
    for (const char* rel : JukeBox::kDefaultRomNames) {
        const std::filesystem::path found = where.find(rel);
        if (found.empty()) continue;
        std::string error;
        if (readFileBounded(found.string(), JukeBox::kRomReadLimit, "Juke-Box ROM",
                            staged.bytes, error)) {
            staged.path = found.string();
            break;
        }
    }
    return staged;
}

// UNDER stateMutex, after validation and before the plug. Only an unplugged,
// blank chip takes the image: a plugged card's ROM is live on the bus.
inline void installStagedJukeBoxRom(JukeBox& chip, bool plugged,
                                    const StagedJukeBoxRom& staged)
{
    if (staged.path.empty() || plugged || chip.isRomResident()) return;
    std::string error;
    (void)chip.loadRomImage(staged.bytes, staged.path, error); // refused -> Memory reads it
}

} // namespace pom1

#endif // POM1_JUKEBOX_ROM_STAGING_H
