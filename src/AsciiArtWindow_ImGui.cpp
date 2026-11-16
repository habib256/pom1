// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "AsciiArtWindow_ImGui.h"

#include "NativeFileDialog.h"
#include "PomRenderer.h"
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

/// The Original panel never magnifies the picture (it always shows all of
/// it), so a copy about the panel's size in device pixels is all it needs —
/// and nearest sampling (Metal) of a much larger one would shimmer.
constexpr int kDisplayMaxSide = 1024;

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
    display_ = asciiart::shrinkToFit(rgba, w, h, kDisplayMaxSide);
    stbi_image_free(rgba);
    textureStale_ = true;
    dragging_ = false;
    path_ = path;
    std::strncpy(pathField_, path.c_str(), sizeof(pathField_) - 1);
    pathField_[sizeof(pathField_) - 1] = '\0';
    params_.framing = asciiart::Framing{};
    loadError_.clear();
    dirty_ = true;
    return true;
}

void AsciiArtWindow_ImGui::releaseTextures()
{
    if (texture_ != nullptr) {
        if (PomRenderer* r = renderer()) r->destroyTexture(texture_);
        texture_ = nullptr;
    }
    textureStale_ = display_.w > 0;
}

void AsciiArtWindow_ImGui::uploadOriginal()
{
    if (!textureStale_) return;
    PomRenderer* r = renderer();
    if (r == nullptr) return;   // headless, or before the backend is up: try next frame
    if (texture_ != nullptr) r->destroyTexture(texture_);
    texture_ = nullptr;
    if (display_.w > 0)
        texture_ = r->createTexture(display_.w, display_.h, PomRenderer::Filter::Linear,
                                    reinterpret_cast<const uint32_t*>(display_.px.data()));
    textureStale_ = false;
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

void AsciiArtWindow_ImGui::drawOriginal(float width, float height)
{
    using namespace asciiart;
    const float w = std::max(px(200.0f), width);
    const float h = std::max(px(150.0f), height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + w, origin.y + h);
    ImGui::InvisibleButton("##asciiartoriginal", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, end, IM_COL32(28, 28, 32, 255));
    if (picture_.w == 0) {
        const char* msg = "No picture";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(origin.x + (w - ts.x) / 2, origin.y + (h - ts.y) / 2),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const double mx = io.MousePos.x - origin.x, my = io.MousePos.y - origin.y;
    const double handle = px(7.0f);
    Framing& f = params_.framing;
    FrameRect fr = frameRect(picture_.w, picture_.h, f);
    const View fitted = fitView(picture_.w, picture_.h, fr, w, h, px(10.0f));
    const View& v = dragging_ ? dragView_ : fitted;
    const FrameHit over = hitFrame(fr, v, mx, my, handle);

    // A drag starts from what was under the mouse; outside the frame it first
    // recentres the frame there, then moves it like a drag from inside.
    if (ImGui::IsItemActivated()) {
        dragging_ = true;
        dragView_ = fitted;
        drag_ = over;
        if (over == FrameHit::Outside) {
            f = centreFrameOn(picture_.w, picture_.h, f, v.toPictureX(mx), v.toPictureY(my));
            drag_ = FrameHit::Inside;
            dirty_ = true;
        }
        dragStart_ = f;
        dragMouseX_ = io.MousePos.x;
        dragMouseY_ = io.MousePos.y;
    }
    if (active && dragging_) {
        const int pw = picture_.w, ph = picture_.h;
        const double px0 = dragView_.toPictureX(mx), py0 = dragView_.toPictureY(my);
        Framing next = f;
        switch (drag_) {
        case FrameHit::Inside:
            // From where the drag began, so a clamped edge does not leave the
            // frame lagging behind the mouse on the way back.
            next = panFrame(pw, ph, dragStart_, (io.MousePos.x - dragMouseX_) / dragView_.scale,
                            (io.MousePos.y - dragMouseY_) / dragView_.scale);
            break;
        case FrameHit::TopLeft:     next = resizeFrameFromCorner(pw, ph, dragStart_, Corner::TopLeft, px0, py0); break;
        case FrameHit::TopRight:    next = resizeFrameFromCorner(pw, ph, dragStart_, Corner::TopRight, px0, py0); break;
        case FrameHit::BottomRight: next = resizeFrameFromCorner(pw, ph, dragStart_, Corner::BottomRight, px0, py0); break;
        case FrameHit::BottomLeft:  next = resizeFrameFromCorner(pw, ph, dragStart_, Corner::BottomLeft, px0, py0); break;
        case FrameHit::Outside: break;
        }
        if (next.zoom != f.zoom || next.centerX != f.centerX || next.centerY != f.centerY) {
            f = next;
            dirty_ = true;
        }
    }
    if (!active) dragging_ = false;
    if (hovered && io.MouseWheel != 0.0f && !dragging_) {
        f = zoomFrameAbout(picture_.w, picture_.h, f, std::pow(1.15, static_cast<double>(io.MouseWheel)),
                           v.toPictureX(mx), v.toPictureY(my));
        dirty_ = true;
    }

    // What the mouse would do, as a cursor.
    const FrameHit shown = dragging_ ? drag_ : over;
    if (hovered || dragging_) {
        switch (shown) {
        case FrameHit::TopLeft: case FrameHit::BottomRight: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
        case FrameHit::TopRight: case FrameHit::BottomLeft: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW); break;
        case FrameHit::Inside: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); break;
        case FrameHit::Outside: ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); break;
        }
    }

    // ---- draw: picture, the frame's black overhang, dimmed outside, frame --
    fr = frameRect(picture_.w, picture_.h, f);
    const View& dv = dragging_ ? dragView_ : fitView(picture_.w, picture_.h, fr, w, h, px(10.0f));
    auto P = [&](double x, double y) {
        return ImVec2(origin.x + static_cast<float>(dv.toPanelX(x)), origin.y + static_cast<float>(dv.toPanelY(y)));
    };
    const ImVec2 f0 = P(fr.x, fr.y), f1 = P(fr.x + fr.w, fr.y + fr.h);
    const ImVec2 p0 = P(0, 0), p1 = P(picture_.w, picture_.h);
    dl->PushClipRect(origin, end, true);
    dl->AddRectFilled(f0, f1, IM_COL32(0, 0, 0, 255));   // what the frame shows of nothing: black
    uploadOriginal();
    if (texture_ != nullptr)
        dl->AddImage(renderer()->asImTextureID(texture_), p0, p1);
    else
        dl->AddRectFilled(p0, p1, IM_COL32(90, 90, 90, 255));
    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 60));
    // Everything outside the frame is dimmed: four bands round it.
    const ImU32 dim = IM_COL32(0, 0, 0, 150);
    dl->AddRectFilled(origin, ImVec2(end.x, f0.y), dim);
    dl->AddRectFilled(ImVec2(origin.x, f1.y), end, dim);
    dl->AddRectFilled(ImVec2(origin.x, f0.y), ImVec2(f0.x, f1.y), dim);
    dl->AddRectFilled(ImVec2(f1.x, f0.y), ImVec2(end.x, f1.y), dim);
    // The 40 × 24 character grid, once its cells are big enough to read.
    const float cw = (f1.x - f0.x) / kCols, ch = (f1.y - f0.y) / kRows;
    if (cw >= px(6.0f) && ch >= px(6.0f)) {
        const ImU32 grid = IM_COL32(110, 255, 140, 40);
        for (int c = 1; c < kCols; ++c) dl->AddLine(ImVec2(f0.x + c * cw, f0.y), ImVec2(f0.x + c * cw, f1.y), grid);
        for (int r = 1; r < kRows; ++r) dl->AddLine(ImVec2(f0.x, f0.y + r * ch), ImVec2(f1.x, f0.y + r * ch), grid);
    }
    const ImU32 frameCol = IM_COL32(110, 255, 140, 255);
    dl->AddRect(f0, f1, frameCol, 0.0f, 0, px(2.0f));
    const float hs = static_cast<float>(handle) * 0.6f;
    for (const ImVec2& c : {f0, ImVec2(f1.x, f0.y), f1, ImVec2(f0.x, f1.y)})
        dl->AddRectFilled(ImVec2(c.x - hs, c.y - hs), ImVec2(c.x + hs, c.y + hs), frameCol);
    dl->PopClipRect();

    if (hovered && !dragging_)
        ImGui::SetTooltip("Drag the frame to move it, a corner to resize it,\n"
                          "click elsewhere to recentre it; wheel zooms about the cursor.");
}

