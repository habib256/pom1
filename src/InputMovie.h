#pragma once
// InputMovie.h -- an input movie: where a session started, and every key the
// machine received, dated to the emulated cycle. Pure: bytes in, a Movie out,
// and back; no Memory, no controller, no file I/O.
//
// WHY THIS EXISTS. Issue #40 asks for re-recording for longplays: play, rewind
// over a mistake, carry on, and end with a clean run that can be replayed and
// captured as video. Its foundation is a replay that lands on EXACTLY the same
// machine state, which on a 6502 means the same keys on the same cycles:
// POM1's programs poll $D011 in tight loops, so a key one cycle late is a key
// read on a different turn of the loop, and a game that seeds its randomness
// from that loop diverges from there on.
//
// A movie is therefore a snapshot (v7: it carries the DRAM-refresh phase and
// the queued keys a cycle-exact resume needs) plus (cycle, key) pairs counted
// from that snapshot, the cycle at which recording stopped, and a hash of the
// machine at that cycle. The hash is what lets a replay VERIFY itself: reaching
// the end cycle with a different state says, out loud, that something the
// movie does not carry -- a reset, a memory load, a card change, the host's
// clock read through the RTC -- happened while it was recorded.
//
// File layout, little-endian:
//     "POM1MOV1"            magic
//     u32 version           kVersion
//     u32 n, n bytes        the start snapshot (a POM1SNAP blob)
//     u32 k, k x {u64 cycle, u8 key}   non-decreasing cycles, all <= end
//     u64 end cycle
//     u64 end-state hash    stateHash() at the end cycle

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pom1::movie {

inline constexpr char kMagic[8] = {'P', 'O', 'M', '1', 'M', 'O', 'V', '1'};
inline constexpr uint32_t kVersion = 1;
/// A snapshot is ~80 KB and a key 9 bytes: 32 MB is years of typing. Checked
/// before anything is allocated from a declared count.
inline constexpr std::size_t kMaxMovieBytes = 32u * 1024u * 1024u;
inline constexpr const char* kExtension = "p1m";

struct KeyEvent {
    uint64_t cycle = 0;   // emulated cycles since the start snapshot
    uint8_t key = 0;      // as handed to the keyboard (Memory::setKeyPressed)
    bool operator==(const KeyEvent& o) const { return cycle == o.cycle && key == o.key; }
};

struct Movie {
    std::vector<uint8_t> snapshot;
    std::vector<KeyEvent> keys;
    uint64_t endCycle = 0;
    uint64_t endHash = 0;
};

/// FNV-1a over the 64 KB address space and the 6502 registers: the state a
/// replay must reach at the end cycle. Deliberately not the whole snapshot --
/// that also holds host-side bookkeeping a faithful replay may differ in.
inline uint64_t stateHash(const uint8_t* ram64k, uint16_t pc, uint8_t a, uint8_t x,
                          uint8_t y, uint8_t sp, uint8_t p)
{
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint8_t b) { h ^= b; h *= 1099511628211ULL; };
    for (std::size_t i = 0; i < 0x10000; ++i) mix(ram64k[i]);
    mix(static_cast<uint8_t>(pc)); mix(static_cast<uint8_t>(pc >> 8));
    mix(a); mix(x); mix(y); mix(sp); mix(p);
    return h;
}

