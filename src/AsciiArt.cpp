// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "AsciiArt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace pom1::asciiart {

namespace {

constexpr int kDotsW = kCols * kCellW;   // 280
constexpr int kDotsH = kRows * kCellH;   // 192

bool allowedCode(int code, bool basicSafe)
{
    if (code < kFirstCode || code >= kFirstCode + kGlyphCount) return false;
    if (basicSafe && (code == '"' || code == '_')) return false;
    return true;
}

/// A glyph as it lands in its 7 × 8 cell: 1 = lit dot.
using Cell = std::array<float, kCellW * kCellH>;

Cell glyphCell(const Glyphs& g, int code)
{
    Cell c{};
    const auto& rows = g.rows[static_cast<std::size_t>(code - kFirstCode)];
    for (int r = 0; r < kCellH; ++r)
        for (int x = 1; x < kCellW; ++x)   // dot x-1 of the glyph, bit x
            c[static_cast<std::size_t>(r * kCellW + x)] = (rows[static_cast<std::size_t>(r)] >> x) & 1u ? 1.0f : 0.0f;
    return c;
}

/// 3 × 3 box blur inside the cell, edges clamped: two shapes one dot apart
/// should score as near, not as far as two unrelated ones.
Cell blur(const Cell& in)
{
    Cell out{};
    for (int y = 0; y < kCellH; ++y) {
        for (int x = 0; x < kCellW; ++x) {
            float sum = 0.0f;
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int yy = y + dy, xx = x + dx;
                    if (yy < 0 || yy >= kCellH || xx < 0 || xx >= kCellW) continue;
                    sum += in[static_cast<std::size_t>(yy * kCellW + xx)];
                    ++n;
                }
            }
            out[static_cast<std::size_t>(y * kCellW + x)] = sum / static_cast<float>(n);
        }
    }
    return out;
}

float mean(const Cell& c)
{
    float s = 0.0f;
    for (float v : c) s += v;
    return s / static_cast<float>(c.size());
}

float adjust(float v, const Params& p)
{
    if (p.invert) v = 1.0f - v;
    v = (v - 0.5f) * p.contrast + 0.5f + p.brightness;
    v = std::min(1.0f, std::max(0.0f, v));
    const float g = std::min(5.0f, std::max(0.2f, p.gamma));
    return std::pow(v, 1.0f / g);
}

/// Frame, then area-sample into the 280 × 192 dot grid. Picture outside the
/// frame is dropped; frame outside the picture samples black.
std::vector<float> sampleDots(const Grey& pic, const Params& p)
{
    std::vector<float> dots(static_cast<std::size_t>(kDotsW * kDotsH), 0.0f);
    if (pic.w <= 0 || pic.h <= 0 || pic.px.size() < static_cast<std::size_t>(pic.w) * pic.h)
        return dots;
    const FrameRect fr = frameRect(pic.w, pic.h, p.framing);
    const double sx = fr.w / kDotsW, sy = fr.h / kDotsH;
    for (int dy = 0; dy < kDotsH; ++dy) {
        const double ya = fr.y + dy * sy, yb = ya + sy;
        for (int dx = 0; dx < kDotsW; ++dx) {
            const double xa = fr.x + dx * sx, xb = xa + sx;
            // Weights over the whole dot (outside = black, counted in the area).
            double acc = 0.0;
            const int y0 = std::max(0, static_cast<int>(std::floor(ya)));
            const int y1 = std::min(pic.h, static_cast<int>(std::ceil(yb)));
            const int x0 = std::max(0, static_cast<int>(std::floor(xa)));
            const int x1 = std::min(pic.w, static_cast<int>(std::ceil(xb)));
            for (int y = y0; y < y1; ++y) {
                const double wy = std::min<double>(y + 1, yb) - std::max<double>(y, ya);
                if (wy <= 0) continue;
                for (int x = x0; x < x1; ++x) {
                    const double wx = std::min<double>(x + 1, xb) - std::max<double>(x, xa);
                    if (wx <= 0) continue;
                    acc += wx * wy * pic.px[static_cast<std::size_t>(y) * pic.w + x];
                }
            }
            dots[static_cast<std::size_t>(dy * kDotsW + dx)] = adjust(static_cast<float>(acc / (sx * sy)), p);
        }
    }
    return dots;
}

} // namespace

