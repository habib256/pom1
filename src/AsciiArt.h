// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_ASCII_ART_H
#define POM1_ASCII_ART_H

// Picture → Apple-1 screen: 40 × 24 characters of the Signetics 2513, and the
// programs that draw them on a real machine.
//
// PURE: pixels and glyph bitmaps in, characters and program text out. No file,
// no ROM lookup, no ImGui — AsciiArtWindow_ImGui is the front end, and the
// caller hands in the 2513 bitmaps it loaded from roms/charmap.rom.
//
// Two ways to choose a character, because they want different pictures:
//
//   * SHAPE — each 7 × 8-dot cell of the picture is compared with every glyph
//     AS IT IS DRAWN by the 2513 (POM1's own charmap.rom, dot for dot), after a
//     light blur so near-misses still score. Edges come out as / \ | - _ and
//     corners as the glyphs that have them: line art and logos.
//   * DENSITY — each cell's brightness is matched to a glyph's lit-dot count,
//     a ramp computed from the same ROM rather than typed in. Photographs.
//
// Both can error-diffuse (Floyd–Steinberg over cells), invert, and take
// brightness / contrast / gamma. The 2513 has 64 glyphs, $20-$5F: no lower
// case, and only lit-on-dark — the Apple-1 cannot draw a dark dot on a lit
// cell.
//
// Inspired by RDK Apple-1's Photo → ASCII (crates/apple1-core/src/asciiart.rs
// @ 0ef0d79, Old-Wirehead/rdk-apple1): the 40 × 24 grid, the 39-column last
// row, the BASIC and 6502 outputs. The shape matcher against the real ROM is
// POM1's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pom1::asciiart {

inline constexpr int kCols = 40;
inline constexpr int kRows = 24;
/// One character cell in 2513 dots: a 5-wide glyph (6 for '_') at dot 1 of a
/// 7-dot cell, 8 rows — the geometry Screen_ImGui rasterises.
inline constexpr int kCellW = 7;
inline constexpr int kCellH = 8;
/// The 64 displayable codes, $20-$5F.
inline constexpr int kFirstCode = 0x20;
inline constexpr int kGlyphCount = 64;
/// The last row keeps its 40th column blank: printing it would wrap the cursor
/// onto a 25th line and scroll the picture's top row away.
inline constexpr int kLastRowCols = kCols - 1;
/// Where toProgram() assembles by default: just past the Woz input buffer.
inline constexpr uint16_t kDefaultOrigin = 0x0280;

/// The 2513's 64 glyphs, charmap.rom layout: 8 row bytes each, dot `c` of a
/// row (0 = left) in bit (c + 1).
struct Glyphs {
    std::array<std::array<uint8_t, 8>, kGlyphCount> rows{};
};

/// Take $20-$5F out of a charmap.rom image (128 glyphs × 8 bytes; at least
/// $60 × 8 bytes are needed). False when the buffer is too short.
bool glyphsFromCharmap(const uint8_t* rom, std::size_t size, Glyphs& out);

/// A grey picture, row-major, 0 = black … 1 = white.
struct Grey {
    int w = 0;
    int h = 0;
    std::vector<float> px;
};

/// From 8-bit RGBA. A transparent pixel is black — the Apple-1 screen.
Grey greyFromRgba(const uint8_t* rgba, int w, int h);

enum class Mode : uint8_t { Shape, Density };

/// Framing. The frame is the screen's 4:3 shape; at zoom 1 it is the largest
/// 4:3 window that fits the picture. Zoom > 1 frames closer (up to 16×),
/// zoom < 1 pulls back and the frame overhangs the picture, the margin black
/// like the screen (down to 0.25×). The centre is in picture fractions,
/// 0 … 1 from the left / top; the frame may slide past an edge, and whatever
/// it shows of nothing is black.
struct Framing {
    float zoom = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    /// Width / height of the frame in PICTURE pixels. 4:3 keeps the picture's
    /// proportions on the Apple-1 monitor; 0 takes the picture's own shape,
    /// i.e. stretches the whole picture over the screen.
    float aspect = 4.0f / 3.0f;
};

/// The frame `f` cuts out of a w × h picture, in picture pixels (x, y, width,
/// height — x and y may be negative, the size may exceed the picture).
struct FrameRect { double x = 0, y = 0, w = 0, h = 0; };
FrameRect frameRect(int w, int h, const Framing& f);

inline constexpr float kMinZoom = 0.25f;
inline constexpr float kMaxZoom = 16.0f;

// ---- Framing by hand ------------------------------------------------------
//
// The window shows the original picture with the frame drawn on it; these
// turn what the mouse does there into a Framing. Every one goes through
// framingFor(), so they all clamp exactly as frameRect() does — a frame asked
// to slide or grow past a limit stops at it.