void AsciiArtWindow_ImGui::drawPreview(float width)
{
    using namespace asciiart;
    constexpr int dotsW = kCols * kCellW, dotsH = kRows * kCellH;
    // The Apple-1 picture is 4:3 on its monitor although its dot grid is not.
    const float w = std::max(px(200.0f), width);
    const float h = w * 3.0f / 4.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##asciiartpreview", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    // Framing on the result: drag pans the picture under the screen, the
    // wheel zooms about the cursor.
    if (picture_.w > 0) {
        Framing& f = params_.framing;
        const FrameRect fr = frameRect(picture_.w, picture_.h, f);
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const ImVec2 d = io.MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                f = panFrame(picture_.w, picture_.h, f, -d.x / w * fr.w, -d.y / h * fr.h);
                dirty_ = true;
            }
        }
        if (hovered && io.MouseWheel != 0.0f) {
            const double ax = fr.x + (io.MousePos.x - origin.x) / w * fr.w;
            const double ay = fr.y + (io.MousePos.y - origin.y) / h * fr.h;
            f = zoomFrameAbout(picture_.w, picture_.h, f, std::pow(1.15, static_cast<double>(io.MouseWheel)), ax, ay);
            dirty_ = true;
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
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
    if (hovered && picture_.w > 0) ImGui::SetTooltip("Drag to move the picture, wheel to zoom about the cursor.");
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
    ImGui::SetNextWindowSize(ImVec2(px(940.0f), px(760.0f)), ImGuiCond_FirstUseEver);
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

    // ---- original and preview: side by side when there is room -----------
    {
        const float avail = ImGui::GetContentRegionAvail().x;
        const bool sideBySide = avail >= px(560.0f);
        const float panelW = sideBySide ? (avail - ImGui::GetStyle().ItemSpacing.x) / 2.0f : avail;
        const float panelH = panelW * 3.0f / 4.0f;
        ImGui::BeginGroup();
        ImGui::TextDisabled("Original - frame it here");
        drawOriginal(panelW, panelH);
        ImGui::EndGroup();
        if (sideBySide) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("Apple-1 screen, 40 x 24");
        drawPreview(panelW);
        ImGui::EndGroup();
    }

    // ---- framing ---------------------------------------------------------
    asciiart::Framing& f = params_.framing;
    if (ImGui::SliderFloat("Zoom", &f.zoom, asciiart::kMinZoom, asciiart::kMaxZoom, "%.2fx", ImGuiSliderFlags_Logarithmic)) dirty_ = true;
    if (ImGui::SliderFloat("Centre X", &f.centerX, 0.0f, 1.0f, "%.3f")) dirty_ = true;
    if (ImGui::SliderFloat("Centre Y", &f.centerY, 0.0f, 1.0f, "%.3f")) dirty_ = true;
    int aspect = f.aspect > 0.0f ? 0 : 1;
    if (ImGui::Combo("Shape", &aspect, "Keep proportions (4:3 frame)\0Stretch the whole picture\0")) {
        f.aspect = aspect == 0 ? 4.0f / 3.0f : 0.0f;
        dirty_ = true;
    }
    if (ImGui::Button("Reset framing")) { f = asciiart::Framing{}; dirty_ = true; }
    ImGui::SameLine();
    if (ImGui::Button("Whole picture") && picture_.w > 0) {
        f = asciiart::fitWholePicture(picture_.w, picture_.h, f);
        dirty_ = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pull back until the whole picture is inside the frame;\n"
                          "the bars it leaves are black on the Apple-1.");
    if (picture_.w > 0) {
        const asciiart::FrameRect fr = asciiart::frameRect(picture_.w, picture_.h, f);
        ImGui::SameLine();
        ImGui::TextDisabled("Frame: %.0f x %.0f px at (%.0f, %.0f)", fr.w, fr.h, fr.x, fr.y);
    }

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
