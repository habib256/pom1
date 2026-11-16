// pom1::asciiart (src/AsciiArt.h) — picture → Apple-1 screen, pure.
//
//   §1  the 2513 bitmaps come out of charmap.rom, and a short ROM is refused;
//   §2  SHAPE matching recovers every one of the 64 glyphs from a picture of
//       itself, cell for cell — the property that makes it a shape matcher and
//       not a brightness ramp;
//   §3  black is blank, white is the densest glyph, invert swaps them, and
//       the last row's 40th column is always blank (printing it would scroll
//       the top row away);
//   §4  the 6502 program: 43 bytes of code, then the 959 characters in screen
//       order with no CR, then $00 — and its branches land where they say;
//   §5  the Woz Monitor dump and the Integer BASIC listing are well-formed:
//       no '"' or '_' inside a BASIC string, full rows end with ';';
//   §6  framing: zoom, pan, and pulling back past the picture's edge (black).
//   §7  framing by hand on the original: inverse, pan, wheel about the cursor,
//       corner drag holding the opposite corner, whole picture, panel fit,
//       hit-testing, and the shrunk texture copy.
//
// argv[1] = roms/charmap.rom

#include "AsciiArt.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace pom1::asciiart;

namespace {

Grey blank(float v)
{
    Grey g;
    g.w = kCols * kCellW;
    g.h = kRows * kCellH;
    g.px.assign(static_cast<std::size_t>(g.w) * g.h, v);
    return g;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s roms/charmap.rom\n", argv[0]); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // §1
    Glyphs g;
    assert(!glyphsFromCharmap(rom.data(), 0x5F * 8, g));
    assert(!glyphsFromCharmap(nullptr, 1024, g));
    assert(glyphsFromCharmap(rom.data(), rom.size(), g));
    assert(litDots(g, ' ') == 0);
    assert(litDots(g, '#') > litDots(g, '.') && litDots(g, '.') > 0);
    assert(litDots(g, 'a') == 0);                         // not a 2513 code

    // §2 Every glyph, drawn dot for dot into its own cell (a 280 × 192 picture
    // IS the dot grid), comes back as itself.
    {
        Grey pic = blank(0.0f);
        for (int i = 0; i < kGlyphCount; ++i) {
            const int col = i % kCols, row = i / kCols;
            for (int y = 0; y < kCellH; ++y)
                for (int x = 1; x < kCellW; ++x)
                    if ((g.rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(y)] >> x) & 1)
                        pic.px[static_cast<std::size_t>((row * kCellH + y) * pic.w + col * kCellW + x)] = 1.0f;
        }
        Params p;
        p.mode = Mode::Shape;
        p.dither = false;
        p.framing.aspect = 0.0f;          // the picture IS the dot grid: no 4:3 crop
        const Art art = convert(pic, g, p);
        int wrong = 0;
        for (int i = 0; i < kGlyphCount; ++i) {
            const char got = art.rows[static_cast<std::size_t>(i / kCols)][static_cast<std::size_t>(i % kCols)];
            if (got != static_cast<char>(kFirstCode + i)) {
                std::fprintf(stderr, "  glyph $%02X came back as '%c'\n", kFirstCode + i, got);
                ++wrong;
            }
        }
        assert(wrong == 0);
    }

    // §3
    {
        Params p;
        p.dither = false;
        for (Mode m : {Mode::Shape, Mode::Density}) {
            p.mode = m;
            p.invert = false;
            const Art black = convert(blank(0.0f), g, p);
            const Art white = convert(blank(1.0f), g, p);
            p.invert = true;
            const Art inverted = convert(blank(0.0f), g, p);
            for (int r = 0; r < kRows; ++r) {
                assert(black.rows[static_cast<std::size_t>(r)] == std::string(kCols, ' '));
                assert(white.rows[static_cast<std::size_t>(r)].size() == static_cast<std::size_t>(kCols));
            }
            const char dense = white.rows[0][0];
            assert(dense != ' ' && litDots(g, dense) >= 14);
            assert(inverted.rows[5][5] == dense);
            assert(white.rows[kRows - 1][kCols - 1] == ' ');  // never the 960th cell
            assert(white.rows[kRows - 1][kCols - 2] == dense);
        }
        // An empty picture is a blank screen, not a crash.
        const Art none = convert(Grey{}, g, Params{});
        assert(none.rows[0] == std::string(kCols, ' '));
        // A gradient in density mode gets darker glyphs on the left.
        Grey ramp = blank(0.0f);
        for (int y = 0; y < ramp.h; ++y)
            for (int x = 0; x < ramp.w; ++x)
                ramp.px[static_cast<std::size_t>(y * ramp.w + x)] = static_cast<float>(x) / (ramp.w - 1);
        p = Params{};
        p.mode = Mode::Density;
        p.dither = false;
        const Art grad = convert(ramp, g, p);
        assert(litDots(g, grad.rows[10][0]) < litDots(g, grad.rows[10][39]));
        // greyFromRgba: luminance, transparency is black.
        const uint8_t px[8] = {255, 255, 255, 255, 255, 255, 255, 0};
        const Grey two = greyFromRgba(px, 2, 1);
        assert(two.w == 2 && two.px[0] > 0.99f && two.px[1] == 0.0f);
        assert(greyFromRgba(nullptr, 2, 2).px.empty());
    }