/// The inverse of frameRect(): the Framing (of `f`'s shape) whose frame is
/// centred on picture point (cx, cy) and `frameW` picture pixels wide.
Framing framingFor(int w, int h, const Framing& f, double cx, double cy, double frameW);

/// Slide the frame by (dx, dy) picture pixels.
Framing panFrame(int w, int h, const Framing& f, double dx, double dy);

/// Zoom by `factor` (> 1 = closer) keeping picture point (px, py) where it is
/// in the frame — the wheel under the cursor.
Framing zoomFrameAbout(int w, int h, const Framing& f, double factor, double px, double py);

/// Centre the frame on picture point (px, py), same size.
Framing centreFrameOn(int w, int h, const Framing& f, double px, double py);

/// The frame's corners, clockwise from the top left.
enum class Corner : uint8_t { TopLeft, TopRight, BottomRight, BottomLeft };

/// Drag `corner` to picture point (px, py), the opposite corner held. The
/// frame keeps its shape: the larger of the two pulls wins.
Framing resizeFrameFromCorner(int w, int h, const Framing& f, Corner corner, double px, double py);

/// The zoom at which the whole picture fits inside the frame (black bars on
/// the sides the frame's shape does not match), centred.
Framing fitWholePicture(int w, int h, const Framing& f);

/// How a panel shows the picture: panel = origin + picture × scale.
struct View {
    double originX = 0, originY = 0, scale = 1;
    double toPanelX(double x) const { return originX + x * scale; }
    double toPanelY(double y) const { return originY + y * scale; }
    double toPictureX(double x) const { return (x - originX) / scale; }
    double toPictureY(double y) const { return (y - originY) / scale; }
};

/// Fit the picture and the frame `fr` together (the frame overhangs the
/// picture when zoomed out) into a viewW × viewH panel, centred, with
/// `marginPx` free on every side.
View fitView(int w, int h, const FrameRect& fr, double viewW, double viewH, double marginPx);

/// What the panel point (mx, my) is over: a corner handle (a square of
/// `handlePx` panel pixels each side of the corner), inside the frame, or
/// outside it.
enum class FrameHit : uint8_t { Outside, Inside, TopLeft, TopRight, BottomRight, BottomLeft };
FrameHit hitFrame(const FrameRect& fr, const View& v, double mx, double my, double handlePx);

/// An 8-bit RGBA picture.
struct Rgba {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;
};

/// A copy no side longer than `maxSide`, box-filtered (a phone photo is past
/// some GPUs' texture limit); the picture itself when it already fits.
Rgba shrinkToFit(const uint8_t* rgba, int w, int h, int maxSide);

struct Params {
    Framing framing;
    Mode mode = Mode::Shape;
    bool dither = true;
    bool invert = false;
    float brightness = 0.0f;   ///< added, -1 … 1
    float contrast = 1.0f;     ///< around mid-grey, 0 … 3
    float gamma = 1.0f;        ///< 0.2 … 5; > 1 opens the shadows
    /// Leave out '"' and '_' — a BASIC string cannot hold the first, and a
    /// typed '_' is the Woz/BASIC backspace. Set for toIntegerBasic().
    bool basicSafe = false;
};

/// 24 rows of 40 printable characters ($20-$5F); the last row's 40th is ' '.
struct Art {
    std::array<std::string, kRows> rows;
};

/// The picture is framed (Params::framing), area-sampled into 280 × 192 dots,
/// then turned into characters. An empty picture gives a blank screen.
Art convert(const Grey& picture, const Glyphs& glyphs, const Params& params);

/// The 2513's ink for one code: lit dots, 0 … 20ish.
int litDots(const Glyphs& glyphs, char code);

/// 24 lines of plain text.
std::string toText(const Art& art);

/// A self-contained 6502 program at `origin` that prints the picture through
/// the Woz Monitor's ECHO ($FFEF) and then waits for a key before returning
/// to the Monitor — returning at once would print a CR and scroll the top row
/// away. Bytes ready for a memory write, in address order.
std::vector<std::pair<uint16_t, uint8_t>> toProgram(const Art& art,
                                                    uint16_t origin = kDefaultOrigin);

/// The same program as a Woz Monitor hex dump ("0280: A9 8D …"), which both
/// File ▸ Load Memory and a real Apple-1's keyboard accept. Run it with the
/// `R` command the first line names.
std::string toWozMonitor(const Art& art, uint16_t origin = kDefaultOrigin);

/// An Integer BASIC listing that PRINTs the picture and then holds it on
/// screen (a final GOTO to itself). Convert with Params::basicSafe, or any
/// '"' / '_' in the art is replaced by a space.
std::string toIntegerBasic(const Art& art);

} // namespace pom1::asciiart

#endif // POM1_ASCII_ART_H