namespace detail {
inline void put32(std::vector<uint8_t>& o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline void put64(std::vector<uint8_t>& o, uint64_t v)
{
    for (int i = 0; i < 8; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline uint64_t get(const uint8_t* p, int bytes)
{
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}
} // namespace detail

inline std::vector<uint8_t> serialize(const Movie& m)
{
    std::vector<uint8_t> o(kMagic, kMagic + 8);
    detail::put32(o, kVersion);
    detail::put32(o, static_cast<uint32_t>(m.snapshot.size()));
    o.insert(o.end(), m.snapshot.begin(), m.snapshot.end());
    detail::put32(o, static_cast<uint32_t>(m.keys.size()));
    for (const KeyEvent& k : m.keys) {
        detail::put64(o, k.cycle);
        o.push_back(k.key);
    }
    detail::put64(o, m.endCycle);
    detail::put64(o, m.endHash);
    return o;
}

/// Parse a movie. Complete or rejected: on false, `out` is untouched and
/// `error` says why. The snapshot inside is NOT validated here -- restoring it
/// goes through the snapshot loader's own gate.
inline bool parse(const uint8_t* data, std::size_t size, Movie& out, std::string& error)
{
    using detail::get;
    auto fail = [&error](const std::string& why) { error = "input movie: " + why; return false; };
    if (size > kMaxMovieBytes) return fail("file too large");
    if (size < 8 + 4 + 4 + 4 + 8 + 8) return fail("file too short");
    for (int i = 0; i < 8; ++i)
        if (static_cast<char>(data[i]) != kMagic[i]) return fail("not a POM1 input movie");
    std::size_t at = 8;
    const uint32_t version = static_cast<uint32_t>(get(data + at, 4)); at += 4;
    if (version != kVersion) return fail("unsupported version " + std::to_string(version));
    const uint64_t snapLen = get(data + at, 4); at += 4;
    if (snapLen == 0 || snapLen > size - at) return fail("snapshot length out of bounds");
    Movie m;
    m.snapshot.assign(data + at, data + at + snapLen);
    at += static_cast<std::size_t>(snapLen);
    if (size - at < 4) return fail("truncated before the key count");
    const uint64_t count = get(data + at, 4); at += 4;
    if (count > (size - at) / 9) return fail("key count larger than the file");
    m.keys.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        KeyEvent k;
        k.cycle = get(data + at, 8);
        k.key = data[at + 8];
        at += 9;
        if (!m.keys.empty() && k.cycle < m.keys.back().cycle)
            return fail("key cycles go backwards");
        m.keys.push_back(k);
    }
    if (size - at != 16) return fail("unexpected length after the keys");
    m.endCycle = get(data + at, 8);
    m.endHash = get(data + at + 8, 8);
    if (!m.keys.empty() && m.keys.back().cycle > m.endCycle)
        return fail("a key lies past the end of the movie");
    out = std::move(m);
    return true;
}

/// The movie machinery's state, driven by the controller under its state
/// lock. It only ever sees an absolute emulated-cycle counter (`now`) and keys:
/// recording notes each key the keyboard hands the machine with its cycle;
/// playback hands them back when their cycle comes and says how far the CPU may
/// run before the next one, so the controller stops EXACTLY there. The two agree
/// because a key is always taken between instructions, and a replay of the same
/// state runs through the same instruction boundaries.
class Session {
public:
    enum class State { Idle, Recording, Playing };
    enum class Verdict { None, Verified, Diverged };

    State state() const { return state_; }
    Verdict verdict() const { return verdict_; }
    uint64_t elapsed(uint64_t now) const { return state_ == State::Idle ? 0 : now - base_; }
    std::size_t keys() const { return movie_.keys.size(); }
    std::size_t keysPlayed() const { return next_; }
    uint64_t length() const { return movie_.endCycle; }

    void startRecording(std::vector<uint8_t> snapshot, uint64_t now)
    {
        movie_ = Movie{};
        movie_.snapshot = std::move(snapshot);
        base_ = now;
        next_ = 0;
        verdict_ = Verdict::None;
        state_ = State::Recording;
    }

    void recordKey(uint64_t now, uint8_t key)
    {
        if (state_ == State::Recording) movie_.keys.push_back({now - base_, key});
    }

    Movie finishRecording(uint64_t now, uint64_t endHash)
    {
        movie_.endCycle = now - base_;
        movie_.endHash = endHash;
        state_ = State::Idle;
        return std::move(movie_);
    }

    void startPlaying(Movie m, uint64_t now)
    {
        movie_ = std::move(m);
        base_ = now;
        next_ = 0;
        verdict_ = Verdict::None;
        state_ = State::Playing;
    }

    /// Hand every key whose cycle has come to `deliver`, in order.
    template <typename Deliver>
    void deliverDue(uint64_t now, Deliver&& deliver)
    {
        if (state_ != State::Playing) return;
        const uint64_t at = now - base_;
        while (next_ < movie_.keys.size() && movie_.keys[next_].cycle <= at)
            deliver(movie_.keys[next_++].key);
    }

    /// How many cycles the CPU may run before the next key or the end.
    uint64_t cyclesToNextStop(uint64_t now) const
    {
        const uint64_t at = now - base_;
        const uint64_t stop = next_ < movie_.keys.size() ? movie_.keys[next_].cycle
                                                         : movie_.endCycle;
        return stop > at ? stop - at : 0;
    }

    bool reachedEnd(uint64_t now) const
    {
        return state_ == State::Playing && next_ == movie_.keys.size()
            && now - base_ >= movie_.endCycle;
    }

    /// Close a playback that reached its end, judging the state it reached.
    void finishPlaying(uint64_t hashNow)
    {
        verdict_ = hashNow == movie_.endHash ? Verdict::Verified : Verdict::Diverged;
        state_ = State::Idle;
    }

    /// Stop whatever is running without a verdict (the user stopped a replay).
    void abort()
    {
        state_ = State::Idle;
        verdict_ = Verdict::None;
    }

private:
    State state_ = State::Idle;
    Verdict verdict_ = Verdict::None;
    Movie movie_;
    uint64_t base_ = 0;
    std::size_t next_ = 0;
};

} // namespace pom1::movie
