// gen2_vsplits_smoke -- Uncle Bernie's own vertical-split demo, run on the real
// core, must scroll one scanline per field and show the text Table 2 promises.
//
// software/Graphic HGR/vsplits.apl is the program exactly as Bernie sent it (Applefritter
// PM, 16 sept. 2026, "iconvsplits.zip"), a WOZMON dump that runs at $0280. It
// fills the text page with $80-$FF, then every field: waits for V-blank by
// sampling HST0 (D7 of any $C25x read) until three double-samples in a row say
// "blank", counts H-blanks to a split scanline, and throws TEXT/GR there. The
// text window is 32 scanlines tall and moves down one scanline per field,
// wrapping from the bottom back to the top. On his bench "it also proves that
// your implementation of the HST0 flag is correct" -- and it is the only
// program in the tree that exercises beam-synchronised vertical splits end to
// end: 6502 timing, HST0, the soft-switch journal, the renderer.
//
// What it pins:
//   1. Motion. In EVERY field the TEXT scanlines are exactly [p, p+32) mod 192,
//      p advances by exactly one per field, and p is the scanline the program
//      itself counted to (its zero-page $2C). A lost HST0 edge, a journal event
//      dated to the wrong line, or a VBL flip applied to the wrong frame all
//      show up as a band that jumps, stalls, changes height or sits off by one.
//   2. Glyphs. When the window reaches the top, text row 0 holds $80-$A7 and
//      must read "@ABCDEFGH...", i.e. each cell must equal the normal glyph of
//      the same character from $C0-$DF. Bernie's build showed a row of hollow
//      boxes -- "a block of 'O' looking characters" -- there.
//
// Invariant-based rather than a golden PNG: the power-on scanner phase and
// latch are random by default, so the same demo lands at a different point of
// its cycle depending on the seed. What must not change is the motion.

#include "TMS9918.h"      // IWYU pragma: keep
#include "WiFiModem.h"    // IWYU pragma: keep
#include "TerminalCard.h" // IWYU pragma: keep
#include "A1IO_RTC.h"     // IWYU pragma: keep
#include "PR40Printer.h"  // IWYU pragma: keep
#include "Memory.h"
#include "M6502.h"
#include "GraphicsCard.h"
#include "Gen2CharGen.h"   // pom1::gen2char::Rows
#include "Gen2FieldRing.h"
#include "SnapshotPublisher.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kW = GraphicsCard::kHiresWidth;
constexpr int kH = GraphicsCard::kHiresHeight;
constexpr int kBand = 32;     // text window height in scanlines ($29 = $20)

int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    if (failures <= 20) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } } while (0)

// Run the CPU until the GEN2 scanner rolls into a new field; the journal the
// renderer reads is then the field that just completed.
void runOneField(Memory& mem, M6502& cpu)
{
    uint64_t prev = mem.peekGen2VideoCycle();
    for (int guard = 0; guard < 400; ++guard) {
        cpu.run(64);
        const uint64_t now = mem.peekGen2VideoCycle();
        if (now < prev) return;
        prev = now;
    }
    CHECK(false, "no field rollover within 25 600 cycles");
}

void renderField(Memory& mem, GraphicsCard& card)
{
    card.render(mem.getMemoryPointer(), mem.gen2DisplayState(),
                mem.gen2PublishedFrameStartState(), mem.gen2PublishedVideoEvents());
}

// A TEXT scanline is pure black/white; a LORES one of this demo never is (every
// row mixes nibbles 0-F).
std::array<bool, kH> textScanlines(const GraphicsCard& card)
{
    std::array<bool, kH> text{};
    const uint32_t* px = card.pixels();
    for (int y = 0; y < kH; ++y) {
        bool bw = true;
        for (int x = 0; x < kW && bw; ++x) {
            const uint32_t rgb = px[y * kW + x] & 0x00FFFFFFu;
            bw = rgb == 0 || rgb == 0x00FFFFFFu;
        }
        text[y] = bw;
    }
    return text;
}

// The p for which `text` is exactly [p, p+32) mod 192, or -1.
int bandStart(const std::array<bool, kH>& text)
{
    for (int p = 0; p < kH; ++p) {
        bool match = true;
        for (int y = 0; y < kH && match; ++y) {
            const bool inBand = ((y - p + kH) % kH) < kBand;
            match = text[y] == inBand;
        }
        if (match) return p;
    }
    return -1;
}

// Rendered cell (row, col) of text row `row`, as the renderer painted it.
pom1::gen2char::Rows renderedCell(const GraphicsCard& card, int row, int col)
{
    pom1::gen2char::Rows r{};
    const uint32_t* px = card.pixels();
    for (int gy = 0; gy < 8; ++gy)
        for (int gx = 0; gx < 7; ++gx)
            if ((px[(row * 8 + gy) * kW + col * 7 + gx] & 0x00FFFFFFu) == 0x00FFFFFFu)
                r[gy] = static_cast<uint8_t>(r[gy] | (1u << gx));
    return r;
}

} // namespace

