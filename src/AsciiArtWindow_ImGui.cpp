// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "AsciiArtWindow_ImGui.h"

#include "NativeFileDialog.h"
#include "ResourceLocator.h"
#include "imgui.h"

// Declarations only: MainWindow_Dialogs.cpp holds STB_IMAGE_IMPLEMENTATION.
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace pom1 {

namespace {

/// Explicit sizes are authored at 100 % and scaled by the live interface
/// zoom, which portable widgets read from the style (see CassetteDeck_ImGui).
float px(float v)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    return v * st.FontScaleMain * st.FontScaleDpi;
}

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool AsciiArtWindow_ImGui::isImagePath(const std::string& path)
{
    const std::string ext = lower(std::filesystem::path(path).extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           ext == ".gif" || ext == ".tga" || ext == ".psd" || ext == ".pnm" ||
           ext == ".ppm" || ext == ".pgm";
}

bool AsciiArtWindow_ImGui::openImage(const std::string& path, std::string& error)
{
    int w = 0, h = 0, n = 0;
    unsigned char* rgba = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (rgba == nullptr) {
        const char* why = stbi_failure_reason();
        error = "Cannot read the picture: " + std::string(why ? why : "unknown format");
        return false;
    }
    picture_ = asciiart::greyFromRgba(rgba, w, h);
    stbi_image_free(rgba);
    path_ = path;
    std::strncpy(pathField_, path.c_str(), sizeof(pathField_) - 1);
    pathField_[sizeof(pathField_) - 1] = '\0';
    params_.framing = asciiart::Framing{};
    loadError_.clear();
    dirty_ = true;
    return true;
}

void AsciiArtWindow_ImGui::ensureGlyphs()
{
    if (glyphsLoaded_ || !glyphError_.empty()) return;
    const std::filesystem::path rom = ResourceLocator::defaultLocator().find("roms/charmap.rom");
    std::ifstream f(rom, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (rom.empty() || !asciiart::glyphsFromCharmap(bytes.data(), bytes.size(), glyphs_)) {
        glyphError_ = "roms/charmap.rom (the 2513 character generator) was not found";
        return;
    }
    glyphsLoaded_ = true;
}

void AsciiArtWindow_ImGui::reconvert()
{
    if (!dirty_ || !glyphsLoaded_) return;
    art_ = asciiart::convert(picture_, glyphs_, params_);
    dirty_ = false;
}

void AsciiArtWindow_ImGui::drawPreview(float width)
{
    using namespace asciiart;
    constexpr int dotsW = kCols * kCellW, dotsH = kRows * kCellH;
    // The Apple-1 picture is 4:3 on its monitor although its dot grid is not.
    const float w = std::max(px(280.0f), width);
    const float h = w * 3.0f / 4.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##asciiartpreview", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    // Framing by hand: drag pans, wheel zooms about the frame's centre.
    if (picture_.w > 0) {
        Framing& f = params_.framing;
        const FrameRect fr = frameRect(picture_.w, picture_.h, f);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                f.centerX = std::clamp(f.centerX - static_cast<float>(d.x / w * fr.w / picture_.w), 0.0f, 1.0f);
                f.centerY = std::clamp(f.centerY - static_cast<float>(d.y / h * fr.h / picture_.h), 0.0f, 1.0f);
                dirty_ = true;
            }
        }
        const float wheel = ImGui::GetIO().MouseWheel;
        if (hovered && wheel != 0.0f) {
            f.zoom = std::clamp(f.zoom * std::pow(1.15f, wheel), 0.25f, 16.0f);
            dirty_ = true;
        }
    }
    reconvert();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + h), IM_COL32(0, 0, 0, 255));
    const float dx = w / dotsW, dy = h / dotsH;
    const ImU32 ink = IM_COL32(110, 255, 140, 255);   // P1-ish phosphor
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int code = static_cast<unsigned char>(art_.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)]);
            if (code <= kFirstCode || code >= kFirstCode + kGlyphCount) continue;
            const auto& g = glyphs_.rows[static_cast<std::size_t>(code - kFirstCode)];
            for (int y = 0; y < kCellH; ++y) {
                const uint8_t bits = g[static_cast<std::size_t>(y)];
                if (!bits) continue;
                for (int x = 1; x < kCellW; ++x) {
                    if (!((bits >> x) & 1u)) continue;
                    const float x0 = origin.x + (col * kCellW + x) * dx;
                    const float y0 = origin.y + (row * kCellH + y) * dy;
                    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + dx * 0.85f, y0 + dy * 0.85f), ink);
                }
            }
        }
    }
    if (hovered) ImGui::SetTooltip("Drag to move the frame, wheel to zoom.");
}

void AsciiArtWindow_ImGui::saveText(const Actions& a, const std::string& what, const std::string& text,
                                    const std::string& suffix, const std::string& filterName)
{
    const std::filesystem::path src(path_);
    const std::string defName = (src.empty() ? std::string("picture") : src.stem().string()) + suffix;
    std::string target;
    if (NativeFileDialog::isAvailable()) {
        const std::vector<FileFilter> filters = {{filterName, {"txt"}}};
        if (!NativeFileDialog::saveFile(a.parent, "Save " + what, a.defaultDir, defName, filters, target))
            return;   // cancelled
    } else {
        // No picker (WASM, a bare Linux box): next to the picture, and say where.
        target = (src.has_parent_path() ? src.parent_path() / defName : std::filesystem::path(defName)).string();
    }
    std::ofstream out(target, std::ios::binary);
    out << text;
    out.flush();
    if (a.status) a.status(out.good() ? what + " saved to " + target : "Cannot write " + target);
}

