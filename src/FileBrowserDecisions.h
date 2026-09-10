#pragma once
// FileBrowserDecisions.h -- what POM1's own file browser should DO, as pure
// functions over strings. No ImGui, no GLFW, no filesystem: the same seam rule
// as Apple1KeyMap / LayoutDecisions / SoftwareDirRules, and for the same
// reason -- no test binary links the UI, so a decision left inside a Begin()
// block is a decision nothing can pin.
//
// WHY THIS EXISTS. Uncle Bernie, developing demo software for his own graphics
// card, reported (Applefritter, 8 sept. 2026) that "with the LOAD menu option
// it seems to be impossible to access a file which is not in the weird .tmp
// path made by POM1", and that typing a full path into the load window "does
// not work". Both were true, and neither was one bug:
//
//   * The built-in browser was CONFINED to the data directory. `..` was
//     suppressed once `currentDir == softAsmRoot` -- deliberately, the comment
//     said so -- and nothing else could move it. That root is wherever
//     ResourceLocator found `software/`, which inside an AppImage is the
//     self-mount `/tmp/.mount_POM1xxxxxx/usr/share/POM1/software`: Bernie's
//     "weird .tmp path", and a directory a user can put nothing into. Any file
//     of one's own was therefore unreachable, on every platform whose native
//     picker POM1 could not use.
//
//   * A TYPED path did not set the file type, while a CLICKED one did
//     (`fileType = ext == "bin" ? 0 : 1`). The field defaults to 1 = hex dump,
//     so typing the full path of a raw `.bin` fed a binary image to the WOZMON
//     text parser, which failed -- correctly, and on a file that was perfectly
//     good. That is the whole of "it does not work".
//
// The filesystem probe (is this a directory? does it exist?) stays in the UI,
// where it belongs; every judgement made about the answer lives here.

#include <optional>
#include <string>
#include <string_view>

#include "HexDumpFile.h"

namespace pom1::filebrowser {

/// Which loader a path asks for. The values are the wire format of the dialog's
/// radio button and of performMemoryLoad's `fileType`, so they are fixed: 0 is
/// the raw binary loader, 1 the hex-dump parser.
enum class MemoryFileType { Binary = 0, HexDump = 1 };

/// The type this path's extension IMPLIES, or nullopt when it implies nothing.
///
/// Returning an optional rather than a default is the point: an unknown
/// extension must leave whatever the user picked on the radio alone. Guessing
/// "hex dump" for everything is what mis-parsed Bernie's binary; guessing
/// "binary" for everything would silently write a hex listing's ASCII into RAM,
/// which is the defect HexDumpFile.h was written to stop.
inline std::optional<MemoryFileType> impliedType(const std::string& path)
{
    const std::string ext = lowerExtension(path);
    if (ext == "bin") return MemoryFileType::Binary;
    if (isHexDumpExtension(ext)) return MemoryFileType::HexDump;
    return std::nullopt;
}

/// Expand a leading `~`. A typed path is the only way out of a packaged data
/// directory on a box whose native picker POM1 cannot use, and `~/demo.bin` is
/// how people write one; without this it resolves to a literal "~" directory
/// that exists nowhere and the load fails with a confusing "no such file".
///
/// Only a LEADING `~` followed by end-of-string or a separator is expanded --
/// `~user/x` is deliberately left alone (POM1 does not read /etc/passwd) and a
/// `~` anywhere else is an ordinary, legal filename character. An empty `home`
/// expands nothing, so a caller that cannot find one degrades to the old
/// behaviour rather than producing "/demo.bin".
inline std::string expandHome(const std::string& text, const std::string& home)
{
    if (text.empty() || text[0] != '~' || home.empty()) return text;
    if (text.size() == 1) return home;
    if (text[1] != '/' && text[1] != '\\') return text;   // ~user/... -- not ours
    std::string out = home;
    if (!out.empty() && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    return out + text.substr(1);
}

/// How the current directory should READ in the dialog's header.
///
/// Inside the data root it stays relative and short -- `software/Graphic HGR/`,
/// which is how the shipped programs are named everywhere else in POM1 and in
/// its docs. Once the user has left that root it becomes the ABSOLUTE path,
/// because the old code printed "software/" plus a substring offset computed
/// from the root's length: outside the root that arithmetic is meaningless, and
/// a header reading `software/` while the listing shows `/home/bernie` is worse
/// than no header at all.
inline std::string displayDirectory(const std::string& dir, const std::string& root)
{
    if (dir.empty()) return std::string();
    if (!root.empty() && dir.size() >= root.size()
        && dir.compare(0, root.size(), root) == 0) {
        // Inside the root -- or the root itself.
        if (dir.size() == root.size()) return "software/";
        const char sep = dir[root.size()];
        if (sep == '/' || sep == '\\')
            return "software/" + dir.substr(root.size() + 1) + "/";
        // Same prefix but a different directory ("software_old") -- not inside.
    }
    std::string out = dir;
    if (out.size() > 1 && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    return out;
}

/// The parent of `dir`, or nullopt when there is none to go to.
///
/// Used for the `..` row, which is now offered all the way up instead of
/// stopping at the data root. The stop condition is the FILESYSTEM's, not
/// POM1's: a path whose parent is itself is a root and the row disappears.
/// Trailing separators are ignored so "/home/bernie/" and "/home/bernie"
/// answer alike.
inline std::optional<std::string> parentDirectory(const std::string& dir)
{
    std::string d = dir;
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    if (d.empty()) return std::nullopt;
    const std::size_t slash = d.find_last_of("/\\");
    if (slash == std::string::npos) return std::nullopt;   // relative, no parent
    if (slash == 0) {                                      // "/home" -> "/"
        return d.size() > 1 ? std::optional<std::string>("/") : std::nullopt;
    }
#if defined(_WIN32)
    // "C:\Users" -> "C:\", and "C:\" itself has no parent.
    if (slash == 2 && d.size() > 2 && d[1] == ':') {
        return d.size() > 3 ? std::optional<std::string>(d.substr(0, 3))
                            : std::nullopt;
    }
#endif
    return d.substr(0, slash);
}

}  // namespace pom1::filebrowser