int main()
{
    Memory mem;
    mem.setHgrFramebufferAttached(true);

    const char* env = std::getenv("POM1_VSPLITS");
    const char* path = env ? env : "software/Graphic HGR/vsplits.apl";
    uint16_t start = 0;
    int bytes = 0;
    if (mem.loadHexDump(path, start, &bytes) != 0) {
        std::printf("FAIL: cannot load %s: %s\n", path, mem.getLastError().c_str());
        return 1;
    }
    CHECK(start == 0x0280, "vsplits loads at $%04X, expected $0280", start);
    CHECK(bytes == 218, "vsplits is %d bytes, expected 218", bytes);

    M6502 cpu(&mem);
    cpu.setProgramCounter(0x0280);
    cpu.start();

    // Settle: the fill loop plus the first (partial) fields.
    for (int f = 0; f < 4; ++f) runOneField(mem, cpu);

    // Reference: row 0 spelled with the same characters from $C0-$DF (letters)
    // and $A0-$A7 (punctuation), rendered as a plain full-screen TEXT page.
    std::array<uint8_t, 40> referenceRow0{};
    for (int col = 0; col < 40; ++col)
        referenceRow0[col] = static_cast<uint8_t>(col < 0x20 ? 0xC0 + col : 0x80 + col);
    GraphicsCard reference;
    {
        std::vector<uint8_t> page(0x10000, 0xA0);
        for (int col = 0; col < 40; ++col) page[0x0400 + col] = referenceRow0[col];
        GraphicsCard::DisplayState text;
        text.textMode = true;
        text.hiRes = false;
        reference.render(page.data(), text, text, {});
    }

    GraphicsCard card;
    int prevP = -1;
    int fieldsAtTop = 0;
    bool sawWrap = false;
    // 200 fields > one full 192-position cycle: every band position, the
    // straddling ones and the wrap from the last line back to the first.
    for (int f = 0; f < 200; ++f) {
        runOneField(mem, cpu);
        renderField(mem, card);
        const int p = bandStart(textScanlines(card));
        CHECK(p >= 0, "field %d: TEXT scanlines are not one %d-line band (mod %d)", f, kBand, kH);
        if (p < 0) { prevP = -1; continue; }
        if (prevP >= 0) {
            CHECK(p == (prevP + 1) % kH,
                  "field %d: band moved from line %d to %d, expected %d", f, prevP, p,
                  (prevP + 1) % kH);
            if (p < prevP) sawWrap = true;
        }
        prevP = p;
        // Absolute position: the band starts on the very scanline the program
        // counted to. $2C is its position variable; it was incremented at the
        // end of the field just rendered (and wrapped $C0 -> 0 at loop top),
        // so it already names the NEXT field's line.
        const int counted = (mem.getMemoryPointer()[0x2C] + kH - 1) % kH;
        CHECK(p == counted, "field %d: band starts on line %d, the program counted %d",
              f, p, counted);

        if (p == 0) {
            // Text rows 0-3 are on screen, and row 0 is $0400-$0427 = $80-$A7:
            // @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_ !"#$%&'. Each cell must be what
            // the renderer draws for the same character in the undisputed band.
            ++fieldsAtTop;
            for (int col = 0; col < 40; ++col) {
                CHECK(renderedCell(card, 0, col) == renderedCell(reference, 0, col),
                      "text row 0 col %d: $%02X does not draw like $%02X", col,
                      0x80 + col, referenceRow0[col]);
            }
        }
    }
    CHECK(sawWrap, "the band never wrapped from the bottom back to the top in 200 fields");
    CHECK(fieldsAtTop == 1, "the band sat at the top in %d fields, expected exactly 1",
          fieldsAtTop);

    // ---- The same demo through the SnapshotPublisher's field ring ----------
    //
    // What the GEN2 window actually draws from: the emulation thread publishes
    // after every slice, and each completed field lands in the ring
    // (Gen2FieldRing.h) with its own framebuffer, text pages and journal. The
    // pacer can only show one field per refresh if the ring holds EVERY field,
    // in order, each one complete -- so render each new ring entry and demand
    // the band one line below the previous entry's, with no seq missing.
    // Slices vary from ~700 to ~5 900 cycles, like a loaded emulation thread.
    {
        SnapshotPublisher publisher;
        EmulationSnapshot snap;
        GraphicsCard ringCard;
        pom1::Gen2FieldPacer composer;
        uint64_t lastSeq = 0;
        int lastBand = -1, fieldsSeen = 0;
        uint32_t lcg = 12345;
        for (int slice = 0; slice < 4000 && fieldsSeen < 200; ++slice) {
            lcg = lcg * 1103515245u + 12345u;
            cpu.run(700 + static_cast<int>((lcg >> 8) % 5200));
            publisher.publish(mem, cpu, true);
            publisher.copyTo(snap);
            for (const auto& f : snap.gen2Fields) {
                if (f->seq <= lastSeq) continue;
                CHECK(lastSeq == 0 || f->seq == lastSeq + 1,
                      "ring skipped from field %llu to %llu", static_cast<unsigned long long>(lastSeq),
                      static_cast<unsigned long long>(f->seq));
                lastSeq = f->seq;
                ringCard.render(composer.compose(*f), f->endState, f->frameStart, f->events);
                const int p = bandStart(textScanlines(ringCard));
                CHECK(p >= 0, "ring field %llu: not one %d-line text band",
                      static_cast<unsigned long long>(f->seq), kBand);
                if (p >= 0 && lastBand >= 0 && fieldsSeen > 1)
                    CHECK(p == (lastBand + 1) % kH, "ring field %llu: band at %d, expected %d",
                          static_cast<unsigned long long>(f->seq), p, (lastBand + 1) % kH);
                lastBand = p;
                ++fieldsSeen;
            }
        }
        CHECK(fieldsSeen >= 200, "only %d fields reached the ring", fieldsSeen);
        std::printf("  ring: %d consecutive fields, each one line below the last\n", fieldsSeen);
    }

    if (failures) {
        std::printf("gen2_vsplits_smoke: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("gen2_vsplits_smoke: OK (200 fields, one scanline per field)\n");
    return 0;
}