    // §4 The program.
    Params p;
    p.mode = Mode::Density;
    Grey ramp = blank(0.5f);
    const Art art = convert(ramp, g, p);
    {
        const auto prog = toProgram(art, kDefaultOrigin);
        assert(prog.size() == 43u + 959u + 1u);
        assert(prog.front().first == 0x0280 && prog.front().second == 0xA9);
        for (std::size_t i = 1; i < prog.size(); ++i) assert(prog[i].first == prog[i - 1].first + 1);
        // LDA #<data / LDA #>data point at the first character.
        const uint16_t data = static_cast<uint16_t>(prog[6].second | (prog[10].second << 8));
        assert(data == 0x0280 + 43);
        // Branches: BEQ wait, BNE loop (×2), BPL wait — targets by arithmetic.
        auto at = [&](uint16_t a) { return prog[static_cast<std::size_t>(a - 0x0280)].second; };
        auto target = [&](uint16_t a) { return static_cast<uint16_t>(a + 2 + static_cast<int8_t>(at(a + 1))); };
        assert(at(0x0291) == 0xF0 && target(0x0291) == 0x02A0);   // BEQ → wait
        assert(at(0x029A) == 0xD0 && target(0x029A) == 0x028F);   // BNE → loop
        assert(at(0x029E) == 0xD0 && target(0x029E) == 0x028F);   // BNE → loop
        assert(at(0x02A3) == 0x10 && target(0x02A3) == 0x02A0);   // BPL → wait
        assert(at(0x02A0) == 0xAD && at(0x02A8) == 0x4C);         // LDA KBDCR … JMP GETLINE
        // The characters, in screen order, then the terminator.
        std::size_t i = 43;
        for (int r = 0; r < kRows; ++r) {
            const int cols = r == kRows - 1 ? kLastRowCols : kCols;
            for (int c = 0; c < cols; ++c, ++i)
                assert(prog[i].second == static_cast<uint8_t>(art.rows[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]));
        }
        assert(prog.back().second == 0x00);
        // Another origin relocates the pointer.
        const auto at1000 = toProgram(art, 0x1000);
        assert(at1000[6].second == 0x2B && at1000[10].second == 0x10);
    }

    // §5 Text outputs.
    {
        const std::string woz = toWozMonitor(art);
        assert(woz.rfind("0280: A9 8D 20 EF FF A9 AB 85", 0) == 0);
        assert(woz.size() > 6 && woz.substr(woz.size() - 6) == "0280R\n");

        Art tricky = art;
        tricky.rows[0] = std::string(kCols, '"');
        tricky.rows[1] = "_" + std::string(kCols - 1, 'X');
        tricky.rows[2] = "AB";
        const std::string basic = toIntegerBasic(tricky);
        int lines = 0;
        std::size_t pos = 0;
        while ((pos = basic.find('\n', pos)) != std::string::npos) { ++lines; ++pos; }
        assert(lines == kRows + 1);
        assert(basic.find("250 GOTO 250\n") != std::string::npos);
        assert(basic.find("10 PRINT\n") == 0);                     // all-quote row → blank
        assert(basic.find("20 PRINT \" XXX") != std::string::npos); // '_' → ' '
        assert(basic.find("30 PRINT \"AB\"\n") != std::string::npos);// short row: CR, no ';'
        // No string carries a quote or an underscore.
        std::size_t q = 0;
        while ((q = basic.find("PRINT \"", q)) != std::string::npos) {
            const std::size_t open = q + 7, close = basic.find('"', open);
            const std::string s = basic.substr(open, close - open);
            assert(s.find('_') == std::string::npos);
            q = close + 1;
        }
        assert(toText(art).size() > 0);
    }

