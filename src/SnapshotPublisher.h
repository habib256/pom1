// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// SnapshotPublisher — single-producer / single-consumer slot for
// EmulationSnapshot. The emulation thread calls publish() while holding the
// mutex that protects Memory & M6502 (EmulationController::stateMutex).
// publish() then serializes the slot update on its own snapshotMutex, so any
// UI-thread copyTo() observes a fully-written snapshot.

#ifndef SNAPSHOTPUBLISHER_H
#define SNAPSHOTPUBLISHER_H

#include <mutex>

#include "EmulationSnapshot.h"
#include "LockOrder.h"

class Memory;
class M6502;

class SnapshotPublisher
{
public:
    /// Pre: caller holds the mutex protecting `mem` and `cpu`.
    /// Acquires snapshotMutex internally; safe for concurrent copyTo().
    /// `mem` is non-const because peripheral copySnapshot() clears internal
    /// dirty flags (e.g. TMS9918 skips its 16 KB VRAM copy when clean).
    void publish(Memory& mem, const M6502& cpu, bool cpuRunning);

    /// Thread-safe: takes only snapshotMutex. Called from the UI thread.
    void copyTo(EmulationSnapshot& out) const;

    /// The input movie's status, kept apart from publish(): it lives in the
    /// controller, not in Memory or the CPU.
    void setMovieStatus(uint8_t state, uint8_t verdict, uint64_t cycles, uint64_t length,
                        uint32_t keys, uint32_t keysPlayed);

private:
    // Innermost rank — nothing may be acquired while this is held.
    mutable pom1::RankedMutex<pom1::LockRank::Snapshot> snapshotMutex;
    EmulationSnapshot latestSnapshot;
    // GEN2 field ring bookkeeping (see publish): the scanner position at the
    // previous publish, whose wrap says a field completed, and the last seq.
    uint64_t gen2LastPosition_ = 0;
    uint64_t gen2FieldSeq_ = 0;
};

#endif // SNAPSHOTPUBLISHER_H