FrameRect frameRect(int w, int h, const Framing& f)
{
    FrameRect r;
    if (w <= 0 || h <= 0) return r;
    // The largest window of the frame's shape inside the picture, then zoomed
    // about the centre.
    const double aspect = f.aspect > 0.0f ? static_cast<double>(f.aspect)
                                          : static_cast<double>(w) / h;
    double cw = w, ch = h;
    // Within float precision of the frame's shape already (an 800 x 600 photo
    // against 4.0f/3.0f): crop nothing rather than a fraction of a pixel.
    const double shape = cw / ch;
    if (shape > aspect * (1.0 + 1e-6)) cw = ch * aspect;
    else if (shape < aspect * (1.0 - 1e-6)) ch = cw / aspect;
    const double zoom = std::min(16.0, std::max(0.25, static_cast<double>(f.zoom)));
    r.w = cw / zoom;
    r.h = ch / zoom;
    r.x = std::min(1.0, std::max(0.0, static_cast<double>(f.centerX))) * w - r.w / 2.0;
    r.y = std::min(1.0, std::max(0.0, static_cast<double>(f.centerY))) * h - r.h / 2.0;
    return r;
}

bool glyphsFromCharmap(const uint8_t* rom, std::size_t size, Glyphs& out)
{
    if (rom == nullptr || size < static_cast<std::size_t>(kFirstCode + kGlyphCount) * 8) return false;
    for (int i = 0; i < kGlyphCount; ++i)
        for (int r = 0; r < 8; ++r)
            out.rows[static_cast<std::size_t>(i)][static_cast<std::size_t>(r)] =
                rom[static_cast<std::size_t>((kFirstCode + i) * 8 + r)];
    return true;
}

Grey greyFromRgba(const uint8_t* rgba, int w, int h)
{
    Grey g;
    if (rgba == nullptr || w <= 0 || h <= 0) return g;
    g.w = w;
    g.h = h;
    g.px.resize(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < g.px.size(); ++i) {
        const uint8_t* p = rgba + i * 4;
        const float l = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f;
        g.px[i] = l * (p[3] / 255.0f);
    }
    return g;
}

int litDots(const Glyphs& glyphs, char code)
{
    const int c = static_cast<unsigned char>(code);
    if (c < kFirstCode || c >= kFirstCode + kGlyphCount) return 0;
    int n = 0;
    for (uint8_t row : glyphs.rows[static_cast<std::size_t>(c - kFirstCode)])
        for (int x = 1; x < kCellW; ++x) n += (row >> x) & 1;
    return n;
}

Art convert(const Grey& picture, const Glyphs& glyphs, const Params& params)
{
    // Candidates, with what each is matched on.
    struct Candidate { char code; Cell shape; float ink; };
    std::vector<Candidate> cands;
    int maxDots = 1;
    for (int code = kFirstCode; code < kFirstCode + kGlyphCount; ++code) {
        if (!allowedCode(code, params.basicSafe)) continue;
        const Cell raw = glyphCell(glyphs, code);
        cands.push_back({static_cast<char>(code), blur(raw), mean(raw)});
        maxDots = std::max(maxDots, litDots(glyphs, static_cast<char>(code)));
    }
    // Density mode compares brightness with ink scaled to the densest glyph,
    // so a white cell maps to the densest character, not to an unreachable 1.0.
    const float inkScale = static_cast<float>(kCellW * kCellH) / static_cast<float>(maxDots);

    std::vector<float> dots = sampleDots(picture, params);
    std::vector<float> carry(static_cast<std::size_t>(kCols * kRows), 0.0f);   // diffused error

    Art art;
    for (int row = 0; row < kRows; ++row) {
        std::string& line = art.rows[static_cast<std::size_t>(row)];
        line.assign(kCols, ' ');
        const int cols = (row == kRows - 1) ? kLastRowCols : kCols;
        for (int col = 0; col < cols; ++col) {
            Cell patch{};
            for (int y = 0; y < kCellH; ++y)
                for (int x = 0; x < kCellW; ++x)
                    patch[static_cast<std::size_t>(y * kCellW + x)] =
                        dots[static_cast<std::size_t>((row * kCellH + y) * kDotsW + col * kCellW + x)];
            const float bias = params.dither ? carry[static_cast<std::size_t>(row * kCols + col)] : 0.0f;
            const Cell target = blur(patch);
            const float want = std::min(1.0f, std::max(0.0f, mean(patch) + bias));

            const Candidate* best = &cands.front();
            float bestScore = std::numeric_limits<float>::max();
            for (const Candidate& c : cands) {
                float score;
                if (params.mode == Mode::Density) {
                    score = std::fabs(c.ink * inkScale - want);
                } else {
                    score = 0.0f;
                    for (std::size_t i = 0; i < target.size(); ++i) {
                        const float d = target[i] + bias - c.shape[i];
                        score += d * d;
                    }
                }
                if (score < bestScore) { bestScore = score; best = &c; }
            }
            line[static_cast<std::size_t>(col)] = best->code;

            if (params.dither) {
                // Floyd–Steinberg over cells, on the brightness the cell asked
                // for versus the ink the glyph gives.
                const float got = params.mode == Mode::Density ? best->ink * inkScale : best->ink;
                const float err = want - got;
                auto push = [&](int r, int c, float w) {
                    if (r < kRows && c >= 0 && c < kCols)
                        carry[static_cast<std::size_t>(r * kCols + c)] += err * w;
                };
                push(row, col + 1, 7.0f / 16.0f);
                push(row + 1, col - 1, 3.0f / 16.0f);
                push(row + 1, col, 5.0f / 16.0f);
                push(row + 1, col + 1, 1.0f / 16.0f);
            }
        }
    }
    return art;
}

