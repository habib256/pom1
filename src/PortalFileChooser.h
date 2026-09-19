#pragma once
// PortalFileChooser.h -- the pure half of the Linux desktop-portal file picker:
// the strings the protocol is made of, with no D-Bus in sight. The transport
// (libdbus-1, dlopen'd) lives in NativeFileDialog.cpp; everything here can be
// pinned by a test that has no session bus.
//
// WHY A PORTAL. POM1 had two ways to show the desktop's own file dialog on
// Linux, both a forked helper: zenity (GTK) and kdialog (KDE). Neither is
// installed by default everywhere -- Uncle Bernie's box had neither when he
// reported that POM1's own browser could not reach his files (Applefritter,
// 8 sept. 2026). The xdg-desktop-portal FileChooser is the interface sandboxed
// Flatpak/Snap applications use: one D-Bus call, answered by whatever desktop is
// running (GNOME, KDE, Cinnamon, XFCE...), with no helper program to install.
// Bundling zenity instead was rejected: it is a GTK front-end, so it means
// shipping a GTK stack, and it would never have reached a source build.
//
// THE PROTOCOL, in the three places it is easy to get wrong:
//
//   * The answer is a SIGNAL, not the method's reply. OpenFile/SaveFile return
//     at once with the object path of a Request; the choice arrives later as
//     org.freedesktop.portal.Request.Response on that path. The client must
//     subscribe BEFORE calling, or a fast portal answers into the void and the
//     wait never ends. So the path is predicted from the caller's unique bus name
//     and a `handle_token` the caller chooses -- requestPath() below.
//   * The chosen file comes back as a URI, percent-encoded: a program called
//     `My Prog.apl` arrives as `file:///home/x/My%20Prog.apl`.
//   * libdbus ABORTS the process on a string argument that is not valid UTF-8
//     (its argument checks are fatal by default). A Linux file name is bytes, so
//     anything that came from the file system is checked before it is sent.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pom1::portal {

inline constexpr const char* kService   = "org.freedesktop.portal.Desktop";
inline constexpr const char* kObject    = "/org/freedesktop/portal/desktop";
inline constexpr const char* kChooser   = "org.freedesktop.portal.FileChooser";
inline constexpr const char* kRequest   = "org.freedesktop.portal.Request";

/// Request.Response's first argument.
enum class Response : uint32_t { Success = 0, Cancelled = 1, Failed = 2 };

/// The Request object a portal creates for `handleToken`, predicted from the
/// caller's unique bus name: ":1.42" -> "1_42" (drop the colon, dots become
/// underscores), per the org.freedesktop.portal.Request documentation.
inline std::string requestPath(const std::string& uniqueName, const std::string& handleToken)
{
    std::string sender;
    for (char c : uniqueName) {
        if (c == ':') continue;
        sender.push_back(c == '.' ? '_' : c);
    }
    return std::string(kObject) + "/request/" + sender + "/" + handleToken;
}

/// A handle token: an object-path element, so [A-Za-z0-9_] only.
inline std::string handleToken(unsigned long pid, unsigned counter)
{
    return "pom1_" + std::to_string(pid) + "_" + std::to_string(counter);
}

/// The match rule that catches the Response for `path` and nothing else.
inline std::string responseMatchRule(const std::string& path)
{
    return std::string("type='signal',sender='") + kService + "',interface='" + kRequest
         + "',member='Response',path='" + path + "'";
}

/// "file:///a/My%20Prog.apl" -> "/a/My Prog.apl". A `file://host/path` form
/// keeps only the path. Anything that is not a file URI -> "" (the caller
/// treats that as no answer rather than handing a URL to fopen()).
inline std::string fileUriToPath(const std::string& uri)
{
    const std::string scheme = "file://";
    if (uri.compare(0, scheme.size(), scheme) != 0) return {};
    std::size_t start = scheme.size();
    if (start < uri.size() && uri[start] != '/') {
        start = uri.find('/', start);          // skip an authority ("localhost")
        if (start == std::string::npos) return {};
    }
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = start; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            const int hi = hex(uri[i + 1]), lo = hex(uri[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(uri[i]);
    }
    return out;
}

/// True when `s` is well-formed UTF-8 with no NUL -- the only strings libdbus
/// will accept without aborting.
inline bool isValidUtf8(const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0) return false;
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3
                        : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) return false;
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800)
                           || (len == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += len;
    }
    return true;
}

/// Glob patterns for one filter's extensions, the portal's `(us)` pairs with
/// type 0 = glob. An empty list matches everything.
inline std::vector<std::string> globPatterns(const std::vector<std::string>& extensions)
{
    if (extensions.empty()) return {"*"};
    std::vector<std::string> out;
    out.reserve(extensions.size());
    for (const auto& e : extensions) out.push_back("*." + e);
    return out;
}

} // namespace pom1::portal
