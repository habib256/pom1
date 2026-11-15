// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#ifndef POM1_ASCII_ART_WINDOW_IMGUI_H
#define POM1_ASCII_ART_WINDOW_IMGUI_H

// DevBench ▸ Image to Apple-1 Text — the front end of src/AsciiArt.h.
//
// Load a picture (or drop one on POM1), frame it — drag the preview to pan,
// wheel to zoom, or the sliders — pick SHAPE or DENSITY, and the preview shows
// the 40 × 24 screen exactly as the 2513 will draw it. Then: show it on the
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

private:
    void ensureGlyphs();
    void reconvert();
    void drawPreview(float width);
    void saveText(const Actions& a, const std::string& what, const std::string& text,
                  const std::string& suffix, const std::string& filterName);

    asciiart::Glyphs glyphs_;
    bool glyphsLoaded_ = false;
    std::string glyphError_;

    asciiart::Grey picture_;
    std::string path_;
    char pathField_[1024] = "";
    std::string loadError_;

    asciiart::Params params_;
    asciiart::Art art_;
    bool dirty_ = true;
};

} // namespace pom1

#endif // POM1_ASCII_ART_WINDOW_IMGUI_H
