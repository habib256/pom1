// file_browser_decisions_test.cpp -- POM1's own file browser, as decisions.
//
// Links NOTHING: src/FileBrowserDecisions.h is a header of pure string
// functions, which is the whole point -- the dialog it serves lives in
// MainWindow_FileDialogs.cpp, and no test binary links the UI.
//
// Every section below is a defect Uncle Bernie hit on a Linux Mint 17 box whose
// native picker POM1 could not use (Applefritter, 8 sept. 2026): the browser
// could not leave the packaged data directory, and a typed path did not set the
// loader the way a clicked one did. See the header for the full account.

#include <cassert>
#include <cstdio>
#include <optional>
#include <string>

#include "FileBrowserDecisions.h"

using namespace pom1::filebrowser;

static void expectType(const char* path, std::optional<MemoryFileType> want)
{
    const auto got = impliedType(path);
    if (got != want) {
        std::printf("  -> impliedType(\"%s\") = %s, expected %s\n", path,
                    got ? (*got == MemoryFileType::Binary ? "Binary" : "HexDump") : "none",
                    want ? (*want == MemoryFileType::Binary ? "Binary" : "HexDump") : "none");
        assert(false);
    }
}

int main()
{
    // ---- 1. The typed path decides the loader, the same way a click does ----
    // The dialog's radio defaults to HexDump. Typing the full path of a raw
    // .bin therefore fed a binary image to the WOZMON text parser and reported
    // failure on a good file -- Bernie's "it does not work".
    expectType("/home/bernie/gfxdemo.bin", MemoryFileType::Binary);
    expectType("/home/bernie/gfxdemo.BIN", MemoryFileType::Binary);      // case
    expectType("C:\\dev\\gfxdemo.bin",     MemoryFileType::Binary);      // separator

    // Every hex dialect HexDumpFile.h knows, since this browser is the ONLY
    // picker on WASM and on a box without zenity/kdialog: a dialect missing
    // here is one a user cannot load at all.
    for (const char* p : { "a.txt", "a.hex", "a.apl", "a.mon", "a.tur",
                           "a.APL", "deep/dir/prog.apl.txt" })
        expectType(p, MemoryFileType::HexDump);

    // ---- 2. An unknown extension must NOT be guessed -----------------------
    // Returning a default instead of nullopt is how this goes wrong in both
    // directions: "always hex" mis-parsed Bernie's binary, "always binary"
    // would write a hex listing's ASCII into RAM -- the exact defect
    // HexDumpFile.h exists to prevent. Silence leaves the user's radio alone.
    expectType("/home/bernie/gfxdemo",      std::nullopt);   // no extension
    expectType("/home/bernie/gfxdemo.prg",  std::nullopt);
    expectType("/home/bernie/gfxdemo.o",    std::nullopt);
    expectType("",                          std::nullopt);
    // A dot in a DIRECTORY name is not an extension (lowerExtension's rule).
    expectType("/home/bernie/v1.2/gfxdemo", std::nullopt);

    // ---- 3. `~` is how a typed escape route is written ---------------------
    assert(expandHome("~/gfxdemo.bin", "/home/bernie") == "/home/bernie/gfxdemo.bin");
    assert(expandHome("~",             "/home/bernie") == "/home/bernie");
    // A trailing separator on HOME must not double up.
    assert(expandHome("~/x", "/home/bernie/") == "/home/bernie/x");
    // Not ours to expand: another user's home (POM1 reads no passwd file), and
    // a `~` anywhere but the front, which is a legal filename character.
    assert(expandHome("~root/x",  "/home/bernie") == "~root/x");
    assert(expandHome("/tmp/a~b",  "/home/bernie") == "/tmp/a~b");
    assert(expandHome("a~",        "/home/bernie") == "a~");
    // No home found: degrade to the literal text rather than invent "/x".
    assert(expandHome("~/x", "") == "~/x");
    assert(expandHome("",    "/home/bernie").empty());

    // ---- 4. The header says where you actually are -------------------------
    const std::string root = "/tmp/.mount_POM1abc/usr/share/POM1/software";
    assert(displayDirectory(root, root, "software/") == "software/");
    assert(displayDirectory(root + "/Graphic HGR", root, "software/") == "software/Graphic HGR/");
    assert(displayDirectory(root + "/a/b", root, "software/") == "software/a/b/");
    // Outside the root the old code printed "software/" plus a substring offset
    // taken from the root's LENGTH -- meaningless once you have left, and it
    // read as "software/" while the listing showed /home/bernie.
    assert(displayDirectory("/home/bernie", root, "software/") == "/home/bernie");
    assert(displayDirectory("/home/bernie/", root, "software/") == "/home/bernie");
    assert(displayDirectory("/", root, "software/") == "/");
    // A sibling that merely SHARES the prefix is not inside it.
    assert(displayDirectory(root + "_old", root, "software/") == root + "_old");
    assert(displayDirectory("", root, "software/").empty());
    // The label is a PARAMETER: the cassette browser had the same confinement
    // and the same header bug, and a baked-in "software/" would have left it a
    // second copy of this rule rather than a second caller of it.
    assert(displayDirectory("/x/cassettes", "/x/cassettes", "cassettes/") == "cassettes/");
    assert(displayDirectory("/x/cassettes/demo", "/x/cassettes", "cassettes/")
           == "cassettes/demo/");
    assert(displayDirectory("/home/bernie", "/x/cassettes", "cassettes/") == "/home/bernie");

    // ---- 5. `..` goes all the way up, and stops where the filesystem does --
    // The confinement being gone is the primary fix: `..` used to vanish at the
    // data root, which inside an AppImage is a read-only /tmp self-mount.
    assert(*parentDirectory("/home/bernie/dev") == "/home/bernie");
    assert(*parentDirectory("/home/bernie/dev/") == "/home/bernie");
    assert(*parentDirectory("/home/bernie") == "/home");
    assert(*parentDirectory("/home") == "/");
    assert(!parentDirectory("/").has_value());        // a root has no parent
    assert(!parentDirectory("").has_value());
    assert(!parentDirectory("relative").has_value()); // nothing to ascend to
    // Walking up from the AppImage mount terminates rather than looping.
    {
        std::string d = root;
        int hops = 0;
        while (auto p = parentDirectory(d)) { d = *p; assert(++hops < 64); }
        assert(d == "/");
    }

    std::printf("file_browser_decisions_smoke: OK\n");
    return 0;
}