std::string toText(const Art& art)
{
    std::string out;
    for (const std::string& r : art.rows) {
        std::string line = r;
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line;
        out += '\n';
    }
    return out;
}

std::vector<std::pair<uint16_t, uint8_t>> toProgram(const Art& art, uint16_t origin)
{
    // Pointer at $1E/$1F: free on an Apple-1 (the Woz Monitor uses $24-$2B).
    constexpr uint8_t kPtr = 0x1E;
    const uint16_t data = static_cast<uint16_t>(origin + 0x2B);
    const uint8_t code[] = {
        0xA9, 0x8D,                         // LDA #$8D       start on a fresh line
        0x20, 0xEF, 0xFF,                   // JSR ECHO
        0xA9, static_cast<uint8_t>(data & 0xFF),   // LDA #<data
        0x85, kPtr,                         // STA ptr
        0xA9, static_cast<uint8_t>(data >> 8),     // LDA #>data
        0x85, kPtr + 1,                     // STA ptr+1
        0xA0, 0x00,                         // LDY #0
        0xB1, kPtr,                         // loop: LDA (ptr),Y
        0xF0, 0x0D,                         //   BEQ wait
        0x09, 0x80,                         //   ORA #$80     as the Monitor echoes
        0x20, 0xEF, 0xFF,                   //   JSR ECHO
        0xE6, kPtr,                         //   INC ptr
        0xD0, 0xF3,                         //   BNE loop
        0xE6, kPtr + 1,                     //   INC ptr+1
        0xD0, 0xEF,                         //   BNE loop
        0xAD, 0x11, 0xD0,                   // wait: LDA KBDCR
        0x10, 0xFB,                         //   BPL wait     hold the picture
        0xAD, 0x10, 0xD0,                   //   LDA KBD      take the key
        0x4C, 0x1F, 0xFF,                   //   JMP GETLINE
    };
    static_assert(sizeof(code) == 0x2B, "the data address above assumes 43 bytes of code");

    std::vector<std::pair<uint16_t, uint8_t>> out;
    uint16_t a = origin;
    for (uint8_t b : code) out.emplace_back(a++, b);
    for (int row = 0; row < kRows; ++row) {
        const int cols = (row == kRows - 1) ? kLastRowCols : kCols;
        for (int col = 0; col < cols; ++col) {
            char c = col < static_cast<int>(art.rows[static_cast<std::size_t>(row)].size())
                   ? art.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] : ' ';
            if (!allowedCode(static_cast<unsigned char>(c), false)) c = ' ';
            out.emplace_back(a++, static_cast<uint8_t>(c));   // no CR: the terminal wraps at 40
        }
    }
    out.emplace_back(a, 0x00);
    return out;
}

std::string toWozMonitor(const Art& art, uint16_t origin)
{
    const auto bytes = toProgram(art, origin);
    std::string out;
    char buf[16];
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i % 8 == 0) {
            if (i) out += '\n';
            std::snprintf(buf, sizeof(buf), "%04X:", bytes[i].first);
            out += buf;
        }
        std::snprintf(buf, sizeof(buf), " %02X", bytes[i].second);
        out += buf;
    }
    std::snprintf(buf, sizeof(buf), "\n%04XR\n", origin);
    out += buf;
    return out;
}

std::string toIntegerBasic(const Art& art)
{
    std::string out;
    char num[16];
    int line = 10;
    for (int row = 0; row < kRows; ++row) {
        const int cols = (row == kRows - 1) ? kLastRowCols : kCols;
        std::string text = art.rows[static_cast<std::size_t>(row)].substr(0, static_cast<std::size_t>(cols));
        for (char& c : text)
            if (!allowedCode(static_cast<unsigned char>(c), true)) c = ' ';
        // A full row wraps by itself: end it with ';' or PRINT adds a CR and a
        // blank line. A shorter row ends with the CR PRINT gives.
        const bool full = static_cast<int>(text.size()) == cols && text.back() != ' ';
        if (!full) while (!text.empty() && text.back() == ' ') text.pop_back();
        std::snprintf(num, sizeof(num), "%d", line);
        out += num;
        if (text.empty()) {
            out += " PRINT\n";
        } else {
            out += " PRINT \"" + text + "\"";
            out += (full || row == kRows - 1) ? ";\n" : "\n";
        }
        line += 10;
    }
    std::snprintf(num, sizeof(num), "%d", line);
    out += std::string(num) + " GOTO " + num + "\n";
    return out;
}

} // namespace pom1::asciiart