    // §6 Framing.
    {
        // frameRect: zoom 1 = the largest 4:3 window, centred. (Compared to a
        // thousandth of a pixel: 4.0f/3.0f is not exactly 4/3.)
        auto same = [](const FrameRect& r, double x, double y, double w, double h) {
            auto near = [](double a, double b) { return a - b < 1e-3 && b - a < 1e-3; };
            return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
        };
        assert(same(frameRect(800, 600, Framing{}), 0, 0, 800, 600));
        assert(same(frameRect(1600, 600, Framing{}), 400, 0, 800, 600));      // wide: sides cropped
        assert(same(frameRect(800, 600, Framing{2.0f, 0.25f, 0.25f}), 0, 0, 400, 300));  // top-left quarter
        assert(same(frameRect(800, 600, Framing{0.5f, 0.5f, 0.5f}), -400, -300, 1600, 1200)); // overhangs
        assert(same(frameRect(800, 600, Framing{100.0f, 9.0f, -3.0f}), 775, -18.75, 50, 37.5)); // clamped
        assert(frameRect(0, 10, Framing{}).w == 0);
        Framing stretch;
        stretch.aspect = 0.0f;                                     // the picture's own shape
        assert(same(frameRect(1600, 600, stretch), 0, 0, 1600, 600));

        // A white left half: zoom on it -> all ink; pan to the right half -> blank;
        // pull back -> black margins round a smaller picture.
        Grey half;
        half.w = 400; half.h = 300;
        half.px.assign(static_cast<std::size_t>(half.w) * half.h, 0.0f);
        for (int y = 0; y < half.h; ++y)
            for (int x = 0; x < half.w / 2; ++x) half.px[static_cast<std::size_t>(y * half.w + x)] = 1.0f;
        Params p2;
        p2.mode = Mode::Density;
        p2.dither = false;
        p2.framing = Framing{2.0f, 0.25f, 0.5f};
        const Art onWhite = convert(half, g, p2);
        p2.framing = Framing{2.0f, 0.75f, 0.5f};
        const Art onBlack = convert(half, g, p2);
        p2.framing = Framing{0.5f, 0.5f, 0.5f};
        const Art pulled = convert(half, g, p2);
        assert(onWhite.rows[12][20] != ' ' && onBlack.rows[12][20] == ' ');
        assert(pulled.rows[0][0] == ' ' && pulled.rows[12][0] == ' ');   // margin
        assert(pulled.rows[12][12] != ' ' && pulled.rows[12][27] == ' '); // picture, then its black half
    }