void AsciiArtWindow_ImGui::render(bool* open, const Actions& a)
{
    if (open == nullptr || !*open) return;
    ImGui::SetNextWindowSize(ImVec2(px(620.0f), px(720.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Image to Apple-1 Text", open)) {
        ImGui::End();
        return;
    }
    ensureGlyphs();
    if (!glyphError_.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", glyphError_.c_str());
        ImGui::End();
        return;
    }

    // ---- picture ---------------------------------------------------------
    ImGui::SetNextItemWidth(-px(160.0f));
    const bool enter = ImGui::InputText("##asciiartpath", pathField_, sizeof(pathField_),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load") || enter) {
        std::string err;
        if (!openImage(pathField_, err)) loadError_ = err;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...") && NativeFileDialog::isAvailable()) {
        const std::vector<FileFilter> filters = {
            {"Pictures", {"png", "jpg", "jpeg", "bmp", "gif", "tga"}}};
        std::string picked;
        if (NativeFileDialog::openFile(a.parent, "Open a picture", a.defaultDir, filters, picked)) {
            std::string err;
            if (!openImage(picked, err)) loadError_ = err;
        }
    }
    if (!loadError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", loadError_.c_str());
    if (picture_.w == 0) ImGui::TextDisabled("Type a path, Browse, or drop a picture on POM1.");
    else ImGui::TextDisabled("%d x %d pixels", picture_.w, picture_.h);

    // ---- preview ---------------------------------------------------------
    drawPreview(ImGui::GetContentRegionAvail().x);

    // ---- framing ---------------------------------------------------------
    asciiart::Framing& f = params_.framing;
    if (ImGui::SliderFloat("Zoom", &f.zoom, 0.25f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) dirty_ = true;
    if (ImGui::SliderFloat("Centre X", &f.centerX, 0.0f, 1.0f, "%.3f")) dirty_ = true;
    if (ImGui::SliderFloat("Centre Y", &f.centerY, 0.0f, 1.0f, "%.3f")) dirty_ = true;
    int aspect = f.aspect > 0.0f ? 0 : 1;
    if (ImGui::Combo("Shape", &aspect, "Keep proportions (4:3 frame)\0Stretch the whole picture\0")) {
        f.aspect = aspect == 0 ? 4.0f / 3.0f : 0.0f;
        dirty_ = true;
    }
    if (ImGui::Button("Reset framing")) { f = asciiart::Framing{}; dirty_ = true; }

    // ---- rendering -------------------------------------------------------
    ImGui::Separator();
    int mode = params_.mode == asciiart::Mode::Shape ? 0 : 1;
    if (ImGui::RadioButton("Shapes (line art, logos)", &mode, 0)) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Density (photos)", &mode, 1)) dirty_ = true;
    params_.mode = mode == 0 ? asciiart::Mode::Shape : asciiart::Mode::Density;
    if (ImGui::Checkbox("Dither", &params_.dither)) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Invert", &params_.invert)) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("BASIC-safe (no \" or _)", &params_.basicSafe)) dirty_ = true;
    if (ImGui::SliderFloat("Brightness", &params_.brightness, -1.0f, 1.0f, "%.2f")) dirty_ = true;
    if (ImGui::SliderFloat("Contrast", &params_.contrast, 0.0f, 3.0f, "%.2f")) dirty_ = true;
    if (ImGui::SliderFloat("Gamma", &params_.gamma, 0.2f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) dirty_ = true;

    // ---- outputs ---------------------------------------------------------
    ImGui::Separator();
    const bool haveArt = picture_.w > 0;
    ImGui::BeginDisabled(!haveArt);
    if (ImGui::Button("Show on the Apple-1") && a.runProgram) {
        a.runProgram(asciiart::toProgram(art_), asciiart::kDefaultOrigin);
        if (a.status) a.status("Picture printed by a 6502 program at $0280 - press a key to return");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Writes a 6502 program at $0280-$066B and runs it: it prints the\n"
                          "picture through the Woz Monitor's ECHO, then waits for a key.");
    ImGui::SameLine();
    if (ImGui::Button("Save program..."))
        saveText(a, "Woz Monitor program", asciiart::toWozMonitor(art_), ".apple1.txt",
                 "Woz Monitor hex dump (*.txt)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A hex dump File > Load Memory reads, and a real Apple-1 takes\n"
                          "typed (or sent over serial) at the Woz Monitor. Then 280R.");
    if (ImGui::Button("Type BASIC listing") && a.typeText) {
        asciiart::Params safe = params_;
        safe.basicSafe = true;
        a.typeText(asciiart::toIntegerBasic(asciiart::convert(picture_, glyphs_, safe)));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Start Integer BASIC first (E000R), then this types the listing;\n"
                          "RUN it. The last line holds the picture (Ctrl-C to stop).");
    ImGui::SameLine();
    if (ImGui::Button("Save BASIC...")) {
        asciiart::Params safe = params_;
        safe.basicSafe = true;
        saveText(a, "Integer BASIC listing",
                 asciiart::toIntegerBasic(asciiart::convert(picture_, glyphs_, safe)), ".bas.txt",
                 "Integer BASIC listing (*.txt)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy text")) {
        ImGui::SetClipboardText(asciiart::toText(art_).c_str());
        if (a.status) a.status("40 x 24 text copied");
    }
    ImGui::EndDisabled();
    ImGui::End();
}

} // namespace pom1
