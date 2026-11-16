// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_ASCII_ART_WINDOW_IMGUI_H
#define POM1_ASCII_ART_WINDOW_IMGUI_H

// DevBench ▸ Image to Apple-1 Text — the front end of src/AsciiArt.h.
//
// Load a picture (or drop one on POM1) and frame it on the ORIGINAL, shown in
// colour with the 40 × 24 frame over it: drag inside the frame to move it, a
// corner to resize it, click outside to recentre it there, wheel to zoom about
// the cursor. Beside it the Apple-1 preview shows the screen exactly as the
// 2513 will draw it (dragging and the wheel work there too). Pick SHAPE or
// DENSITY. Then: show it on the
// emulated Apple-1 (a 6502 program written to $0280 and started), save that
// program as a Woz Monitor hex dump for a real machine, save or type an
// Integer BASIC listing, or copy the text.
//
// All the decisions are in AsciiArt.{h,cpp} (pure, `ascii_art_smoke`); this
// class only draws and wires. It knows nothing of MainWindow: what it needs
// from the machine arrives as callbacks.

#include "AsciiArt.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace pom1 {

struct Texture;

class AsciiArtWindow_ImGui {
public:
    struct Actions {
        GLFWwindow* parent = nullptr;     ///< for the native file pickers
        std::string defaultDir;           ///< where Browse / Save start
        /// Write the bytes, then start the CPU at the address.
        std::function<void(const std::vector<std::pair<uint16_t, uint8_t>>&, uint16_t)> runProgram;
        /// Type text into the Apple-1 keyboard (the BASIC listing).
        std::function<void(const std::string&)> typeText;
        /// One line in the status bar.
        std::function<void(const std::string&)> status;
    };

    /// Decode a picture file (PNG, JPEG, BMP, GIF, TGA…). Resets the framing.
    bool openImage(const std::string& path, std::string& error);

    /// Draw the window while `*open`; closing it clears `*open`.
    void render(bool* open, const Actions& actions);

    /// Extensions openImage() takes, lower case, no dot — the drop router asks.
    static bool isImagePath(const std::string& path);

    /// Hand the original's texture back to the renderer (renderer teardown).
    void releaseTextures();

private:
    void ensureGlyphs();
    void reconvert();
    void drawOriginal(float width, float height);
    void drawPreview(float width);
    void uploadOriginal();
    void saveText(const Actions& a, const std::string& what, const std::string& text,
                  const std::string& suffix, const std::string& filterName);

    asciiart::Glyphs glyphs_;
    bool glyphsLoaded_ = false;
    std::string glyphError_;

    asciiart::Grey picture_;
    asciiart::Rgba display_;              ///< the colour copy the Original panel shows
    pom1::Texture* texture_ = nullptr;    ///< display_ on the GPU, made on first draw
    bool textureStale_ = false;
    /// What a drag on the Original started on, and the view frozen for it —
    /// refitting the panel while the frame moves would slide it under the mouse.
    asciiart::FrameHit drag_ = asciiart::FrameHit::Outside;
    bool dragging_ = false;
    asciiart::View dragView_;
    asciiart::Framing dragStart_;         ///< the framing the drag started from
    float dragMouseX_ = 0.0f, dragMouseY_ = 0.0f;
    std::string path_;
    char pathField_[1024] = "";
    std::string loadError_;

    asciiart::Params params_;
    asciiart::Art art_;
    bool dirty_ = true;
};

} // namespace pom1

#endif // POM1_ASCII_ART_WINDOW_IMGUI_H