    // §7 Framing by hand, on the original picture.
    {
        auto near = [](double a, double b) { return a - b < 1e-3 && b - a < 1e-3; };
        auto same = [&](const FrameRect& r, double x, double y, double w, double h) {
            return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
        };
        const Framing base;   // 800 x 600 picture: the whole of it at zoom 1

        // framingFor is frameRect's inverse, and clamps the way it does.
        assert(same(frameRect(800, 600, framingFor(800, 600, base, 200, 150, 400)), 0, 0, 400, 300));
        assert(near(framingFor(800, 600, base, 400, 300, 1.0).zoom, kMaxZoom));
        assert(near(framingFor(800, 600, base, 400, 300, 1e6).zoom, kMinZoom));
        assert(near(framingFor(800, 600, base, -50, 900, 400).centerX, 0.0));
        assert(near(framingFor(800, 600, base, -50, 900, 400).centerY, 1.0));

        // Pan: by picture pixels; the centre stops at the picture's edge.
        Framing half = framingFor(800, 600, base, 400, 300, 400);
        assert(same(frameRect(800, 600, panFrame(800, 600, half, 100, -50)), 300, 100, 400, 300));
        assert(same(frameRect(800, 600, panFrame(800, 600, half, 5000, 0)), 600, 150, 400, 300));

        // Wheel: the point under the cursor stays put in the frame.
        const Framing z = zoomFrameAbout(800, 600, base, 2.0, 200, 150);
        assert(same(frameRect(800, 600, z), 100, 75, 400, 300));
        const FrameRect zr = frameRect(800, 600, z);
        assert(near((200 - zr.x) / zr.w, 0.25) && near((150 - zr.y) / zr.h, 0.25));
        // At the limit the factor taken is the clamped one, and the anchor follows it.
        const Framing top = framingFor(800, 600, base, 400, 300, 800.0 / kMaxZoom);
        assert(same(frameRect(800, 600, zoomFrameAbout(800, 600, top, 4.0, 0, 0)), frameRect(800, 600, top).x,
                    frameRect(800, 600, top).y, 50, 37.5));

        // Click outside: recentred, same size.
        assert(same(frameRect(800, 600, centreFrameOn(800, 600, half, 600, 450)), 400, 300, 400, 300));

        // Corner drag: the opposite corner holds, the shape holds, the larger pull wins.
        const FrameRect h0 = frameRect(800, 600, half);   // 200,150 400x300
        assert(same(frameRect(800, 600, resizeFrameFromCorner(800, 600, half, Corner::BottomRight, 800, 450)),
                     200, 150, 600, 450));
        assert(same(frameRect(800, 600, resizeFrameFromCorner(800, 600, half, Corner::BottomRight, 300, 600)),
                     200, 150, 600, 450));                 // taller pull: 450 high -> 600 wide
        assert(same(frameRect(800, 600, resizeFrameFromCorner(800, 600, half, Corner::TopLeft, 400, 300)),
                     400, 300, 200, 150));                 // shrunk towards the held bottom-right
        {
            const FrameRect tr = frameRect(800, 600, resizeFrameFromCorner(800, 600, half, Corner::TopRight, 800, 150));
            assert(near(tr.x, 200) && near(tr.y + tr.h, 450) && near(tr.w, 600) && near(tr.h, 450));
        }
        // Dragged across the held corner: never folds, never inverts.
        assert(frameRect(800, 600, resizeFrameFromCorner(800, 600, half, Corner::BottomRight, 0, 0)).w > 0.0);

        // Whole picture: a wide picture in a 4:3 frame gets bars above and below.
        const FrameRect whole = frameRect(1600, 600, fitWholePicture(1600, 600, base));
        assert(same(whole, 0, -300, 1600, 1200));
        assert(same(frameRect(800, 600, fitWholePicture(800, 600, base)), 0, 0, 800, 600));

        // The panel fits picture and frame together, centred.
        const View v = fitView(800, 600, frameRect(800, 600, base), 420, 320, 10);
        assert(near(v.scale, 0.5) && near(v.originX, 10) && near(v.originY, 10));
        assert(near(v.toPictureX(v.toPanelX(123.0)), 123.0));
        const View vo = fitView(800, 600, frameRect(800, 600, Framing{0.5f, 0.5f, 0.5f}), 400, 300, 0);
        assert(near(vo.scale, 0.25) && near(vo.toPanelX(-400), 0) && near(vo.toPanelY(900), 300));

        // Hit-testing: corners first, then inside, then outside.
        assert(hitFrame(h0, v, v.toPanelX(200), v.toPanelY(150), 6) == FrameHit::TopLeft);
        assert(hitFrame(h0, v, v.toPanelX(600) + 4, v.toPanelY(450) - 4, 6) == FrameHit::BottomRight);
        assert(hitFrame(h0, v, v.toPanelX(600), v.toPanelY(150), 6) == FrameHit::TopRight);
        assert(hitFrame(h0, v, v.toPanelX(200), v.toPanelY(450), 6) == FrameHit::BottomLeft);
        assert(hitFrame(h0, v, v.toPanelX(400), v.toPanelY(300), 6) == FrameHit::Inside);
        assert(hitFrame(h0, v, v.toPanelX(50), v.toPanelY(300), 6) == FrameHit::Outside);

        // The texture copy: kept as is when small, box-filtered when not.
        std::vector<uint8_t> img(4 * 4 * 4, 0);
        for (int i = 0; i < 4 * 4; ++i) {
            img[static_cast<std::size_t>(i) * 4] = static_cast<uint8_t>((i % 4) < 2 ? 0 : 200);
            img[static_cast<std::size_t>(i) * 4 + 3] = 255;
        }
        const Rgba keep = shrinkToFit(img.data(), 4, 4, 4);
        assert(keep.w == 4 && keep.h == 4 && keep.px == img);
        const Rgba small = shrinkToFit(img.data(), 4, 4, 2);
        assert(small.w == 2 && small.h == 2 && small.px[0] == 0 && small.px[4] == 200 && small.px[3] == 255);
        const Rgba wide = shrinkToFit(img.data(), 4, 1, 2);
        assert(wide.w == 2 && wide.h == 1);
        assert(shrinkToFit(nullptr, 4, 4, 2).w == 0);
    }

    std::puts("ascii_art_smoke: OK");
    return 0;
}
